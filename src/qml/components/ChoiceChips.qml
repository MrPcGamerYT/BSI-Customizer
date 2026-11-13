import BSICustomizer
import QtQuick

Row {
    id: control

    property var model: []
    property int currentIndex: 0

    signal selected(int index)

    spacing: 8

    Repeater {
        model: control.model
        delegate: Rectangle {
            id: chip
            required property var modelData
            required property int index

            width: chipLabel.implicitWidth + 30
            height: 34
            radius: 9
            color: control.currentIndex === index ? Theme.accentSoft : Theme.raised
            border.width: 1
            border.color: control.currentIndex === index ? Theme.accent : Theme.border

            Text {
                id: chipLabel
                anchors.centerIn: parent
                text: chip.modelData
                font.pixelSize: 13
                color: control.currentIndex === index ? Theme.accentText : Theme.textDim
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    control.currentIndex = index;
                    control.selected(index);
                }
            }
        }
    }
}
