import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    id: page
    FontLoader { id: brandingFont; source: "../fonts/NotoSansMath-Regular.ttf" }
    signal notify(string message)
    signal navigate(int index)
    readonly property var packs: [
        {key: "carbon", name: "VOID / Carbon", color: "#c4c4c8", radius: 4, note: "Compact · restrained fade"},
        {key: "amethyst", name: "Amethyst", color: "#ac8cff", radius: 12, note: "Soft corners · gentle rise"},
        {key: "arctic", name: "Arctic", color: "#69b7ff", radius: 8, note: "Balanced · glide"},
        {key: "ember", name: "Ember", color: "#ff727d", radius: 2, note: "Sharp · quick reveal"},
        {key: "matrix", name: "Matrix", color: "#70e5aa", radius: 0, note: "Dense · stepped reveal"},
        {key: "nord", name: "Nord", color: "#88c0d0", radius: 10, note: "Spacious · soft scale"}
    ]
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 18
            Label { text: qsTr("Add-ons / Player collections"); color: Theme.text; font.pixelSize: 26; font.bold: true }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim; text: qsTr("Six black-based palettes and button designs. Stage one pack, then review its Dry Run in Patch workspace. Existing batches are never cleared automatically.") }
            Label { Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.textDim; text: qsTr("Motion descriptions are preview concepts only. Player animations, icons, native dialogs, optimizer and boot are unchanged. This is not the complete VOID emulator ZIP.") }
            Repeater {
                model: page.packs
                Card {
                    required property var modelData
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width; spacing: 12
                        Label { text: modelData.name; color: Theme.text; font.bold: true; font.pixelSize: 18 }
                        Rectangle {
                            Layout.fillWidth: true; Layout.preferredHeight: 96
                            color: "#0b0b0e"; radius: modelData.radius; border.color: modelData.color
                            Row { anchors.centerIn: parent; spacing: 16
                                Label { text: "𝓥𝓞𝓘𝓓"; font.family: brandingFont.name; color: "#f3f3f5"; font.pixelSize: 24 }
                                Rectangle { width: 92; height: 32; radius: modelData.radius; color: modelData.color
                                    Label { anchors.centerIn: parent; text: "Play"; color: "#09090b" }
                                }
                            }
                        }
                        Label { text: modelData.note + qsTr(" (preview only)"); color: Theme.textDim }
                        ActionButton {
                            text: qsTr("Stage pack & review →"); enabled: !backend.busy
                            onClicked: {
                                const r = backend.workspace("queue-addon", {id: modelData.key});
                                page.notify(r.msg || "");
                                if (r.ok) page.navigate(9);
                            }
                        }
                    }
                }
            }
        }
    }
}
