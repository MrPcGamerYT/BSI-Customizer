import QtQuick 2.15
import QtQuick.Controls 2.15

CheckBox {
    id: control
    property var shaderStyle
    implicitHeight: 28
    implicitWidth: contentItem.implicitWidth + 22
    padding: 0; spacing: 8
    font: shaderStyle.bodyFont
    hoverEnabled: true
    indicator: Rectangle {
        implicitWidth: 14; implicitHeight: 14
        y: (control.height - height) / 2
        radius: 2
        color: control.checked ? control.shaderStyle.accent : "transparent"
        border.color: control.activeFocus || control.hovered ? control.shaderStyle.accent : control.checked ? control.shaderStyle.accent : control.shaderStyle.muted
        opacity: control.enabled ? 1 : .5
        Text {
            anchors.centerIn: parent; text: "✓"; visible: control.checked
            color: control.shaderStyle.text; font.pixelSize: 13
        }
    }
    contentItem: Text {
        text: control.text; font: control.font
        leftPadding: 22; verticalAlignment: Text.AlignVCenter
        color: control.enabled ? control.shaderStyle.secondary : control.shaderStyle.muted
        opacity: control.enabled ? 1 : .5
        elide: Text.ElideRight
    }
    background: null
}
