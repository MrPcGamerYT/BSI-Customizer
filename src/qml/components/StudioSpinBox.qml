import QtQuick
import QtQuick.Controls.Basic as Basic
import BSICustomizer

Basic.SpinBox {
    id: control
    implicitWidth: 114; implicitHeight: 36
    leftPadding: 30; rightPadding: 30; topPadding: 4; bottomPadding: 4
    opacity: enabled ? 1 : 0.45
    font.family: Theme.monoFont; font.pixelSize: 12
    contentItem: TextInput {
        text: control.displayText
        font: control.font; color: Theme.text
        selectionColor: Theme.accent; selectedTextColor: Theme.primaryText
        horizontalAlignment: Qt.AlignHCenter; verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable; validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true; clip: true
    }
    up.indicator: Rectangle {
        x: control.width - width; width: 30; height: control.height; radius: 9
        color: control.up.pressed || control.up.hovered ? Theme.hoverFill : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
        Text { anchors.centerIn: parent; text: "+"; color: control.value < control.to || control.wrap ? Theme.text : Theme.tertiary; font.pixelSize: 17 }
    }
    down.indicator: Rectangle {
        width: 30; height: control.height; radius: 9
        color: control.down.pressed || control.down.hovered ? Theme.hoverFill : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
        Text { anchors.centerIn: parent; text: "−"; color: control.value > control.from || control.wrap ? Theme.text : Theme.tertiary; font.pixelSize: 17 }
    }
    background: Rectangle {
        radius: 10; color: Theme.cardLow
        border.color: control.activeFocus ? Theme.accent : control.hovered ? Theme.hoverBorder : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
    }
}
