import QtQuick
import QtQuick.Controls.Basic as Basic
import BSICustomizer

Basic.CheckBox {
    id: control
    implicitHeight: Math.max(32, contentItem.implicitHeight + 8)
    spacing: 9; padding: 4
    opacity: enabled ? 1 : 0.45
    indicator: Rectangle {
        x: control.leftPadding; y: (control.height - height) / 2
        implicitWidth: 18; implicitHeight: 18; radius: 5
        color: control.checked ? Theme.accentSoft : Theme.cardLow
        border.color: control.activeFocus || control.checked ? Theme.accent : control.hovered ? Theme.hoverBorder : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
        Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
        Text {
            anchors.centerIn: parent
            text: control.checkState === Qt.PartiallyChecked ? "−" : "✓"
            visible: control.checkState !== Qt.Unchecked
            color: Theme.accentText; font.pixelSize: 13
        }
    }
    contentItem: Text {
        text: control.text; textFormat: Text.PlainText
        leftPadding: control.indicator.width + control.spacing
        verticalAlignment: Text.AlignVCenter
        font.family: Theme.bodyFont; font.pixelSize: 12
        color: Theme.text; wrapMode: Text.Wrap
    }
}
