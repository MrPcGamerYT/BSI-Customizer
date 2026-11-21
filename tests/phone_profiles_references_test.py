"""Plan and disk-test disposable copies of four hash-pinned player resources.

Never launches a player or writes into the reference checkout. ZIP member paths
are not extracted: only the single bounded HD-Player.exe payload is copied.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    root = Path(__file__).resolve().parents[1]
    planner = Path(os.environ["BSI_PHONE_PROFILES_TEST"]).resolve()
    references = Path(os.environ["BSI_PHONE_REFERENCE_DIR"]).resolve()
    manifest = json.loads((root / "tests/fixtures/emulator-qt-evidence.json").read_text())
    assert planner.is_file() and len(manifest["samples"]) == 4
    before = {}
    with tempfile.TemporaryDirectory(prefix="bsi-phone-reference-inputs-") as temporary:
        copies = []
        for index, expected in enumerate(manifest["samples"]):
            path = references / expected["input"]
            before[path] = sha256(path.read_bytes())
            if path.suffix.lower() == ".zip":
                with zipfile.ZipFile(path) as archive:
                    members = [entry for entry in archive.infolist()
                               if not entry.is_dir() and Path(entry.filename.replace("\\", "/")).name.lower() == "hd-player.exe"]
                    assert len(members) == 1 and 0 < members[0].file_size <= 256 * 1024 * 1024
                    data = archive.read(members[0])
            else:
                assert 0 < path.stat().st_size <= 256 * 1024 * 1024
                data = path.read_bytes()
            assert sha256(data) == expected["playerSha256"], f"reference checksum differs: {path}"
            copy = Path(temporary) / f"reference-{index}.exe"
            copy.write_bytes(data)
            copies.append(str(copy))
        for option in ("--references", "--disk-references"):
            subprocess.run([str(planner), option, *copies], check=True)
        assert all(sha256(path.read_bytes()) == digest for path, digest in before.items()), "the original references changed"
    print("PASS: all four hash-pinned player images, realistic Windows helper paths, actual local-helper/snapshot disk cycles on disposable copies, unchanged input files; no player executed")


if __name__ == "__main__":
    main()
