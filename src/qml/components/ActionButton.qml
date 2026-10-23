import BSICustomizer
import QtQuick

// FalcoForge button language: accent = the neon cyan gradient with dark text
// and one quiet bloom; plain = the secondary plate.
Rectangle {
    id: button

    property string text
    property bool accent: false

    // Tracked with a HoverHandler, not the MouseArea: the MouseArea is disabled
    // with the button, and a disabled button still needs to explain itself
    // through a ToolTip.
    readonly property bool hovered: buttonHover.hovered

    signal clicked()

    implicitWidth: buttonLabel.implicitWidth + 30
    implicitHeight: 36
    radius: 9

    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop {
            position: 0
            color: !button.enabled ? "transparent"
                 : button.accent
                   ? (buttonArea.pressed ? "#089db8" : "#06b6d4")
                   : (buttonArea.pressed || buttonArea.containsMouse ? "#0b1a2b" : "#0a1626")
        }
        GradientStop {
            position: 0.55
            color: !button.enabled ? "transparent"
                 : button.accent
                   ? (buttonArea.pressed ? "#1bb6cf" : "#22d3ee")
                   : (buttonArea.pressed || buttonArea.containsMouse ? "#071120" : "#071120")
        }
        GradientStop {
            position: 1
            color: !button.enabled ? "transparent"
                 : button.accent
                   ? (buttonArea.pressed ? "#4fcfe6" : "#67e8f9")
                   : (buttonArea.pressed || buttonArea.containsMouse ? "#071120" : "#071120")
        }
    }
    border.width: 1
    border.color: button.accent && button.enabled
                  ? Qt.rgba(165 / 255, 243 / 255, 252 / 255, 0.8)
                  : (buttonArea.containsMouse && button.enabled ? Theme.neonSoft : Theme.border)
    opacity: button.enabled ? 1.0 : 0.45
    Behavior on opacity { NumberAnimation { duration: 120 } }

    Text {
        id: buttonLabel
        anchors.centerIn: parent
        text: button.text
        color: button.accent && button.enabled ? Theme.primaryText : Theme.text
        font.pixelSize: 13
        font.weight: button.accent ? Font.Bold : Font.Medium
    }

    MouseArea {
        id: buttonArea
        anchors.fill: parent
        enabled: button.enabled
        hoverEnabled: true
        cursorShape: button.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        // Finish a focused color/path edit BEFORE the action consumes it.
        onPressed: button.forceActiveFocus(Qt.MouseFocusReason)
        onClicked: button.clicked()
    }

    HoverHandler {
        id: buttonHover
    }
}
