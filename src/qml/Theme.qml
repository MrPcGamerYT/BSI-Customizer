pragma Singleton
import QtQuick

// ---------------------------------------------------------------------------
// Theme singleton — every color in the app is a BINDING to these properties.
// 'themes' is the C++ ThemeManager context property; when its NOTIFY fires,
// all bindings below update live.
//
// Dark palette is the Premium-Optimizer (FalcoForge) "midnight neon" identity:
// deep blue-navy surfaces, one clean cyan, a cyan -> green accent gradient for
// primary actions, unified neon hairline borders and soft glows.
// ---------------------------------------------------------------------------
QtObject {
    id: theme

    readonly property bool dark: themes.effectiveTheme === 0
    readonly property color accent: themes.accentColor
    readonly property color accentSoft: Qt.alpha(accent, dark ? 0.16 : 0.12)
    readonly property color accentText: dark ? Qt.lighter(accent, 1.25) : Qt.darker(accent, 1.1)

    // ---- midnight neon surfaces ----
    readonly property color window:  dark ? "#04070f" : "#eef1f8"
    readonly property color sidebar: dark ? "#050e1a" : "#ffffff"
    readonly property color topbar:  dark ? "#061120" : "#f7f8fc"
    readonly property color card:    dark ? "#081524" : "#ffffff"
    readonly property color cardLow: dark ? "#040b16" : "#f6f7fb"
    readonly property color raised:  dark ? "#0b1a2b" : "#eef1f8"
    readonly property color border:  dark ? Qt.rgba(56 / 255, 225 / 255, 255 / 255, 0.16)
                                          : "#dfe2ec"
    readonly property color borderStrong: dark ? Qt.rgba(56 / 255, 225 / 255, 255 / 255, 0.55)
                                               : Qt.darker(accent, 1.2)
    readonly property color hairline: dark ? Qt.rgba(255 / 255, 255 / 255, 255 / 255, 0.07)
                                           : "#e7eaf2"
    readonly property color text:    dark ? "#eef4fb" : "#1c2230"
    readonly property color textDim: dark ? "#93a4bd" : "#667085"
    readonly property color soft:    dark ? "#d6e2f2" : "#3c4658"
    readonly property color danger:  dark ? "#ff6f83" : "#d63a5f"
    readonly property color ok:      dark ? "#4dffd9" : "#1d9e63"
    readonly property color warn:    dark ? "#ffc45c" : "#c77b1f"

    // Neon accents used by the FalcoForge-style chrome
    readonly property color neon:      dark ? "#38e1ff" : accent
    readonly property color neonSoft:  dark ? Qt.rgba(56 / 255, 225 / 255, 255 / 255, 0.55)
                                            : Qt.alpha(accent, 0.55)
    readonly property color neonFaint: dark ? Qt.rgba(56 / 255, 225 / 255, 255 / 255, 0.16)
                                            : Qt.alpha(accent, 0.16)
    readonly property color green:     dark ? "#4dffd9" : "#1d9e63"

    // Text that sits on top of a primary (accent) button.
    readonly property color primaryText: dark ? "#04202b" : "#ffffff"

    // Log colors used by the log panel
    readonly property color logGreen:  dark ? "#4dffd9" : "#1d9e63"
    readonly property color logRed:    dark ? "#ff6f83" : "#d63a5f"
    readonly property color logOrange: dark ? "#ffc45c" : "#c77b1f"
    readonly property color logBlue:   dark ? "#38e1ff" : "#2b6fd6"
    readonly property color logGray:   dark ? "#93a4bd" : "#667085"

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
        return "#23E3FF";
    }
}
