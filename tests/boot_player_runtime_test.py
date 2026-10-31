"""Exercise the actual generated player using synthetic APIs, never codecs."""
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
import re
import subprocess
import sys

# Isolate composite module/type caches and Qt engine teardown per provider.
if len(sys.argv) == 1:
    for provider in ("qt6", "qt5", "web"):
        subprocess.run([sys.executable, "-X", "faulthandler", "-u", __file__, provider], check=True)
    sys.exit(0)
import tempfile
from pathlib import Path
from PySide6.QtCore import QObject, QUrl, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest

app = QGuiApplication([])
source = (Path(__file__).resolve().parents[1] / "src/core/BootVideoPatcher.cpp").read_text()
qml = re.search(r'R"QML\((.*?)\)QML"', source, re.S).group(1)
# Expose swallowed import/type errors without changing production behavior.
qml = qml.replace('catch(e) {}', 'catch(e) { console.warn("Provider unavailable: " + e) }')
messages = []
def log(kind, context, message):
    messages.append(message)
qInstallMessageHandler(log)

for provider in (sys.argv[1],):
    with tempfile.TemporaryDirectory(prefix="boot ' & Ω ") as tmp:
        directory = Path(tmp)
        imports = directory / "imports"
        media = imports / "QtMultimedia"
        media.mkdir(parents=True)
        files = {}
        if provider == "qt6":
            files = {
                "MediaPlayer": 'import QtQuick\nQtObject{enum Loops{Infinite=1}property url source;property int loops;property var videoOutput;property var audioOutput;property bool started:false;function play(){started=true}}',
                "VideoOutput": 'import QtQuick\nItem{enum FillMode{PreserveAspectCrop=1}property int fillMode}',
                "AudioOutput": 'import QtQuick\nQtObject{property bool muted:false}',
            }
        elif provider == "qt5":
            files = {
                "MediaPlayer": 'import QtQuick\nQtObject{enum Loops{Infinite=1}}',
                "VideoOutput": 'import QtQuick\nQtObject{enum FillMode{PreserveAspectCrop=1}}',
                "Video": 'import QtQuick\nItem{property url source;property int loops;property int fillMode;property bool muted;property bool autoPlay;property bool started:false;function play(){started=true}}',
            }
        elif provider == "web":
            # Empty qmldir files can fall through to the real SDK module.
            # Explicitly unavailable types keep this a codec-free fallback test.
            files = {
                "MediaPlayer": 'import QtQuick\nMissingSyntheticMultimediaApi {}',
                "Video": 'import QtQuick\nMissingSyntheticMultimediaApi {}',
            }
        (media / "qmldir").write_text("module QtMultimedia\n" + "\n".join(f"{name} 5.15 {name}.qml" for name in files))
        for name, content in files.items():
            (media / f"{name}.qml").write_text(content)
        web = imports / "QtWebEngine"
        web.mkdir()
        (web / "qmldir").write_text("module QtWebEngine\nWebEngineView 1.0 WebEngineView.qml\n")
        (web / "WebEngineView.qml").write_text("import QtQuick\nItem{objectName:'bsiWebBootVideo';property url url}")
        path = directory / "BootVideo.qml"
        path.write_text(qml)
        engine = QQmlEngine()
        engine.addImportPath(str(imports))
        component = QQmlComponent(engine, QUrl.fromLocalFile(str(path)))
        obj = component.create()
        QTest.qWait(30)
        name = "bsiWebBootVideo" if provider == "web" else "bsiBootVideo"
        player = obj.findChild(QObject, name) if obj else None
        try:
            assert obj is not None, [e.toString() for e in component.errors()]
            assert player is not None, messages
            if provider != "web":
                assert player.property("started"), messages
                assert player.property("source") == QUrl.fromLocalFile(str(directory / "Promo.mp4")), player.property("source")
            else:
                assert player.property("url").scheme() == "data"
        except AssertionError as error:
            for line in [f"{provider}: {error}"] + messages:
                escaped = line.replace("%", "%25").replace("\n", "%0A").replace("\r", "%0D")
                print(f"::error::{escaped}", flush=True)
            raise
        print(f"PASS: adaptive boot player {provider}", flush=True)
        import shiboken6
        shiboken6.delete(obj)
        shiboken6.delete(component)
        shiboken6.delete(engine)
        messages.clear()
