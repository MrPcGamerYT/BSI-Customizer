import QtQuick
import QtQuick.Controls.Basic as Basic
import BSICustomizer

Basic.TabButton {
    id: control
    implicitHeight: 34
    horizontalPadding: 14; verticalPadding: 8
    contentItem: Text {
        text: control.text; font.family: Theme.bodyFont; font.pixelSize: 12
        font.weight: control.checked ? Font.DemiBold : Font.Normal
        color: control.checked ? Theme.text : Theme.textDim
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight; textFormat: Text.PlainText
    }
    background: Rectangle {
        radius: 9
        color: control.checked ? Theme.raised : control.hovered ? Theme.hoverFill : "transparent"
        border.color: control.activeFocus ? Theme.accent : control.checked ? Theme.borderStrong : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
        Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
    }
}
