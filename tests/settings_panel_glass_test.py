"""Real Qt 5/6 pixels/input: flat native Settings, glass ONLY on selected tabs.

The generated resources are unmodified. Authored page doubles reproduce the
observed public contracts: Loader-backed buttons, asset-backed indicators,
slider track/fill/handle, text validation and an Overlay-reparented combo popup.
Require exact theme pixels across the whole frame/page/footer, with no gradient
on the shell or unselected/hovered tabs. The selected reflection stays strict.
No OEM source/artwork, player process, game renderer or optional effects plugin.
"""
import importlib.util
import json
import os
from pathlib import Path

_preview = None

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("glass_preview", REPO / ".harness/settings_window_preview.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
if m.QT5:
    from PyQt5.QtCore import QMetaObject, qVersion
    from PyQt5.QtQml import QQmlExpression
else:
    from PySide6.QtCore import QMetaObject, qVersion
    from PySide6.QtQml import QQmlExpression

# Not copies of the vendor components: small, self-authored behavior contracts.
PAGE = r'''import QtQuick 2.15
import QtQuick.Controls 2.15
import BsiGuiFixture 1.0
Item {
    id: p
    property int pageIndex: 0
    property string pageTitle: ""
    property int saves: 0
    property int choices: 0
    property bool alternate: false
    property bool invalid: false
    function fOnQmlLoaded() {}
    Component { id: normal; Rectangle {
        objectName: "glassButtonBackground"; radius: 4
        color: UiTheme.colors.primary70
        border.color: UiTheme.colors.subWindowHeaderClick
    } }
    Component { id: replacement; Rectangle {
        objectName: "glassReplacementBackground"; radius: 4
        color: UiTheme.colors.primary60
        border.color: UiTheme.colors.subWindowHeaderClick
    } }
    Button {
        objectName: "glassSave"; x: 24; y: 12; width: 160; height: 34
        text: "Save changes"; onClicked: p.saves++
        background: Loader { sourceComponent: p.alternate ? replacement : normal }
    }
    ComboBox {
        id: combo; objectName: "glassCombo"; x: 230; y: 12; width: 200; height: 34
        model: ["Native choice A", "Native choice B", "Native choice C"]
        onActivated: p.choices++
        background: Rectangle { color: UiTheme.colors.primary70; border.color: UiTheme.colors.primary60 }
        delegate: ItemDelegate {
            objectName: "glassChoice"; width: combo.width; height: 32
            text: modelData; highlighted: combo.highlightedIndex === index
            background: Rectangle { color: parent.highlighted ? UiTheme.colors.subWindowHeaderClick : "transparent" }
        }
        popup: Popup {
            y: combo.height; width: combo.width; padding: 1
            implicitHeight: contentItem.implicitHeight + 2
            contentItem: ListView {
                clip: true; implicitHeight: contentHeight
                model: combo.delegateModel; currentIndex: combo.highlightedIndex
            }
            background: Rectangle { objectName: "glassPopup"; color: UiTheme.colors.primary80; border.color: UiTheme.colors.primary60 }
        }
    }
    TextField {
        id: field; objectName: "glassField"; x: 24; y: 72; width: 200; height: 34
        text: "Native editable text"; selectByMouse: true
        background: Rectangle {
            objectName: "glassFieldBackground"
            color: UiTheme.colors.primary70
            border.color: p.invalid ? UiTheme.colors.dangerNormal : field.activeFocus ? UiTheme.colors.subWindowHeaderClick : UiTheme.colors.primary60
        }
    }
    Button {
        objectName: "glassExisting"; x: 230; y: 72; width: 120; height: 34
        text: "Custom gradient"
        background: Rectangle {
            gradient: Gradient {
                objectName: "nativeGradient"
                GradientStop { position: 0; color: "#554433" }
                GradientStop { position: 1; color: "#221100" }
            }
        }
    }
    Button {
        objectName: "glassDisabled"; x: 370; y: 72; width: 150; height: 34
        enabled: false; text: "Disabled"
        background: Rectangle { objectName: "glassDisabledBackground"; color: "transparent"; border.color: UiTheme.colors.primary60 }
    }
    Switch {
        id: toggle; objectName: "glassToggle"; x: 24; y: 136; width: 72; height: 28
        indicator: Item {
            property string asset: "Toggle" + (toggle.checked ? "On" : "Off")
            width: 48; height: 24
            Rectangle { anchors.fill: parent; radius: 12; color: toggle.checked ? UiTheme.colors.subWindowHeaderClick : UiTheme.colors.primary60 }
            Rectangle { x: toggle.checked ? 26 : 2; y: 2; width: 20; height: 20; radius: 10; color: UiTheme.colors.primary10 }
        }
    }
    CheckBox {
        id: check; objectName: "glassCheck"; x: 146; y: 136; width: 70; height: 28
        indicator: Item {
            property string asset: "Check" + (check.checked ? "On" : "Off")
            width: 24; height: 24
            Rectangle { anchors.fill: parent; radius: 3; color: check.checked ? UiTheme.colors.subWindowHeaderClick : UiTheme.colors.primary70; border.color: UiTheme.colors.primary20 }
            Text { anchors.centerIn: parent; text: check.checked ? "X" : ""; color: UiTheme.colors.primary10 }
        }
    }
    RadioButton {
        id: radio; objectName: "glassRadio"; x: 268; y: 136; width: 70; height: 28
        indicator: Item {
            property string asset: "Radio" + (radio.checked ? "On" : "Off")
            width: 24; height: 24
            Rectangle { anchors.fill: parent; radius: 12; color: UiTheme.colors.primary70; border.color: UiTheme.colors.primary20 }
            Rectangle { anchors.centerIn: parent; width: 12; height: 12; radius: 6; visible: radio.checked; color: UiTheme.colors.subWindowHeaderClick }
        }
    }
    Button {
        objectName: "nativeLayeredButton"; x: 370; y: 115; width: 150; height: 30
        background: Rectangle {
            objectName: "nativeLayeredBackground"
            color: "#123456"; radius: 0; layer.enabled: true
            Rectangle { objectName: "nativeLayeredInner"; anchors.fill: parent; anchors.margins: 1; color: "#234567" }
        }
    }
    Rectangle { objectName: "nativeLayoutRectangle"; x: 370; y: 160; width: 150; height: 24; color: "#234567"; radius: 0 }
    ScrollBar {
        id: bar; objectName: "nativeScrollBar"; x: 548; y: 12; width: 6; height: 220
        orientation: Qt.Vertical; policy: ScrollBar.AlwaysOn; size: .3; position: .2
        property color nativeThumbColor: "#717585"
        contentItem: Rectangle { objectName: "nativeScrollThumb"; implicitWidth: 6; implicitHeight: 6; color: bar.nativeThumbColor; radius: 0 }
        background: Rectangle { objectName: "nativeScrollBackground"; color: "transparent" }
    }
    Slider {
        id: slider; objectName: "glassSlider"; x: 24; y: 200; width: 480; height: 32
        from: 1; to: 240; value: 60
        background: Rectangle {
            color: "transparent"
            Rectangle {
                objectName: "glassTrack"; width: parent.width; height: 4; y: 14
                color: UiTheme.colors.primary60
                Rectangle { objectName: "glassFill"; width: slider.visualPosition * parent.width; height: 4; color: UiTheme.colors.subWindowHeaderClick }
            }
        }
        handle: Rectangle {
            objectName: "glassHandle"; x: slider.visualPosition * (slider.width - width); y: 6
            width: 20; height: 20; radius: 10; color: UiTheme.colors.primary20
            border.color: UiTheme.colors.subWindowHeaderClick
        }
    }
}
'''


def evaluate(item, text):
    expression = QQmlExpression(m.QQmlEngine.contextForObject(item), item, text)
    result = expression.evaluate()
    assert not expression.hasError(), expression.error().toString()
    if isinstance(result, tuple):
        value, undefined = result
        return None if undefined else value
    return result


def main():
    global _preview
    preview = _preview = m.SettingsPreview(page_source=PAGE, about_links=[{"title": "Project", "url": "https://example.com"}])
    window = preview.window
    m.QTest.qWait(240)
    content = window.contentItem()

    def tree():
        return list(m.items(content))

    def named(name, visible=True):
        return next(i for i in tree() if i.objectName() == name and (not visible or i.isVisible()))

    def gradient(item):
        return bool(evaluate(item, "gradient != null && gradient.stops.length === 2"))

    def click(item, fx=.5):
        point = item.mapToScene(m.QPointF(item.width() * fx, item.height() / 2))
        for kind in ("down", "up"):
            preview.event({"type": kind, "x": point.x(), "y": point.y()})
        m.QTest.qWait(30)

    first_frame = True

    def frame():
        nonlocal first_frame
        image = m.QImage.fromData(preview.frame())
        assert not image.isNull()
        if first_frame:
            # grabToImage's PNG dimensions can be physical pixels; PNG decoding
            # need not preserve its DPR metadata. Report actual bounds, not an
            # assumption about a particular OS/Qt host's monitor scaling.
            print("Glass capture:", json.dumps({"qt": qVersion(),
                "window": [window.width(), window.height()],
                "image": [image.width(), image.height()],
                "windowDpr": window.devicePixelRatio(), "imageDpr": image.devicePixelRatio()}), flush=True)
            first_frame = False
        return image

    def scene_pixel(point, image=None):
        if image is None:
            image = frame()
        # QQuickItem.mapToScene returns logical window coordinates. Sampling
        # that directly in a high-DPI QImage tests a DIFFERENT item (or the
        # transparent shadow), not the intended material. Keep every existing
        # color/contrast assertion, scaling by this capture's actual extent.
        x = int(point.x() * image.width() / window.width())
        y = int(point.y() * image.height() / window.height())
        assert 0 <= x < image.width() and 0 <= y < image.height(), (point, x, y, image.size())
        return image.pixelColor(x, y)

    def pixel(item, x, y, image=None):
        return scene_pixel(item.mapToScene(m.QPointF(x, y)), image)

    about = named("aboutSettings", visible=False)
    surface = next(i for i in tree() if m.prop(i, "radius") == 14)
    expected_panel = m.QColor(evaluate(about, "String(UiTheme.colors.primary80)"))
    if os.environ.get("BSI_SETTINGS_NEGATIVE_FRAME") == "1":
        # Reproduce the screenshot's lighter frame/darker inset without even
        # needing transparency or a Gradient. The exact PIXELS must reject it.
        surface.setProperty("color", m.QColor("#353b47"))
    top = pixel(surface, 12, 30)
    bottom = pixel(surface, 12, surface.height() - 30)
    assert top == bottom == expected_panel, ("the whole Settings frame must use the exact flat native theme", top.name(), bottom.name(), expected_panel.name())
    assert not gradient(surface), "glass must not restyle the Settings frame"
    assert expected_panel.alpha() == 255 and surface.property("color") == expected_panel
    selected_fill = next(i for i in tree() if i.objectName() == "bsiTabGlass" and m.prop(i, "opacity") == .2)
    assert gradient(selected_fill), "only the selected tab must have glass"
    if os.environ.get("BSI_GLASS_NEGATIVE_FLAT") == "1":
        # The requested reflection belongs ONLY to the selected tab now. Keep
        # the same strict >6 painted-lightness delta and five-DPI coverage;
        # flattening it must still fail, not count as opaque-theme success.
        evaluate(selected_fill, "gradient.stops[0].color = gradient.stops[1].color")
    # Compare two pixels from ONE actual frame, not different render instants.
    # Keep the original sample points and strict >6 contrast unchanged.
    reflection = frame()
    tab_top = pixel(selected_fill, 5, 8, reflection)
    tab_bottom = pixel(selected_fill, 5, selected_fill.height() - 8, reflection)
    print("Selected-tab paint:", json.dumps({"top": tab_top.name(), "bottom": tab_bottom.name(),
        "lightness": [tab_top.lightness(), tab_bottom.lightness()],
        "size": [selected_fill.width(), selected_fill.height()],
        "origin": [selected_fill.mapToScene(m.QPointF()).x(), selected_fill.mapToScene(m.QPointF()).y()],
        "opacity": selected_fill.opacity(), "stripX": preview.strip.property("contentX"),
        "gradient": evaluate(selected_fill, "[String(gradient.stops[0].color),String(gradient.stops[1].color)].join(',')"),
        "positions": evaluate(selected_fill, "[gradient.stops[0].position,gradient.stops[1].position].join(',')")}), flush=True)
    assert evaluate(selected_fill, "gradient.stops[0].position === 0 && gradient.stops[1].position === 1"), "selected-tab endpoints must never read Qt's uninitialized default"
    assert tab_top.lightness() > tab_bottom.lightness() + 6, ("the selected-tab reflection is not present in the rendered pixels", tab_top.name(), tab_bottom.name(), tab_top.lightness(), tab_bottom.lightness())
    # Both frame and page/footer must stay native-primary80 over a repainting
    # game; no brighter outer frame/dark inset rectangle as in the screenshot.
    # This compositing test also works with software captures in RGB32.
    backdrop_component = m.QQmlComponent(preview.engine)
    backdrop_component.setData(b'import QtQuick 2.15; Rectangle { width:780; height:500; z:-1; color:"white" }', m.QUrl("Backdrop.qml"))
    backdrop = backdrop_component.create()
    assert backdrop is not None
    backdrop.setParentItem(content)
    backdrop.setParent(content)
    page = named("glassSave").parentItem()
    foundation = page.parentItem().parentItem().parentItem()
    assert "Rectangle" in foundation.metaObject().className()
    assert abs(foundation.width() - 568) < .1 and foundation.property("color").alpha() == 255
    assert not evaluate(foundation, "gradient != null"), "the page/footer backing must stay on the flat, opaque paint path"
    # Test INSIDE the native-page rectangle, not just the shell's side gutter.
    # Every native page is transparent in this fixture. Changing the simulated
    # game must not change even one of these page/footer-background samples.
    probes = [(foundation.width() - 8, y) for y in (8, 100, foundation.height() - 8)]
    before_page = [pixel(foundation, x, y) for x, y in probes]
    assert all(color == expected_panel for color in before_page), "the page/footer must match the surrounding native frame exactly"
    backdrop.setProperty("color", m.QColor("#ff00ff"))
    m.QTest.qWait(20)
    assert [pixel(foundation, x, y) for x, y in probes] == before_page, "the native page/footer exposed the simulated game"
    m.QTest.qWait(20)
    assert pixel(surface, 12, surface.height() - 30) == bottom and pixel(surface, 12, 30) == top, "the Settings foundation leaked the unrelated/game surface below it"
    assert not gradient(surface), "a deferred pass must not add glass to the frame"
    assert not gradient(named("glassButtonBackground")), "the tab pass must not replace native control materials"
    assert evaluate(backdrop, "gradient == null"), "Settings styling escaped into an unrelated sibling"
    backdrop.setProperty("visible", False)

    for name in ("glassButtonBackground", "glassFieldBackground", "glassTrack", "glassFill", "glassHandle", "glassDisabledBackground"):
        assert not evaluate(named(name), "gradient != null"), name + " acquired an unsafe material"
    disabled = named("glassDisabled")
    assert not disabled.isEnabled()
    assert evaluate(named("glassDisabledBackground"), "color.a") == 0, "a disabled transparent native button must retain its fill"
    existing = named("glassExisting")
    assert evaluate(existing, "background.gradient.objectName") == "nativeGradient", "existing user/native gradients must not be overwritten"
    native_color = evaluate(existing, "String(background.gradient.stops[0].color)")
    assert m.QColor(native_color) == m.QColor("#554433")

    page = named("glassSave").parentItem()
    save = named("glassSave")
    before = page.property("saves")
    click(save)
    assert page.property("saves") == before + 1, "the glass swallowed native Save input"
    click(disabled)
    assert page.property("saves") == before + 1
    field = named("glassField")
    click(field)
    assert field.hasActiveFocus()
    bg = named("glassFieldBackground")
    assert evaluate(bg, "String(border.color)") == "#54c8ff"
    page.setProperty("invalid", True)
    assert evaluate(bg, "String(border.color)") == "#ee3344", "native validation must retain its own border binding"
    page.setProperty("invalid", False)
    m.QTest.keyClick(window, m.Qt.Key.Key_End)
    m.QTest.keyClick(window, m.Qt.Key.Key_A)
    assert str(field.property("text")).endswith("a"), "native text editing was lost"

    for name in ("glassToggle", "glassCheck", "glassRadio"):
        control = named(name)
        assert evaluate(control, "indicator.children.length") >= 2
        overlays = [i for i in m.items(control) if i.objectName() == "bsiGlass"]
        assert not overlays, "do not overlay native asset-based indicators or alter their paint/state"
        assert not evaluate(control, "indicator.children[0].gradient != null"), "indicator geometry/material must remain native"
        assert evaluate(control, "indicator.children[0].radius") == (12 if name != "glassCheck" else 3)
        assert not control.property("checked")
        click(control, .2)
        assert control.property("checked"), name + " stopped toggling"
    slider = named("glassSlider")
    click(slider, .8)
    assert slider.property("value") > 150, "the glass handle/track intercepted the native slider"

    # A button's native Loader is free to replace its entire background.
    page.setProperty("alternate", True)
    m.QTest.qWait(200)
    assert not gradient(named("glassReplacementBackground")), "lazy native Loader content must remain unmodified"
    selected_material = evaluate(selected_fill, "gradient")
    for _ in range(10):
        assert QMetaObject.invokeMethod(about, "fGlassTabs") is not False
    assert evaluate(selected_fill, "gradient") == selected_material, "rescanning allocated a replacement selected-tab material"
    assert not gradient(surface), "rescanning decorated the entire window instead of only its selected tab"
    assert not [i for i in m.items(named("glassToggle")) if i.objectName() == "bsiGlass"], "rescanning inserted a native-indicator overlay"
    assert named("glassReplacementBackground").property("radius") == 4
    for name in ("nativeLayeredBackground", "nativeLayeredInner", "nativeLayoutRectangle", "nativeScrollThumb", "nativeScrollBackground"):
        item = named(name)
        assert not evaluate(item, "gradient != null"), name + " was incorrectly decorated"
        assert item.property("radius") == 0, name + " geometry was changed"
    thumb = named("nativeScrollThumb")
    bar = named("nativeScrollBar")
    assert pixel(thumb, thumb.width() / 2, thumb.height() / 2) == m.QColor("#717585"), "the native thumb became transparent"
    bar.setProperty("nativeThumbColor", m.QColor("#aabbcc"))
    m.QTest.qWait(180)
    assert pixel(thumb, thumb.width() / 2, thumb.height() / 2) == m.QColor("#aabbcc"), "native scrollbar color binding was lost"
    # First visits and the deferred rescan must not replace a flat/black frame.
    for index in (4, 2, 7, 11, 0):
        preview.event({"type": "navigate", "index": index})
        first = pixel(surface, 12, surface.height() - 30)
        m.QTest.qWait(180)
        assert pixel(surface, 12, surface.height() - 30) == first, "first-tab paint changed after the material timer"

    # Popups leave the shell's visual tree and live in the window Overlay.
    combo = named("glassCombo")
    click(combo)
    m.QTest.qWait(200)
    assert evaluate(combo, "popup.visible")
    assert evaluate(combo, "popup.background.gradient == null"), "shell styling must not follow a native popup into the Overlay"
    choices = [i for i in tree() if i.objectName() == "glassChoice" and i.isVisible()]
    # Overlay items may be siblings of the shell; find through the whole window.
    assert len(choices) == 3
    click(choices[1])
    assert combo.property("currentIndex") == 1 and page.property("choices") == 1, "native combo activation was changed"
    assert not evaluate(combo, "popup.visible")

    # Labels, selected/hover fill and full-colour outline remain native-themed.
    labels = {m.prop(i, "text"): i for i in m.items(preview.strip) if m.prop(i, "text") in m.LABELS}
    selected = labels["Performance"].parentItem()
    assert gradient(next(i for i in selected.childItems() if m.prop(i, "opacity") == .2))
    hover = labels["Display"].parentItem()
    point = hover.mapToScene(m.QPointF(5, 22)).toPoint()
    m.QTest.mouseMove(window, point)
    m.QTest.qWait(20)
    assert hover.property("containsMouse") and window.property("pCurrentTabIndex") == 0
    fill = next(i for i in hover.childItems() if m.prop(i, "radius") == 6 and m.prop(i, "opacity") == .1)
    assert not gradient(fill), "an unselected/hovered tab must keep its flat tint, not glass"
    assert pixel(fill, 5, 8) == pixel(fill, 5, fill.height() - 8), "hover acquired a painted reflection"
    # Sample the centre of the one-logical-pixel stroke, not its inner
    # antialiased boundary at fractional DPI. Still require the exact opaque
    # accent color; do not loosen it to a property-only/approximate assertion.
    edge = selected.mapToScene(m.QPointF(selected.width() / 2, selected.height() - .5))
    assert scene_pixel(edge) == m.QColor("#54c8ff"), "the glass faded the selected outline"
    preview.event({"type": "accent", "warm": True})
    assert labels["Performance"].property("color") == m.QColor("#f59e0b")
    click(field)
    assert evaluate(bg, "String(border.color)") == "#f59e0b", "native focus bindings must follow live theme changes"

    # Palette bindings must keep working after decoration, including light mode.
    old = pixel(surface, 12, 60)
    evaluate(about, "UiTheme.colors.primary80 = '#edf1f7'; UiTheme.colors.primary10 = '#19202b'")
    m.QTest.qWait(30)
    assert pixel(surface, 12, 60) != old
    assert not gradient(surface)
    assert pixel(surface, 12, 60) == pixel(foundation, foundation.width() - 8, 100) == m.QColor("#edf1f7"), "the flat complete window must follow the live native light theme exactly"
    evaluate(about, "UiTheme.colors.primary80 = '#101a27'; UiTheme.colors.primary10 = '#f4f7fb'")
    preview.event({"type": "accent", "warm": False})

    # Cover lazy/zero-sized pages only after they have their native layout.
    for index in range(12):
        preview.event({"type": "navigate", "index": index})
        m.QTest.qWait(180)
        assert window.property("pCurrentTabIndex") == index
        fills = [i for i in tree() if i.objectName() == "bsiTabGlass"]
        assert sum(gradient(i) for i in fills) == 1, "exactly the selected tab, never all tabs, must have glass"
        assert all(gradient(i) == (m.prop(i, "opacity") == .2) for i in fills)
        assert not gradient(surface) and pixel(surface, 12, 60) == pixel(foundation, foundation.width() - 8, 100) == m.QColor("#101a27"), "a middle page or deferred pass changed the uniform native background"
        if index != 11:
            assert not gradient(named("glassFieldBackground")), "a newly visible native page was restyled"
    assert named("bsiAboutEmulatorLogo").width() == 160
    link = named("bsiAboutDeveloperLink")
    assert evaluate(link, "background.gradient == null"), "the page Loader boundary must also protect About controls"
    point = link.mapToScene(m.QPointF(20, link.height() / 2)).toPoint()
    m.QTest.mouseMove(window, point)
    m.QTest.qWait(20)
    assert link.property("hovered"), "the actual About link lost native hover input"
    assert evaluate(link, "String(background.border.color)") == "#54c8ff"
    state = preview.event({"type": "resize", "small": True})
    assert abs(state["width"] - 760 * .96) <= 1
    assert preview.frame().startswith(b"\x89PNG")
    preview.event({"type": "resize", "small": False})
    assert preview.state()["visibleWidth"] == 750 and preview.state()["visibleHeight"] == 470

    timers = [o for o in about.children() if o.metaObject().className() == "QQmlTimer"]
    assert len(timers) == 1 and timers[0].property("running")
    window.hide()
    m.QTest.qWait(20)
    assert not timers[0].property("running"), "hidden Settings must not keep scanning or rendering glass"
    window.show()
    m.QTest.qWait(20)
    assert timers[0].property("running")
    assert not preview.warnings, "\n".join(preview.warnings)
    window.hide()
    window.deleteLater()
    m.QTest.qWait(20)
    print(f"PASS Qt {qVersion()}: exact flat opaque native-theme frame/page/footer pixels, selected-tab-only glass and flat hover, untouched native materials/input/validation/Loader/popup, preserved native gradients, dark/light/accent bindings, all twelve pages, bounds and no hidden-window work; authored services only")


if __name__ == "__main__":
    try:
        main()
    except BaseException:
        # Failure diagnostics only: no retries, property fallback or relaxed
        # threshold. Preserve the last rendered frame before atexit teardown.
        output = os.environ.get("BSI_GLASS_ARTIFACTS")
        if output and _preview is not None:
            directory = Path(output)
            directory.mkdir(parents=True, exist_ok=True)
            (directory / "glass-failure.png").write_bytes(_preview.last_frame)
            (directory / "glass-state.json").write_text(json.dumps({
                "qt": qVersion(), "state": _preview.state(),
                "windowDpr": _preview.window.devicePixelRatio(),
                "scaleFactor": os.environ.get("QT_SCALE_FACTOR"),
                "warnings": _preview.warnings}, indent=2))
        raise
