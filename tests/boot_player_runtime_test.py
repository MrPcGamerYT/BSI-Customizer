"""Exercise the HTML-backed player wrapper with a synthetic WebEngine API."""
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
import re
import subprocess
import sys

if len(sys.argv) == 1:
    subprocess.run([sys.executable, "-X", "faulthandler", "-u", __file__, "web"], check=True)
    sys.exit(0)

import tempfile
from pathlib import Path
from PySide6.QtCore import QObject, QUrl, QCoreApplication, QEvent, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest

app = QGuiApplication([])
source = (Path(__file__).resolve().parents[1] / "src/core/BootVideoPatcher.cpp").read_text()
qml = re.search(r'R"QML\((.*?)\)QML"', source, re.S).group(1)
html = re.search(r'R"HTML\((.*?)\)HTML"', source, re.S).group(1)
assert 'Qt.resolvedUrl("BootScreen.html")' in qml
assert "localContentCanAccessFileUrls: true" in qml
assert "playbackRequiresUserGesture: false" in qml
assert 'class="ring-track"' in html and "Emulator is starting…" in html
assert "__BSI_EMULATOR_LOGO__" in html and "__VIDEO_BACKGROUND__" in html
assert "Promo.mp4" in html and "@media(prefers-reduced-motion:reduce)" in html

messages = []
def log(kind, context, message):
    messages.append(message)
qInstallMessageHandler(log)

with tempfile.TemporaryDirectory(prefix="boot html ' & Ω ") as tmp:
    directory = Path(tmp)
    module = directory / "imports" / "BsiFixtureWebEngine"
    module.mkdir(parents=True)
    (module / "qmldir").write_text("module BsiFixtureWebEngine\nWebEngineView 1.0 WebEngineView.qml\n")
    (module / "WebEngineView.qml").write_text(
        "import QtQuick\n"
        "Item{objectName:'bsiBootHtmlView';property url url;property color backgroundColor;"
        "property QtObject settings:QtObject{property bool playbackRequiresUserGesture:true;"
        "property bool localContentCanAccessFileUrls:false}}\n"
    )
    qml_path = directory / "Assets" / "BootVideo.qml"
    qml_path.parent.mkdir()
    qml_path.write_text(qml.replace("import QtWebEngine", "import BsiFixtureWebEngine"))
    html_path = qml_path.parent / "BootScreen.html"
    html_path.write_text(html.replace("__BSI_EMULATOR_LOGO__", "data:image/svg+xml;base64,PHN2Zz48L3N2Zz4=")
                         .replace("__VIDEO_BACKGROUND__", ""))

    engine = QQmlEngine()
    engine.addImportPath(str(directory / "imports"))
    component = QQmlComponent(engine, QUrl.fromLocalFile(str(qml_path)))
    obj = component.create()
    QTest.qWait(30)
    try:
        assert obj is not None, [error.toString() for error in component.errors()]
        view = obj.findChild(QObject, "bsiBootHtmlView")
        assert view is not None, messages
        assert view.property("url") == QUrl.fromLocalFile(str(html_path)), view.property("url")
        settings = view.property("settings")
        assert settings.property("playbackRequiresUserGesture") is False
        assert settings.property("localContentCanAccessFileUrls") is True
    except AssertionError as error:
        for line in [str(error)] + messages:
            escaped = line.replace("%", "%25").replace("\n", "%0A").replace("\r", "%0D")
            print(f"::error::{escaped}", flush=True)
        raise
    view = None
    obj.deleteLater()
    obj = None
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    component.deleteLater()
    component = None
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    engine.collectGarbage()
    engine.clearComponentCache()
    engine.deleteLater()
    engine = None
    QCoreApplication.sendPostedEvents(None, QEvent.DeferredDelete)
    QTest.qWait(1)

print("PASS: boot page loads Assets/BootScreen.html through WebEngine with local MP4 access and autoplay")
