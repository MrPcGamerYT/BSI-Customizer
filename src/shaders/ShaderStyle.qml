// Original presentation adapter. Reads the owning Settings theme directly;
// no OEM implementation/artwork is copied and no host-wide style is changed.
import QtQuick 2.15

QtObject {
    property var nativeTheme: null
    property var fallbackPalette: null
    property font fallbackFont: Qt.font({family: "Segoe UI", pixelSize: 14})
    function get(path, fallback) {
        var value = nativeTheme
        var keys = path.split(".")
        for (var i = 0; value !== null && value !== undefined && i < keys.length; ++i)
            value = value[keys[i]]
        return value === undefined || value === null ? fallback : value
    }
    function color(name, fallback) {
        var value = get("colors." + name, undefined)
        if (value === undefined && fallbackPalette) value = fallbackPalette[name]
        return value === undefined || value === null ? fallback : value
    }
    readonly property color text: color("primary10", "#ffffff")
    readonly property color secondary: color("primary20", "#c7c9d1")
    readonly property color muted: color("primary30", "#abafba")
    readonly property color accent: color("accentNormal", color("subWindowHeaderClick", "#55b8ff"))
    readonly property color accentHover: color("accentHover", accent)
    readonly property color panel: color("primary80", "#21242c")
    readonly property color field: get("comboBoxes.colors.accent.normal.background", color("primary90", Qt.darker(panel, 1.2)))
    readonly property color border: get("comboBoxes.colors.accent.normal.border", color("primary50", "#5c6170"))
    readonly property color separator: color("seperator", color("primary60", "#454954"))
    readonly property color danger: color("dangerNormal", "#ed1c24")
    readonly property font bodyFont: get("fonts.bodyLarge", fallbackFont)
    readonly property font smallFont: get("fonts.bodySmall", Qt.font({family: bodyFont.family, pixelSize: 12}))
    readonly property font buttonFont: get("buttons.sizes.medium.textFont", bodyFont)
    readonly property real buttonHeight: get("buttons.sizes.medium.height", 32)
    readonly property real buttonPadding: get("buttons.sizes.medium.horizontalPadding", 16)
    readonly property real buttonRadius: get("buttons.sizes.medium.radius", 0)
    readonly property real comboHeight: get("comboBoxes.sizes.medium.height", 32)
    readonly property real sliderHandle: get("sliders.sizes.medium.handleWidth", 20)
    readonly property color sliderTrack: get("sliders.colors.accent.sliderBackground", secondary)
    readonly property color sliderFill: get("sliders.colors.accent.sliderFill", accent)
    readonly property color sliderDisabled: get("sliders.colors.accent.sliderBackgroundDisabled", border)
    readonly property color sliderHandleColor: get("sliders.colors.accent.handleBackground", accent)
    readonly property color sliderHandleBorder: get("sliders.colors.accent.handleBorder", panel)
    readonly property color scrollColor: get("scrollBars.colors.accent.background", color("primary50", "#5c6170"))
    readonly property color scrollHover: get("scrollBars.colors.accent.backgroundHover", color("primary40", "#8f94a3"))
    readonly property var buttonGradient: get("gradients.gradientButton", null)
    function buttonState(primary, down, hovered) {
        return get("buttons.colors." + (primary ? "accent" : "secondary") + "." + (down ? "click" : hovered ? "hover" : "normal"), null)
    }
    function value(object, name, fallback) {
        return object && object[name] !== undefined && object[name] !== null ? object[name] : fallback
    }
}
