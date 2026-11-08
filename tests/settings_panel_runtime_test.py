"""Instantiate the generated in-emulator Settings shell with real Qt Quick.

This checks the patched shell's QML syntax, category selection, page loading,
window drag and close callbacks. It uses a tiny host/model double; it does not
launch the Android VM or claim a live HD-Player rendering test.
Requires PySide6==6.8.3 and is run by run_qt_runtime_test.py.
"""
import os
import re
import tempfile
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QSG_RHI_BACKEND", "software")

from PySide6.QtCore import QObject, QPointF, Property, Qt, Slot, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickItem, QQuickWindow
from PySide6.QtQml import QQmlComponent, QQmlEngine
from PySide6.QtTest import QTest


class PlayerWindowDouble(QObject):
    def __init__(self):
        super().__init__()
        self.move_requested = False
        self.close_requested = False
        self._width, self._height, self._x, self._y = 1500.0, 900.0, 0.0, 0.0

    def get_width(self): return self._width
    def set_width(self, value): self._width = float(value)
    width = Property(float, get_width, set_width)

    def get_height(self): return self._height
    def set_height(self, value): self._height = float(value)
    height = Property(float, get_height, set_height)

    def get_x(self): return self._x
    def set_x(self, value): self._x = float(value)
    x = Property(float, get_x, set_x)

    def get_y(self): return self._y
    def set_y(self, value): self._y = float(value)
    y = Property(float, get_y, set_y)

    @Slot()
    def startSystemMove(self):
        self.move_requested = True

    @Slot()
    def close(self):
        self.close_requested = True


app = QGuiApplication([])
repo = Path(__file__).resolve().parents[1]
cpp = (repo / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
match = re.search(
    r'(?:const )?QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);',
    cpp,
    re.S,
)
assert match, "Could not find the generated Settings-window QML template"
replacement = match.group(1)
assert "BSI Settings v7" in replacement
assert "pSettingsItems.pSettingElements" in replacement
assert "property var bHidden:" in replacement and "property var bIcons:" in replacement
assert "@@BSI_HIDDEN_TABS@@" in replacement and "property var bIcons:({})" in replacement
assert "if(item.bsiTabIcons)iSettingsWindowShadowContainer.bIcons=item.bsiTabIcons" in replacement
assert "property var bsiTabIcons: @@BSI_TAB_ICONS@@" in cpp
assert "Rectangle{anchors.fill:parent;color:UiTheme.colors.primary80;radius:12" in replacement
assert "Column{anchors.fill:parent;anchors.margins:12;spacing:8" in replacement
assert "height:parent.height-128" in replacement
assert "StackLayout{id:iSelectedSettingControl;anchors.fill:parent;anchors.margins:6" in replacement
assert "delegate:Loader{width:iSelectedSettingControl.width;height:iSelectedSettingControl.height;" in replacement
assert "pSettingBtnText:" in replacement and "pSelected:index===pCurrentTabIndex" in replacement and "pShowRedDot:modelData.pShowRedDot" in replacement
assert "pSettingIdx:index" in replacement
assert "fChangeSelectedSetting(pSettingIdx)" in replacement
assert "onPressed:iSettingsWindow.startSystemMove()" in replacement
helper_match = re.search(
    r'const QString helper = QStringLiteral\(R"QML\((.*?)\)QML"\);',
    cpp,
    re.S,
)
assert helper_match, "Could not find the PR #88 Settings-window geometry helper"
helper = helper_match.group(1)
assert "onVisibleChanged:if(visible&&transientParent)" in helper
assert "width=transientParent.width*.7" in helper
assert "height=transientParent.height*.7" in helper
assert "x=transientParent.x+transientParent.width*.15" in helper
assert "y=transientParent.y+transientParent.height*.15" in helper
replacement = replacement.replace("@@BSI_HIDDEN_TABS@@", "[]")
replacement = replacement.replace("@@BSI_TAB_ICONS@@", "({})")

with tempfile.TemporaryDirectory(prefix="bsi-settings-qml-") as temp:
    root = Path(temp)
    module = root / "BsiSettingsFixture"
    module.mkdir()
    (module / "qmldir").write_text(
        "module BsiSettingsFixture\n"
        "UiShadowContainer 1.0 UiShadowContainer.qml\n"
        "UiImageButton 1.0 UiImageButton.qml\n"
        "UiSettingsButton 1.0 UiSettingsButton.qml\n"
        "singleton UiTheme 1.0 UiTheme.qml\n",
        encoding="utf-8",
    )
    (module / "UiShadowContainer.qml").write_text(
        "import QtQuick 2.15\nItem { }\n", encoding="utf-8"
    )
    (module / "UiImageButton.qml").write_text(
        "import QtQuick 2.15\n"
        "Item {\n"
        "    width: 32; height: 32\n"
        "    property string asset: \"\"\n"
        "    property int pImageWidth: 16\n"
        "    property int pImageHeight: 16\n"
        "    signal clicked()\n"
        "    MouseArea { anchors.fill: parent; onClicked: parent.clicked() }\n"
        "}\n",
        encoding="utf-8",
    )
    (module / "UiSettingsButton.qml").write_text(
        "import QtQuick 2.15\n"
        "Rectangle {\n"
        "    property int pSettingIdx: 0\n"
        "    property string pSettingBtnText: \"\"\n"
        "    property bool pSelected: false\n"
        "    property bool pShowRedDot: false\n"
        "    signal settingClicked(int index)\n"
        "    radius: 8\n"
        "    color: pSelected ? \"#203c55\" : \"#182635\"\n"
        "    Text { anchors.fill: parent; text: pSettingBtnText; color: \"white\"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }\n"
        "    MouseArea { anchors.fill: parent; onClicked: parent.settingClicked(parent.pSettingIdx) }\n"
        "}\n",
        encoding="utf-8",
    )
    (module / "UiTheme.qml").write_text(
        "pragma Singleton\n"
        "import QtQuick 2.15\n"
        "QtObject {\n"
        "    property QtObject colors: QtObject {\n"
        "        property color primary10: \"#f4f7fb\"\n"
        "        property color primary20: \"#d2dae4\"\n"
        "        property color primary30: \"#9eacbd\"\n"
        "        property color primary60: \"#35465a\"\n"
        "        property color primary70: \"#1b2838\"\n"
        "        property color primary80: \"#101a27\"\n"
        "        property color accentNormal: \"#54c8ff\"\n"
        "    }\n"
        "    property QtObject fonts: QtObject {\n"
        "        property font bodySmall: Qt.font({pixelSize: 11})\n"
        "        property font bodyMedium: Qt.font({pixelSize: 13})\n"
        "        property font bodyLarge: Qt.font({pixelSize: 17})\n"
        "    }\n"
        "}\n",
        encoding="utf-8",
    )
    (root / "SettingPage.qml").write_text(
        "import QtQuick 2.15\n"
        "Item {\n"
        "    objectName: \"runtimeSettingPage\"\n"
        "    implicitWidth: 520; implicitHeight: 360\n"
        "    anchors.fill: parent\n"
        "    function fOnQmlLoaded() { }\n"
        "}\n",
        encoding="utf-8",
    )

    host = root / "SettingsHost.qml"
    host.write_text(
        "import QtQuick 2.15\n"
        "import QtQuick.Controls 2.15\n"
        "import QtQuick.Layouts 1.15\n"
        "import BsiSettingsFixture 1.0\n"
        "Item {\n"
        "    objectName: \"settingsTestHost\"\n"
        "    visible: false\n"
        "    width: 1500; height: 900\n"
        "    property var transientParent: iSettingsWindow\n"
        "    property int pCurrentTabIndex: 0\n"
        "    property var pSettingsItems: ({ pSettingElements: [\n"
        "        { pButtonText: \"Display\", pShowRedDot: false, pQmlSource: \"SettingPage.qml\", pComponentArgs: null, pComponentObj: null },\n"
        "        { pButtonText: \"Graphics\", pShowRedDot: true, pQmlSource: \"SettingPage.qml\", pComponentArgs: null, pComponentObj: null }\n"
        "    ] })\n"
        "    function fChangeSelectedSetting(index) { pCurrentTabIndex = index }\n"
        + replacement
        + "\n"
        + helper
        + "\n}\n",
        encoding="utf-8",
    )

    engine = QQmlEngine()
    engine.addImportPath(str(root))
    player = PlayerWindowDouble()
    engine.rootContext().setContextProperty("iSettingsWindow", player)
    component = QQmlComponent(engine, QUrl.fromLocalFile(str(host)))
    obj = component.create()
    if obj is None:
        raise AssertionError("Settings QML failed to create: " + "\n".join(e.toString() for e in component.errors()))

    window = QQuickWindow()
    window.resize(int(player.width), int(player.height))
    obj.setWidth(player.width)
    obj.setHeight(player.height)
    obj.setParentItem(window.contentItem())
    window.show()
    obj.setVisible(True)
    QTest.qWait(120)

    def visual_items(item):
        found = []
        pending = list(item.childItems())
        while pending:
            child = pending.pop()
            found.append(child)
            pending.extend(child.childItems())
        return found

    visual_tree = visual_items(obj)
    def visual_named(name):
        return next((item for item in visual_tree if item.objectName() == name), None)

    tabs = next(
        (item for item in visual_tree if item.metaObject().className().endswith("ListView")),
        None,
    )
    stack = next(
        (item for item in visual_tree if item.metaObject().className().endswith("StackLayout")),
        None,
    )
    page_loader = next(
        (item for item in visual_tree
         if item.metaObject().className().endswith("Loader")
         and item.property("item") is not None),
        None,
    )
    panel = next(
        (item for item in visual_tree
         if abs(item.width() - obj.width()) < 2
         and abs(item.height() - obj.height()) < 2),
        None,
    )
    first_tab = next(
        (
            item for item in visual_tree
            if str(item.property("pSettingBtnText")).endswith("Graphics")
        ),
        None,
    )
    loaded_page = visual_named("runtimeSettingPage")
    drag = next(
        (
            item for item in visual_tree
            if item.metaObject().className().endswith("MouseArea")
            and item.width() > window.width() * 0.5
            and item.height() < 50
        ),
        None,
    )
    close = next(
        (item for item in visual_tree if str(item.property("asset")) == "CloseIcon"),
        None,
    )
    controls = {
        "compact centered panel": panel,
        "centered native page loader": page_loader,
        "tabs": tabs,
        "stack": stack,
        "loaded native page": loaded_page,
        "category tab 1": first_tab,
        "drag handle": drag,
        "close button": close,
    }
    missing = [name for name, item in controls.items() if item is None]
    found_names = sorted(item.objectName() for item in visual_tree if item.objectName())
    assert not missing, f"Missing controls: {missing}; found object names: {found_names}"
    panel_center = panel.mapToItem(window.contentItem(), QPointF(panel.width() / 2, panel.height() / 2))
    assert abs(panel_center.x() - window.width() / 2) < 2 and abs(panel_center.y() - window.height() / 2) < 2, "The PR #88 Settings shell should stay centered in the player window"
    assert abs(obj.width() - 1050) < 2 and abs(obj.height() - 630) < 2, "The PR #88 Settings window should use its centered 70% footprint"
    assert abs(obj.x() - 225) < 2 and abs(obj.y() - 135) < 2, "The PR #88 Settings window should remain centered"
    assert abs(player.width - 1500) < 2 and abs(player.height - 900) < 2, "The shell should resize without changing the host player window"
    assert abs(panel.width() - obj.width()) < 2 and abs(panel.height() - obj.height()) < 2, "The PR #88 shell should fill its resized host"
    stack_center = stack.mapToItem(panel, QPointF(stack.width() / 2, stack.height() / 2))
    assert abs(stack_center.x() - panel.width() / 2) < 2, "The native page stack was not centered inside the PR #88 shell"
    assert abs(stack.width() - (panel.width() - 36)) < 2, "The native page stack should preserve the PR #88 inset layout"
    loader_position = page_loader.mapToItem(panel, QPointF(0, 0))
    assert 0 < page_loader.width() <= panel.width(), "The PR #88 native page Loader should remain visible inside the shell"
    assert loader_position.x() >= -2 and loader_position.x() + page_loader.width() <= panel.width() + 2, "The PR #88 native Loader escaped the Settings shell"
    assert abs(loaded_page.width() - page_loader.width()) < 2, "The loaded native page did not follow its Loader width"
    assert loaded_page.objectName() == "runtimeSettingPage", "The native Settings page Loader did not load its page"
    assert str(first_tab.property("pSettingBtnText")).splitlines()[0], "Category tabs should show their own icon glyphs"

    tab_point = first_tab.mapToScene(QPointF(first_tab.width() / 2, first_tab.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, tab_point)
    QTest.qWait(60)
    assert obj.property("pCurrentTabIndex") == 1, "Clicking a category did not call the native selection callback"
    assert tabs.property("currentIndex") == 1 and stack.property("currentIndex") == 1, "Category selection did not update the page stack"

    drag_point = drag.mapToScene(QPointF(8, 8)).toPoint()
    QTest.mousePress(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, drag_point)
    QTest.mouseRelease(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, drag_point)
    assert player.move_requested, "The title bar did not invoke the native window-drag callback"

    close_point = close.mapToScene(QPointF(close.width() / 2, close.height() / 2)).toPoint()
    QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, close_point)
    assert player.close_requested, "The close button did not invoke the native window-close callback"

    about_match = re.search(r'QString generated = QStringLiteral\(R"QML\((.*?)\)QML"\);', cpp, re.S)
    assert about_match, "Could not find the generated About-page QML template"
    about_qml = about_match.group(1)
    about_qml = about_qml.replace(
        "import QtQuick.Layouts 1.15",
        "import QtQuick.Layouts 1.15\nimport BsiSettingsFixture 1.0",
    )
    about_qml = about_qml.replace("@@BSI_DEVELOPER@@", '"Runtime Developer"')
    about_qml = about_qml.replace("@@BSI_TAGLINE@@", '"Runtime subtitle"')
    about_qml = about_qml.replace(
        "@@BSI_LINKS@@",
        '{ title: "GitHub", url: "https://github.com/example" },\n'
        '{ title: "Community", url: "https://example.com/community" },',
    )
    about_qml = about_qml.replace("@@BSI_TAB_ICONS@@", "({})")
    about_qml = about_qml.replace("@@BSI_LOGO_SOURCE@@", '""')
    about_path = root / "AboutHost.qml"
    about_path.write_text(about_qml, encoding="utf-8")
    about_component = QQmlComponent(engine, QUrl.fromLocalFile(str(about_path)))
    about_obj = about_component.create()
    if about_obj is None:
        raise AssertionError("About QML failed to create: " + "\n".join(e.toString() for e in about_component.errors()))
    about_tree = visual_items(about_obj)
    about_links = [item for item in about_tree if item.objectName() == "bsiAboutDeveloperLink"]
    assert about_obj.property("pDeveloperName") == "Runtime Developer", "About page did not retain the customizable developer name"
    assert about_obj.property("pDeveloperTagline") == "Runtime subtitle", "About page did not render the customizable subtitle"
    assert any(item.objectName() == "bsiAboutDeveloperTagline" for item in about_tree), "About page did not create its subtitle element"
    assert len(about_links) == 2, "About page did not render the configured developer links"
    assert any(item.objectName() == "bsiAboutEmulatorLogo" for item in about_tree), "About page did not create its centered emulator logo"

    print("PASS: generated Settings and About QML instantiate, retain native page behavior, show custom tabs, and render profile links")
    about_obj.deleteLater()
    about_component.deleteLater()
    window.close()
    obj.deleteLater()
    component.deleteLater()
    engine.deleteLater()
    QTest.qWait(10)
