"""Measure the actual labels and hit targets in the generated Settings shell.

No centred UiSettingsButton test double: the player's real button privately
left-aligns a themed label. The generated shell must own and centre its Text.
Exercise all twelve categories, font/bearing changes, translations, badges,
default/custom/missing icons, consecutive hidden tabs, and narrow viewports.
This is a real offscreen Qt Quick scene, not a live HD-Player session.

Usage: python tests/run_qt_runtime_test.py tests/settings_panel_post_patch_gui_test.py
Optional direct argument: path at which to save a real rendered PNG.
"""
import json
import os
import pathlib
import re
import sys
import tempfile

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")
os.environ.setdefault("QSG_RHI_BACKEND", "software")

from PySide6.QtCore import QMetaObject, QPointF, Qt
from PySide6.QtGui import QColor, QFont, QFontMetricsF, QGuiApplication, QImage
from PySide6.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
from PySide6.QtQuick import QQuickItem
from PySide6.QtTest import QTest

REPO = pathlib.Path(__file__).resolve().parents[1]
CPP = (REPO / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
SHELL = re.search(r'QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S).group(1)
LABELS = [
    "Performance", "Display", "Graphics", "Game settings", "Devices", "Gamepad",
    "Preferences", "Phone", "Shortcuts", "User data", "Advanced", "About",
]
EPSILON = 0.05


def visual_items(item):
    found, pending = [], list(item.childItems())
    while pending:
        child = pending.pop()
        found.append(child)
        pending.extend(child.childItems())
    return found


def prop(item, name, default=None):
    if item.metaObject().indexOfProperty(name) < 0:
        return default
    return item.property(name)


def write_module(root):
    module = root / "BsiGuiFixture"
    module.mkdir()
    (module / "qmldir").write_text(
        "module BsiGuiFixture\n"
        "UiShadowContainer 1.0 UiShadowContainer.qml\n"
        "UiImageButton 1.0 UiImageButton.qml\n"
        "singleton UiTheme 1.0 UiTheme.qml\n", encoding="utf-8")
    # The real player redirects default children into an inner shadow frame.
    # A plain Item double hid r.parent.bIcons bugs. Keep that parenting contract
    # and exercise both zero and the native 15 px shadow inset below.
    (module / "UiShadowContainer.qml").write_text("""import QtQuick 2.15
Item {
    default property alias children: content.data
    property real shadowThickness: 0
    Item { id: content; anchors.fill: parent; anchors.margins: shadowThickness }
}
""", encoding="utf-8")
    (module / "UiImageButton.qml").write_text("""import QtQuick 2.15
Item {
    width: 32; height: 32
    property string asset: ""
    signal clicked()
    Text { anchors.centerIn: parent; text: "X"; color: "#8b93a3" }
    MouseArea { anchors.fill: parent; onClicked: parent.clicked() }
}
""", encoding="utf-8")
    (module / "UiTheme.qml").write_text("""pragma Singleton
import QtQuick 2.15
QtObject {
    property QtObject colors: QtObject {
        property color primary10: "#f4f7fb"
        property color primary20: "#d2dae4"
        property color primary80: "#101a27"
        property color subWindowHeaderClick: "#54c8ff"
        property color dangerNormal: "#ee3344"
    }
    property QtObject fonts: QtObject {
        property font bodyLarge: Qt.font({pixelSize: 17})
    }
}
""", encoding="utf-8")
    (root / "SettingPage.qml").write_text("""import QtQuick 2.15
Item {
    objectName: "runtimeSettingPage"
    Text { x: 24; y: 16; text: "CPU allocation"; color: "#e8edf5" }
    Item {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; height: 46
        Text { x: 24; text: "Some changes will apply on next launch"; color: "#8b93a3" }
    }
    function fOnQmlLoaded() {}
}
""", encoding="utf-8")


def model(labels):
    return [{"pQmlSource": "SettingPage.qml", "pButtonText": label,
             "pShowRedDot": i % 4 == 2, "pComponentArgs": {}, "pComponentObj": None}
            for i, label in enumerate(labels)]


def main():
    app = QGuiApplication.instance() or QGuiApplication([])
    # Deliberately different from the native themed font, reproducing the old
    # app-font FontMetrics / bodyLarge label mismatch.
    app.setFont(QFont("Sans Serif", 9))
    with tempfile.TemporaryDirectory(prefix="bsi-post-patch-") as temp:
        root = pathlib.Path(temp)
        write_module(root)
        icon = QImage(54, 54, QImage.Format.Format_ARGB32)
        icon.fill(QColor("#33dd77"))
        icon_path = root / "tab icon.png"
        assert icon.save(str(icon_path))
        custom_path = root / "custom icon.png"
        icon.fill(QColor("#cc77ff"))
        assert icon.save(str(custom_path))
        icon_url, custom_url = icon_path.as_uri(), custom_path.as_uri()
        bound = SHELL.replace("property var bIcons:({})", "property var bIcons:iSettingsWindow.testIcons")
        host = root / "SettingsHost.qml"
        host.write_text("""import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import BsiGuiFixture 1.0
Window {
    id: iSettingsWindow
    width: 1120; height: 670; visible: true; color: "transparent"
    property int pCurrentTabIndex: 0
    property var testIcons: ({})
    property var tabData: %s
    property QtObject pSettingsItems: QtObject { property var pSettingElements: iSettingsWindow.tabData }
    property int testFontSize: 17
    property bool testItalic: false
    function updateFont() { UiTheme.fonts.bodyLarge = Qt.font({pixelSize:testFontSize,italic:testItalic}) }
    onTestFontSizeChanged: updateFont()
    onTestItalicChanged: updateFont()
    function fChangeSelectedSetting(i) { pCurrentTabIndex = i }
    %s
}
""" % (json.dumps(model(LABELS)), bound), encoding="utf-8")
        engine = QQmlEngine()
        engine.addImportPath(str(root))
        warnings = []
        engine.warnings.connect(lambda errors: warnings.extend(e.toString() for e in errors))
        component = QQmlComponent(engine, str(host))
        window = component.create()
        assert window is not None, "Settings QML did not instantiate: " + "\n".join(e.toString() for e in component.errors())
        content = window.contentItem()
        strip = next(i for i in visual_items(content) if i.metaObject().className().endswith("ListView"))
        # Inspect even off-viewport delegates; clipping/reachability are checked
        # separately. The production view may virtualise those delegates.
        strip.setProperty("cacheBuffer", 10000)
        shell_root = next(i for i in visual_items(content) if i.metaObject().indexOfProperty("bIcons") >= 0)
        surface = next(i for i in visual_items(content) if prop(i, "radius") == 14)
        QTest.qWait(100)

        def check_layout(labels, icons):
            # Width/model/inset changes are polished asynchronously. Flush the
            # view's documented deferred-layout queue before measuring a steady
            # frame, rather than sampling a partially virtualised transition.
            QTest.qWait(60)
            QMetaObject.invokeMethod(strip, "forceLayout")
            QTest.qWait(20)
            tree = visual_items(content)
            text_items = {prop(i, "text"): i for i in tree if prop(i, "text") in labels}
            assert len(text_items) == len(labels), "not all twelve real tab labels were instantiated"
            assert 0 <= strip.width() <= surface.width() - 48 + EPSILON
            strip_mid = strip.mapToItem(content, QPointF(strip.width() / 2, 0)).x()
            assert abs(strip_mid - window.width() / 2) < EPSILON, "the tab viewport is not centred"
            assert strip.property("spacing") == 4
            visible, inks = [], []
            for index, text in enumerate(labels):
                label = text_items[text]
                slot = label.parentItem()
                shown = icons.get(str(index)) != ""
                assert slot.isVisible() == shown, f"wrong visibility for {text}"
                if not shown:
                    assert slot.width() == -4, "hidden tabs must cancel their spacing too"
                    continue
                font = label.property("font")
                assert font.pixelSize() == window.property("testFontSize"), "label did not use the native theme font"
                bounds = QFontMetricsF(font).boundingRect(text)
                left = label.mapToItem(slot, QPointF(bounds.left(), 0)).x()
                right = label.mapToItem(slot, QPointF(bounds.right(), 0)).x()
                assert abs(left - 11) < EPSILON and abs(slot.width() - right - 11) < EPSILON, (
                    f"{text}: label ink is not centred with equal padding ({left}, {slot.width() - right})")
                assert abs((left + right) / 2 - slot.width() / 2) < EPSILON, f"{text}: off-centre label"
                origin = slot.mapToItem(content, QPointF(0, 0)).x()
                visible.append((origin, slot.width(), slot))
                inks.append((origin + left, origin + right))
                has_icon = bool(icons.get(str(index)))
                images = [i for i in slot.childItems() if prop(i, "source") is not None]
                assert len(images) == 1
                image = images[0]
                if has_icon:
                    assert image.width() == image.height() == 18
                    assert abs(image.x() + 9 - slot.width() / 2) < EPSILON
                    assert label.y() == 0 and label.height() <= image.y() + EPSILON, f"{text}: label overlaps its icon"
                    decoded = QQmlExpression(engine.rootContext(), image, "status === 1")
                    result = decoded.evaluate()
                    assert not decoded.hasError() and (result[0] if isinstance(result, tuple) else result), "tab icon did not decode"
                else:
                    assert abs(label.y() + label.height() / 2 - 22) < EPSILON, "text-only label is not vertically centred"
                badges = [i for i in slot.childItems() if prop(i, "color") == QColor("#ee3344")]
                assert len(badges) == 1 and badges[0].isVisible() == (index % 4 == 2)
                if badges[0].isVisible():
                    assert right < badges[0].x() and badges[0].x() + 6 <= slot.width(), "badge shifted or covered the label"
                expected = "#54c8ff" if index == window.property("pCurrentTabIndex") else "#d2dae4"
                assert label.property("color") == QColor(expected), "native selected-tab colours were lost"
            for (x, width, _), (next_x, _, _) in zip(visible, visible[1:]):
                assert abs(next_x - x - width - 4) < EPSILON, f"uneven hit-target gap: {next_x - x - width} (x={x}, width={width}, next={next_x})"
            for (_, right), (next_left, _) in zip(inks, inks[1:]):
                assert abs(next_left - right - 26) < EPSILON, f"uneven visible label gap: {next_left - right}"
            if visible:
                assert len({round(width, 2) for _, width, _ in visible}) > 1 or len(visible) == 1
            page = next(i for i in tree if i.objectName() == "runtimeSettingPage" and i.isVisible())
            assert abs(page.width() - min(surface.width() - 48, 568)) < EPSILON
            page_origin = page.mapToItem(content, QPointF(0, 0))
            assert abs(page_origin.x() + page.width() / 2 - window.width() / 2) < EPSILON
            surface_origin = surface.mapToItem(content, QPointF(0, 0))
            expected_top = surface_origin.y() + (100 if visible else 56)
            assert abs(page_origin.y() - expected_top) < EPSILON, f"wrong page position: {page_origin.y()}"
            assert page_origin.y() + page.height() <= surface_origin.y() + surface.height() - 18 + EPSILON, "native footer escaped the shell"
            return text_items

        defaults = {str(i): icon_url for i in range(12)}
        mixed = {str(i): (custom_url if i == 4 else icon_url) for i in range(0, 12, 2)}
        scenarios = [({}, ()), (defaults, ()), (mixed, ()),
                     (defaults, (2,)), (defaults, (2, 3, 8)),
                     (mixed, (0, 1)), (mixed, (10, 11)),
                     (defaults, (0, 3, 4, 11)), (defaults, tuple(range(11))),
                     (defaults, tuple(range(12)))]
        for width, height, inset in [(1120, 670, 0), (720, 480, 0), (1120, 670, 15), (720, 480, 15)]:
            shell_root.setProperty("shadowThickness", inset)
            window.resize(width, height)
            for base, hidden in scenarios:
                icons = dict(base, **{str(i): "" for i in hidden})
                assert window.setProperty("testIcons", icons)
                try:
                    check_layout(LABELS, icons)
                except AssertionError:
                    print("FAILED CASE:", width, height, "shadow inset", inset, "hidden", hidden)
                    raise

        # Live theme changes exercise positive/negative glyph bearings rather
        # than merely trusting the label's layout rectangle to be centred.
        assert window.setProperty("testIcons", mixed)
        for size, italic in [(13, False), (20, True), (17, False)]:
            window.setProperty("testFontSize", size)
            window.setProperty("testItalic", italic)
            check_layout(LABELS, mixed)
        translated = ["Performances", "Affichage", "Graphismes", "Réglages du jeu", "Périphériques", "Manette",
                      "Préférences", "Téléphone", "Raccourcis clavier", "Données utilisateur", "Avancé", "À propos"]
        assert window.setProperty("tabData", model(translated))
        check_layout(translated, mixed)

        # Scroll to the final option at a narrow size, click its full hit target,
        # and require the native selection/page callback and focus to survive.
        assert window.setProperty("tabData", model(LABELS))
        assert window.setProperty("testIcons", defaults)
        strip.setProperty("contentX", 0)
        labels = check_layout(LABELS, defaults)
        strip.setProperty("contentX", max(0, strip.property("contentWidth") - strip.width()))
        QTest.qWait(50)
        last = labels["About"].parentItem()
        point = last.mapToScene(QPointF(last.width() / 2, last.height() / 2)).toPoint()
        assert 24 <= point.x() <= window.width() - 24, "the last option cannot be reached by scrolling"
        QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
        QTest.qWait(40)
        assert window.property("pCurrentTabIndex") == 11 and last.hasActiveFocus(), "tab click did not retain native selection and focus"
        check_layout(LABELS, defaults)
        assert not re.search(r"color:\s*[\"']#", SHELL), "the shell must use the player's native palette"
        assert not warnings, "QML runtime warnings:\n" + "\n".join(warnings)
        if len(sys.argv) > 1:
            window.resize(1120, 670)
            strip.setProperty("contentX", 0)
            QTest.qWait(80)
            assert window.grabWindow().save(sys.argv[1]), "could not save the actual Qt scene"
        window.setVisible(False)
        window.deleteLater()
        QTest.qWait(10)
        engine.deleteLater()
    print("PASS: all twelve real labels are centred; hit-target gaps are 4 px and ink gaps 26 px across native shadow parenting/insets, icons, fonts, translations, hidden tabs and viewports")
    return 0


if __name__ == "__main__":
    sys.exit(main())
