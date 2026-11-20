"""Offline hash/license/packaging checks for the pinned resource encoder."""
import hashlib
import json
import shutil
import subprocess
import tempfile
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
# The pinned original stays intact. Independently execute/verify the two-load
# build correction instead of disabling UBSan alignment/alias checks.
with tempfile.TemporaryDirectory(prefix="bsi-zopfli-provenance-") as temporary:
    output = Path(temporary) / "bsi_lz77.c"
    script = root / "src/core/ZopfliMatch.cmake"
    command = [shutil.which("cmake"),
               f"-DBSI_ZOPFLI_UPSTREAM_FILE={directory / 'lz77.c'}",
               f"-DBSI_ZOPFLI_MATCH_OUTPUT={output}", "-P", str(script)]
    assert command[0], "CMake is required to verify the compiled match-source correction"
    subprocess.run(command, check=True, capture_output=True, text=True)
    # The build script is project text, so Windows may check it out as CRLF.
    # CMake parsing must produce the very same pinned generated C bytes.
    crlf_script = Path(temporary) / "crlf-match.cmake"
    crlf_script.write_bytes(script.read_text().replace("\n", "\r\n").encode())
    subprocess.run(command[:-1] + [str(crlf_script)], check=True, capture_output=True, text=True)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    assert digest == "557de25382d40718ef590ed8d71056d247f9b2843081d3f37845c64a6291fa95"
    assert digest in (directory / "README.bsi.md").read_text()
    assert output.read_bytes().count(b"memcpy(&scan_word") == 2
    damaged = Path(temporary) / "changed-lz77.c"
    damaged.write_bytes((directory / "lz77.c").read_bytes() + b"\n")
    command[1] = f"-DBSI_ZOPFLI_UPSTREAM_FILE={damaged}"
    assert subprocess.run(command, capture_output=True).returncode != 0, "Changed upstream input must refuse"
for name in ("Build-SingleExe.ps1", "Test-SingleExe.ps1"):
    text = (root / "scripts" / name).read_text()
    for notice in ("LICENSE", "README.bsi.md", "UPSTREAM.json"):
        assert "licenses/zopfli/" + notice in text, (name, notice)
print("PASS: 23 byte-exact upstream Zopfli hashes, separately hash-verified defined-load build correction, changed-source refusal and mandatory Apache-2.0/provenance notices")
