"""Compare the two supplied Potato archives as DATA; never load their DLLs.

Requires rarfile and a trusted UnRAR executable only for a real local review.
The default tests use synthetic bytes and an in-memory fake archive, no network,
original payload, decompressor or Windows execution. No extracted files persist.
This tool is deliberately separate from the opted-in original-DLL runner.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import struct
import sys

PREVIOUS = {
    "name": "POTATO FF DLL 7FARFF.rar",
    "driveFileId": "1QGBvjTpW0mRNPwv2eqj77UEsLQtNDV7Y",
    "bytes": 344442,
    "sha256": "80751bed1151e7f5bbcf186b51b8aada3b446af0278e5575384de7fa9eeb8b17",
    "entries": 17,
}
NEW = {
    "name": "POTATO FF DLL BY ANSHU.rar",
    "driveFileId": "1Ex-E3YCwkq5wWschqaFMdNv9abB0MV9-",
    "bytes": 345058,
    "sha256": "ed56050e890d9bf9efa8df08cd5eee2f143041cb49b983760c231e2c1fbca449",
    "entries": 18,
}
MAX_MEMBER_BYTES = 2 * 1024 * 1024
MAX_EXPANDED_BYTES = 16 * 1024 * 1024


def require(okay, message):
    if not okay:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def amd64_dll(data):
    """Bounded PE header parsing, not loading/entry-point invocation."""
    if len(data) < 64 or data[:2] != b"MZ":
        return False
    offset = struct.unpack_from("<I", data, 0x3c)[0]
    if offset < 64 or offset + 26 > len(data) or data[offset:offset + 4] != b"PE\0\0":
        return False
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    characteristics = struct.unpack_from("<H", data, offset + 22)[0]
    magic = struct.unpack_from("<H", data, offset + 24)[0]
    return machine == 0x8664 and bool(characteristics & 0x2000) and magic == 0x20b


def inspect_archive(path, identity, password, archive_factory):
    require(path.is_file() and not path.is_symlink(), "A regular local archive is required")
    require(path.stat().st_size == identity["bytes"], "Archive size differs from fixed reference")
    require(sha(path.read_bytes()) == identity["sha256"], "Archive SHA-256 differs from fixed reference")
    require(bool(password), "Archive password is required")
    rows = []
    with archive_factory(path) as archive:
        archive.setpassword(password)
        infos = archive.infolist()
        require(len(infos) == identity["entries"] and 0 < len(infos) <= 100,
                "Unexpected or excessive archive inventory")
        require(all(0 <= info.file_size <= MAX_MEMBER_BYTES for info in infos),
                "Archive member exceeds the data inspection bound")
        require(sum(info.file_size for info in infos) <= MAX_EXPANDED_BYTES,
                "Archive expanded size exceeds the data inspection bound")
        seen = set()
        # Validate ALL destinations before asking a decompressor for any data.
        for info in infos:
            name = PurePosixPath(info.filename.replace("\\", "/"))
            require(not name.is_absolute() and ".." not in name.parts
                    and ":" not in str(name) and "\0" not in str(name)
                    and str(name) not in {"", "."} and not info.is_symlink(),
                    "Unsafe archive entry")
            key = str(name).casefold()
            require(key not in seen, "Duplicate normalized archive entry")
            seen.add(key)
            if info.isdir():
                require(info.file_size == 0, "Directory claims payload bytes")
            else:
                require(name.suffix.lower() == ".dll", "Unexpected non-DLL member; not executed")
        for info in infos:
            if info.isdir():
                continue
            # read(), not extract()/extractall(): no member name is a disk path.
            data = archive.read(info)
            require(len(data) == info.file_size, "Expanded member size differs from inventory")
            require(amd64_dll(data), "Reference member is not an AMD64 PE DLL")
            rows.append({"name": info.filename, "bytes": len(data), "sha256": sha(data), "data": data})
    require(bool(rows), "Empty DLL set cannot establish a match")
    return {"identity": dict(identity), "expandedFileBytes": sum(r["bytes"] for r in rows), "members": rows}


def compare(previous, new):
    old_rows = previous["members"]
    new_rows = new["members"]
    members = []
    for row in new_rows:
        candidates = [old for old in old_rows if old["sha256"] == row["sha256"]]
        # The actual review compares full bytes as well as SHA-256; matching
        # filenames, archive labels or only the .text section are insufficient.
        matches = [old for old in candidates if old["data"] == row["data"]]
        members.append({key: row[key] for key in ("name", "bytes", "sha256")} | {
            "sameCompleteBytesAsPrevious": bool(matches),
            "previousMembers": [old["name"] for old in matches],
        })
    same_set = {r["sha256"] for r in old_rows} == {r["sha256"] for r in new_rows}
    same_counts = Counter(r["sha256"] for r in old_rows) == Counter(r["sha256"] for r in new_rows)
    all_bytes = bool(members) and all(r["sameCompleteBytesAsPrevious"] for r in members)
    return {
        "schema": 1,
        "execution": "none; archives decompressed and PE headers/full bytes parsed as data only",
        "previousArchive": previous["identity"],
        "newArchive": new["identity"],
        "expandedFileBytes": new["expandedFileBytes"],
        "dllFiles": len(members),
        "uniqueDllByteSequences": len({r["sha256"] for r in new_rows}),
        "allDllBytesPreviouslyReviewed": all_bytes,
        "sameUniqueDllSet": same_set,
        "sameDllMultiplicity": same_counts,
        "identicalDllContents": all_bytes and same_set and same_counts,
        "members": members,
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--previous", type=Path, required=True)
    parser.add_argument("--new", type=Path, required=True)
    parser.add_argument("--unrar", type=Path, help="Trusted upstream decompressor, never an archive member")
    parser.add_argument("--report", type=Path, help="New JSON file only; never DLL/archive output")
    args = parser.parse_args(argv)
    import rarfile
    if args.unrar:
        require(args.unrar.is_file(), "Trusted UnRAR tool is missing")
        rarfile.UNRAR_TOOL = str(args.unrar.resolve())
    previous = inspect_archive(args.previous, PREVIOUS, os.environ.get("BSI_POTATO_PREVIOUS_PASSWORD"), rarfile.RarFile)
    new = inspect_archive(args.new, NEW, os.environ.get("BSI_POTATO_NEW_PASSWORD"), rarfile.RarFile)
    result = compare(previous, new)
    text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.report:
        require(args.report.suffix.lower() == ".json", "Report must have a JSON extension")
        require(not args.report.exists() and not args.report.is_symlink(), "Refusing to overwrite an existing report or source")
        with args.report.open("x", encoding="utf-8") as report:
            report.write(text)
    print(text, end="")
    return 0 if result["identicalDllContents"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print("Reference DATA comparison refused: " + str(error), file=sys.stderr)
        raise SystemExit(2)
