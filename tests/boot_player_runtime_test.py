"""Exercise HTML loading and the legacy QtQuick fallback without real codecs."""
import os
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")
import re
import subprocess
import sys

if len(sys.argv) == 1:
    for provider in ("web", "webfail", "fallback"):
        subprocess.run([sys.executable, "-X", "faulthandler", "-u", __file__, provider], check=True)
    sys.exit(0)

import tempfile
from pathlib import Path
from PySide6.QtCore import QObject, QUrl, QCoreApplication, QEvent, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest

provider = sys.argv[1]
assert provider in ("web", "webfail", "fallback")
app = QGuiApplication([])
source = (Path(__file__).resolve().parents[1] / "src/core/BootVideoPatcher.cpp").read_text()
qml = re.search(r'R"QML\((.*?)\)QML"', source, re.S).group(1)
html = re.search(r'R"HTML\((.*?)\)HTML"', source, re.S).group(1)
assert 'Qt.resolvedUrl("BootScreen.html")' in qml
assert "webView.settings.localContentCanAccessFileUrls = true" in qml
assert "webView.settings.playbackRequiresUserGesture = false" in qml
assert "request.status===WebEngineView.LoadSucceededStatus" in qml
assert "request.status===WebEngineView.LoadFailedStatus" in qml
assert "webView.url = htmlUrl;\n            return;" in qml

assert 'class="ring-track"' in html and "Emulator is starting…" in html
assert "__BSI_EMULATOR_LOGO__" in qml and "__BSI_WITH_VIDEO__" in qml
assert "Promo.mp4" in source and "__VIDEO_BACKGROUND__" in html

messages = []
def log(kind, context, message):
    messages.append(message)
qInstallMessageHandler(log)

with tempfile.TemporaryDirectory(prefix="boot html ' & Ω ") as tmp:
    directory = Path(tmp)
    imports = directory / "imports"
    module = imports / "BsiFixtureWebEngine"
    module.mkdir(parents=True)
    (module / "qmldir").write_text("module BsiFixtureWebEngine\nWebEngineView 1.0 WebEngineView.qml\n")
    fake_load_status = "3" if provider == "webfail" else "2"
    fake_web = (
        "import QtQuick\n"
        "Item {\n"
        "    objectName: 'bsiBootHtmlView'\n"
        "    enum LoadStatus { LoadStartedStatus, LoadStoppedStatus, LoadSucceededStatus, LoadFailedStatus }\n"
        "    signal loadingChanged(var request)\n"
        "    signal renderProcessTerminated(var status, int exitCode)\n"
        "    property url url\n"
        "    property color backgroundColor\n"
        "    property QtObject settings: QtObject {\n"
        "        property bool playbackRequiresUserGesture: true\n"
        "        property bool localContentCanAccessFileUrls: false\n"
        "    }\n"
        f"    Timer {{ interval: 10; running: true; onTriggered: loadingChanged({{status: {fake_load_status}}}) }}\n"
        "}\n"
    )
    (module / "WebEngineView.qml").write_text(fake_web)
    assets = directory / "Assets"
    assets.mkdir()
    qml_path = assets / "BootVideo.qml"
    runtime_qml = qml.replace("__BSI_EMULATOR_LOGO__", "data:image/svg+xml;base64,PHN2Zz48L3N2Zz4=")
    runtime_qml = runtime_qml.replace("__BSI_WITH_VIDEO__", "false")
    if provider in ("web", "webfail"):
        runtime_qml = runtime_qml.replace("import QtWebEngine", "import BsiFixtureWebEngine")
    else:
        runtime_qml = runtime_qml.replace("import QtWebEngine", "import BsiMissingWebEngine")
    qml_path.write_text(runtime_qml)
    html_path = assets / "BootScreen.html"
    html_path.write_text(html.replace("__BSI_EMULATOR_LOGO__", "data:image/svg+xml;base64,PHN2Zz48L3N2Zz4=")
                         .replace("__VIDEO_BACKGROUND__", ""))

    engine = QQmlEngine()
    engine.addImportPath(str(imports))
    component = QQmlComponent(engine, QUrl.fromLocalFile(str(qml_path)))
    obj = component.create()
    QTest.qWait(30)
    try:
        assert obj is not None, [error.toString() for error in component.errors()]
        ring = obj.findChild(QObject, "bsiLoadingRing")
        status = obj.findChild(QObject, "bsiBootStatus")
        assert ring is not None and status is not None, messages
        if provider in ("web", "webfail"):
            view = obj.findChild(QObject, "bsiBootHtmlView")
            assert view is not None, messages
            assert view.property("url") == QUrl.fromLocalFile(str(html_path)), view.property("url")
            settings = view.property("settings")
            assert settings.property("playbackRequiresUserGesture") is False
            assert settings.property("localContentCanAccessFileUrls") is True
            if provider == "web":
                assert obj.property("htmlLoaded") is True
                assert ring.property("visible") is False
                assert view.property("visible") is True
            else:
                assert obj.property("htmlLoaded") is False
                assert ring.property("visible") is True
                assert status.property("visible") is True
                assert status.property("text") == "Emulator is starting…"
                assert view.property("visible") is False
        else:
            assert ring.property("visible") is True, messages
            assert status.property("text") == "Emulator is starting…", messages
    except AssertionError as error:
        for line in [str(error)] + messages:
            escaped = line.replace("%", "%25").replace("\n", "%0A").replace("\r", "%0D")
            print(f"::error::{escaped}", flush=True)
        raise
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

print(f"PASS: HTML-first boot {provider} path and startup ring/status")
