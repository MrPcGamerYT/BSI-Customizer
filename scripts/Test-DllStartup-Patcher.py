#!/usr/bin/env python3
"""Audit the exact user-supplied Pay Load Patcher Python/JS revisions.

Not an installer or a security sandbox. Runs reviewed, SHA-256-pinned scripts
against disposable copies; never loads/runs a Windows EXE or DLL. Requires
Python 3.8+, Node.js 14+ and pefile==2024.8.26. The synthetic DLLs are PE data
fixtures, NOT runnable DLLs and NOT the user's actual zlag.dll.

Usage:
    python scripts/Test-DllStartup-Patcher.py --source-dir /path/to/uploads
    python scripts/Test-DllStartup-Patcher.py --source-dir /path/to/uploads \
        --reference /path/to/HD-Player.exe --json /new/path/to/report.json

Exit 1 means a safety finding (expected for the reviewed uploads) or failed
assertion; 0 means these limited checks passed, NOT Windows runtime proof.
Source mismatches refuse execution. See docs/DLL-STARTUP-REVIEW.md.
"""
import argparse
import hashlib
import json
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

EXPECTED = {
    "Pay Load Patcher.py": "9b5f05aa2897ae94f1a22b28af838f1ac53db35312dddc2b738b469d23ee6832",
    "Pay Load Patcher.js": "9de47ff88e6201347df7d342fb16159bd915b370959cad1f820d04449ee03916",
}


def u16(b, o, v):
    struct.pack_into("<H", b, o, v)


def u32(b, o, v):
    struct.pack_into("<I", b, o, v)


def u64(b, o, v):
    struct.pack_into("<Q", b, o, v)


def fixture(dll=False, exports=("zlag_init", "other_init"), machine=0x8664, header_room=True):
    b = bytearray(0xA00)
    b[:2] = b"MZ"
    u32(b, 0x3C, 0x80)
    b[0x80:0x84] = b"PE\0\0"
    coff, opt, sec = 0x84, 0x98, 0x188
    u16(b, coff, machine)
    u16(b, coff + 2, 2)
    u16(b, coff + 16, 0xF0)
    u16(b, coff + 18, 0x22 | (0x2000 if dll else 0))
    u16(b, opt, 0x20B)
    u32(b, opt + 4, 0x200)
    u32(b, opt + 8, 0x400)
    u32(b, opt + 16, 0x1000)
    u32(b, opt + 20, 0x1000)
    u64(b, opt + 24, 0x180000000 if dll else 0x140000000)
    u32(b, opt + 32, 0x1000)
    u32(b, opt + 36, 0x200)
    u16(b, opt + 40, 6)
    u16(b, opt + 48, 6)
    u32(b, opt + 56, 0x3000)
    u32(b, opt + 60, 0x400 if header_room else sec + 80)
    u16(b, opt + 68, 3)
    u64(b, opt + 72, 0x100000)
    u64(b, opt + 80, 0x1000)
    u64(b, opt + 88, 0x100000)
    u64(b, opt + 96, 0x1000)
    u32(b, opt + 108, 16)
    for index, (name, va, roff, size, chars) in enumerate(
        [
            (b".text", 0x1000, 0x400, 0x200, 0x60000020),
            (b".edata" if dll else b".idata", 0x2000, 0x600, 0x400, 0xC0000040),
        ]
    ):
        o = sec + index * 40
        struct.pack_into("<8sIIIIIIHHI", b, o, name, size, va, size, roff, 0, 0, 0, 0, chars)
    b[0x400] = 0xC3
    if dll:
        u32(b, opt + 112, 0x2000)
        u32(b, opt + 116, 0x200)
        u32(b, 0x600 + 12, 0x2050)
        u32(b, 0x600 + 16, 1)
        u32(b, 0x600 + 20, len(exports))
        u32(b, 0x600 + 24, len(exports))
        u32(b, 0x600 + 28, 0x2080)
        u32(b, 0x600 + 32, 0x2090)
        u32(b, 0x600 + 36, 0x20A0)
        b[0x650:0x659] = b"zlag.dll\0"
        cursor = 0x6B0
        for index, name in enumerate(exports):
            raw = name.encode("ascii") + b"\0"
            u32(b, 0x680 + index * 4, 0x1000)
            u32(b, 0x690 + index * 4, 0x2000 + cursor - 0x600)
            u16(b, 0x6A0 + index * 2, index)
            b[cursor : cursor + len(raw)] = raw
            cursor += len(raw)
    else:
        u32(b, opt + 120, 0x2000)
        u32(b, opt + 124, 40)
        u32(b, opt + 112 + 12 * 8, 0x2060)
        u32(b, opt + 116 + 12 * 8, 16)
        struct.pack_into("<IIIII", b, 0x600, 0x2040, 0, 0, 0x20C0, 0x2060)
        u64(b, 0x640, 0x20E0)
        u64(b, 0x660, 0x20E0)
        b[0x6C0:0x6CD] = b"KERNEL32.dll\0"
        b[0x6E0:0x6EE] = b"\0\0ExitProcess\0"
    return bytes(b)


def parse_imports(data):
    pe = pefile.PE(data=data, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
    return [
        {
            "dll": entry.dll.decode("ascii", "replace"),
            "functions": [
                imp.name.decode("ascii", "replace") if imp.name else f"#{imp.ordinal}"
                for imp in entry.imports
            ],
        }
        for entry in getattr(pe, "DIRECTORY_ENTRY_IMPORT", [])
    ]


def audit(source_bytes, references):
    REPORT = {"sources": dict(EXPECTED), "runtimeTested": False, "integrationEnabled": False, "cases": []}
    with tempfile.TemporaryDirectory(prefix="bsi-reviewed-sources-") as snapshot:
        snapshot = pathlib.Path(snapshot)
        for name, data in source_bytes.items():
            (snapshot / name).write_bytes(data)
        SOURCES = {"python": snapshot / "Pay Load Patcher.py", "javascript": snapshot / "Pay Load Patcher.js"}

        def run(language, root, extra=()):
            command = (
                [sys.executable, "-X", "utf8", str(SOURCES[language])]
                if language == "python"
                else ["node", str(SOURCES[language])]
            )
            result = subprocess.run(
                command + [str(root / "HD-Player.exe"), "--no-elevate", *extra],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=30,
                cwd=root,
            )
            return {"exitCode": result.returncode, "output": (result.stdout + result.stderr).strip()}

        def record(language, name, **values):
            entry = {"language": language, "name": name, **values}
            entry["output"] = entry.get("output", "").replace(str(base), "<temporary>")
            if not entry["output"]:
                entry.pop("output")
            REPORT["cases"].append(entry)
            print(json.dumps(entry, ensure_ascii=False), flush=True)

        with tempfile.TemporaryDirectory(prefix="bsi-dll-import-review-") as temp:
            base = pathlib.Path(temp)
            outputs = {}
            for language in SOURCES:
                root = base / language
                root.mkdir()
                target = root / "HD-Player.exe"
                dll = root / "zlag.dll"
                source = fixture()
                payload = fixture(dll=True)
                target.write_bytes(source)
                dll.write_bytes(payload)
                assert parse_imports(source) == [{"dll": "KERNEL32.dll", "functions": ["ExitProcess"]}]
                p = pefile.PE(data=payload)
                assert {e.name.decode() for e in p.DIRECTORY_ENTRY_EXPORT.symbols} == {
                    "zlag_init",
                    "other_init",
                }
                result = run(language, root)
                patched = target.read_bytes()
                pe = pefile.PE(data=patched)
                imports = parse_imports(patched)
                assert result["exitCode"] == 0 and imports[-1] == {
                    "dll": "zlag.dll",
                    "functions": ["zlag_init"],
                }, result
                assert imports[:-1] == parse_imports(source)
                assert (
                    pe.OPTIONAL_HEADER.AddressOfEntryPoint
                    == pefile.PE(data=source).OPTIONAL_HEADER.AddressOfEntryPoint
                )
                assert patched[0x400:0xA00] == source[0x400:0xA00]
                assert (root / "HD-Player.exe.zlag-backup").read_bytes() == source
                assert pe.OPTIONAL_HEADER.CheckSum == pe.generate_checksum()
                assert pe.sections[-1].Name.rstrip(b"\0") == b".zimp"
                assert pe.sections[-1].Characteristics == 0xC0000040
                outputs[language] = patched
                record(
                    language,
                    "valid-synthetic-static-import",
                    passed=True,
                    imports=imports,
                    originalBytes=len(source),
                    patchedBytes=len(patched),
                    entryPointUnchanged=True,
                    codeAndExistingSectionBytesUnchanged=True,
                )
                result = run(language, root)
                assert result["exitCode"] == 0 and target.read_bytes() == patched
                record(language, "idempotent-same-export", passed=True)
                result = run(language, root, ("--function", "other_init"))
                record(
                    language,
                    "different-export-on-existing-module",
                    reportedSuccess=result["exitCode"] == 0,
                    requestedImportPresent="other_init"
                    in parse_imports(target.read_bytes())[-1]["functions"],
                    output=result["output"],
                )
                for name, dll_bytes, options in [
                    ("missing-dll", None, ()),
                    ("missing-export", fixture(dll=True, exports=("wrong_export",)), ()),
                    ("wrong-export-case", fixture(dll=True, exports=("ZLAG_INIT",)), ()),
                    ("wrong-architecture", fixture(dll=True, machine=0xAA64), ()),
                    ("no-section-header-room", payload, ()),
                ]:
                    original = fixture(header_room=name != "no-section-header-room")
                    target.write_bytes(original)
                    if dll.exists():
                        dll.unlink()
                    if dll_bytes is not None:
                        dll.write_bytes(dll_bytes)
                    result = run(language, root, options)
                    assert result["exitCode"] != 0 and target.read_bytes() == original, (name, result)
                    record(language, name, passed=True, targetUnchanged=True)
                target.write_bytes(source)
                dll.write_bytes(payload)
                victim = root / "keep-existing-backup.bin"
                victim.write_bytes(b"KEEP EXISTING BACKUP")
                result = run(language, root, ("--backup", str(victim)))
                record(
                    language,
                    "existing-custom-backup-preservation",
                    reportedSuccess=result["exitCode"] == 0,
                    existingBackupPreserved=victim.read_bytes() == b"KEEP EXISTING BACKUP",
                )
                target.write_bytes(source)
                not_dll = bytearray(payload)
                u16(not_dll, 0x84 + 18, 0x22)
                dll.write_bytes(not_dll)
                result = run(language, root)
                record(
                    language,
                    "reject-non-dll-payload",
                    reportedSuccess=result["exitCode"] == 0,
                    targetUnchanged=target.read_bytes() == source,
                )
                target.write_bytes(source)
                dll.write_bytes(payload)
                invalid_export = bytearray(payload)
                u32(invalid_export, 0x680, 0)
                dll.write_bytes(invalid_export)
                result = run(language, root)
                record(
                    language,
                    "reject-null-export-address",
                    reportedSuccess=result["exitCode"] == 0,
                    targetUnchanged=target.read_bytes() == source,
                )
                target.write_bytes(source)
                dll.write_bytes(payload)
                unicode_dll = root / "zlag-\u00e9.dll"
                unicode_dll.write_bytes(payload)
                result = run(language, root, ("--dll", unicode_dll.name))
                record(
                    language,
                    "unicode-dll-filename",
                    exitCode=result["exitCode"],
                    targetUnchanged=target.read_bytes() == source,
                    output=result["output"],
                )
            assert (
                outputs["python"] == outputs["javascript"]
            ), "supplied versions disagree on valid ASCII fixture"
            REPORT["syntheticOutputsIdentical"] = True
            for ref_index, reference in enumerate(references):
                original = reference.read_bytes()
                original_hash = hashlib.sha256(original).hexdigest()
                old = pefile.PE(data=original, fast_load=True)
                for language in SOURCES:
                    root = base / f"{language}-reference-{ref_index}"
                    root.mkdir()
                    (root / "HD-Player.exe").write_bytes(original)
                    (root / "zlag.dll").write_bytes(fixture(dll=True))
                    result = run(language, root)
                    patched = (root / "HD-Player.exe").read_bytes()
                    entry = {
                        "reference": reference.name,
                        "referenceSha256": original_hash,
                        "exitCode": result["exitCode"],
                        "output": result["output"],
                        "originalUnchanged": hashlib.sha256(reference.read_bytes()).hexdigest()
                        == original_hash,
                    }
                    if result["exitCode"] == 0:
                        new = pefile.PE(data=patched, fast_load=True)
                        assert parse_imports(patched)[:-1] == parse_imports(original)
                        assert parse_imports(patched)[-1] == {"dll": "zlag.dll", "functions": ["zlag_init"]}
                        assert (
                            new.OPTIONAL_HEADER.AddressOfEntryPoint == old.OPTIONAL_HEADER.AddressOfEntryPoint
                        )
                        assert new.OPTIONAL_HEADER.CheckSum == new.generate_checksum()
                        for section in old.sections:
                            start, size = section.PointerToRawData, section.SizeOfRawData
                            assert patched[start : start + size] == original[start : start + size]
                        entry.update(
                            importsBefore=len(parse_imports(original)),
                            importsAfter=len(parse_imports(patched)),
                            patchedBytes=len(patched),
                            oldSectionBytesUnchanged=True,
                            entryPointUnchanged=True,
                        )
                    record(language, "real-player-copy-structural-test", **entry)

    findings = []
    for case in REPORT["cases"]:
        name = case["name"]
        if name == "real-player-copy-structural-test" and (
            case["exitCode"] != 0 or not case["originalUnchanged"]
        ):
            findings.append({"language": case["language"], "issue": "reference-copy check failed"})
        if (
            name == "different-export-on-existing-module"
            and case["reportedSuccess"]
            and not case["requestedImportPresent"]
        ):
            findings.append(
                {
                    "language": case["language"],
                    "issue": "false already-patched success for a different export",
                }
            )
        if name == "existing-custom-backup-preservation" and not case["existingBackupPreserved"]:
            findings.append({"language": case["language"], "issue": "existing custom backup overwritten"})
        if name in ("reject-non-dll-payload", "reject-null-export-address") and not case["targetUnchanged"]:
            findings.append({"language": case["language"], "issue": name})
        if name == "unicode-dll-filename" and not case["targetUnchanged"]:
            findings.append(
                {
                    "language": case["language"],
                    "issue": "Unicode DLL filename leaves target modified on failed verification",
                }
            )
    REPORT["findings"] = findings
    REPORT["safeToIntegrate"] = False  # No Windows runtime or actual payload test.
    return REPORT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True, type=pathlib.Path)
    parser.add_argument(
        "--reference",
        action="append",
        default=[],
        type=pathlib.Path,
        help="Read-only PE input; repeat for multiple versions. Only copies are patched.",
    )
    parser.add_argument("--json", type=pathlib.Path, help="New report path; existing files are refused.")
    args = parser.parse_args()
    if not __debug__:
        parser.error("Run without -O: this diagnostic requires its assertions.")
    if args.json and args.json.exists():
        parser.error("The report already exists; choose a new --json path.")
    if not shutil.which("node"):
        parser.error("Node.js is required for the JavaScript checks.")
    source_bytes = {}
    for name, expected in EXPECTED.items():
        path = args.source_dir / name
        if not path.is_file():
            parser.error("Missing reviewed source: " + str(path))
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected:
            parser.error("Source hash mismatch; review this revision before executing it: " + name)
        source_bytes[name] = data
    references = [p.resolve() for p in args.reference]
    for path in references:
        if not path.is_file() or path.stat().st_size > 128 * 1024 * 1024:
            parser.error("Reference must be a regular file of at most 128 MiB: " + str(path))
    global pefile
    try:
        import pefile
    except ImportError:
        parser.error("Install the independent decoder with: python -m pip install pefile==2024.8.26")
    report = audit(source_bytes, references)
    if args.json:
        # Never clobber an existing diagnostic/backup/input file.
        with args.json.open("x", encoding="utf-8") as handle:
            json.dump(report, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
        print("Review report:", args.json)
    print(
        "No Windows EXE/DLL was executed. PowerShell, UAC and actual payload behavior are NOT runtime-tested."
    )
    print("Safety findings:", len(report["findings"]))
    return 1 if report["findings"] else 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, AssertionError, subprocess.SubprocessError) as error:
        print("AUDIT FAILED:", error, file=sys.stderr)
        sys.exit(1)
