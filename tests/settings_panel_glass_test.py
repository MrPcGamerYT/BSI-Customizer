"""Real Qt 5/6 pixels and input for the shipping shell AND About-hosted glass.

The generated resources are unmodified. Authored page doubles reproduce the
observed public contracts: Loader-backed buttons, asset-backed indicators,
slider track/fill/handle, text validation and an Overlay-reparented combo popup.
No OEM source/artwork, player process, game renderer or optional effects plugin.
"""
import importlib.util
import os
from pathlib import Path

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
                GradientStop { color: "#554433" }
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
    preview = m.SettingsPreview(page_source=PAGE, about_links=[{"title": "Project", "url": "https://example.com"}])
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

    def frame():
        image = m.QImage.fromData(preview.frame())
        assert not image.isNull()
        return image

    def pixel(item, x, y):
        point = item.mapToScene(m.QPointF(x, y))
        return frame().pixelColor(round(point.x()), round(point.y()))

    about = named("aboutSettings", visible=False)
    surface = next(i for i in tree() if m.prop(i, "radius") == 14)
    assert gradient(surface), "the actual window stayed flat: Qt's unset gradient can be undefined, not only null"
    assert evaluate(surface, "gradient.stops[1].color.a") < .9, "the window must actually paint translucent glass"
    top = pixel(surface, 12, 30)
    bottom = pixel(surface, 12, surface.height() - 30)
    assert top.lightness() > bottom.lightness() + 6, "the window reflection is not present in the rendered pixels"
    # Software item captures are RGB32, so prove transparency by compositing
    # over a real contrasting sibling, not by asserting a fictitious PNG alpha.
    backdrop_component = m.QQmlComponent(preview.engine)
    backdrop_component.setData(b'import QtQuick 2.15; Rectangle { width:780; height:500; z:-1; color:"white" }', m.QUrl("Backdrop.qml"))
    backdrop = backdrop_component.create()
    assert backdrop is not None
    backdrop.setParentItem(content)
    backdrop.setParent(content)
    m.QTest.qWait(20)
    glass_bottom = pixel(surface, 12, surface.height() - 30)
    glass_top = pixel(surface, 12, 30)
    assert glass_bottom.red() > bottom.red() + 15, "the glass did not actually reveal the contrasting surface below it"
    assert glass_top.red() - top.red() < glass_bottom.red() - bottom.red(), "the lower glass must be more translucent than its reflection"
    assert evaluate(backdrop, "gradient == null"), "Settings styling escaped into an unrelated sibling"
    backdrop.setProperty("visible", False)
    assert 0 < evaluate(surface, "border.color.a") < .3, "the outer glass rim must stay quiet"

    for name in ("glassButtonBackground", "glassFieldBackground", "glassTrack", "glassFill", "glassHandle", "glassDisabledBackground"):
        assert gradient(named(name)), name + " is still flat"
    disabled = named("glassDisabled")
    assert not disabled.isEnabled()
    assert evaluate(named("glassDisabledBackground"), "gradient.stops[0].color.a") == 0
    assert evaluate(named("glassDisabledBackground"), "gradient.stops[1].color.a") == 0, "styling must not turn a disabled transparent button into an enabled-looking fill"
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
        assert len(overlays) == 1, "each asset-based native indicator needs exactly one non-intercepting glass surface"
        assert gradient(overlays[0])
        assert abs(overlays[0].width() - evaluate(control, "indicator.width")) < .1
        assert overlays[0].property("radius") == (12 if name != "glassCheck" else 3)
        assert not control.property("checked")
        click(control, .2)
        assert control.property("checked"), name + " stopped toggling"
    slider = named("glassSlider")
    click(slider, .8)
    assert slider.property("value") > 150, "the glass handle/track intercepted the native slider"

    # A button's native Loader is free to replace its entire background.
    page.setProperty("alternate", True)
    m.QTest.qWait(200)
    assert gradient(named("glassReplacementBackground"))
    for _ in range(10):
        assert QMetaObject.invokeMethod(about, "fGlassAll") is not False
    assert len([i for i in m.items(named("glassToggle")) if i.objectName() == "bsiGlass"]) == 1, "rescanning duplicated overlays"

    # Popups leave the shell's visual tree and live in the window Overlay.
    combo = named("glassCombo")
    click(combo)
    m.QTest.qWait(200)
    assert evaluate(combo, "popup.visible")
    assert evaluate(combo, "popup.background.gradient != null"), "the reparented popup was not styled"
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
    assert gradient(fill), "hover must use the same glass material, at lower opacity"
    edge = selected.mapToScene(m.QPointF(selected.width() / 2, selected.height() - 1))
    assert frame().pixelColor(round(edge.x()), round(edge.y())) == m.QColor("#54c8ff"), "the glass faded the selected outline"
    preview.event({"type": "accent", "warm": True})
    assert labels["Performance"].property("color") == m.QColor("#f59e0b")
    click(field)
    assert evaluate(bg, "String(border.color)") == "#f59e0b", "native focus bindings must follow live theme changes"

    # Palette bindings must keep working after decoration, including light mode.
    old = pixel(surface, 12, 60)
    evaluate(about, "UiTheme.colors.primary80 = '#edf1f7'; UiTheme.colors.primary10 = '#19202b'")
    m.QTest.qWait(30)
    assert pixel(surface, 12, 60) != old
    assert m.QColor(evaluate(surface, "String(gradient.stops[1].color)")).red() > 200
    assert evaluate(surface, "border.color.r") < .2, "the glass rim did not follow the light palette"
    evaluate(about, "UiTheme.colors.primary80 = '#101a27'; UiTheme.colors.primary10 = '#f4f7fb'")
    preview.event({"type": "accent", "warm": False})

    # Cover lazy/zero-sized pages only after they have their native layout.
    for index in range(12):
        preview.event({"type": "navigate", "index": index})
        m.QTest.qWait(180)
        assert window.property("pCurrentTabIndex") == index
        if index != 11:
            assert gradient(named("glassFieldBackground")), "a newly visible native page stayed flat"
    assert named("bsiAboutEmulatorLogo").width() == 160
    link = named("bsiAboutDeveloperLink")
    assert evaluate(link, "background.gradient != null"), "the About link surface stayed flat"
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
    print(f"PASS Qt {qVersion()}: real generated full Settings glass pixels/alpha, native control input/validation/Loader/popup, preserved gradients, dark/light/accent bindings, selected/hover/indicator states, all twelve pages, bounds and no hidden-window work; authored services only")


if __name__ == "__main__":
    main()
