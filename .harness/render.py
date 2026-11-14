"""Render the generated in-player Settings shell to PNG with a realistic
native-page fixture (left-anchored ~520px content column + bottom footer)."""
import os, re, sys, tempfile, pathlib
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")

from PySide6.QtCore import QObject, Property, Qt, Slot, QUrl, QTimer
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickWindow
from PySide6.QtQml import QQmlComponent, QQmlEngine

REPO = pathlib.Path(__file__).resolve().parents[1]
CPP = (REPO / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
REPL = re.search(r'QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S).group(1)
HELPER = re.search(r'const QString helper = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S).group(1)


class Player(QObject):
    def __init__(self):
        super().__init__()
        self._w, self._h, self._x, self._y = 1500.0, 900.0, 0.0, 0.0
    def g_w(self): return self._w
    def s_w(self, v): self._w = float(v)
    width = Property(float, g_w, s_w)
    def g_h(self): return self._h
    def s_h(self, v): self._h = float(v)
    height = Property(float, g_h, s_h)
    def g_x(self): return self._x
    def s_x(self, v): self._x = float(v)
    x = Property(float, g_x, s_x)
    def g_y(self): return self._y
    def s_y(self, v): self._y = float(v)
    y = Property(float, g_y, s_y)
    @Slot()
    def startSystemMove(self): pass
    @Slot()
    def close(self): pass


PAGE = r"""import QtQuick 2.15
Item {
    objectName: "runtimeSettingPage"
    // Mimics the stock BlueStacks page exactly: the page root is transparent,
    // its ~520 px content column is pinned to the top-left and its footer is
    // anchored to the bottom edges. A page like this only looks right when the
    // shell gives it a bounded host surface instead of stretching it.

    Item {
        id: col
        x: 24; y: 16
        width: 520; height: parent.height - 60
        Column {
            width: parent.width; spacing: 18
            Text { text: "CPU allocation"; color: "#e8edf5"; font.pixelSize: 13 }
            Rectangle { width: 200; height: 34; radius: 4; color: "#1b1e26"; border.color: "#2c3040"
                Text { anchors.centerIn: parent; text: "Medium (2 Cores)"; color: "#e8edf5"; font.pixelSize: 12 } }
            Rectangle { width: parent.width; height: 1; color: "#262a33" }
            Text { text: "Memory allocation"; color: "#e8edf5"; font.pixelSize: 13 }
            Rectangle { width: 200; height: 34; radius: 4; color: "#1b1e26"; border.color: "#2c3040"
                Text { anchors.centerIn: parent; text: "Enhanced (4 GB)"; color: "#e8edf5"; font.pixelSize: 12 } }
            Rectangle { width: parent.width; height: 1; color: "#262a33" }
            Text { text: "Frame rate : 79"; color: "#e8edf5"; font.pixelSize: 13 }
            Rectangle { width: parent.width; height: 6; radius: 3; color: "#2c3040"
                Rectangle { width: parent.width * 0.32; height: parent.height; radius: 3; color: "#4c8dff" } }
            Text { text: "Play at 60 FPS for smooth gameplay. If you are running multiple instances, we recommend selecting 20 FPS."; color: "#8b93a3"; font.pixelSize: 11; width: parent.width; wrapMode: Text.Wrap }
        }
    }
    Item {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; height: 46
        Rectangle { anchors.fill: parent; color: "#0b0e13" }
        Text { anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter
               text: "Some changes will apply on next launch"; color: "#8b93a3"; font.pixelSize: 11 }
        Rectangle { anchors.right: parent.right; anchors.rightMargin: 24; anchors.verticalCenter: parent.verticalCenter
            width: 112; height: 32; radius: 6; color: "#1b1e26"; border.color: "#2c3040"
            Text { anchors.centerIn: parent; text: "Save changes"; color: "#8b93a3"; font.pixelSize: 12 } }
    }
    function fOnQmlLoaded() { }
}
"""


def run(out_path, width, height):
    app = QGuiApplication.instance() or QGuiApplication([])
    temp = tempfile.mkdtemp(prefix="bsi-harness-")
    root = pathlib.Path(temp)
    module = root / "BsiSettingsFixture"
    module.mkdir()
    (module / "qmldir").write_text(
        "module BsiSettingsFixture\n"
        "UiShadowContainer 1.0 UiShadowContainer.qml\n"
        "UiImageButton 1.0 UiImageButton.qml\n"
        "UiSettingsButton 1.0 UiSettingsButton.qml\n"
        "singleton UiTheme 1.0 UiTheme.qml\n", encoding="utf-8")
    (module / "UiShadowContainer.qml").write_text(
        "import QtQuick 2.15\nItem { }\n", encoding="utf-8")
    (module / "UiImageButton.qml").write_text(
        "import QtQuick 2.15\nItem {\n    width: 32; height: 32\n"
        "    property string asset: \"\"\n    property int pImageWidth: 16\n    property int pImageHeight: 16\n"
        "    signal clicked()\n"
        "    Text { anchors.centerIn: parent; text: \"X\"; color: \"#8b93a3\"; font.pixelSize: 14 }\n"
        "    MouseArea { anchors.fill: parent; onClicked: parent.clicked() }\n}\n", encoding="utf-8")
    (module / "UiSettingsButton.qml").write_text(
        "import QtQuick 2.15\nRectangle {\n    property int pSettingIdx: 0\n"
        "    property string pSettingBtnText: \"\"\n    property bool pSelected: false\n"
        "    property bool pShowRedDot: false\n    signal settingClicked(int index)\n"
        "    radius: 8; color: pSelected ? \"#203c55\" : \"#182635\"\n"
        "    Text { anchors.fill: parent; text: pSettingBtnText; color: \"white\"\n"
        "        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }\n"
        "    MouseArea { anchors.fill: parent; onClicked: parent.settingClicked(parent.pSettingIdx) }\n}\n",
        encoding="utf-8")
    (module / "UiTheme.qml").write_text(
        "pragma Singleton\nimport QtQuick 2.15\nQtObject {\n"
        "    property QtObject colors: QtObject {\n"
        "        property color primary10: \"#f4f7fb\"\n        property color primary20: \"#d2dae4\"\n"
        "        property color primary30: \"#9eacbd\"\n        property color primary60: \"#35465a\"\n"
        "        property color primary70: \"#1b2838\"\n        property color primary80: \"#101a27\"\n"
        "        property color accentNormal: \"#54c8ff\"\n    }\n"
        "    property QtObject fonts: QtObject {\n"
        "        property font bodySmall: Qt.font({pixelSize: 11})\n"
        "        property font bodyMedium: Qt.font({pixelSize: 13})\n"
        "        property font bodyLarge: Qt.font({pixelSize: 17})\n    }\n}\n", encoding="utf-8")
    (root / "SettingPage.qml").write_text(PAGE, encoding="utf-8")

    tabs = ["Performance", "Display", "Graphics", "Game settings", "Devices", "Gamepad",
            "Preferences", "Phone", "Shortcuts", "User data", "Advanced", "About"]
    rows = ",\n        ".join(
        f'{{ pButtonText: "{name}", pShowRedDot: false, pQmlSource: "SettingPage.qml", '
        f'pComponentArgs: null, pComponentObj: null }}' for name in tabs)
    host = root / "SettingsHost.qml"
    host.write_text(
        "import QtQuick 2.15\nimport QtQuick.Controls 2.15\nimport QtQuick.Layouts 1.15\n"
        "import BsiSettingsFixture 1.0\n"
        "Item {\n    objectName: \"settingsTestHost\"\n    visible: false\n"
        f"    width: {width}; height: {height}\n"
        "    property var transientParent: iSettingsWindow\n    property int pCurrentTabIndex: 0\n"
        "    property var pSettingsItems: ({ pSettingElements: [\n        " + rows + "\n    ] })\n"
        "    function fChangeSelectedSetting(index) { pCurrentTabIndex = index }\n"
        + REPL + "\n" + HELPER + "\n}\n", encoding="utf-8")

    engine = QQmlEngine()
    engine.addImportPath(str(root))
    player = Player()
    engine.rootContext().setContextProperty("iSettingsWindow", player)
    comp = QQmlComponent(engine, QUrl.fromLocalFile(str(host)))
    obj = comp.create()
    if obj is None:
        print("\n".join(e.toString() for e in comp.errors()), file=sys.stderr)
        raise SystemExit("shell failed to load")
    win = QQuickWindow()
    win.resize(width, height)
    obj.setParentItem(win.contentItem())
    win.show()
    obj.setVisible(True)
    app.processEvents()
    QTimer.singleShot(250, app.quit)
    app.exec()
    img = win.grabWindow()
    img.save(str(out_path))
    print("saved", out_path)


if __name__ == "__main__":
    run(sys.argv[1] if len(sys.argv) > 1 else "/tmp/bsi-harness/before.png",
        int(sys.argv[2]) if len(sys.argv) > 2 else 1213,
        int(sys.argv[3]) if len(sys.argv) > 3 else 680)
