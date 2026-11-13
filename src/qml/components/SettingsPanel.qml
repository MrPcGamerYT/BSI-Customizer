import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../Icons.js" as Icons

Popup {
    id: panel

    signal notify(string message)

    property int currentTab: 0
    property real dragOffsetX: 0
    property real dragOffsetY: 0
    property real dragStartX: 0
    property real dragStartY: 0
    property real dragPointerX: 0
    property real dragPointerY: 0
    readonly property var tabs: [
        { label: qsTr("Appearance"), icon: Icons.appearance },
        { label: qsTr("Updates"), icon: Icons.refresh },
        { label: qsTr("About"), icon: Icons.about }
    ]

    parent: Overlay.overlay
    x: parent ? Math.max(0, Math.min(Math.max(0, parent.width - width),
                                     (parent.width - width) / 2 + dragOffsetX)) : 0
    y: parent ? Math.max(0, Math.min(Math.max(0, parent.height - height),
                                     (parent.height - height) / 2 + dragOffsetY)) : 0
    width: Math.min(1100, Math.max(660, parent ? parent.width * 0.70 : 830))
    height: Math.min(700, Math.max(480, parent ? parent.height * 0.74 : 560))
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150 }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 150; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100 }
    }

    Overlay.modal: Rectangle { color: Qt.rgba(1 / 255, 5 / 255, 12 / 255, 0.72) }

    background: Rectangle {
        radius: 20
        color: Theme.window
        border.width: 1
        border.color: Theme.borderStrong
        clip: true

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 2
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.alpha(Theme.accent, 0.1) }
                GradientStop { position: 0.5; color: Theme.accent }
                GradientStop { position: 1; color: Qt.alpha(Theme.green, 0.1) }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // Compact title row; settings content stays inside a BlueStacks-sized
        // floating panel instead of taking over the whole application window.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 14
            Layout.topMargin: 16
            Layout.bottomMargin: 14
            spacing: 12

            Item {
                id: dragHandle
                objectName: "settingsPanelDragHandle"
                Layout.fillWidth: true
                Layout.preferredHeight: 40

                RowLayout {
                    anchors.fill: parent
                    spacing: 12
                    Rectangle {
                        Layout.preferredWidth: 40
                        Layout.preferredHeight: 40
                        radius: 12
                        color: Theme.accentSoft
                        border.width: 1
                        border.color: Theme.border
                        Image {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
                            source: Icons.icon(Icons.settings, Theme.accentText)
                            smooth: true
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("Settings")
                            color: Theme.text
                            font.pixelSize: 19
                            font.weight: Font.Bold
                        }
                        Text {
                            text: qsTr("Personalize BSI Customizer · drag here to move")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeAllCursor
                    onPressed: {
                        panel.dragStartX = panel.x;
                        panel.dragStartY = panel.y;
                        const point = mapToItem(panel.parent, mouse.x, mouse.y);
                        panel.dragPointerX = point.x;
                        panel.dragPointerY = point.y;
                    }
                    onPositionChanged: {
                        if (!pressed || !panel.parent)
                            return;
                        const point = mapToItem(panel.parent, mouse.x, mouse.y);
                        panel.dragOffsetX = panel.dragStartX + point.x - panel.dragPointerX
                                - (panel.parent.width - panel.width) / 2;
                        panel.dragOffsetY = panel.dragStartY + point.y - panel.dragPointerY
                                - (panel.parent.height - panel.height) / 2;
                    }
                }
            }

            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: 10
                color: closeArea.containsMouse ? Theme.raised : "transparent"
                border.width: 1
                border.color: closeArea.containsMouse ? Theme.border : "transparent"
                Text {
                    anchors.centerIn: parent
                    text: "×"
                    color: Theme.soft
                    font.pixelSize: 23
                    font.weight: Font.Light
                }
                MouseArea {
                    id: closeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.close()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.hairline
        }

        // Each settings destination is a proper top tab with its own icon.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.topMargin: 13
            Layout.bottomMargin: 13
            spacing: 9

            Repeater {
                model: panel.tabs
                delegate: Rectangle {
                    id: tabButton
                    required property var modelData
                    required property int index
                    Layout.preferredWidth: Math.max(132, tabLabel.implicitWidth + 58)
                    Layout.preferredHeight: 42
                    radius: 11
                    color: panel.currentTab === index ? Theme.accentSoft
                           : tabMouse.containsMouse ? Theme.raised : "transparent"
                    border.width: panel.currentTab === index ? 1 : 1
                    border.color: panel.currentTab === index ? Theme.borderStrong
                                                             : tabMouse.containsMouse ? Theme.border : "transparent"
                    Behavior on color { ColorAnimation { duration: 130 } }

                    Row {
                        anchors.centerIn: parent
                        spacing: 9
                        Image {
                            width: 17
                            height: 17
                            anchors.verticalCenter: parent.verticalCenter
                            source: Icons.icon(tabButton.modelData.icon,
                                               panel.currentTab === tabButton.index ? Theme.accentText : Theme.textDim)
                            smooth: true
                        }
                        Text {
                            id: tabLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: tabButton.modelData.label
                            color: panel.currentTab === tabButton.index ? Theme.accentText : Theme.soft
                            font.pixelSize: 12
                            font.weight: panel.currentTab === tabButton.index ? Font.DemiBold : Font.Medium
                        }
                    }
                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: panel.currentTab = tabButton.index
                    }
                }
            }
            Item { Layout.fillWidth: true }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.hairline
        }

        StackLayout {
            id: settingPages
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: panel.currentTab
            clip: true

            AppearancePage {
                onNotify: (message) => panel.notify(message)
            }
            UpdatesPage {
                onNotify: (message) => panel.notify(message)
            }
            AboutPage {
                onNotify: (message) => panel.notify(message)
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.hairline
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            Layout.topMargin: 9
            Layout.bottomMargin: 10
            Text {
                text: qsTr("BSI CUSTOMIZER")
                color: Theme.textDim
                font.pixelSize: 9
                font.weight: Font.Bold
                font.letterSpacing: 1.3
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("Appearance saves instantly · About saves on request")
                color: Theme.textDim
                font.pixelSize: 10
            }
        }
    }
}
