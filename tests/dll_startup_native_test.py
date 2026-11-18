"""Independently verify the native planner on pinned player data; never execute it.

The --engine binary is built from tests/dll-startup/test.cpp, not a shipping
helper. pefile==2024.8.26 is a TEST dependency only. Originals are hash-checked
before/after; all patch output and extracted ZIP members stay temporary.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zipfile

import pefile

REFERENCES = {
    "HD-Player MSI v5.9.exe": "1f473ca2ec1e684cf85dbf1cacd47ed0cb33c27c6fe298ada1775f2a3414f2ed",
    "HD-Player v.5.10 bluestacks.exe": "bcec63030f032fef60850559b5a5c40fdcce44f3b129aa5ccee05e990957467b",
    "HD-Player MSI Latest version.zip": "b373ad8ce5dbc6b1e605b3c153d20b69bf5769b7ff82f896291e4844ab418efd",
    "HD-Player latest version Bluestacks 5.zip": "d2f8509d025f90bd23604aa1c8f6981c456468bceab4db4a30bb380806826f39",
}
LIMIT = 128 * 1024 * 1024


def require(pass_, message):
    if not pass_:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def reference_bytes(path):
    require(
        path.is_file() and not path.is_symlink() and path.stat().st_size <= LIMIT, f"Invalid input: {path}"
    )
    if path.suffix.lower() != ".zip":
        return path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        files = [
            f
            for f in archive.infolist()
            if Path(f.filename.replace("\\", "/")).name.lower() == "hd-player.exe"
        ]
        require(
            len(files) == 1 and 0 < files[0].file_size <= LIMIT,
            "ZIP must have exactly one bounded HD-Player.exe",
        )
        # Read bytes, never extract archive paths or execute a member.
        return archive.read(files[0])


def descriptors(pe):
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[1]
    rows = []
    for i in range(directory.Size // 20):
        at = pe.get_offset_from_rva(directory.VirtualAddress + i * 20)
        data = pe.__data__[at : at + 20]
        require(len(data) == 20, "Truncated import descriptor")
        if data == bytes(20):
            return rows
        fields = struct.unpack("<5I", data)
        rows.append((data, pe.get_string_at_rva(fields[3])))
    raise RuntimeError("Unterminated import descriptors")


def verify(original, patched, dll_name=b"zlag.dll", required_export=b"zlag_init"):
    before, after = [pefile.PE(data=data, fast_load=True) for data in (original, patched)]
    try:
        old, new = descriptors(before), descriptors(after)
        require(new[:-1] == old and new[-1][1] == dll_name, "Original descriptors/names changed")
        require(
            after.FILE_HEADER.NumberOfSections == before.FILE_HEADER.NumberOfSections + 1,
            "Not one new section",
        )
        require(
            after.OPTIONAL_HEADER.AddressOfEntryPoint == before.OPTIONAL_HEADER.AddressOfEntryPoint,
            "Entry point changed",
        )
        for a, b in zip(before.sections, after.sections):
            require(
                a.__pack__() == b.__pack__() and a.get_data() == b.get_data(), "Original section/data changed"
            )
        first = min(s.PointerToRawData for s in before.sections if s.SizeOfRawData)
        require(
            original[first:] == patched[first : len(original)],
            "Original section/overlay/certificate bytes changed",
        )
        require(after.OPTIONAL_HEADER.CheckSum == after.generate_checksum(), "PE checksum mismatch")
        added = after.sections[-1]
        require(
            added.Name.rstrip(b"\0") == b".bsidll" and added.Characteristics == 0xC0000040,
            "Unexpected added section",
        )
        # Independently verify both real, terminated import-by-name thunk lists.
        # Empty lists passed structural parsing but were skipped on Windows.
        ilt, timestamp, chain, name, iat = struct.unpack("<5I", new[-1][0])
        require(ilt != iat and timestamp == chain == 0, "Unexpected new import descriptor")
        thunk_data = after.get_data(ilt, 16)
        require(len(thunk_data) == 16 and thunk_data == after.get_data(iat, 16), "ILT/IAT mismatch")
        symbol, end = struct.unpack("<2Q", thunk_data)
        require(symbol and not symbol & (1 << 63) and end == 0, "Missing real terminated import-by-name")
        require(after.get_data(symbol, 2) == bytes(2), "Unexpected symbol hint")
        require(after.get_string_at_rva(symbol + 2) == required_export, "Wrong resolved export")
        require(after.get_string_at_rva(name) == dll_name, "Wrong startup DLL name")
        return {
            "importsBefore": len(old),
            "importsAfter": len(new),
            "outputBytes": len(patched),
            "sha256": sha(patched),
        }
    finally:
        before.close()
        after.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--file-test", type=Path)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--reference-dir", type=Path)
    group.add_argument("--reference", action="append", type=Path)
    args = parser.parse_args()
    engine = str(args.engine.resolve(strict=True))
    inputs = (
        [(args.reference_dir / filename, expected) for filename, expected in REFERENCES.items()]
        if args.reference_dir
        else [(path, None) for path in args.reference]
    )
    rows = []
    with tempfile.TemporaryDirectory(prefix="bsi-native-import-") as directory:
        folder = Path(directory)
        subprocess.run([engine, "--fixtures", str(folder)], check=True)
        for i, (path, expected) in enumerate(inputs):
            filename = path.name
            original = reference_bytes(path)
            input_sha = sha(path.read_bytes())
            actual = sha(original)
            require(
                actual == expected if expected else actual in REFERENCES.values(),
                f"Unexpected reference SHA-256: {filename}",
            )
            expected = actual
            source, output = folder / f"original-{i}.exe", folder / f"patched-{i}.exe"
            source.write_bytes(original)
            subprocess.run(
                [engine, "--prepare", str(source), str(folder / "probe.dll"), "zlag.dll", str(output)],
                check=True,
            )
            result = verify(original, output.read_bytes())
            require(source.read_bytes() == original, "Planner modified its input")
            if args.file_test:
                subprocess.run([str(args.file_test.resolve(strict=True)), str(source)], check=True)
            require(sha(path.read_bytes()) == input_sha, "Original reference/archive changed")
            rows.append({"reference": filename, "inputSha256": expected, "playerExecuted": False, **result})
    print(json.dumps({"nativeStructuralReferences": rows}, indent=2))


if __name__ == "__main__":
    main()
