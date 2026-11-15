"""Real QML syntax and generated-Assets runtime checks (no OEM backend mocked).
The native hooked sources are parsed only: live emulator behavior needs Windows.
"""
from pathlib import Path
import json
import os
import subprocess
import sys
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
import PySide6
from PySide6.QtCore import QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest

root = Path(sys.argv[1])
formatter = Path(PySide6.__file__).parent / ("qmlformat.exe" if os.name == "nt" else "qmlformat")
app = QGuiApplication([])
engine = QQmlEngine()
errors = []
engine.warnings.connect(lambda es: errors.extend(e.toString() for e in es))
count = 0
for name in ("carbon", "amethyst", "arctic", "matrix", "ember", "nord"):
    group = root / name
    files = list(group.rglob("*.qml"))
    assert len(files) == 22, (name, len(files))
    for path in files:
        result = subprocess.run([str(formatter), str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        assert result.returncode == 0, (path, result.stderr)
        count += 1
    bundles = list(group.glob("Assets*/studio-*"))
    assert len(bundles) == 1
    bundle = bundles[0]
    assert not (bundle / "BootVideo.qml").exists()
    assert not (bundle / "Navigation.js").exists(), "retain native navigation, never ship a mock replacement"
    for leaf in ("UiSettingsWindow.qml", "PlrSidebar.qml", "PlrTopbar.qml", "UiImageButton.qml"):
        text = (group / "native" / leaf).read_text()
        assert text.count("BSI_STUDIO_HOOK_BEGIN") == 1
        assert text.count("BSIStudio.Theme.") == 4
    qml = '''import QtQuick 2.15
import %s as Studio
Item {
 width: 640; height: 400
 Studio.SettingsShell { width: 500; height: 300 }
 Studio.BSISettingsOutline { y: 310; width: 150; height: 32; selected: true }
 Studio.NativeControlStyle { x: 160; y: 310; width: 40; height: 32; hovered: true }
 Studio.SidebarSkin { x: 560; width: 40; height: 300 }
 Studio.TopbarSkin { y: 360; width: 600; height: 32 }
 property int actualCorner: Studio.Theme.corner
 property string actualMotion: Studio.Theme.motion
}''' % json.dumps(QUrl.fromLocalFile(str(bundle.resolve())).toString())
    component = QQmlComponent(engine)
    component.setData(qml.encode(), QUrl.fromLocalFile(str(root.resolve() / "host.qml")))
    obj = component.create()
    assert obj is not None, [e.toString() for e in component.errors()]
    profile = json.loads((bundle / "profile.json").read_text())
    assert obj.property("actualCorner") == profile["radius"]
    assert obj.property("actualMotion") == profile["motion"]
    QTest.qWait(220)
    obj.deleteLater()
    QTest.qWait(1)
assert not errors, errors
print(f"PASS: {count} native/Assets QML sources parsed, all six generated bundles instantiate against real Qt; no emulator behavior claimed")
