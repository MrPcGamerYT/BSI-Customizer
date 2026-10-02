pragma Singleton
import QtQuick

// ---------------------------------------------------------------------------
// Theme singleton — every color in the app is a BINDING to these properties.
// 'themes' is the C++ ThemeManager context property; when its NOTIFY fires,
// all bindings below update live. Default palette: #0d0f14 / #151821 / #7c5cff.
// ---------------------------------------------------------------------------
QtObject {
    id: theme

    readonly property bool dark: themes.effectiveTheme === 0
    readonly property color accent: themes.accentColor
    readonly property color accentSoft: Qt.alpha(accent, dark ? 0.16 : 0.12)
    readonly property color accentText: dark ? Qt.lighter(accent, 1.25) : Qt.darker(accent, 1.1)

    readonly property color window:  dark ? "#0d0f14" : "#eef0f6"
    readonly property color sidebar: dark ? "#0b0d12" : "#ffffff"
    readonly property color card:    dark ? "#151821" : "#ffffff"
    readonly property color raised:  dark ? "#1e222d" : "#f1f2f8"
    readonly property color border:  dark ? "#272c39" : "#dfe2ec"
    readonly property color text:    dark ? "#e9edf5" : "#1c1c28"
    readonly property color textDim: dark ? "#9098ab" : "#666c80"
    readonly property color danger:  dark ? "#ff6b6b" : "#d63a5f"
    readonly property color ok:      dark ? "#38d996" : "#2e9e5b"

    // Log colors used by the log panel
    readonly property color logGreen:  dark ? "#38d996" : "#1d9e63"
    readonly property color logRed:    dark ? "#ff6b6b" : "#d63a5f"
    readonly property color logOrange: dark ? "#ffb454" : "#c77b1f"
    readonly property color logBlue:   dark ? "#5aa7ff" : "#2b6fd6"
    readonly property color logGray:   dark ? "#9098ab" : "#666c80"

    function logColor(name) {
        if (name === "green") return logGreen;
        if (name === "red") return logRed;
        if (name === "orange") return logOrange;
        if (name === "blue") return logBlue;
        return logGray;
    }

    // "#RRGGBB", upper-case. Qt prints a color as "#AARRGGBB" whenever alpha < 255;
    // BlueStacks' own records use plain 6-digit hex, so the alpha pair is dropped.
    function hexFromColor(c) {
        const s = String(c);
        if (/^#[0-9a-fA-F]{8}$/.test(s))
            return ("#" + s.substring(3)).toUpperCase();
        if (/^#[0-9a-fA-F]{6}$/.test(s))
            return s.toUpperCase();
        return "#7C5CFF";
    }
}
