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
            color: control.currentIndex === index ? Theme.accentSoft : chipMouse.containsMouse ? Theme.hoverFill : Theme.raised
            border.width: 1
            border.color: control.currentIndex === index ? Theme.accent : chipMouse.containsMouse ? Theme.hoverBorder : Theme.border
            Behavior on color { ColorAnimation { duration: Theme.motionDuration } }
            Behavior on border.color { ColorAnimation { duration: Theme.motionDuration } }
            activeFocusOnTab: true
            Accessible.role: Accessible.RadioButton
            Accessible.name: String(modelData)
            Accessible.checked: control.currentIndex === index
            Keys.onSpacePressed: { control.currentIndex = index; control.selected(index); }

            Text {
                id: chipLabel
                anchors.centerIn: parent
                text: chip.modelData
                font.pixelSize: 13
                color: control.currentIndex === index ? Theme.accentText : Theme.textDim
            }

            MouseArea {
                id: chipMouse
                hoverEnabled: true
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
