"""Pinned supplied-DLL checks: parse data and exercise the production transaction.

Never loads the DLL, executes archive scripts, contacts its license service, or
launches a reference player. The separately opt-in Windows job may launch an
original probe host only AFTER installing its outbound firewall block.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import stat
import struct
import subprocess
import tempfile
import zipfile

import pefile

from dll_startup_native_test import REFERENCES, reference_bytes, require, sha, verify

BLOB = "980007812e078635ddc2569caf4d885459ecb104"
ARCHIVE_SHA256 = "48289b241b789c4cdc0e553e84d0af4eb1cb3afa73b657698256ea5fdb4687c3"
DLL_SHA256 = "80fa1503e6071777ff3ba9daabfe27918bbcad94fb1b4726ca88dbdb533be296"
DLL_NAME = "Z-Lag.dll"
EXPORT = "ZLagImportAnchor"
URL = (
    "https://github.com/dsajgddghdsaakovvgheww/BSI-Customizer/"
    "blob/main/SCibJBt4nEzcDlU-hNCvTrHLOvpztldpglbqX9tGh8M.zip"
)


def payload_bytes(path):
    require(path.is_file() and not path.is_symlink(), "Archive must be a regular file")
    require(path.stat().st_size == 1457358, "Unexpected archive size")
    data = path.read_bytes()
    require(
        sha(data) == ARCHIVE_SHA256, "Archive SHA-256 differs from the reviewed payload"
    )
    require(
        hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        == BLOB,
        "Archive Git blob differs",
    )
    with zipfile.ZipFile(path) as archive:
        members = archive.infolist()
        require(len(members) <= 3000, "Too many archive members")
        require(
            sum(i.file_size for i in members) <= 256 * 1024 * 1024,
            "Expanded archive exceeds limit",
        )
        seen = set()
        for info in members:
            name = PurePosixPath(info.filename.replace("\\", "/"))
            require(
                not name.is_absolute()
                and ".." not in name.parts
                and ":" not in info.filename,
                "Unsafe archive path",
            )
            require(
                not stat.S_ISLNK(info.external_attr >> 16), "Archive symlink refused"
            )
            require(
                not info.flag_bits & 1 and info.file_size <= 32 * 1024 * 1024,
                "Unsafe archive member",
            )
            require(
                str(name).casefold() not in seen,
                "Duplicate/case-colliding archive member",
            )
            seen.add(str(name).casefold())
        dll = archive.read("dist/" + DLL_NAME)
    require(sha(dll) == DLL_SHA256, "Unexpected DLL hash")
    return dll


def binary_info(data):
    pe = pefile.PE(data=data)
    try:
        require(
            pe.FILE_HEADER.Machine == 0x8664 and pe.OPTIONAL_HEADER.Magic == 0x20B,
            "Not AMD64 PE32+",
        )
        require(bool(pe.FILE_HEADER.Characteristics & 0x2000), "Not a DLL")
        symbols = getattr(getattr(pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", [])
        require(
            len(symbols) == 1 and symbols[0].name == EXPORT.encode(),
            "Unexpected export table",
        )
        symbol = symbols[0]
        section = pe.get_section_by_rva(symbol.address)
        require(
            symbol.address != 0
            and not symbol.forwarder
            and section is not None
            and bool(section.Characteristics & 0x20000000)
            and len(pe.get_data(symbol.address, 1)) == 1,
            "Import anchor is not a file-backed executable native export",
        )
        require(
            pe.generate_checksum() == pe.OPTIONAL_HEADER.CheckSum,
            "DLL checksum mismatch",
        )
        return {
            "filename": DLL_NAME,
            "bytes": len(data),
            "sha256": DLL_SHA256,
            "machine": "AMD64 / PE32+",
            "requiredExport": EXPORT,
            "executableExportRva": hex(symbol.address),
            "directDependencies": [
                i.dll.decode("ascii") for i in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])
            ],
            "delayDependencies": [
                i.dll.decode("ascii")
                for i in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", [])
            ],
            "authenticodeDirectoryBytes": pe.OPTIONAL_HEADER.DATA_DIRECTORY[4].Size,
            "checksumValid": True,
            "peWarnings": pe.get_warnings(),
        }
    finally:
        pe.close()


def checked(command):
    result = subprocess.run(
        [str(s) for s in command], capture_output=True, text=True, timeout=90
    )
    require(
        result.returncode == 0,
        "Native test adapter failed: " + result.stdout + result.stderr,
    )
    return result


def exercise(source, original, dll, file_test, destination, keep=False):
    checked(
        [file_test, "--prepare-deployment", source, dll, destination, DLL_NAME, EXPORT]
    )
    target = destination / "HD-Player.exe"
    patched = target.read_bytes()
    result = verify(original, patched, DLL_NAME.encode(), EXPORT.encode())
    journal = json.loads(Path(str(target) + ".bsi-startup-dll.json").read_text())
    require(
        journal["schema"] == "bsi.dll-startup.v2"
        and journal["dllName"] == DLL_NAME
        and journal["requiredExport"] == EXPORT
        and journal["dllSha256"] == DLL_SHA256
        and journal["originalSha256"] == sha(original)
        and journal["patchedSha256"] == sha(patched),
        "Production recovery record differs from actual EXE/DLL/export",
    )
    backup = destination / journal["backupFile"]
    require(backup.read_bytes() == original, "Production backup is not byte-exact")
    copied_dll = destination / DLL_NAME
    require(sha(copied_dll.read_bytes()) == DLL_SHA256, "Payload was changed")
    if not keep:
        # Exercise the actual recovery API with the dependency absent, not a
        # Python rewrite of the backup or a heuristic import removal.
        withheld = destination / "withheld-payload.dll"
        copied_dll.rename(withheld)
        checked([file_test, "--restore-deployment", target])
        require(
            target.read_bytes() == original, "Production restore differs from original"
        )
        require(
            backup.read_bytes() == original, "Production restore removed/changed backup"
        )
        require(
            Path(str(backup) + ".json").is_file(), "Recovery journal was not archived"
        )
        require(sha(withheld.read_bytes()) == DLL_SHA256, "Restore changed/deleted DLL")
    return {
        "inputSha256": sha(original),
        "productionApplyVerified": True,
        "restoreWithoutDllVerified": not keep,
        "playerExecuted": False,
        **result,
    }


def rejected_exports(engine, source, dll, directory):
    results = []
    # Both the old UI default and a wrong-case anchor must fail, rather than
    # producing a startup failure on the recipient's machine.
    for index, export in enumerate(("zlag_init", EXPORT.lower())):
        output = directory / f"must-not-exist-{index}.exe"
        r = subprocess.run(
            [
                str(engine),
                "--prepare",
                str(source),
                str(dll),
                DLL_NAME,
                str(output),
                export,
            ],
            capture_output=True,
            text=True,
            timeout=90,
        )
        require(
            r.returncode == 1
            and "Selected export is absent from the DLL" in r.stderr
            and "Sanitizer" not in r.stderr
            and not output.exists(),
            "Missing/wrong-case export was not safely rejected",
        )
        results.append(export)
    data = bytearray(dll.read_bytes())
    struct.pack_into("<H", data, struct.unpack_from("<I", data, 0x3C)[0] + 4, 0x14C)
    wrong = directory / "wrong-architecture.dll"
    wrong.write_bytes(data)
    output = directory / "wrong-architecture-must-not-exist.exe"
    r = subprocess.run(
        [
            str(engine),
            "--prepare",
            str(source),
            str(wrong),
            DLL_NAME,
            str(output),
            EXPORT,
        ],
        capture_output=True,
        text=True,
        timeout=90,
    )
    require(
        r.returncode == 1
        and "An AMD64 executable and an AMD64 DLL are required" in r.stderr
        and "Sanitizer" not in r.stderr
        and not output.exists(),
        "Wrong-architecture DLL was not safely rejected",
    )
    return {
        "refusedMissingOrWrongCaseExports": results,
        "refusedWrongArchitecture": True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--file-test", required=True, type=Path)
    parser.add_argument("--engine", type=Path)
    parser.add_argument("--reference", action="append", default=[], type=Path)
    parser.add_argument(
        "--probe-host",
        type=Path,
        help="Original test host only; prepared, never launched here",
    )
    parser.add_argument("--prepare-dir", type=Path)
    parser.add_argument("--report", required=True, type=Path)
    args = parser.parse_args()
    require(
        bool(args.reference) or args.probe_host is not None,
        "Select a reference or original probe host",
    )
    require(
        bool(args.probe_host) == bool(args.prepare_dir),
        "Probe host and preparation directory go together",
    )
    payload = payload_bytes(args.archive)
    report = {
        "archive": {"url": URL, "gitBlob": BLOB, "sha256": ARCHIVE_SHA256},
        "payload": binary_info(payload),
        "nativeDataReferences": [],
        "executionBoundary": "DLL/player not executed by this script; no license requests or source builds",
        "runtimeVisualEffectsVerified": False,
    }
    file_test = args.file_test.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="bsi-supplied-dll-") as temp:
        directory = Path(temp)
        dll = directory / DLL_NAME
        dll.write_bytes(payload)
        for index, source in enumerate(args.reference):
            original_file_hash = sha(source.read_bytes())
            original = reference_bytes(source)
            require(
                sha(original) in REFERENCES.values(),
                "Reference does not match a pinned player hash",
            )
            # ZIP members are read as bounded bytes and staged only as data.
            staged = directory / f"original-{index}.exe"
            staged.write_bytes(original)
            row = exercise(
                staged, original, dll, file_test, directory / f"case-{index}"
            )
            row["reference"] = source.name
            if args.engine:
                row["negativeGuards"] = rejected_exports(
                    args.engine.resolve(strict=True), staged, dll, directory
                )
            require(
                sha(source.read_bytes()) == original_file_hash,
                "Original reference/archive was changed",
            )
            report["nativeDataReferences"].append(row)
        if args.probe_host:
            source = args.probe_host.resolve(strict=True)
            original = source.read_bytes()
            with pefile.PE(data=original) as pe:
                imports = [
                    i.dll.decode("ascii").casefold()
                    for i in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])
                ]
                require(
                    DLL_NAME.casefold() not in imports,
                    "Probe already links the payload; import test invalid",
                )
            report["originalProbePreparation"] = exercise(
                source, original, dll, file_test, args.prepare_dir.resolve(), keep=True
            )
            require(source.read_bytes() == original, "Original probe was modified")
        require(dll.read_bytes() == payload, "Source payload was modified")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Hosted log downloads are not always reachable from the review
        # sandbox. Surface data-only failures through GitHub's annotations too;
        # this stage never executes the DLL or obtains a hardware identifier.
        if os.environ.get("GITHUB_ACTIONS"):
            for line in str(error).splitlines()[-30:]:
                escaped = line.replace("%", "%25").replace("\r", "%0D")
                print("::error::Supplied-DLL data check: " + escaped, flush=True)
        raise
