import BSICustomizer
import QtQuick
import QtQuick.Layouts

RowLayout {
    id: control

    property string label: ""
    property string description: ""
    property bool checked: false

    // Parent decides what happens (keeps `checked` bindings intact).
    signal toggled()

    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Text {
            text: control.label
            color: Theme.text
            font.pixelSize: 14
            font.weight: Font.Medium
        }
        Text {
            visible: control.description.length > 0
            Layout.fillWidth: true
            text: control.description
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }

    Rectangle {
        id: track
        Layout.preferredWidth: 46
        Layout.preferredHeight: 26
        radius: 13
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: control.checked ? "#06b6d4" : Theme.raised
            }
            GradientStop {
                position: 1
                color: control.checked ? "#4dffd9" : Theme.raised
            }
        }
        border.width: 1
        border.color: control.checked ? "transparent" : Theme.border
        Behavior on opacity { NumberAnimation { duration: 150 } }

        Rectangle {
            id: knob
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? track.width - width - 4 : 4
            color: control.checked ? "#04202b" : Theme.textDim
            Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: control.toggled()
        }
    }
}
