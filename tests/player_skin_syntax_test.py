"""Parse generated emulator QML with Qt's real parser, without OEM modules.
This validates syntax, not native backend behavior or live emulator rendering.
"""
from pathlib import Path
import os
import subprocess
import sys
import PySide6

formatter=Path(PySide6.__file__).parent / ("qmlformat.exe" if os.name=="nt" else "qmlformat")
files=list(Path(sys.argv[1]).rglob("*.qml"))
assert len(files)==78, f"Expected 13 generated components for each of 6 packs, found {len(files)}"
for path in files:
    result=subprocess.run([str(formatter),str(path)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,text=True)
    if result.returncode and os.environ.get("GITHUB_ACTIONS"):
        print(f"::error::{path}: {result.stderr}",flush=True)
    assert result.returncode==0,(str(path),result.stderr)
print(f"PASS: real Qt QML parser accepts {len(files)} generated player components; no OEM code executed")
