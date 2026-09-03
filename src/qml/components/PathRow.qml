import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

RowLayout {
    id: row

    property string label: ""
    property alias text: field.text
    property bool showLoad: false

    signal load()

    spacing: 8

    Text {
        text: row.label
        color: Theme.textDim
        font.pixelSize: 13
        Layout.preferredWidth: 130
    }

    TextField {
        id: field
        Layout.fillWidth: true
        color: Theme.text
        font.pixelSize: 12
        font.family: "Consolas"
        selectByMouse: true
        background: Rectangle {
            color: Theme.raised
            border.color: field.activeFocus ? Theme.accent : Theme.border
            radius: 8
        }
        onAccepted: row.load()
    }

    ActionButton {
        text: qsTr("Browse")
        onClicked: fileDialog.open()
    }

    ActionButton {
        visible: row.showLoad
        accent: true
        text: qsTr("Load")
        onClicked: row.load()
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Select file")
        onAccepted: {
            // file:///C:/... -> C:/...
            field.text = selectedFile.toString().replace(/^file:\/{2,3}/, "");
            row.load();
        }
    }
}
