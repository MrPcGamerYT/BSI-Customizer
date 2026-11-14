import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../Icons.js" as Icons

Rectangle {
    id: preview
    property string packName: "VOID / Carbon"
    property color accentColor: "#d8faec"
    property color surfaceColor: "#101012"
    property int cornerRadius: 4
    property string motion: "fade"
    property int duration: 240
    property string iconStyle: "line"
    property int controlHeight: 32
    implicitHeight: 250
    color: surfaceColor
    radius: 14
    border.color: "#39393e"
    clip: true
    function replay() {
        entrance.stop(); content.opacity = 1; content.y = 22; content.x = 22; content.scale = 1;
        if (!Theme.reduceMotion && preview.motion !== "none") entrance.start();
    }
    ParallelAnimation {
        id: entrance
        NumberAnimation { target: content; property: "opacity"; from: 0; to: 1; duration: preview.duration }
        NumberAnimation { target: content; property: "y"; from: preview.motion === "rise" ? 38 : 22; to: 22; duration: preview.duration; easing.type: Easing.OutCubic }
        NumberAnimation { target: content; property: "x"; from: preview.motion === "slide" ? 42 : 22; to: 22; duration: preview.duration; easing.type: Easing.OutCubic }
        NumberAnimation { target: content; property: "scale"; from: preview.motion === "scale" ? 0.94 : 1; to: 1; duration: preview.duration; easing.type: Easing.OutCubic }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 36; color: "#18181b"
            RowLayout { anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 12
                Label { text: preview.packName; color: "#ededf0"; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                Label { text: "—   □   ×"; color: "#9b9ba3"; font.pixelSize: 11 }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.preferredWidth: 40; Layout.fillHeight: true; color: "#1b1b1e"
                Column { anchors.horizontalCenter: parent.horizontalCenter; anchors.top: parent.top; anchors.topMargin: 16; spacing: 16
                    Repeater { model: [Icons.dashboard, Icons.images, Icons.settings]
                        Image { required property string modelData; width: 17; height: 17; source: Icons.icon(modelData, preview.accentColor, preview.iconStyle); sourceSize: Qt.size(24,24) }
                    }
                }
            }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                Item {
                    id: content; x: 22; y: 22; width: parent.width - 44; height: parent.height - 44
                    ColumnLayout {
                        anchors.fill: parent; spacing: 8
                        Label { text: qsTr("YOUR PLAYER. YOUR IDENTITY."); color: "#9999a3"; font.pixelSize: 8; font.letterSpacing: 1.5 }
                        Label { Layout.fillWidth: true; text: preview.packName; textFormat: Text.PlainText; color: "#f4f4f5"; font.pixelSize: 23; font.family: Theme.headingFont; font.bold: true; elide: Text.ElideRight }
                        Label { text: "Developed By NOVA XIS"; color: "#aaaab3"; font.pixelSize: 10 }
                        Rectangle {
                            width: 116; height: preview.controlHeight; radius: preview.cornerRadius; color: preview.accentColor
                            Label { anchors.centerIn: parent; text: qsTr("▶  Replay"); color: "#111114"; font.pixelSize: 11; font.bold: true }
                            TapHandler { onTapped: preview.replay() }
                        }
                        Item { Layout.fillHeight: true }
                        Label { text: "youtube.com/@nova_xis20"; color: "#8f8f99"; font.pixelSize: 9 }
                    }
                }
            }
        }
    }
}
