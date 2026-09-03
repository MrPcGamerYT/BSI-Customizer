import BSICustomizer
import QtQuick
import "../Icons.js" as Icons

Item {
    id: button

    property string label
    property string iconPath    // raw SVG inner markup from Icons.js
    property bool active: false

    signal clicked()

    height: 44

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        radius: 10
        color: button.active ? Theme.accentSoft
                             : (hoverArea.containsMouse ? Qt.alpha(Theme.text, 0.06) : "transparent")
        Behavior on color { ColorAnimation { duration: 120 } }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 12
            spacing: 12

            Image {
                source: Icons.icon(button.iconPath,
                                   button.active ? Theme.accentText : Theme.textDim)
                width: 20
                height: 20
                smooth: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: button.label
                color: button.active ? Theme.accentText : Theme.textDim
                font.pixelSize: 14
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
        onClicked: button.clicked()
    }
}
