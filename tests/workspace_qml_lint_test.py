"""Type-check the app entry point and editor QML against Qt 6.8 metadata.

Uses the PySide6 test toolchain only, never the emulator or a mocked Qt module.
Runtime desktop testing is separate; context-property accesses are untyped by
Qt's linter and are reported as informational rather than suppressed errors.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import PySide6
from PySide6.QtCore import QLibraryInfo

source = Path(__file__).resolve().parents[1] / "src/qml"
qt = Path(PySide6.__file__).resolve().parent
linter = qt / ("qmllint.exe" if os.name == "nt" else "qmllint")
with tempfile.TemporaryDirectory() as directory:
    module = Path(directory) / "BSICustomizer"
    shutil.copytree(source, module)
    entries = ["module BSICustomizer", "singleton Theme 1.0 Theme.qml"]
    for path in module.rglob("*.qml"):
        if path.name != "Theme.qml":
            entries.append(f"{path.stem} 1.0 {path.relative_to(module).as_posix()}")
    (module / "qmldir").write_text("\n".join(entries))
    report = Path(directory) / "lint.json"
    files = [source / "pages" / name for name in (
        "WorkspacePage.qml", "ThemePage.qml", "GifRpcPage.qml", "BootVideoPage.qml")]
    files += [source / "components/ThemePreview.qml", source / "Main.qml", source / "pages/ResourcesPage.qml"]
    result = subprocess.run([str(linter), "-I", directory, "-I", QLibraryInfo.path(QLibraryInfo.QmlImportsPath),
                             "--unqualified", "info", "--unused-imports", "info",
                             "--json", str(report), *map(str, files)], capture_output=True, text=True)
    data = json.loads(report.read_text())
    failures = [(Path(f["filename"]).name, w) for f in data["files"]
                for w in f.get("warnings", []) if w["type"] in ("warning", "error")]
    assert result.returncode == 0 and not failures, (result.stdout, result.stderr, failures)
print("PASS: real Qt 6.8 type metadata; app entry point, Resources, Workspace, Theme, preview, Boot and GIF RPC QML")
