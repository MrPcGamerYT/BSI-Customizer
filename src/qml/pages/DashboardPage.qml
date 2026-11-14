import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../Icons.js" as Icons

Item {
    id: dashPage
    objectName: "studioDashboard"
    signal navigate(int index)
    property var instances: []

    component Metric: Rectangle {
        id: metric
        property string value
        property string caption
        implicitHeight: 100
        radius: 15; color: Qt.alpha(Theme.card, 0.8); border.color: Theme.border
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 17; spacing: 9
            Text { text: metric.caption.toUpperCase(); color: Theme.textDim; font.pixelSize: 9; font.letterSpacing: 1.4 }
            Text { Layout.fillWidth: true; text: metric.value; textFormat: Text.PlainText; color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 22; font.weight: Font.DemiBold; elide: Text.ElideRight }
        }
    }
    component ToolTile: AbstractButton {
        id: tile
        property string title
        property string description
        property string iconPath
        property bool primary: false
        property string number
        signal activated()
        implicitHeight: 180
        enabled: !backend.busy
        Accessible.name: title
        background: Rectangle {
            radius: 17
            color: tile.hovered ? Theme.raised : Theme.card
            border.color: tile.activeFocus || tile.hovered ? Theme.accent : Theme.border
        }
        contentItem: Item {
            Image { x: 20; y: 20; width: 22; height: 22; source: Icons.icon(tile.iconPath, Theme.textDim) }
            Text { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 20; text: tile.number; color: Theme.tertiary; font.pixelSize: 10 }
            ColumnLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                anchors.margins: 20; spacing: 9
                Text { Layout.fillWidth: true; text: tile.title; color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 16; font.weight: Font.Bold; wrapMode: Text.Wrap }
                Text { Layout.fillWidth: true; text: tile.description; color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap }
            }
        }
        onClicked: activated()
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 14
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 145
                radius: 17; color: Theme.card; border.color: Theme.border
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 24; spacing: 10
                    Text { text: qsTr("CONTROL CENTER / DASHBOARD"); color: Theme.textDim; font.pixelSize: 10; font.letterSpacing: 2 }
                    Text { Layout.fillWidth: true; text: qsTr("Customize your emulator."); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 30; font.weight: Font.Bold; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: qsTr("Precision tools for themes, removals and advanced BlueStacks tweaks."); color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap }
                    Rectangle { width: 130; height: 1; color: Theme.neonSoft }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                GridLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    columns: 2; columnSpacing: 12; rowSpacing: 12
                    ToolTile { Layout.fillWidth: true; primary: true; number: "01"; title: qsTr("Color studio"); description: qsTr("Edit the original QML or Hex palette, presets and gradients."); iconPath: Icons.theme; onActivated: dashPage.navigate(1) }
                    ToolTile { Layout.fillWidth: true; number: "02"; title: qsTr("Resource explorer"); description: qsTr("Inspect and edit player resources. Scans stay manual."); iconPath: Icons.images; onActivated: dashPage.navigate(2) }
                    ToolTile { Layout.fillWidth: true; number: "03"; title: qsTr("Player interface"); description: qsTr("UI removals, sidebar ordering, layout and About controls."); iconPath: Icons.extras; onActivated: dashPage.navigate(6) }
                    ToolTile { Layout.fillWidth: true; number: "04"; title: qsTr("Debloat & Android"); description: qsTr("The original cleanup, ADB and Root.vhd tools."); iconPath: Icons.debloat; onActivated: dashPage.navigate(10) }
                }
                Card {
                    Layout.preferredWidth: 250
                    Layout.fillHeight: true
                    pad: 20
                    ColumnLayout {
                        anchors.fill: parent; spacing: 14
                        Text { text: qsTr("LIVE OVERVIEW"); color: Theme.textDim; font.pixelSize: 9; font.letterSpacing: 1.8 }
                        Text { text: qsTr("System state"); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 19; font.weight: Font.DemiBold }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                        Text { text: qsTr("Selected executable"); color: Theme.textDim; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; text: backend.exePath ? backend.exePath.replace(/\\/g, "/").split("/").pop() : qsTr("No target selected"); textFormat: Text.PlainText; color: Theme.accentText; font.pixelSize: 13; elide: Text.ElideMiddle }
                        Text { Layout.fillWidth: true; text: backend.exePath || qsTr("Detect installations below to get started."); textFormat: Text.PlainText; color: Theme.textDim; font.pixelSize: 10; wrapMode: Text.WrapAnywhere }
                        Text { text: qsTr("Theme: %1").arg(backend.themeFound ? qsTr("Loaded") : qsTr("Not loaded")); color: Theme.textDim; font.pixelSize: 11 }
                        Item { Layout.fillHeight: true }
                        ActionButton { Layout.fillWidth: true; text: qsTr("DETECT INSTALLATIONS"); accent: true; enabled: !backend.busy; onClicked: backend.detectInstalls() }
                        ActionButton { Layout.fillWidth: true; text: qsTr("PATCH WORKSPACE"); onClicked: dashPage.navigate(9) }
                    }
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: scroll.availableWidth > 1000 ? 4 : 2
                columnSpacing: 12; rowSpacing: 12
                Metric { Layout.fillWidth: true; caption: qsTr("Installations"); value: String(backend.installs.length) }
                Metric { Layout.fillWidth: true; caption: qsTr("Resources scanned"); value: String(backend.resourceInfos.length) }
                Metric { Layout.fillWidth: true; caption: qsTr("Queued operations"); value: String(backend.workspaceOperations.length) }
                Metric { Layout.fillWidth: true; caption: qsTr("Feature pages"); value: "13" }
            }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Installations"); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: qsTr("Detect installations"); enabled: !backend.busy; onClicked: backend.detectInstalls() }
                    }
                    Repeater {
                        model: backend.installs
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true; implicitHeight: 44
                            radius: 9; color: Theme.cardLow; border.color: Theme.border
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 8; spacing: 10
                                Text { text: modelData.profile; textFormat: Text.PlainText; color: Theme.accentText; font.pixelSize: 11 }
                                Text { Layout.fillWidth: true; text: modelData.root; textFormat: Text.PlainText; color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideMiddle }
                                ActionButton { text: qsTr("Use player"); implicitHeight: 28; enabled: !backend.busy; onClicked: backend.useInstall(index) }
                            }
                        }
                    }
                    Text { visible: backend.installs.length === 0; Layout.fillWidth: true; text: qsTr("No installations detected yet. Detect players or enter your existing paths below."); color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap }
                }
            }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    width: parent.width; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Target paths & instances"); color: Theme.text; font.family: Theme.headingFont; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: qsTr("Detect instances"); enabled: !backend.busy; onClicked: dashPage.instances = backend.detectInstances() }
                    }
                    PathRow { Layout.fillWidth: true; label: qsTr("HD-Player.exe"); text: backend.exePath; onLoad: backend.exePath = text }
                    PathRow { Layout.fillWidth: true; label: qsTr("FPS slider DLL"); text: backend.dllPath; onLoad: { settings.fpsAutoDetect = false; backend.dllPath = text; } }
                    Flow {
                        objectName: "studioInstanceFlow"
                        Layout.fillWidth: true
                        spacing: 7
                        Repeater {
                            model: dashPage.instances
                            delegate: Text { required property var modelData; text: modelData.display || modelData.name; textFormat: Text.PlainText; color: Theme.accentText; font.pixelSize: 11 }
                        }
                    }
                }
            }
        }
    }
}
