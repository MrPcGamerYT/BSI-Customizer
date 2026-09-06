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
    property bool readOnly: false

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
        readOnly: row.readOnly
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
        onAccepted: { if (!row.readOnly) row.load(); }
    }

    ActionButton {
        text: qsTr("Browse")
        enabled: !row.readOnly
        onClicked: fileDialog.open()
    }

    ActionButton {
        visible: row.showLoad && !row.readOnly
        accent: true
        text: qsTr("Load")
        onClicked: row.load()
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Select file")
        onAccepted: {
            if (row.readOnly) return;
            // Decode file URLs properly (Program Files, Unicode and UNC paths).
            field.text = backend.localFilePath(selectedFile);
            row.load();
        }
    }
}
