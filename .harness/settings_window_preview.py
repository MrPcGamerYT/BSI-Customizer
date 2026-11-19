"""Live preview of the GENERATED in-player Settings shell, not its editor.

Uses the exact C++ QML template and sizing helper, real Qt 6 controls, the
supplied default icons rasterized to PNG, and explicit sample pages/services.
No player executable, VM, license service or system setting is accessed.

    python .harness/settings_window_preview.py --port 8080

HTTP requests enqueue work for Qt's GUI thread; the browser forwards actual
pointer/wheel/key input and receives rendered Qt frames (not an HTML remake).
"""
import argparse
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

from PySide6.QtCore import QBuffer, QEvent, QIODevice, QObject, QPoint, QPointF, QTimer, QUrl, Qt, Slot
from PySide6.QtGui import QColor, QFont, QFontDatabase, QGuiApplication, QImage, QMouseEvent, QPainter, QWheelEvent
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtQuick import QQuickWindow
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtTest import QTest

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


class SampleBridge(QObject):
    """No persistent writes: Save changes only counts a demonstration click."""
    def __init__(self):
        super().__init__()
        self.saves = 0

    @Slot()
    def save(self):
        self.saves += 1


class SettingsPreview:
    def __init__(self):
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
        (module / "UiTheme.qml").write_text(THEME)
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
        for index, name in enumerate(ICON_FILES):
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
                  "HIDDEN_TABS": "[]", "LINKS": ""}
        for key, value in values.items():
            about = about.replace("@@BSI_" + key + "@@", value)
        about = about.replace("import QtQuick.Layouts 1.15", "import QtQuick.Layouts 1.15\nimport BsiGuiFixture 1.0")
        (root / "About.qml").write_text(about)
        (root / "DemoPage.qml").write_text((REPO / ".harness/settings_preview_page.qml").read_text())
        model = [{"pButtonText": label, "pShowRedDot": False,
                  "pQmlSource": "About.qml" if i == 11 else "DemoPage.qml",
                  "pComponentArgs": {} if i == 11 else {"pageIndex": i, "pageTitle": label},
                  "pComponentObj": None} for i, label in enumerate(LABELS)]
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
    onPreviewAccentChanged: UiTheme.colors.subWindowHeaderClick = previewAccent
    property QtObject pSettingsItems: QtObject { property var pSettingElements: %s }
    function fChangeSelectedSetting(index) { pCurrentTabIndex = index }
    %s
    %s
}
""" % (json.dumps(model), shell, helper)
        path = root / "SettingsHost.qml"
        path.write_text(host)
        self.engine = QQmlEngine()
        self.engine.addImportPath(str(root))
        self.bridge = SampleBridge()
        self.player = QQuickWindow()
        self.player.setGeometry(0, 0, 1011, 584)
        self.engine.rootContext().setContextProperty("fixturePlayer", self.player)
        self.engine.rootContext().setContextProperty("previewBridge", self.bridge)
        self.warnings = []
        self.engine.warnings.connect(lambda errors: self.warnings.extend(e.toString() for e in errors))
        self.component = QQmlComponent(self.engine, QUrl.fromLocalFile(str(path)))
        self.window = self.component.create()
        assert self.window is not None, [e.toString() for e in self.component.errors()]
        self.strip = next(i for i in items(self.window.contentItem()) if i.metaObject().className().endswith("ListView"))
        self.strip.setProperty("cacheBuffer", 10000)
        self.pressed = False
        self.last_frame = b""
        self.window.show()
        QTest.qWait(150)
        self.frame()
        assert not self.warnings, self.warnings

    def frame(self):
        if self.window.isVisible():
            image = self.window.grabWindow()
            assert not image.isNull(), "Qt did not render the Settings window"
            buffer = QBuffer()
            buffer.open(QIODevice.OpenModeFlag.WriteOnly)
            assert image.save(buffer, "PNG")
            self.last_frame = bytes(buffer.data())
        return self.last_frame

    def state(self):
        index = self.window.property("pCurrentTabIndex")
        return {"selected": index, "label": LABELS[index], "width": self.window.width(),
                "height": self.window.height(), "visibleWidth": self.window.width() - 30,
                "visibleHeight": self.window.height() - 30, "open": self.window.isVisible(),
                "saves": self.bridge.saves, "warnings": list(self.warnings)}

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
            if not 0 <= index < len(LABELS):
                raise ValueError("unknown category")
            label = next(i for i in items(self.strip) if prop(i, "text") == LABELS[index])
            tab = label.parentItem()
            target = tab.x() + tab.width() / 2 - self.strip.width() / 2
            self.strip.setProperty("contentX", max(0, min(target, self.strip.property("contentWidth") - self.strip.width())))
            self.app.processEvents()
            point = tab.mapToScene(QPointF(tab.width() / 2, 33)).toPoint()
            QTest.mouseClick(self.window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
        elif kind == "resize":
            width, height = (760, 480) if event.get("small") else (1011, 584)
            self.window.hide()
            self.player.setGeometry(0, 0, width, height)
            self.window.show()  # Executes the production transient-window helper.
        elif kind == "accent":
            self.window.setProperty("previewAccent", QColor("#f59e0b" if event.get("warm") else "#54c8ff"))
        elif kind == "reopen":
            self.window.show()
        else:
            raise ValueError("unknown preview input")
        self.app.processEvents()
        return self.state()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    preview = SettingsPreview()
    jobs = queue.Queue(maxsize=32)
    html = (REPO / ".harness/settings_preview.html").read_text().replace("__LABELS__", json.dumps(LABELS)).encode()

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
    preview.app.exec()


if __name__ == "__main__":
    main()
