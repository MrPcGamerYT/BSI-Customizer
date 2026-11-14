import BSICustomizer
import QtQuick
import "../Icons.js" as Icons

Item {
    id: button

    property string label
    property string iconPath    // raw SVG inner markup from Icons.js
    property bool active: false

    signal clicked()

    height: 38
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: button.label
    Keys.onReturnPressed: button.clicked()
    Keys.onSpacePressed: button.clicked()

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        radius: 7
        color: button.active ? Theme.accentSoft : (hoverArea.containsMouse ? Theme.raised : "transparent")
        border.width: button.activeFocus ? 1 : 0
        border.color: Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }

        Rectangle {
            visible: button.active
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 5; height: 5; radius: 3
            color: Theme.accent
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 14
            spacing: 11

            Image {
                source: Icons.icon(button.iconPath,
                                   button.active ? Theme.accentText : Theme.textDim)
                width: 17
                height: 17
                smooth: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: button.label
                color: button.active ? Theme.accentText : Theme.textDim
                font.pixelSize: 12
                font.weight: button.active ? Font.DemiBold : Font.Normal
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: button.forceActiveFocus(Qt.MouseFocusReason)
        onClicked: button.clicked()
    }
}
