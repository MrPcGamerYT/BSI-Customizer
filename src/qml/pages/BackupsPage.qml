import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page
    signal notify(string message)
    property var entries: []
    ColumnLayout {
        anchors.fill: parent; spacing: 14
        Label { text: qsTr("Backups / Recovery"); color: Theme.text; font.pixelSize: 26; font.bold: true }
        Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim; text: qsTr("Workspace and Add-ons retain transactional snapshots. Restore requires the exact post-patch EXE hash; an emulator update or unrelated edit blocks recovery. Legacy editor backups remain on their original pages.") }
        ActionButton { text: qsTr("Refresh snapshots"); enabled: !backend.busy; onClicked: { const r = backend.workspace("history", {}); page.entries = r.entries || []; page.notify(r.msg || ""); } }
        StudioComboBox { id: selection; Layout.fillWidth: true; model: page.entries; textRole: "id" }
        ScrollView { Layout.fillWidth: true; Layout.fillHeight: true
            StudioTextArea { readOnly: true; selectByMouse: true; color: Theme.textDim; wrapMode: TextEdit.Wrap; text: selection.currentIndex >= 0 ? JSON.stringify(page.entries[selection.currentIndex], null, 2) : qsTr("Refresh to load snapshots. No file changes happen on page entry.") }
        }
        ActionButton { text: qsTr("Restore selected snapshot…"); enabled: selection.currentIndex >= 0 && !backend.busy; onClicked: confirmRestore.open() }
    }
    Dialog {
        id: confirmRestore; anchors.centerIn: parent; modal: true; title: qsTr("Restore verified snapshot?")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: { const entry = page.entries[selection.currentIndex]; if (entry) { const r = backend.workspace("restore", {id: entry.id}); page.notify(r.msg || ""); } }
    }
}
