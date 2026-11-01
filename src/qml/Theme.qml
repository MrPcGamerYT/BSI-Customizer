pragma Singleton
import QtQuick

// Shared graphite/mint design tokens. Bindings keep every screen in sync with
// the existing ThemeManager, including saved accents and the system theme.
QtObject {
    id: theme

    readonly property bool dark: themes.effectiveTheme === 0
    readonly property color accent: dark ? themes.accentColor : Qt.darker(themes.accentColor, 1.65)
    readonly property color accentSoft: Qt.alpha(accent, dark ? 0.10 : 0.08)
    readonly property color accentText: dark ? accent : Qt.darker(accent, 1.1)

    readonly property color window: dark ? "#080d0c" : "#f4f6fa"
    readonly property color sidebar: dark ? "#0d1613" : "#ffffff"
    readonly property color topbar: dark ? "#080d0c" : "#f4f6fa"
    readonly property color card: dark ? "#111e18" : "#ffffff"
    readonly property color cardLow: dark ? "#0b1510" : "#f6f8fc"
    readonly property color raised: dark ? "#1b2b23" : "#eef1f7"
    readonly property color border: dark ? "#2a3b31" : "#dce2ec"
    readonly property color borderStrong: dark ? "#4b6556" : "#aab6c8"
    readonly property color hairline: dark ? "#203127" : "#e7ebf2"
    readonly property color text: dark ? "#f5f8f6" : "#1b263a"
    readonly property color textDim: dark ? "#97a69d" : "#5c6880"
    readonly property color soft: dark ? "#bdc9c2" : "#3c4658"
    readonly property color danger: dark ? "#fa9a9a" : "#b73652"
    readonly property color ok: dark ? "#6ce6d0" : "#167565"
    readonly property color warn: dark ? "#e8bd7b" : "#8d611b"

    // Compatibility aliases used by existing editors and the sign-in gate.
    readonly property color neon: accent
    readonly property color neonSoft: Qt.alpha(accent, 0.35)
    readonly property color neonFaint: Qt.alpha(accent, 0.12)
    readonly property color green: ok
    readonly property color purple: dark ? "#b7a1ff" : "#6354a9"
    readonly property color blue: dark ? "#8cbbff" : "#325e9c"
    readonly property color primaryText: (accent.r * 0.299 + accent.g * 0.587 + accent.b * 0.114) > 0.58 ? "#0b2924" : "#ffffff"
    readonly property color logGreen: ok
    readonly property color logRed: danger
    readonly property color logOrange: warn
    readonly property color logBlue: blue
    readonly property color logGray: textDim

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
        return "#6CE6D0";
    }
}
