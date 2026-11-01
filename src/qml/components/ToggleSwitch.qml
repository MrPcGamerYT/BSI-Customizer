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

    activeFocusOnTab: true
    Accessible.role: Accessible.CheckBox
    Accessible.name: control.label
    Accessible.checkable: true
    Accessible.checked: control.checked
    Keys.onSpacePressed: control.toggled()
    Keys.onReturnPressed: control.toggled()

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
        color: control.checked ? Theme.accent : Theme.raised
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : (control.checked ? "transparent" : Theme.border)
        Behavior on opacity { NumberAnimation { duration: 150 } }

        Rectangle {
            id: knob
            width: 18
            height: 18
            radius: 9
            y: 3
            x: control.checked ? track.width - width - 4 : 4
            color: control.checked ? Theme.primaryText : Theme.textDim
            Behavior on x { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onPressed: control.forceActiveFocus(Qt.MouseFocusReason)
            onClicked: control.toggled()
        }
    }
}
