"""Offline hash/license/packaging checks for the pinned resource encoder."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
directory = root / "src/third_party/zopfli"
record = json.loads((directory / "UPSTREAM.json").read_text())
assert record["repository"] == "https://github.com/google/zopfli"
assert record["commit"] == "bd64b2f0553d4f1ef4e6627647c5d9fc8c71ffc0"
assert record["tag"] == "zopfli-1.0.3"
assert "src/third_party/zopfli/* -text" in (root / ".gitattributes").read_text(), "Windows checkouts must preserve upstream source/license bytes"
assert len(record["files"]) == 23
for name, digest in record["files"].items():
    assert Path(name).name == name
    assert hashlib.sha256((directory / name).read_bytes()).hexdigest() == digest, name
assert set(record["files"]) == {p.name for p in directory.iterdir() if p.suffix in (".c", ".h")} | {"LICENSE"}
assert "Apache License" in (directory / "LICENSE").read_text()
assert "Version 2.0" in (directory / "LICENSE").read_text()
for name in ("Build-SingleExe.ps1", "Test-SingleExe.ps1"):
    text = (root / "scripts" / name).read_text()
    for notice in ("LICENSE", "README.bsi.md", "UPSTREAM.json"):
        assert "licenses/zopfli/" + notice in text, (name, notice)
print("PASS: pinned unmodified Zopfli source hashes, Apache-2.0 license and mandatory portable attribution/provenance files")
