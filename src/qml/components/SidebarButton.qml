import BSICustomizer
import QtQuick
import "../Icons.js" as Icons

Item {
    id: button

    property string label
    property string iconPath    // raw SVG inner markup from Icons.js
    property bool active: false

    signal clicked()

    height: 42

    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        radius: 10
        color: button.active
               ? Qt.rgba(56 / 255, 225 / 255, 255 / 255, 0.10)
               : (hoverArea.containsMouse ? Qt.rgba(1, 1, 1, 0.03) : "transparent")
        border.width: button.active ? 1 : 0
        border.color: button.active ? Theme.neonSoft : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }

        // The lit rail: a cyan -> green bar on the active item.
        Rectangle {
            visible: button.active
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: 24
            radius: 2
            gradient: Gradient {
                GradientStop { position: 0; color: "#38e1ff" }
                GradientStop { position: 1; color: "#4dffd9" }
            }
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 14
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
                font.pixelSize: 13.5
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
