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
    radius: height / 2

    // Press feedback stays inside the hit target; no layout scaling or delays.
    Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
    Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
    color: button.accent ? (buttonArea.pressed ? "#d8d8df" : Theme.text)
                         : (buttonArea.pressed || buttonArea.containsMouse ? Theme.hoverFill : "transparent")
    border.width: 1
    border.color: button.activeFocus ? Theme.accent
                  : (button.accent ? Theme.text : (buttonArea.containsMouse ? Theme.hoverBorder : Theme.border))
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: button.text
    Keys.onReturnPressed: if (button.enabled) button.clicked()
    Keys.onSpacePressed: if (button.enabled) button.clicked()
    opacity: button.enabled ? 1.0 : 0.45
    Behavior on opacity { NumberAnimation { duration: Theme.motionDuration } }

    Rectangle {
        anchors.fill: parent; radius: parent.radius
        color: Qt.alpha(Theme.accent, button.accent ? 0.16 : 0.08)
        opacity: buttonArea.pressed ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.motionDuration } }
    }
    Text {
        id: buttonLabel
        anchors.centerIn: parent
        text: button.text
        color: button.accent ? Theme.window : Theme.text
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
