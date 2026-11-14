import BSICustomizer
import QtQuick

// Flat studio controls, shared by every existing editor.
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

    scale: buttonArea.pressed ? 0.985 : 1
    Behavior on scale { NumberAnimation { duration: Theme.motionDuration; easing.type: Easing.OutCubic } }
    Behavior on color { ColorAnimation { duration: Theme.motionDuration } }

    color: button.accent ? (buttonArea.pressed ? Qt.darker(Theme.accent, 1.12) : Theme.accent)
                         : (buttonArea.pressed || buttonArea.containsMouse ? Theme.raised : Theme.card)
    border.width: 1
    border.color: button.activeFocus ? Theme.accent
                  : (button.accent ? Theme.accent : (buttonArea.containsMouse ? Theme.borderStrong : Theme.border))
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: button.text
    Keys.onReturnPressed: if (button.enabled) button.clicked()
    Keys.onSpacePressed: if (button.enabled) button.clicked()
    opacity: button.enabled ? 1.0 : 0.45
    Behavior on opacity { NumberAnimation { duration: Theme.motionDuration } }

    Text {
        id: buttonLabel
        anchors.centerIn: parent
        text: button.text
        color: button.accent && button.enabled ? Theme.primaryText : Theme.text
        font.pixelSize: 12
        font.weight: Font.DemiBold
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
