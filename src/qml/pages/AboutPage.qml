import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            Rectangle {
                width: 56
                height: 56
                radius: 14
                color: Theme.accent
                Text {
                    anchors.centerIn: parent
                    text: "BSI"
                    color: "#ffffff"
                    font.pixelSize: 15
                    font.weight: Font.Black
                }
            }
            ColumnLayout {
                spacing: 2
                Text {
                    text: qsTr("BSI Customizer")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }
                Text {
                    text: qsTr("BlueStacks 5 / MSI 5 modding toolkit — v%1 · Qt %2")
                          .arg(settings.version).arg(Qt.application.version)
                    color: Theme.textDim
                    font.pixelSize: 13
                }
            }
            Item { Layout.fillWidth: true }
        }

        Card {
            Layout.fillWidth: true
            ColumnLayout {
                anchors.fill: parent
                spacing: 8
                Text {
                    text: qsTr("What it does")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.textDim
                    font.pixelSize: 13
                    lineHeight: 1.4
                    text: qsTr("• Theme Changer — recolors the BlueStacks UI by decompressing the QML "
                               + "theme inside HD-Player.exe (zlib), with automatic relocation when the "
                               + "new theme is bigger than the original, plus a legacy hex-palette mode.\n"
                               + "• SVG Extract/Replace — pull the vector icons out of the EXE and swap "
                               + "in your own.\n"
                               + "• FPS Unlocker — rewrites the frame-rate cap in libOpenglRender.dll.\n"
                               + "• Integrity Bypass — stops BlueStacks from reverting your patches.\n"
                               + "• Boot Video — plays your own Promo.mp4 at startup.\n"
                               + "• Extras — version spoofing, ad/UI string removal, instance detection.\n"
                               + "• Every patch writes a .bak backup first; Restore rolls it back.")
                }
            }
        }

        Card {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                spacing: 24
                Column {
                    Text { text: qsTr("CPU cores"); color: Theme.textDim; font.pixelSize: 11 }
                    Text { text: sysinfo.cpuCores; color: Theme.text; font.pixelSize: 15; font.weight: Font.Bold }
                }
                Column {
                    Text { text: qsTr("RAM (GB)"); color: Theme.textDim; font.pixelSize: 11 }
                    Text { text: sysinfo.ramGb.toFixed(1); color: Theme.text; font.pixelSize: 15; font.weight: Font.Bold }
                }
                Column {
                    Text { text: qsTr("Free disk (GB)"); color: Theme.textDim; font.pixelSize: 11 }
                    Text { text: sysinfo.diskFreeGb.toFixed(0); color: Theme.text; font.pixelSize: 15; font.weight: Font.Bold }
                }
                Column {
                    Text { text: qsTr("OS"); color: Theme.textDim; font.pixelSize: 11 }
                    Text { text: sysinfo.os; color: Theme.text; font.pixelSize: 13 }
                }
                Item { Layout.fillWidth: true }
            }
        }

        Text {
            text: qsTr("Made by MrPcGamerYT · C++/Qt 6 rebuild")
            color: Theme.textDim
            font.pixelSize: 12
        }

        Item { Layout.fillHeight: true }
    }
}
