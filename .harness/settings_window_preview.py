"""Live preview of the GENERATED in-player Settings shell, not its editor.

Uses the exact C++ QML template and sizing helper, real Qt 6 controls, the
supplied default icons rasterized to PNG, and explicit sample pages/services.
No player executable, VM, license service or system setting is accessed.

    python .harness/settings_window_preview.py --port 8080

HTTP requests enqueue work for Qt's GUI thread; the browser forwards actual
pointer/wheel/key input and receives rendered Qt frames (not an HTML remake).
"""
import argparse
import atexit
import hashlib
import sys
import uuid
import http.server
import json
import os
import queue
import re
import tempfile
import threading
import time
import urllib.parse
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")

QT5 = os.environ.get("BSI_TEST_QT_API") == "PyQt5"
os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Default" if QT5 else "Basic")
if QT5:
    from PyQt5.QtCore import QBuffer, QCoreApplication, QEvent, QIODevice, QLibraryInfo, QObject, QPoint, QPointF, QStandardPaths, QTimer, QUrl, Qt, pyqtSlot as Slot
    from PyQt5.QtGui import QColor, QFont, QFontDatabase, QGuiApplication, QImage, QMouseEvent, QPainter, QWheelEvent
    from PyQt5.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PyQt5.QtQuick import QQuickWindow
    from PyQt5.QtSvg import QSvgRenderer
    from PyQt5.QtTest import QSignalSpy, QTest
else:
    from PySide6.QtCore import QBuffer, QCoreApplication, QEvent, QIODevice, QLibraryInfo, QObject, QPoint, QPointF, QStandardPaths, QTimer, QUrl, Qt, Slot
    from PySide6.QtGui import QColor, QFont, QFontDatabase, QGuiApplication, QImage, QMouseEvent, QPainter, QWheelEvent
    from PySide6.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PySide6.QtQuick import QQuickWindow
    from PySide6.QtSvg import QSvgRenderer
    from PySide6.QtTest import QSignalSpy, QTest

REPO = Path(__file__).resolve().parents[1]
LABELS = ["Performance", "Display", "Graphics", "Game settings", "Devices", "Gamepad",
          "Preferences", "Phone", "Shortcuts", "User data", "Advanced", "About"]
ICON_FILES = ["performance", "display", "graphics", "game-settings", "devices", "game-settings",
              "preferences", "phone", "shortcuts", "user-data", "advanced", "about"]
THEME = """pragma Singleton
import QtQuick 2.15
QtObject {
    property QtObject colors: QtObject {
        property color primary10: "#f4f7fb"
        property color primary20: "#d2dae4"
        property color primary30: "#8c9aaf"
        property color primary60: "#2d3a4d"
        property color primary70: "#192637"
        property color primary80: "#101a27"
        property color accentNormal: "#54c8ff"
        property color subWindowHeaderClick: "#54c8ff"
        property color dangerNormal: "#ee3344"
    }
    property QtObject fonts: QtObject {
        property font bodySmall: Qt.font({family:"Inter",pixelSize:11})
        property font bodyMedium: Qt.font({family:"Inter",pixelSize:13})
        property font bodyLarge: Qt.font({family:"Inter",pixelSize:14})
    }
}
"""


def items(root):
    pending = list(root.childItems())
    while pending:
        child = pending.pop()
        yield child
        pending.extend(child.childItems())


def prop(item, name):
    return item.property(name) if item.metaObject().indexOfProperty(name) >= 0 else None


def evaluate(item, text):
    expression = QQmlExpression(QQmlEngine.contextForObject(item), item, text)
    result = expression.evaluate()
    assert not expression.hasError(), expression.error().toString()
    if isinstance(result, tuple):
        value, undefined = result
        return None if undefined else value
    return result


class SampleBridge(QObject):
    """No persistent writes: Save changes only counts a demonstration click."""
    def __init__(self):
        super().__init__()
        self.saves = 0

    @Slot()
    def save(self):
        self.saves += 1


class SettingsPreview:
    def __init__(self, page_source=None, about_links=(), shader_module=None):
        self._closed = False
        self.profile = None
        self.native_ui_cleanup = None
        self.shader_module = Path(shader_module).resolve() if shader_module else None
        self.labels = LABELS + (["Shaders"] if self.shader_module else [])
        if self.shader_module:
            assert (self.shader_module / "BSIShaders/qmldir").is_file(), "Build the real shader module before enabling its preview"
            QStandardPaths.setTestModeEnabled(True)
        # The independent host must not import a different build SDK's QML/QPA
        # plugins (particularly Qt 6's qmldir files in a Qt 5 test process).
        if QT5:
            plugins = QLibraryInfo.location(QLibraryInfo.PluginsPath)
            qml = QLibraryInfo.location(QLibraryInfo.Qml2ImportsPath)
        else:
            plugins = QLibraryInfo.path(QLibraryInfo.LibraryPath.PluginsPath)
            qml = QLibraryInfo.path(QLibraryInfo.LibraryPath.QmlImportsPath)
        os.environ["QT_PLUGIN_PATH"] = plugins
        os.environ["QT_QPA_PLATFORM_PLUGIN_PATH"] = str(Path(plugins) / "platforms")
        os.environ.pop("QML2_IMPORT_PATH", None)
        os.environ.pop("QML_IMPORT_PATH", None)
        QCoreApplication.setLibraryPaths([plugins])
        self.app = QGuiApplication.instance() or QGuiApplication([])
        self.app.setQuitOnLastWindowClosed(False)
        QFontDatabase.addApplicationFont(str(REPO / "src/qml/fonts/Inter.ttf"))
        self.app.setFont(QFont("Inter", 10))
        self.directory = tempfile.TemporaryDirectory(prefix="bsi-settings-preview-")
        root = Path(self.directory.name)
        module = root / "BsiGuiFixture"
        module.mkdir()
        (module / "qmldir").write_text("module BsiGuiFixture\n"
            "singleton UiTheme 1.0 UiTheme.qml\n"
            "UiShadowContainer 1.0 UiShadowContainer.qml\n"
            "UiImageButton 1.0 UiImageButton.qml\n")
        if self.shader_module:
            # The production module now delegates to the player's root-QRC
            # controls/full live theme. This preview has no player resources:
            # use the same explicitly authored contracts as standalone tests,
            # never resurrect the removed Shader* lookalikes or export OEM QML.
            if str(REPO / "tests") not in sys.path:
                sys.path.insert(0, str(REPO / "tests"))
            from shaders import native_ui_fixture as native_ui
            theme_source = native_ui.theme_source(singleton=True)
        else:
            theme_source = THEME
        (module / "UiTheme.qml").write_text(theme_source)
        # Match the native 15 px shadow inset and redirected default children.
        (module / "UiShadowContainer.qml").write_text("""import QtQuick 2.15
Item {
    default property alias children: content.data
    readonly property real shadowThickness: 15
    Item { id: content; anchors.fill: parent; anchors.margins: 15 }
}
""")
        (module / "UiImageButton.qml").write_text("""import QtQuick 2.15
Item {
    width: 24; height: 24
    property string asset: ""
    signal clicked()
    Text { anchors.centerIn: parent; text: "×"; font.pixelSize: 21; color: "#8c9aaf" }
    MouseArea { anchors.fill: parent; onClicked: parent.clicked() }
}
""")
        icons = {}
        for index, name in enumerate(ICON_FILES + (["shaders"] if self.shader_module else [])):
            target = root / (name + ".png")
            if not target.exists():
                svg = (REPO / "src/assets/settings-panel-icons" / (name + ".svg")).read_text()
                svg = svg.replace("<svg ", '<svg color="#e7e9f2" ', 1)
                image = QImage(54, 54, QImage.Format.Format_ARGB32)
                image.fill(Qt.GlobalColor.transparent)
                painter = QPainter(image)
                QSvgRenderer(svg.encode()).render(painter)
                painter.end()
                assert image.save(str(target))
            icons[str(index)] = target.as_uri()
        cpp = (REPO / "src/core/SettingsPanelPatcher.cpp").read_text()
        shell = re.search(r'QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S)[1]
        helper = re.search(r'const QString helper = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S)[1]
        shell = shell.replace("property var bIcons:({})", "property var bIcons:(" + json.dumps(icons) + ")")
        about = re.search(r'QString generated = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S)[1]
        values = {"DEVELOPER": json.dumps("BSI Customizer"), "TAGLINE": json.dumps("Settings window · live shell preview"),
                  "LOGO_SOURCE": json.dumps(icons["11"]), "TAB_ICONS": "(" + json.dumps(icons) + ")",
                  "HIDDEN_TABS": "[]", "LINKS": ",".join(json.dumps(link) for link in about_links)}
        for key, value in values.items():
            about = about.replace("@@BSI_" + key + "@@", value)
        about = about.replace("import QtQuick.Controls 2.15", "import QtQuick.Controls 2.15\nimport BsiGuiFixture 1.0")
        (root / "PlrAboutSetting.qml").write_text(about)
        (root / "DemoPage.qml").write_text(page_source or (REPO / ".harness/settings_preview_page.qml").read_text())
        model = [{"pButtonText": label, "pShowRedDot": False,
                  "pQmlSource": "PlrAboutSetting.qml" if i == 11 else "DemoPage.qml",
                  "pComponentArgs": {} if i == 11 else {"pageIndex": i, "pageTitle": label},
                  "pComponentObj": None} for i, label in enumerate(self.labels)]
        if self.shader_module:
            model[-1].update(pQmlSource="qrc:/bsi-shaders/BsiShadersSetting.qml",
                             pComponentArgs={"bsiTabIndex": 12, "bsiPalette": {}})
        self.instance_name = "SettingsPreview-" + uuid.uuid4().hex
        # Real native rows are QObjects, not QVariantMap/JS-copy snapshots.
        # Loader assignments to pComponentObj must remain shared so native
        # discard callbacks, theme discovery and visual sorting see each page.
        model_qml = ",".join(
            "QtObject{property string pButtonText:" + json.dumps(row["pButtonText"])
            + ";property url pQmlSource:" + json.dumps(row["pQmlSource"])
            + ";property var pComponentArgs:(" + json.dumps(row["pComponentArgs"]) + ")"
            + ";property QtObject pComponentObj:null;property bool pIsVisible:true;property bool pShowRedDot:false}"
            for row in model)
        host = """import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import BsiGuiFixture 1.0
Window {
    id: iSettingsWindow
    color: "transparent"; visible: false
    transientParent: fixturePlayer
    property int pCurrentTabIndex: 0
    property color previewAccent: "#54c8ff"
    onPreviewAccentChanged: { UiTheme.colors.subWindowHeaderClick = previewAccent; UiTheme.colors.accentNormal = previewAccent }
    property bool bsiShaderDirty2: false
    function fPreviewPalette(msi) {
        var c = UiTheme.colors
        c.primary80 = msi ? "#21242c" : "#101a27"
        c.primary70 = msi ? "#17181c" : "#192637"
        c.primary60 = msi ? "#454954" : "#2d3a4d"
        c.primary30 = msi ? "#abafba" : "#8c9aaf"
        c.primary20 = msi ? "#c7c9d1" : "#d2dae4"
        previewAccent = msi ? "#eb2c43" : "#54c8ff"
    }
    property QtObject pSettingsItems: QtObject { property list<QtObject> pSettingElements: [%s] }
    function fChangeSelectedSetting(index) { pCurrentTabIndex = index }
    %s
    %s
}
""" % (model_qml, shell, helper)
        path = root / "SettingsHost.qml"
        if self.shader_module:
            (root / "ShaderDiscardPrompt.qml").write_text((REPO / ".harness/settings_preview_shader_prompt.qml").read_text())
            host = host.replace("import BsiGuiFixture 1.0", "import BsiGuiFixture 1.0\nimport BSIShaders 1.0", 1)
            host = host.replace("property int pCurrentTabIndex: 0", "property int pCurrentTabIndex: 0\n    Bootstrap { hostWindow: fixturePlayer; instanceName: " + json.dumps(self.instance_name) + "; graphicsRenderer: \"gl\" }")
            host = host.replace("function fChangeSelectedSetting(index) { pCurrentTabIndex = index }", '''property alias previewDiscardOpen: shaderPrompt.visible
    function fChangeSelectedSetting(index) {
        if (index === pCurrentTabIndex) return
        if (bsiShaderDirty2) shaderPrompt.request(index, false)
        else pCurrentTabIndex = index
    }
    onClosing: function(event) {
        if (bsiShaderDirty2) { event.accepted = false; shaderPrompt.request(-1, true) }
    }
    ShaderDiscardPrompt {
        id: shaderPrompt
        parent: Overlay.overlay
        onDiscardRequested: function(index, closing) {
            var page = pSettingsItems.pSettingElements[12].pComponentObj
            if (page) page.fDiscardChanges()
            if (closing) iSettingsWindow.close()
            else iSettingsWindow.pCurrentTabIndex = index
        }
    }
    function fShowToast(message) {
        previewToast.message = message
        previewToast.open()
        toastTimer.restart()
    }
    Popup {
        id: previewToast
        objectName: "previewSavedToast"
        property string message: ""
        parent: Overlay.overlay
        x: (parent.width - width) / 2; y: parent.height - height - 76
        padding: 12; closePolicy: Popup.NoAutoClose
        contentItem: Text { text: previewToast.message; font: UiTheme.fonts.bodySmall; color: UiTheme.colors.primary10 }
        background: Rectangle { radius: 6; color: UiTheme.colors.primary70; border.color: UiTheme.colors.primary60 }
    }
    Timer { id: toastTimer; interval: 1400; onTriggered: previewToast.close() }
''')
        path.write_text(host)
        self.engine = QQmlEngine()
        self.engine.setImportPathList(([str(self.shader_module)] if self.shader_module else []) + [str(root), qml, "qrc:/qt-project.org/imports", "qrc:/qt/qml"])
        self.bridge = SampleBridge()
        self.player = QQuickWindow()
        self.player.setGeometry(0, 0, 1440, 900)
        self.engine.rootContext().setContextProperty("fixturePlayer", self.player)
        self.engine.rootContext().setContextProperty("previewBridge", self.bridge)
        if self.shader_module:
            assert not os.environ.get("BSI_SHADERS_NATIVE_UI_DIR"), "the shareable live preview uses authored services, not exported player assets"
            carrier_component = QQmlComponent(self.engine)
            carrier_component.setData(b'import QtQml 2.15; import BsiGuiFixture 1.0; QtObject { property QtObject theme: UiTheme }', QUrl("PreviewTheme.qml"))
            self.theme_carrier = carrier_component.create()
            assert self.theme_carrier is not None, [e.toString() for e in carrier_component.errors()]
            self.theme_carrier.setParent(self.engine)
            _, self.native_ui_cleanup = native_ui.install(self.engine, root, theme=self.theme_carrier.property("theme"))
        self.warnings = []
        self.engine.warnings.connect(lambda errors: self.warnings.extend(e.toString() for e in errors))
        self.component = QQmlComponent(self.engine, QUrl.fromLocalFile(str(path)))
        self.window = self.component.create()
        assert self.window is not None, [e.toString() for e in self.component.errors()]
        atexit.register(self.close)
        self.strip = next(i for i in items(self.window.contentItem()) if i.metaObject().className().endswith("ListView") and evaluate(i, "orientation === Qt.Horizontal"))
        self.strip.setProperty("cacheBuffer", 10000)
        self.pressed = False
        self.last_frame = b""
        if self.shader_module:
            key = hashlib.sha256((str(Path(QCoreApplication.applicationFilePath()).resolve()) + "\n" + self.instance_name).encode()).hexdigest()
            profile = Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.GenericConfigLocation)) / "BSICustomizer/Shaders" / (key + ".json")
            assert not profile.exists() and not profile.is_symlink()
            self.profile = profile
        # Realize the independent offscreen host while hidden, before the
        # production onVisibleChanged sizing callback. Qt 5's offscreen QPA can
        # otherwise report its first cached geometry as native pixels and divide
        # our logical 780x500 by the monitor DPR on first show. Do not compensate
        # by changing the generated QML, page geometry or capture dimensions.
        self.window.create()
        self.app.processEvents()
        self.window.show()
        QTest.qWait(150)
        self.frame()
        assert not self.warnings, self.warnings

    def close(self):
        if self._closed:
            return
        self._closed = True
        # Dispose Quick objects before their engine/singleton theme. This also
        # keeps failed assertions from producing misleading teardown crashes.
        for item in (self.window, self.player):
            try:
                item.hide()
                item.deleteLater()
            except RuntimeError:
                pass  # A test may already have disposed its fixture window.
        QTest.qWait(20)
        self.component = None
        try:
            self.engine.deleteLater()
        except RuntimeError:
            pass
        QTest.qWait(20)
        if self.profile is not None:
            self.profile.unlink(missing_ok=True)
        if self.native_ui_cleanup:
            self.native_ui_cleanup()
            self.native_ui_cleanup = None
        self.directory.cleanup()

    def frame(self):
        if self.window.isVisible():
            # Qt 5's software renderer needs the public asynchronous item
            # capture API rather than QQuickWindow::grabWindow(). Give pending
            # bindings a single update boundary before scheduling the grab;
            # never repeat a capture until a pixel assertion happens to pass.
            self.window.requestUpdate()
            QTest.qWait(40)
            capture = self.window.contentItem().grabToImage()
            assert capture is not None
            ready = QSignalSpy(capture.ready)
            if capture.image().isNull():
                assert ready.wait(2000), "Qt did not finish the Settings frame"
            image = capture.image()
            assert not image.isNull(), "Qt did not render the Settings window"
            buffer = QBuffer()
            buffer.open(QIODevice.OpenModeFlag.WriteOnly)
            assert image.save(buffer, "PNG")
            self.last_frame = bytes(buffer.data())
        return self.last_frame

    def state(self):
        index = self.window.property("pCurrentTabIndex")
        return {"selected": index, "label": self.labels[index], "width": self.window.width(),
                "height": self.window.height(), "visibleWidth": self.window.width() - 30,
                "visibleHeight": self.window.height() - 30, "open": self.window.isVisible(),
                "saves": self.bridge.saves, "shaders": bool(self.shader_module),
                "shaderDirty": bool(self.window.property("bsiShaderDirty2")),
                "discardPrompt": bool(self.window.property("previewDiscardOpen")),
                "warnings": list(self.warnings)}

    def event(self, event):
        kind = event.get("type")
        x, y = float(event.get("x", 0)), float(event.get("y", 0))
        if not (0 <= x <= self.window.width() and 0 <= y <= self.window.height()):
            raise ValueError("pointer is outside the Settings window")
        point = QPoint(round(x), round(y))
        if kind == "down":
            self.pressed = True
            QTest.mousePress(self.window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
        elif kind == "up":
            QTest.mouseRelease(self.window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
            self.pressed = False
        elif kind == "move":
            if self.pressed:
                move = QMouseEvent(QEvent.Type.MouseMove, QPointF(point), QPointF(point),
                    Qt.MouseButton.NoButton, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier)
                QGuiApplication.sendEvent(self.window, move)
            else:
                QTest.mouseMove(self.window, point)
        elif kind == "wheel":
            dx, dy = float(event.get("dx", 0)), float(event.get("dy", 0))
            if y < 115 and abs(dy) > abs(dx):
                dx, dy = dy, 0
            wheel = QWheelEvent(QPointF(point), QPointF(point), QPoint(), QPoint(round(-dx), round(-dy)),
                Qt.MouseButton.NoButton, Qt.KeyboardModifier.NoModifier, Qt.ScrollPhase.NoScrollPhase, False)
            # QQuickFlickable integrates wheel gestures by timestamp. Zero is
            # not a fresh event: repeated browser scrolls would stall after the
            # first 72 px, making lower native controls look unreachable.
            wheel.setTimestamp(round(time.monotonic() * 1000))
            QGuiApplication.sendEvent(self.window, wheel)
        elif kind == "key":
            key = {"Escape": Qt.Key.Key_Escape, "Tab": Qt.Key.Key_Tab,
                   "ArrowDown": Qt.Key.Key_Down, "ArrowUp": Qt.Key.Key_Up,
                   "Enter": Qt.Key.Key_Return, "Space": Qt.Key.Key_Space}.get(event.get("key"))
            if key is not None:
                QTest.keyClick(self.window, key)
        elif kind == "navigate":
            index = int(event["index"])
            if not 0 <= index < len(self.labels):
                raise ValueError("unknown category")
            label = next(i for i in items(self.strip) if prop(i, "text") == self.labels[index])
            tab = label.parentItem()
            target = tab.x() + tab.width() / 2 - self.strip.width() / 2
            self.strip.setProperty("contentX", max(0, min(target, self.strip.property("contentWidth") - self.strip.width())))
            self.app.processEvents()
            point = tab.mapToScene(QPointF(tab.width() / 2, 33)).toPoint()
            QTest.mouseClick(self.window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
        elif kind == "resize":
            width, height = (760, 480) if event.get("small") else (1440, 900)
            self.window.hide()
            self.player.setGeometry(0, 0, width, height)
            self.window.show()  # Executes the production transient-window helper.
        elif kind == "accent":
            self.window.setProperty("previewAccent", QColor("#f59e0b" if event.get("warm") else "#54c8ff"))
        elif kind == "palette":
            expression = QQmlExpression(self.engine.rootContext(), self.window, "fPreviewPalette(" + ("true" if event.get("msi") else "false") + ")")
            expression.evaluate()
            assert not expression.hasError(), expression.error().toString()
        elif kind == "reopen":
            self.window.show()
        else:
            raise ValueError("unknown preview input")
        self.app.processEvents()
        return self.state()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--shader-module", help="Parent of the compiled BSIShaders module; loads its real C++ bridge and shipped page")
    parser.add_argument("--start-shaders", action="store_true", help="Open on the actual Shaders page (requires --shader-module)")
    parser.add_argument("--theme", choices=("blue", "msi"), default="msi")
    args = parser.parse_args()
    assert not args.start_shaders or args.shader_module, "--start-shaders requires a real compiled module"
    preview = SettingsPreview(shader_module=args.shader_module)
    preview.event({"type": "palette", "msi": args.theme == "msi"})
    if args.start_shaders:
        preview.event({"type": "navigate", "index": 12})
    jobs = queue.Queue(maxsize=32)
    html = (REPO / ".harness/settings_preview.html").read_text().replace("__LABELS__", json.dumps(preview.labels)).encode()

    class Handler(http.server.BaseHTTPRequestHandler):
        def on_qt(self, callback):
            done, result = threading.Event(), {}
            jobs.put((callback, done, result), timeout=5)
            if not done.wait(15):
                raise TimeoutError("Qt preview timed out")
            if "error" in result:
                raise result["error"]
            return result["value"]

        def reply(self, body, mime):
            self.send_response(200)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urllib.parse.urlparse(self.path).path
            try:
                if path in ("/", "/index.html"):
                    self.reply(html, "text/html; charset=utf-8")
                elif path == "/frame.png":
                    self.reply(self.on_qt(preview.frame), "image/png")
                elif path == "/state":
                    self.reply(json.dumps(self.on_qt(preview.state)).encode(), "application/json")
                else:
                    self.send_error(404)
            except (ValueError, TimeoutError, queue.Full) as error:
                self.send_error(503, str(error))

        def do_POST(self):
            try:
                if self.path != "/event":
                    self.send_error(404)
                    return
                length = int(self.headers.get("Content-Length", 0))
                if not 0 < length <= 4096:
                    raise ValueError("invalid input length")
                event = json.loads(self.rfile.read(length))
                state = self.on_qt(lambda: preview.event(event))
                self.reply(json.dumps(state).encode(), "application/json")
            except (ValueError, KeyError, TypeError) as error:
                self.send_error(400, str(error))
            except (TimeoutError, queue.Full) as error:
                self.send_error(503, str(error))

        def log_message(self, *_args):
            pass

    pumping = False

    def pump():
        nonlocal pumping
        if pumping:
            return
        pumping = True
        try:
            for _ in range(12):
                try:
                    callback, done, result = jobs.get_nowait()
                except queue.Empty:
                    break
                try:
                    result["value"] = callback()
                except Exception as error:
                    result["error"] = error
                finally:
                    done.set()
        finally:
            pumping = False

    timer = QTimer()
    timer.timeout.connect(pump)
    timer.start(10)
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    server = http.server.ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f"Settings window live on 0.0.0.0:{args.port} — actual generated Qt shell; sample services only", flush=True)
    try:
        preview.app.exec()
    finally:
        preview.close()


if __name__ == "__main__":
    main()
