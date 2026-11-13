import BSICustomizer
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: control

    property string label: ""
    property int value: 0
    property int from: 0
    property int to: 100
    property int step: 1

    spacing: 12

    Text {
        text: control.label
        color: Theme.text
        font.pixelSize: 14
        Layout.fillWidth: true
    }

    Row {
        spacing: 6

        Rectangle {
            width: 32
            height: 32
            radius: 8
            color: minusArea.pressed ? Theme.raised : "transparent"
            border.width: 1
            border.color: Theme.border
            Text {
                anchors.centerIn: parent
                text: "\u2212"
                color: Theme.textDim
                font.pixelSize: 16
            }
            MouseArea {
                id: minusArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: control.value = Math.max(control.from, control.value - control.step)
            }
        }

        Rectangle {
            width: 88
            height: 32
            radius: 8
            color: Theme.raised
            border.width: 1
            border.color: Theme.border
            Text {
                anchors.centerIn: parent
                text: control.value
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.Medium
            }
        }

        Rectangle {
            width: 32
            height: 32
            radius: 8
            color: plusArea.pressed ? Theme.raised : "transparent"
            border.width: 1
            border.color: Theme.border
            Text {
                anchors.centerIn: parent
                text: "+"
                color: Theme.textDim
                font.pixelSize: 16
            }
            MouseArea {
                id: plusArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: control.value = Math.min(control.to, control.value + control.step)
            }
        }
    }
}
