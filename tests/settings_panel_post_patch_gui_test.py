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
if os.environ.get("BSI_TEST_QT_API") != "PyQt5":
    os.environ.setdefault("QSG_RHI_BACKEND", "software")

# Use a separate interpreter process for each Qt generation; never load both
# bindings in one process. CI exercises the exact same assertions with each.
if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
    from PyQt5.QtCore import QMetaObject, QPointF, Qt
    from PyQt5.QtGui import QColor, QFont, QFontMetricsF, QGuiApplication, QImage, QPainter
    from PyQt5.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PyQt5.QtQuick import QQuickItem, QQuickWindow
    from PyQt5.QtSvg import QSvgRenderer
    from PyQt5.QtTest import QSignalSpy, QTest
else:
    from PySide6.QtCore import QMetaObject, QPointF, Qt
    from PySide6.QtGui import QColor, QFont, QFontMetricsF, QGuiApplication, QImage, QPainter
    from PySide6.QtQml import QQmlComponent, QQmlEngine, QQmlExpression
    from PySide6.QtQuick import QQuickItem, QQuickWindow
    from PySide6.QtSvg import QSvgRenderer
    from PySide6.QtTest import QSignalSpy, QTest

REPO = pathlib.Path(__file__).resolve().parents[1]
CPP = (REPO / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
SHELL = re.search(r'QString replacement = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S).group(1)
HELPER = re.search(r'const QString helper = QStringLiteral\(R"QML\((.*?)\)QML"\);', CPP, re.S).group(1)
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
    # Match native pages' scrollable form + fixed footer contract. A short
    # one-line body would not catch compact windows cutting off Save changes.
    (root / "SettingPage.qml").write_text("""import QtQuick 2.15
Item {
    objectName: "runtimeSettingPage"
    Flickable {
        objectName: "runtimeSettingScroll"
        x: 24; y: 16; width: parent.width - 48
        height: Math.max(0, parent.height - 72); clip: true
        contentWidth: width; contentHeight: form.height
        Column {
            id: form; width: parent.width; spacing: 14
            Repeater {
                model: ["CPU allocation", "Memory allocation", "Performance mode", "Graphics renderer", "Frame rate", "High frame rate", "VSync", "Texture quality", "Other setting"]
                Column {
                    width: parent.width; spacing: 6
                    Text { text: modelData; color: "#e8edf5" }
                    Rectangle {
                        objectName: index === 8 ? "runtimeLastControl" : ""
                        width: Math.min(200, parent.width); height: 34
                        color: "#1b2838"
                    }
                }
            }
        }
    }
    Item {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; height: 46
        Text { x: 24; anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 168; elide: Text.ElideRight
            text: "Some changes will apply on next launch"; color: "#8b93a3" }
        Rectangle {
            objectName: "runtimeSaveChanges"
            anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
            width: 120; height: 32; color: "#35465a"
            Text { anchors.centerIn: parent; text: "Save changes"; color: "#e8edf5" }
        }
    }
    function fOnQmlLoaded() {}
}
""", encoding="utf-8")


def model(labels):
    return [{"pQmlSource": "SettingPage.qml", "pButtonText": label,
             "pShowRedDot": i % 4 == 2, "pComponentArgs": {}, "pComponentObj": None}
            for i, label in enumerate(labels)]


def check_reserved_shader_artwork():
    # Artwork-only coverage: no mock shader controls or fabricated game frame.
    source = (REPO / "src/assets/settings-panel-icons/shaders.svg").read_bytes()
    for size in (24, 48):
        for accent in ("#54c8ff", "#f59e0b"):
            renderer = QSvgRenderer(source.replace(b"<svg ", f'<svg color="{accent}" '.encode(), 1))
            assert renderer.isValid(), "the original reserved Shaders SVG must parse in Qt 5 and Qt 6"
            image = QImage(size, size, QImage.Format.Format_ARGB32_Premultiplied)
            image.fill(QColor("transparent"))
            painter = QPainter(image)
            renderer.render(painter)
            painter.end()
            pixels = [(x, y, image.pixelColor(x, y)) for y in range(size) for x in range(size)
                      if image.pixelColor(x, y).alpha() > 0]
            assert size * size * .1 < len(pixels) < size * size * .7, "icon must paint a visible outline, not a blank or solid tile"
            assert all(0 < x < size - 1 and 0 < y < size - 1 for x, y, _ in pixels), "SVG strokes must not clip at the icon edges"
            ink = [color for _, _, color in pixels if color.alpha() >= 128]
            expected = QColor(accent)
            assert ink and all(abs(color.red() - expected.red()) <= 2
                               and abs(color.green() - expected.green()) <= 2
                               and abs(color.blue() - expected.blue()) <= 2 for color in ink), "the reserved artwork must inherit the selected-tab accent"
    print("PASS: original reserved Shaders SVG paints unclipped at 1x/2x with two theme accents; artwork only, not shader-effect validation")


def main():
    app = QGuiApplication.instance() or QGuiApplication([])
    check_reserved_shader_artwork()
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
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import BsiGuiFixture 1.0
Window {
    id: iSettingsWindow
    width: 780; height: 500; visible: false; color: "transparent"
    transientParent: fixturePlayer
    property int pCurrentTabIndex: 0
    property var testIcons: ({})
    property var tabData: %s
    property QtObject pSettingsItems: QtObject { property var pSettingElements: iSettingsWindow.tabData }
    property int testFontSize: 17
    property color testAccent: "#54c8ff"
    onTestAccentChanged: UiTheme.colors.subWindowHeaderClick = testAccent
    property bool testItalic: false
    function updateFont() { UiTheme.fonts.bodyLarge = Qt.font({pixelSize:testFontSize,italic:testItalic}) }
    onTestFontSizeChanged: updateFont()
    onTestItalicChanged: updateFont()
    function fChangeSelectedSetting(i) { pCurrentTabIndex = i }
    %s
    %s
}
""" % (json.dumps(model(LABELS)), bound, HELPER), encoding="utf-8")
        engine = QQmlEngine()
        engine.addImportPath(str(root))
        player = QQuickWindow()
        player.setGeometry(0, 0, 1440, 900)
        engine.rootContext().setContextProperty("fixturePlayer", player)
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
        window.show()
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
                images = [i for i in visual_items(slot) if prop(i, "source") is not None]
                # Controls creates its internal IconImage lazily, only when a
                # source exists. Inspect that painted image, not the Button API.
                if has_icon:
                    assert len(images) == 1
                    image = images[0]
                    assert image.width() == image.height() == 18, \
                        f"{text}: actual icon is {image.width()}x{image.height()}, not 18x18"
                    assert abs(image.mapToItem(slot, QPointF(9, 0)).x() - slot.width() / 2) < EPSILON
                    assert label.y() == 0 and label.height() <= image.mapToItem(slot, QPointF(0, 0)).y() + EPSILON, f"{text}: label overlaps its icon"
                    decoded = QQmlExpression(engine.rootContext(), image, "status === 1")
                    result = decoded.evaluate()
                    assert not decoded.hasError() and (result[0] if isinstance(result, tuple) else result), "tab icon did not decode"
                else:
                    assert abs(label.y() + label.height() / 2 - 22) < EPSILON, "text-only label is not vertically centred"
                badges = [i for i in slot.childItems() if prop(i, "color") == QColor("#ee3344")]
                assert len(badges) == 1 and badges[0].isVisible() == (index % 4 == 2)
                if badges[0].isVisible():
                    assert right < badges[0].x() and badges[0].x() + 6 <= slot.width(), "badge shifted or covered the label"
                selected = index == window.property("pCurrentTabIndex")
                expected = window.property("testAccent") if selected else QColor("#d2dae4")
                assert label.property("color") == QColor(expected), "native selected-tab colours were lost"
                highlights = [i for i in slot.childItems() if prop(i, "radius") == 6]
                assert len(highlights) == 2, "each tab needs a soft surface plus an independent opaque outline"
                highlight = next(i for i in highlights if prop(i, "color") == label.property("color"))
                outline = next(i for i in highlights if i != highlight)
                stroke = QQmlExpression(engine.rootContext(), outline, "border.color")
                result = stroke.evaluate()
                assert not stroke.hasError()
                stroke_color = result[0] if isinstance(result, tuple) else result
                assert stroke_color == (label.property("color") if selected else QColor("transparent")), \
                    "only selection, never hover, gets the native accent outline"
                assert outline.opacity() == 1 and outline.size() == slot.size(), \
                    "the selected border must be fully opaque without changing geometry"
                expected_opacity = .2 if selected else .1 if slot.property("containsMouse") else 0
                assert abs(highlight.opacity() - expected_opacity) < EPSILON
                assert highlight.property("color") == label.property("color")
                assert highlight.size() == slot.size(), "selection must not alter tab bounds or neighbouring gaps"
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
            # Resize only the window. The approved 568 px native page and its
            # controls stay unchanged; trim only the excess outer side space.
            left_gutter = page_origin.x() - surface_origin.x()
            right_gutter = surface_origin.x() + surface.width() - page_origin.x() - page.width()
            assert abs(left_gutter - right_gutter) < EPSILON, "page gutters must be symmetric"
            assert abs(left_gutter - (surface.width() - page.width()) / 2) < EPSILON
            if window.width() == 780 and window.height() == 500 and shell_root.property("shadowThickness") == 15:
                assert abs(surface.width() - 750) < EPSILON, "visible width must match the screenshot border"
                assert abs(surface.height() - 470) < EPSILON, "visible height must match the screenshot border"
                assert abs(page.width() - 568) < EPSILON, "resizing the window must not stretch native controls"
                assert abs(left_gutter - 91) < EPSILON
                assert left_gutter < 100, "the marked empty side bands returned"
                assert surface.width() / surface.height() >= 1.5, "trimming the sides must not make Settings square"
            expected_top = surface_origin.y() + (112 if visible else 68)
            assert abs(page_origin.y() - expected_top) < EPSILON, f"wrong page position: {page_origin.y()}"
            assert abs(page.height() - (surface.height() - 130)) < EPSILON, "top padding changed the footer clearance"
            if visible:
                strip_bottom = strip.mapToItem(content, QPointF(0, strip.height())).y()
                assert abs(page_origin.y() - strip_bottom - 12) < EPSILON, "page content must leave exactly 12 px below the tab strip"
            assert page_origin.y() + page.height() <= surface_origin.y() + surface.height() - 18 + EPSILON, "native footer escaped the shell"
            page_items = visual_items(page)
            scroll = next(i for i in page_items if i.objectName() == "runtimeSettingScroll")
            save = next(i for i in page_items if i.objectName() == "runtimeSaveChanges")
            assert scroll.height() > 0 and scroll.property("contentHeight") > scroll.height(), "long native forms must remain scrollable"
            save_origin = save.mapToItem(page, QPointF(0, 0))
            assert save_origin.x() >= 0 and save_origin.x() + save.width() <= page.width()
            assert save_origin.y() >= scroll.y() + scroll.height(), "Save changes overlaps the scrollable form"
            assert save_origin.y() + save.height() <= page.height(), "Save changes was clipped by the compact window"
            return text_items

        def rendered_frame():
            # Qt 5's software backend cannot grabWindow(), but its documented
            # asynchronous item capture renders the exact same scene to a PNG.
            capture = content.grabToImage()
            assert capture is not None
            ready = QSignalSpy(capture.ready)
            if capture.image().isNull():
                assert ready.wait(2000), "the real Qt scene did not finish rendering"
            frame = capture.image()
            assert not frame.isNull(), "the real Qt scene did not render"
            return frame

        def check_selection_paint(labels, icons):
            # Assert the pixels of the ACTUAL Controls icon image, not merely an
            # icon.color binding or a coloured rectangle behind an unchanged PNG.
            frame = rendered_frame()
            checked = 0
            for index, text in enumerate(LABELS):
                slot = labels[text].parentItem()
                source = icons.get(str(index))
                if not slot.isVisible() or not source:
                    continue
                image = next(i for i in visual_items(slot) if prop(i, "source") is not None)
                point = image.mapToScene(QPointF(9, 9))
                strip_point = image.mapToItem(strip, QPointF(9, 9))
                if not 0 <= strip_point.x() < strip.width():
                    continue
                selected = index == window.property("pCurrentTabIndex")
                expected = (QColor(window.property("testAccent")) if selected
                            else QColor("#cc77ff" if source == custom_url else "#33dd77"))
                painted = frame.pixelColor(round(point.x()), round(point.y()))
                assert painted == expected, f"{text}: selected/custom icon pixels are {painted.name()}, expected {expected.name()}"
                checked += 1
                if selected:
                    # A full-colour 1 px bottom edge is clear of text, icons,
                    # badges and rounded corners. Merely setting border.color
                    # on the .2-opacity background would fail this pixel check.
                    edge = slot.mapToScene(QPointF(slot.width() / 2, slot.height() - 1))
                    painted_edge = frame.pixelColor(round(edge.x()), round(edge.y()))
                    accent = QColor(window.property("testAccent"))
                    assert all(abs(getattr(painted_edge, c)() - getattr(accent, c)()) <= 2
                               for c in ("red", "green", "blue")), \
                        f"{text}: selected outline was not painted at full opacity ({painted_edge.name()})"
                # Sample the pill in the label's left padding, clear of glyphs,
                # the centered icon, notification dot and rounded corners.
                point = slot.mapToScene(QPointF(5, 22))
                if 0 <= slot.mapToItem(strip, QPointF(5, 22)).x() < strip.width():
                    opacity = .2 if selected else .1 if slot.property("containsMouse") else 0
                    foreground = labels[text].property("color")
                    background = QColor("#101a27")
                    actual = frame.pixelColor(round(point.x()), round(point.y()))
                    for channel in ("red", "green", "blue"):
                        expected_channel = round(getattr(foreground, channel)() * opacity
                                                 + getattr(background, channel)() * (1 - opacity))
                        assert abs(getattr(actual, channel)() - expected_channel) <= 2, \
                            f"{text}: the selected/hover background was not actually painted"
            assert checked, "no visible icon pixels were checked"

        def fit_player(width, height, inset, x=0, y=0):
            # Execute the real production helper on an actual transient window,
            # not a manually resized scene that could hide a broken size cap.
            window.hide()
            player.setGeometry(x, y, width, height)
            shell_root.setProperty("shadowThickness", inset)
            window.show()
            QTest.qWait(80)
            assert abs(window.width() - min(width * .96, 780)) <= 1
            assert abs(window.height() - min(height * .94, 500)) <= 1
            assert window.width() <= 780 and window.height() <= 500, "Settings restored the unwanted side bands"
            assert abs(window.x() + window.width() / 2 - (player.x() + width / 2)) <= 1
            assert abs(window.y() + window.height() / 2 - (player.y() + height / 2)) <= 1
            assert window.x() >= player.x() and window.y() >= player.y()
            assert window.x() + window.width() <= player.x() + width
            assert window.y() + window.height() <= player.y() + height
            assert player.width() == width and player.height() == height, "sizing Settings must not resize the player"

        defaults = {str(i): icon_url for i in range(12)}
        mixed = {str(i): (custom_url if i == 4 else icon_url) for i in range(0, 12, 2)}
        scenarios = [({}, ()), (defaults, ()), (mixed, ()),
                     (defaults, (2,)), (defaults, (2, 3, 8)),
                     (mixed, (0, 1)), (mixed, (10, 11)),
                     (defaults, (0, 3, 4, 11)), (defaults, tuple(range(11))),
                     (defaults, tuple(range(12)))]
        viewports = [(1440, 900, 0), (1301, 665, 0),
                     (1440, 900, 15), (1301, 665, 15), (1011, 584, 15), (720, 480, 15),
                     (814, 534, 15), (811, 530, 15),  # both sides of the size-cap breakpoints
                     (640, 400, 15), (560, 400, 15)]
        for width, height, inset in viewports:
            fit_player(width, height, inset, x=80, y=50)
            for base, hidden in scenarios:
                icons = dict(base, **{str(i): "" for i in hidden})
                assert window.setProperty("testIcons", icons)
                try:
                    check_layout(LABELS, icons)
                except AssertionError:
                    print("FAILED CASE:", width, height, "shadow inset", inset, "hidden", hidden)
                    raise

        # A long page must still reach its final setting without moving the
        # footer out of view at the smallest viewport, with all tabs visible.
        assert window.setProperty("testIcons", defaults)
        check_layout(LABELS, defaults)
        page = next(i for i in visual_items(content) if i.objectName() == "runtimeSettingPage" and i.isVisible())
        scroll = next(i for i in visual_items(page) if i.objectName() == "runtimeSettingScroll")
        last_control = next(i for i in visual_items(scroll) if i.objectName() == "runtimeLastControl")
        scroll.setProperty("contentY", scroll.property("contentHeight") - scroll.height())
        QTest.qWait(30)
        last_bottom = last_control.mapToItem(scroll, QPointF(0, last_control.height())).y()
        assert 0 < last_bottom <= scroll.height() + EPSILON, "the compact window made lower native form controls unreachable"

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

        # The tighter desktop row deliberately scrolls instead of shrinking
        # labels/icons or changing their approved gaps. Reach and click EVERY
        # category at desktop and narrow sizes, then return to the first one.
        assert window.setProperty("tabData", model(LABELS))
        assert window.setProperty("testIcons", defaults)
        for width, height in [(1440, 900), (560, 400)]:
            fit_player(width, height, 15)
            strip.setProperty("contentX", 0)
            labels = check_layout(LABELS, defaults)
            assert strip.property("contentWidth") > strip.width(), "the fixture must exercise a scrolling tab row"
            for index in list(range(len(LABELS))) + [0]:
                tab = labels[LABELS[index]].parentItem()
                target = tab.x() + tab.width() / 2 - strip.width() / 2
                strip.setProperty("contentX", max(0, min(target, strip.property("contentWidth") - strip.width())))
                QTest.qWait(30)
                # Alternate full-tab and actual icon-area clicks: the icon's
                # disabled background-free Button must never swallow selection.
                point = tab.mapToScene(QPointF(tab.width() / 2, 33 if index % 2 else 12)).toPoint()
                assert 24 <= point.x() <= window.width() - 24, f"{LABELS[index]} cannot be reached by scrolling"
                QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
                QTest.qWait(20)
                assert window.property("pCurrentTabIndex") == index and tab.hasActiveFocus(), \
                    f"{LABELS[index]} lost native selection/focus after the window was tightened"
                check_selection_paint(labels, defaults)
            check_layout(LABELS, defaults)

        # Hover is weaker than selection, never changes the selected page, and
        # disappears on exit. Theme updates must reach the label, pill AND icon.
        fit_player(1301, 665, 15)
        assert window.setProperty("testIcons", dict(defaults, **{"4": custom_url}))
        actual_icons = dict(defaults, **{"4": custom_url})
        strip.setProperty("contentX", 0)
        labels = check_layout(LABELS, actual_icons)
        hover_tab = labels["Display"].parentItem()
        QTest.mouseMove(window, hover_tab.mapToScene(QPointF(hover_tab.width() / 2, 33)).toPoint())
        QTest.qWait(30)
        assert hover_tab.property("containsMouse"), "tab hover was not delivered to the real scene"
        assert window.property("pCurrentTabIndex") == 0, "hover must not change the native selected page"
        check_layout(LABELS, actual_icons)
        check_selection_paint(labels, actual_icons)
        QTest.mouseMove(window, surface.mapToScene(QPointF(12, 12)).toPoint())
        QTest.qWait(30)
        assert not hover_tab.property("containsMouse"), "hover highlight stuck after pointer exit"
        check_selection_paint(labels, actual_icons)
        for index in (4, 0):
            tab = labels[LABELS[index]].parentItem()
            QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier,
                             tab.mapToScene(QPointF(tab.width() / 2, 33)).toPoint())
            QTest.qWait(30)
            assert window.property("pCurrentTabIndex") == index
            check_selection_paint(labels, actual_icons)
        assert window.setProperty("testAccent", QColor("#f59e0b"))
        QTest.qWait(30)
        check_layout(LABELS, actual_icons)
        check_selection_paint(labels, actual_icons)
        assert window.setProperty("testAccent", QColor("#54c8ff"))
        assert not re.search(r"color:\s*[\"']#", SHELL), "the shell must use the player's native palette"
        assert not warnings, "QML runtime warnings:\n" + "\n".join(warnings)
        if len(sys.argv) > 1:
            fit_player(1440, 900, 15)
            strip.setProperty("contentX", 0)
            QTest.qWait(80)
            assert rendered_frame().save(sys.argv[1]), "could not save the actual Qt scene"
        window.setVisible(False)
        window.deleteLater()
        player.deleteLater()
        QTest.qWait(10)
        engine.deleteLater()
    print("PASS: wide 750x470 visible Settings frame (780x500 with native shadow), reduced side gutters; themed selected-tab full-opacity border/background/label/icon, quieter hover, custom colours restored, twelve centred tabs, uniform gaps, native page/footer and scrolling")
    return 0


if __name__ == "__main__":
    sys.exit(main())
