pragma Singleton
import QtQuick

// Quiet control-center tokens. System light mode remains supported in Settings.
QtObject {
    id: theme
    readonly property string bodyFont: "Inter"
    readonly property string headingFont: "Inter"
    readonly property string monoFont: "JetBrains Mono"
    readonly property bool reduceMotion: themes.reduceMotion
    readonly property bool backgroundMotion: themes.backgroundMotion
    readonly property int revealDuration: reduceMotion ? 0 : 220
    readonly property color hoverFill: Qt.alpha(accent, dark ? 0.12 : 0.08)
    readonly property color hoverBorder: Qt.alpha(accent, 0.45)
    readonly property int motionDuration: reduceMotion ? 0 : 150
    readonly property bool dark: themes.effectiveTheme === 0
    readonly property color defaultAccent: "#7C5CFF"
    readonly property color accent: themes.accentColor
    readonly property color accentSoft: Qt.alpha(accent, 0.08)
    readonly property color accentText: dark ? "#c3b6ff" : "#6143da"
    readonly property color window: dark ? "#0A0A0E" : "#f4f4f8"
    readonly property color sidebar: window
    readonly property color topbar: window
    readonly property color card: dark ? "#111116" : "#ffffff"
    readonly property color cardLow: dark ? "#0D0D12" : "#f7f7fa"
    readonly property color raised: dark ? "#17171D" : "#eeeeF4"
    readonly property color border: dark ? "#12ffffff" : "#16000000"
    readonly property color borderStrong: dark ? "#29ffffff" : "#30000000"
    readonly property color hairline: border
    readonly property color text: dark ? "#F2F2F5" : "#17171d"
    readonly property color textDim: dark ? "#8A8A96" : "#63636f"
    readonly property color tertiary: dark ? "#5C5C68" : "#777783"
    readonly property color soft: dark ? "#b1b1bd" : "#494955"
    readonly property color danger: dark ? "#e59797" : "#ac3549"
    readonly property color ok: dark ? "#c1c1cc" : "#53604b"
    readonly property color ready: "#C8E86A"
    readonly property color warn: dark ? "#d2b18d" : "#886339"

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
        return "#7C5CFF";
    }
}
