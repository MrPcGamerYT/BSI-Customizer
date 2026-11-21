#!/usr/bin/env python3
"""Developer/research inventory of HD-Player and adjacent Qt DLLs, as DATA ONLY.

Requires the test dependency pefile==2024.8.26. This is not an end-user runtime,
installer, compatibility override, exhaustive release database or shader test.
No network, extraction, target writes, DLL loading or player execution occurs.
Exact runtime versions come ONLY from adjacent DLL VS_FIXEDFILEINFO, never from
an EXE name, QML cache compiler version, DLL-name string or Qt version-tag import.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import zipfile

import pefile

LIMIT = 128 * 1024 * 1024
MAX_ENTRIES = 4096
PARTS = ("core", "gui", "qml", "quick", "quickcontrols2", "quicktemplates2")
REQUIRED = ("core", "gui", "qml", "quick")


class InventoryError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise InventoryError(message)


def safe_path(path):
    path = Path(os.path.abspath(path))
    for ancestor in (path, *path.parents):
        info = ancestor.lstat()
        require(not stat.S_ISLNK(info.st_mode) and not getattr(info, "st_file_attributes", 0) & 0x400,
                "Linked/reparse inventory paths are refused")
    return path


def read(path):
    path = safe_path(path)
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    with os.fdopen(os.open(path, flags), "rb") as stream:
        before = os.fstat(stream.fileno())
        require(stat.S_ISREG(before.st_mode) and 0 < before.st_size <= LIMIT, "Expected a nonempty regular file of at most 128 MiB")
        data = stream.read(LIMIT + 1)
        after = os.fstat(stream.fileno())
        require(len(data) == before.st_size and (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns)
                == (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns), "Inventory input changed while reading")
        return data


def input_bytes(path):
    data = read(path)
    if path.suffix.lower() != ".zip":
        return data, None
    # Read ONE member's bytes without ever extracting an archive path.
    from io import BytesIO
    with zipfile.ZipFile(BytesIO(data)) as archive:
        entries = archive.infolist()
        require(len(entries) <= MAX_ENTRIES, "Too many ZIP members")
        matches = [entry for entry in entries if entry.filename.replace("\\", "/").split("/")[-1].lower() == "hd-player.exe"]
        require(len(matches) == 1, "ZIP must contain exactly one HD-Player.exe")
        entry = matches[0]
        require(not entry.flag_bits & 1 and not stat.S_ISLNK(entry.external_attr >> 16)
                and 0 < entry.file_size <= LIMIT, "Encrypted, linked or oversized ZIP player")
        with archive.open(entry) as stream:
            player = stream.read(LIMIT + 1)
        require(len(player) == entry.file_size, "Truncated/oversized ZIP player")
        return player, hashlib.sha256(data).hexdigest()


def version(ms, ls):
    return f"{ms >> 16}.{ms & 65535}.{ls >> 16}.{ls & 65535}"


def image(data):
    require(0 < len(data) <= LIMIT, "PE exceeds the inventory byte limit")
    try:
        pe = pefile.PE(data=data, fast_load=True)
    except pefile.PEFormatError as error:
        raise InventoryError("Malformed PE image") from error
    try:
        require(0 < pe.FILE_HEADER.NumberOfSections <= 96, "Invalid PE section count")
        pe.parse_data_directories(directories=[1, 2, 13])
        imports, tags = set(), set()
        modules = [entry for kind in ("DIRECTORY_ENTRY_IMPORT", "DIRECTORY_ENTRY_DELAY_IMPORT") for entry in getattr(pe, kind, [])]
        require(len(modules) <= MAX_ENTRIES, "Too many imported modules")
        work = 0
        for module in modules:
            name = module.dll.decode("ascii").lower()
            require(re.fullmatch(r"[a-z0-9_.-]{1,260}", name) is not None, "Unsafe import basename")
            imports.add(name)
            for symbol in module.imports:
                work += 1
                require(work <= 131072, "Too many imported symbols")
                match = re.fullmatch(rb"qt_version_tag_([56])_([0-9]{1,3})", symbol.name or b"")
                if match and name == f"qt{int(match[1])}core.dll":
                    tags.add(f"{int(match[1])}.{int(match[2])}")
        fixed = getattr(pe, "VS_FIXEDFILEINFO", [])
        require(len(fixed) <= 64, "Too many fixed-version records")
        versions = {(version(v.FileVersionMS, v.FileVersionLS), version(v.ProductVersionMS, v.ProductVersionLS),
                     v.FileFlags, v.FileType, v.Signature, v.StrucVersion) for v in fixed}
        require(len(versions) <= 1, "Conflicting fixed-version records")
        file_version = product_version = None
        release = False
        file_type = None
        if versions:
            file_version, product_version, flags, file_type, signature, structure = versions.pop()
            require(signature == 0xFEEF04BD and structure == 0x10000, "Invalid VS_FIXEDFILEINFO")
            release = flags == 0
        return {
            "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data),
            "machine": f"0x{pe.FILE_HEADER.Machine:04x}", "dll": bool(pe.FILE_HEADER.Characteristics & 0x2000),
            "fileVersion": file_version, "productVersion": product_version,
            "releaseFixedFlags": release, "fileType": file_type,
            "imports": sorted(imports), "compiledMinorTags": sorted(tags),
            "sections": [(s.PointerToRawData, s.SizeOfRawData, s.Characteristics) for s in pe.sections],
        }
    except (UnicodeError, struct.error, AttributeError, pefile.PEFormatError) as error:
        raise InventoryError("Malformed PE inventory data") from error
    finally:
        pe.close()


def qml_compiler_headers(data, sections):
    """Bounded header evidence, NOT resource ownership or a runtime version.

    Qt 5.15's Unit prefix labels qtVersion as the COMPILER version. Check the
    stable prefix and table bounds, but do not claim these are active resources.
    """
    counts = Counter()
    position = work = 0
    while True:
        position = data.find(b"qv4cdata", position)
        if position < 0:
            break
        work += 1
        require(work <= MAX_ENTRIES, "Too many QML cache header candidates")
        if position + 248 <= len(data):
            fmt, qt = struct.unpack_from("<II", data, position + 8)
            size, = struct.unpack_from("<I", data, position + 24)
            count, table = struct.unpack_from("<II", data, position + 112)
            mapped = any(flags & 0x40000000 and start <= position and position + size <= min(start + length, len(data))
                         for start, length, flags in sections)
            if (fmt == 0x29 and qt >> 16 == 5 and size >= 248 and mapped
                    and 248 <= table <= size and count <= (size - table) // 4):
                counts[f"{qt >> 16}.{(qt >> 8) & 255}.{qt & 255}"] += 1
        position += 8
    return dict(sorted(counts.items()))


def inspect(path):
    path = safe_path(path)
    data, archive_hash = input_bytes(path)
    player = image(data)
    require(not player["dll"], "Expected a player EXE, not a renamed DLL")
    families = [major for major in (5, 6) if f"qt{major}core.dll" in player["imports"]]
    qt_imports = [name for name in player["imports"] if name.startswith("qt")]
    compiler_headers = qml_compiler_headers(data, player.pop("sections"))
    report = {"input": path.name, "archiveSha256": archive_hash, "player": player,
              "qtMajor": families[0] if len(families) == 1 else None, "qtImports": qt_imports,
              "qmlCacheCompilerHeaders": compiler_headers, "runtimeDlls": {}, "runtimeQtVersion": None,
              "runtimeInventoryComplete": False, "playerExecuted": False, "targetModified": False,
              "shaderSupport": "not-certified-by-this-inventory", "issues": []}
    if len(families) != 1 or any(not re.fullmatch(rf"qt{families[0]}[a-z0-9]+\.dll", name)
                                 or name.endswith("d.dll") for name in qt_imports):
        report["issues"].append("Missing, mixed, debug/suffixed or unknown Qt import family; no runtime version guessed.")
        return report
    if player["machine"] != "0x8664":
        report["issues"].append("The current shader implementation requires a native Windows x64 host; Android instance bitness is separate.")
    if archive_hash:
        report["issues"].append("EXE-only archive evidence; adjacent runtime DLLs were not inspected.")
        return report
    adjacent = list(path.parent.iterdir())
    require(len(adjacent) <= MAX_ENTRIES, "Too many adjacent directory entries")
    major = families[0]
    runtime_versions = set()
    required_present = True
    valid = True
    for part in PARTS:
        name = f"qt{major}{part}.dll"
        matches = [p for p in adjacent if p.name.lower() == name]
        require(len(matches) <= 1, f"Case-ambiguous adjacent Qt provider: {name}")
        if not matches:
            if part in REQUIRED:
                required_present = False
                report["issues"].append(f"Missing adjacent {name}; exact runtime release remains unverified.")
            continue
        runtime = image(read(matches[0]))
        runtime.pop("sections")
        version_parts = (runtime["fileVersion"] or "").split(".")
        acceptable = (runtime["dll"] and runtime["machine"] == "0x8664" and runtime["fileType"] == 2
                      and runtime["releaseFixedFlags"] and runtime["fileVersion"] == runtime["productVersion"]
                      and len(version_parts) == 4 and version_parts[0] == str(major) and version_parts[3] == "0")
        runtime["fixedVersionConsistent"] = acceptable
        report["runtimeDlls"][name] = runtime
        valid = valid and acceptable
        if acceptable:
            runtime_versions.add(".".join(version_parts[:3]))
        else:
            report["issues"].append(f"Invalid/missing release fixed version, architecture or DLL type: {name}")
    if len(runtime_versions) > 1:
        report["issues"].append("Adjacent Qt runtime providers have mixed fixed versions; no compatible set is claimed.")
    if required_present and valid and len(runtime_versions) == 1 and player["machine"] == "0x8664":
        report["runtimeQtVersion"] = runtime_versions.pop()
        report["runtimeInventoryComplete"] = True
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("players", nargs="+", type=Path, help="Explicit player EXEs or EXE-only reference ZIPs; no installation is auto-selected")
    args = parser.parse_args()
    reports = []
    failed = False
    for path in args.players:
        try:
            reports.append(inspect(path))
        except (InventoryError, OSError, zipfile.BadZipFile, RuntimeError) as error:
            reports.append({"input": path.name, "error": str(error), "playerExecuted": False, "targetModified": False})
            failed = True
    print(json.dumps({"schema": "bsi.emulator-qt-inventory.v1", "reports": reports}, indent=2))
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
