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
        property string iconPath
        property color tone: Theme.accent
        implicitHeight: 78
        radius: 11
        color: Theme.card
        border.color: Theme.border
        RowLayout {
            anchors.fill: parent; anchors.margins: 15; spacing: 12
            Rectangle {
                Layout.preferredWidth: 36; Layout.preferredHeight: 36
                radius: 9; color: Qt.alpha(metric.tone, 0.07); border.color: Qt.alpha(metric.tone, 0.16)
                Image { anchors.centerIn: parent; width: 17; height: 17; source: Icons.icon(metric.iconPath, metric.tone) }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 5
                Text { text: metric.value; color: Theme.text; font.pixelSize: 21; font.weight: Font.DemiBold }
                Text { Layout.fillWidth: true; text: metric.caption; color: Theme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap }
            }
        }
    }

    component ToolTile: Rectangle {
        id: tile
        property string title
        property string description
        property string iconPath
        property color tone: Theme.accent
        signal activated()
        implicitHeight: 142
        radius: 11
        color: hover.hovered ? Theme.raised : Theme.card
        border.color: hover.hovered || activeFocus ? Theme.borderStrong : Theme.border
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: tile.title
        Keys.onReturnPressed: tile.activated()
        Keys.onSpacePressed: tile.activated()
        Behavior on color { ColorAnimation { duration: 120 } }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: { tile.forceActiveFocus(); tile.activated(); } }
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 16; spacing: 9
            RowLayout {
                Layout.fillWidth: true
                Rectangle {
                    width: 34; height: 34; radius: 8; color: Qt.alpha(tile.tone, 0.08)
                    Image { anchors.centerIn: parent; width: 18; height: 18; source: Icons.icon(tile.iconPath, tile.tone) }
                }
                Item { Layout.fillWidth: true }
                Text { text: "↗"; color: Theme.textDim; font.pixelSize: 16 }
            }
            Item { Layout.fillHeight: true }
            Text { Layout.fillWidth: true; text: tile.title; color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
            Text { Layout.fillWidth: true; text: tile.description; color: Theme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: scroll.availableWidth
            spacing: 20
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 26; Layout.rightMargin: 26; Layout.topMargin: 28
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 8
                    Text { text: qsTr("YOUR PERSONAL CONTROL ROOM"); color: Theme.accentText; font.pixelSize: 9; font.weight: Font.DemiBold; font.letterSpacing: 1.6 }
                    Text { Layout.fillWidth: true; text: qsTr("Make your player, yours."); color: Theme.text; font.pixelSize: 28; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
                    Text { Layout.fillWidth: true; text: qsTr("A little more personality. A lot more possibility."); color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.Wrap }
                }
                ActionButton { text: qsTr("Open workspace →"); onClicked: dashPage.navigate(11) }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 26; Layout.rightMargin: 26
                Layout.preferredHeight: 236
                radius: 14
                color: Theme.dark ? "#17232b" : "#e5f0ef"
                border.color: Theme.dark ? "#2e4148" : "#bdcfcb"
                clip: true
                RowLayout {
                    anchors.fill: parent; anchors.margins: 26; spacing: 20
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 12
                        Text { text: qsTr("A NEW WAY TO CUSTOMIZE"); color: Theme.accentText; font.pixelSize: 9; font.letterSpacing: 1.3 }
                        Text { Layout.fillWidth: true; text: qsTr("Same player.\nA whole new experience."); color: Theme.text; font.pixelSize: 27; font.weight: Font.DemiBold; wrapMode: Text.Wrap }
                        Text { Layout.fillWidth: true; text: qsTr("Fine-tune the look, feel, and little details of your emulator. All your customization tools, thoughtfully in one place."); color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap }
                        RowLayout {
                            spacing: 10
                            ActionButton { text: qsTr("Open theme studio →"); accent: true; onClicked: dashPage.navigate(1) }
                            ActionButton { text: qsTr("Explore resources"); onClicked: dashPage.navigate(2) }
                        }
                    }
                    Item {
                        visible: scroll.availableWidth > 850
                        Layout.preferredWidth: 235
                        Layout.fillHeight: true
                        Rectangle {
                            anchors.centerIn: parent; width: 214; height: 144; radius: 12; rotation: -8
                            color: Theme.dark ? "#1c3339" : "#d0e8df"; border.color: Qt.alpha(Theme.accent, 0.4)
                            Rectangle { x: 14; y: 19; width: 5; height: 5; radius: 3; color: Theme.accent }
                            Rectangle { x: 25; y: 19; width: 5; height: 5; radius: 3; color: Qt.alpha(Theme.accent, 0.4) }
                            Rectangle { x: 36; y: 19; width: 5; height: 5; radius: 3; color: Qt.alpha(Theme.accent, 0.2) }
                            Image { anchors.centerIn: parent; width: 52; height: 52; source: Icons.icon(Icons.extras, Theme.accent) }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; y: 113; text: qsTr("MAKE IT PERSONAL"); font.pixelSize: 9; font.letterSpacing: 1.5; color: Theme.soft }
                        }
                        Rectangle {
                            x: 103; y: 126; width: 128; height: 38; radius: 8; rotation: 4
                            color: Theme.dark ? "#29263c" : "#ece5fb"; border.color: Qt.alpha(Theme.purple, 0.35)
                            Text { anchors.centerIn: parent; text: qsTr("A little more you."); color: Theme.purple; font.pixelSize: 11 }
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 26; Layout.rightMargin: 26
                columns: scroll.availableWidth > 820 ? 4 : 2
                columnSpacing: 12; rowSpacing: 12
                Metric { Layout.fillWidth: true; value: String(backend.installs.length); caption: qsTr("Detected installations"); iconPath: Icons.dashboard }
                Metric { Layout.fillWidth: true; value: String(backend.resourceInfos.length); caption: qsTr("Resources scanned"); iconPath: Icons.images; tone: Theme.purple }
                Metric { Layout.fillWidth: true; value: qsTr("Local"); caption: qsTr("Native patch engines"); iconPath: Icons.integrity; tone: Theme.blue }
                Metric { Layout.fillWidth: true; value: String(backend.workspaceOperations.length); caption: qsTr("Workspace operations"); iconPath: Icons.extras; tone: Theme.warn }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 26; Layout.rightMargin: 26
                spacing: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.alignment: Qt.AlignTop
                    spacing: 14
                    Text { text: qsTr("Your creative toolkit"); color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                    GridLayout {
                        id: toolkit
                        Layout.fillWidth: true
                        columns: width > 570 ? 3 : 2
                        columnSpacing: 12; rowSpacing: 12
                        Repeater {
                            model: [
                                { page: 1, title: qsTr("Theme studio"), description: qsTr("A palette that feels like you."), icon: Icons.theme, tone: Theme.accent },
                                { page: 2, title: qsTr("Resource explorer"), description: qsTr("Find the details. Make them yours."), icon: Icons.images, tone: Theme.purple },
                                { page: 3, title: qsTr("Performance"), description: qsTr("A smoother way to play."), icon: Icons.fps, tone: Theme.warn },
                                { page: 5, title: qsTr("Boot experience"), description: qsTr("Make a great first impression."), icon: Icons.boot, tone: Theme.blue },
                                { page: 7, title: qsTr("Discord presence"), description: qsTr("Show what you are playing."), icon: Icons.discord, tone: Theme.purple },
                                { page: 6, title: qsTr("Player interface"), description: qsTr("Less clutter. More you."), icon: Icons.extras, tone: Theme.danger }
                            ]
                            delegate: ToolTile {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                title: modelData.title; description: modelData.description; iconPath: modelData.icon; tone: modelData.tone
                                onActivated: dashPage.navigate(modelData.page)
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.preferredWidth: 242
                    Layout.minimumWidth: 220
                    Layout.maximumWidth: 270
                    Layout.alignment: Qt.AlignTop
                    spacing: 14
                    Card {
                        Layout.fillWidth: true
                        ColumnLayout {
                            width: parent.width; spacing: 12
                            Text { text: qsTr("Selected player"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: backend.exePath ? backend.exePath.replace(/\\/g, "/").split("/").pop() : qsTr("No player selected"); textFormat: Text.PlainText; color: Theme.accentText; font.pixelSize: 12; elide: Text.ElideMiddle }
                            Text { Layout.fillWidth: true; text: backend.exePath || qsTr("Detect an installation below, or enter a target path."); textFormat: Text.PlainText; color: Theme.textDim; font.pixelSize: 10; wrapMode: Text.WrapAnywhere }
                            Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                            Text { Layout.fillWidth: true; text: qsTr("Theme: %1").arg(backend.themeFound ? qsTr("Detected") : qsTr("Not loaded")); color: Theme.textDim; font.pixelSize: 10; wrapMode: Text.Wrap }
                            ActionButton { Layout.fillWidth: true; text: qsTr("Detect installations"); enabled: !backend.busy; onClicked: backend.detectInstalls() }
                        }
                    }
                    Card {
                        Layout.fillWidth: true
                        ColumnLayout {
                            width: parent.width; spacing: 10
                            Text { text: qsTr("A little peace of mind."); color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text { Layout.fillWidth: true; text: qsTr("Review your target, preserve the original, and verify every change. Resource scans remain manual."); color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap }
                            ActionButton { Layout.fillWidth: true; text: qsTr("Open patch workspace →"); onClicked: dashPage.navigate(11) }
                        }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 26; Layout.rightMargin: 26
                ColumnLayout {
                    width: parent.width; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Installations"); color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        Text { text: qsTr("%1 detected").arg(backend.installs.length); color: Theme.textDim; font.pixelSize: 10 }
                    }
                    Repeater {
                        model: backend.installs
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            implicitHeight: 43
                            radius: 7; color: Theme.cardLow; border.color: Theme.border
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 8; spacing: 10
                                Text { text: modelData.profile; textFormat: Text.PlainText; color: Theme.accentText; font.pixelSize: 10; font.weight: Font.DemiBold }
                                Text { Layout.fillWidth: true; Layout.minimumWidth: 0; text: modelData.root; textFormat: Text.PlainText; color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideMiddle }
                                ActionButton { text: qsTr("Use player"); implicitHeight: 27; enabled: !backend.busy; onClicked: backend.useInstall(index) }
                            }
                        }
                    }
                    Text { visible: backend.installs.length === 0; Layout.fillWidth: true; text: qsTr("No installations detected yet. Choose Detect installations to search your disk and registry."); color: Theme.textDim; font.pixelSize: 11; wrapMode: Text.Wrap }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.leftMargin: 26; Layout.rightMargin: 26
                ColumnLayout {
                    width: parent.width; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Target paths"); color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Item { Layout.fillWidth: true }
                        ActionButton { text: qsTr("Detect instances"); enabled: !backend.busy; implicitHeight: 28; onClicked: dashPage.instances = backend.detectInstances() }
                    }
                    PathRow { Layout.fillWidth: true; label: qsTr("HD-Player.exe"); text: backend.exePath; onLoad: backend.exePath = text }
                    PathRow { Layout.fillWidth: true; label: qsTr("FPS slider DLL"); text: backend.dllPath; onLoad: { settings.fpsAutoDetect = false; backend.dllPath = text; } }
                    Flow {
                        Layout.fillWidth: true
                        implicitHeight: childrenRect.height
                        spacing: 7
                        Repeater {
                            model: dashPage.instances
                            delegate: Text { required property var modelData; text: modelData.display || modelData.name; textFormat: Text.PlainText; color: Theme.accentText; font.pixelSize: 11 }
                        }
                    }
                }
            }
            Item { Layout.preferredHeight: 8 }
        }
    }
}
