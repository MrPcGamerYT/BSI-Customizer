import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Window {
    id: fixture
    width: 1068; height: 430; visible: true; color: "#101a27"
    property int barHeight: 48
    property int callbackCount: 0
    QtObject {
        id: plrBackend
        property string title: "BlueStacks App Player"
        property string version: "5.22.0.1001"
    }
    Rectangle {
        id: bar; objectName: "playerTitleBar"
        width: parent.width; height: fixture.barHeight; color: "#1b2838"
        RowLayout {
            anchors.fill: parent; spacing: 0
            Rectangle { Layout.leftMargin: 12; width: 20; height: 20; radius: 4; color: "#54c8ff" }
            ColumnLayout {
                id: iTitleLayout
                Layout.leftMargin: 12
                Text {
                    id: iBstTxt; objectName: "playerName"
                    text: plrBackend.title
                    color: "#f4f7fb"; font.pixelSize: 15
                    verticalAlignment: Text.AlignTop
                }
                RowLayout { // DETAILS ROW OPEN
                    id: iVersionImgLayout
                    Text {
                        id: iVersionTxt; objectName: "versionLabel"
                        text: plrBackend.version
                        color: "#94a3b8"; font.pixelSize: 11
                    }
                    Text {
                        id: iImageTxt; objectName: "instanceLabel"
                        text: "N32"
                        color: "#94a3b8"; font.pixelSize: 11
                        Layout.leftMargin: 4
                    }
                } // DETAILS ROW CLOSE
            }
            Item { Layout.fillWidth: true }
            Button {
                objectName: "settingsButton"; text: "Settings"
                Layout.preferredHeight: 30
                onClicked: fixture.callbackCount++
            }
            Text { Layout.leftMargin: 16; Layout.rightMargin: 12; text: "—    □    ×"; color: "#f4f7fb" }
        }
    }
    function reloadDetails() { iVersionImgLayout.visible = true }
    // SETTINGS PROTECTION: this complete subtree must remain byte-for-byte unchanged.
    Column {
        x: 30; y: bar.height + 25; spacing: 16
        Text { text: "Settings — sample controls"; color: "#f4f7fb"; font.pixelSize: 20 }
        Repeater {
            model: ["Performance", "CPU allocation", "Memory allocation", "Version", "Instance name", "Display"]
            Text {
                objectName: "settingsText" + index
                text: modelData; color: "#e7edf5"; font.pixelSize: 16
            }
        }
        CheckBox { objectName: "settingsCheck"; text: "Keep Settings text visible"; checked: true }
    }
}
