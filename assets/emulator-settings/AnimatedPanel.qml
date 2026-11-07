// BSI Settings — animated SVG edition. Native pages, guards and Save stay embedded.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "Icons.js" as Icons

Item {
    id: shell
    objectName: "bsiSettingsShell"
    property var bsiHost
    property var bsiTheme
    // Set false for reduced motion. No infinite/polling animations.
    property bool bsiAnimations: true
    readonly property bool bsiCompact: width < 720
    readonly property int bsiRailWidth: bsiCompact ? 54 : 164
    readonly property var bsiTabs: (bsiHost && bsiHost.pSettingsItems) ? bsiHost.pSettingsItems.pSettingElements : []
    readonly property int selectedIndex: bsiHost ? bsiHost.pCurrentTabIndex : 0
    property alias pageContainer: bsiPageBody

    function iconKind(id) {
        var key = String(id || "").toLowerCase().replace(/[^a-z0-9]/g, "")
        if (/performance|engine/.test(key)) return "performance"
        if (/display|resolution/.test(key)) return "display"
        if (/graphic/.test(key)) return "graphics"
        if (/gamesetting/.test(key)) return "gameSettings"
        if (/gamepad|controller/.test(key)) return "controller"
        if (/phone|device/.test(key)) return "smartphone"
        if (/theme/.test(key)) return "palette"
        if (/advance/.test(key)) return "advanced"
        if (/shortcut|keyboard/.test(key)) return "keyboard"
        if (/about|info/.test(key)) return "info"
        if (/audio|sound/.test(key)) return "headphones"
        if (/storage|userdata/.test(key)) return "harddrive"
        return "preferences"
    }
    // Passive SVG visual: the native ItemDelegate/ToolButton owns every click.
    component BsiOutlineIcon: Item {
        id: icon
        property string kind: "preferences"
        property color stroke: "#f4f4f7"
        property bool hot: false
        property bool downState: false
        property bool active: false
        property real hoverTwist: 0
        readonly property bool ready: glyph.status === Image.Ready
        implicitWidth: 24
        implicitHeight: 24
        scale: downState ? 0.88 : hot ? 1.13 : active ? 1.04 : 1
        rotation: hot ? hoverTwist : 0
        opacity: downState ? 0.8 : hot || active ? 1 : 0.76
        Behavior on scale { enabled: shell.bsiAnimations; NumberAnimation { duration: 170; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
        Behavior on rotation { enabled: shell.bsiAnimations; NumberAnimation { duration: 190; easing.type: Easing.OutCubic } }
        Behavior on opacity { enabled: shell.bsiAnimations; NumberAnimation { duration: 140 } }
        Image {
            id: glyph
            objectName: "bsiSettingsSvgGlyph"
            anchors.fill: parent
            source: Icons.url(icon.kind, icon.stroke.toString())
            sourceSize.width: Math.max(1, Math.ceil(width * 2))
            sourceSize.height: Math.max(1, Math.ceil(height * 2))
            fillMode: Image.PreserveAspectFit
            smooth: true
            // Only the icon moves; native tab hitboxes and pages stay put.
            transform: Translate {
                y: icon.hot ? -1 : 0
                Behavior on y { enabled: shell.bsiAnimations; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            }
            onStatusChanged: if (status === Image.Error) console.warn("Settings SVG could not load: " + icon.kind)
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 15
        clip: true
        gradient: Gradient {
            GradientStop { position: 0; color: "#0c0c0d" }
            GradientStop { position: 0.24; color: "#030304" }
            GradientStop { position: 0.74; color: "#030304" }
            GradientStop { position: 1; color: "#111114" }
        }
        border.width: 1
        border.color: "#56565c"
        Rectangle {
            anchors.fill: parent; anchors.margins: 1
            radius: 14; color: "transparent"
            border.width: 1; border.color: "#17171b"
        }
        Item {
            id: header
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 88
            MouseArea {
                anchors.fill: parent; anchors.rightMargin: 64
                onPressed: if (shell.bsiHost) shell.bsiHost.startSystemMove()
            }
            Text {
                anchors.left: parent.left; anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter; anchors.verticalCenterOffset: 8
                text: qsTranslate("QObject", "Settings")
                color: "#f3f3f5"; font.pixelSize: 20; font.weight: Font.DemiBold
            }
            ToolButton {
                id: closeButton
                objectName: "bsiSettingsClose"
                anchors.right: parent.right; anchors.rightMargin: 22
                anchors.top: parent.top; anchors.topMargin: 23
                width: 30; height: 30; hoverEnabled: true
                padding: 6
                Accessible.name: qsTranslate("QObject", "Close")
                contentItem: BsiOutlineIcon {
                    kind: "close"; hot: closeButton.hovered; downState: closeButton.down
                    hoverTwist: 90
                }
                background: Rectangle {
                    radius: 7
                    color: closeButton.hovered ? "#29292e" : "transparent"
                    Behavior on color { enabled: shell.bsiAnimations; ColorAnimation { duration: 110 } }
                }
                onClicked: if (shell.bsiHost) shell.bsiHost.close()
                ToolTip.visible: hovered; ToolTip.text: qsTranslate("QObject", "Close")
            }
        }
        Item {
            id: rail
            anchors.left: parent.left; anchors.leftMargin: 18
            anchors.top: header.bottom; anchors.topMargin: 8
            anchors.bottom: parent.bottom; anchors.bottomMargin: 16
            width: shell.bsiRailWidth
            ListView {
                id: navigation
                objectName: "bsiSettingsNavigation"
                anchors.fill: parent
                model: shell.bsiTabs
                currentIndex: shell.selectedIndex
                clip: true; spacing: 5
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 3 }
                delegate: ItemDelegate {
                    id: tab
                    objectName: "bsiSettingsTab_" + index
                    width: navigation.width; height: 32
                    property var nativeEntry: model.modelData
                    readonly property bool selected: index === shell.selectedIndex
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
                    Accessible.name: nativeEntry.pButtonText
                    contentItem: Item {
                        transform: Translate {
                            x: tab.hovered ? 2 : 0
                            Behavior on x { enabled: shell.bsiAnimations; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                        }
                        BsiOutlineIcon {
                            id: tabIcon
                            anchors.left: parent.left
                            anchors.leftMargin: shell.bsiCompact ? 20 : 17
                            anchors.verticalCenter: parent.verticalCenter
                            width: 18; height: 18
                            kind: shell.iconKind(tab.nativeEntry.pButtonId)
                            hot: tab.hovered; downState: tab.down; active: tab.selected
                            hoverTwist: kind === "advanced" ? 18 : kind === "palette" ? -10 : 0
                        }
                        Text {
                            id: tabLabel
                            anchors.left: tabIcon.right; anchors.leftMargin: 12
                            anchors.right: parent.right; anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !shell.bsiCompact
                            text: tab.nativeEntry.pButtonText
                            font.pixelSize: 12; font.weight: tab.selected ? Font.Medium : Font.Normal
                            Behavior on color { enabled: shell.bsiAnimations; ColorAnimation { duration: 140 } }
                            color: tab.selected || tab.hovered ? "#f7f7fa" : "#c6c6cd"
                            elide: Text.ElideRight
                        }
                    }
                    background: Rectangle {
                        scale: tab.down ? 0.97 : 1
                        Behavior on scale { enabled: shell.bsiAnimations; NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
                        radius: 11
                        border.width: 1
                        border.color: tab.selected || tab.activeFocus ? "#e4e4eb" : tab.hovered ? "#65656d" : "transparent"
                        gradient: Gradient {
                            GradientStop { position: 0; color: tab.selected ? "#29292e" : tab.hovered ? "#19191e" : "#00000000" }
                            GradientStop { position: 1; color: tab.selected ? "#131316" : tab.hovered ? "#101014" : "#00000000" }
                        }
                        Behavior on border.color { enabled: shell.bsiAnimations; ColorAnimation { duration: 150 } }
                        Rectangle {
                            anchors.fill: parent; anchors.margins: 2
                            radius: 9; color: "transparent"
                            border.width: 1; border.color: "#35353e"
                            opacity: tab.selected ? 1 : 0
                            visible: opacity > 0
                            Behavior on opacity { enabled: shell.bsiAnimations; NumberAnimation { duration: 170 } }
                        }
                        Rectangle {
                            anchors.left: parent.left; anchors.leftMargin: 5
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2; height: tab.selected ? 18 : 6; radius: 1
                            color: "#f3f3f7"; opacity: tab.selected ? 1 : 0
                            visible: opacity > 0
                            Behavior on height { enabled: shell.bsiAnimations; NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                            Behavior on opacity { enabled: shell.bsiAnimations; NumberAnimation { duration: 150 } }
                        }
                        // One short selection flash, only after native selection changes.
                        Rectangle {
                            id: selectionFlash
                            objectName: "bsiSettingsSelectionFlash"
                            anchors.fill: parent; anchors.margins: 1; radius: 10
                            color: "#ffffff"; opacity: 0
                            visible: shell.bsiAnimations && opacity > 0
                        }
                        SequentialAnimation {
                            running: shell.bsiAnimations && tab.selected
                            NumberAnimation { target: selectionFlash; property: "opacity"; from: 0; to: 0.10; duration: 100 }
                            NumberAnimation { target: selectionFlash; property: "opacity"; to: 0; duration: 260; easing.type: Easing.OutCubic }
                        }
                        Rectangle {
                            width: 5; height: 5; radius: 2.5
                            anchors.right: parent.right; anchors.rightMargin: 6
                            anchors.top: parent.top; anchors.topMargin: 6
                            color: "#e6a2aa"; visible: !!tab.nativeEntry.pShowRedDot
                        }
                    }
                    // Native code alone decides whether an unsaved page can be left.
                    onClicked: if (shell.bsiHost) shell.bsiHost.fChangeSelectedSetting(index)
                    ToolTip.visible: hovered && (shell.bsiCompact || tabLabel.truncated)
                    ToolTip.text: nativeEntry.pButtonText; ToolTip.delay: 400
                }
            }
        }
        Rectangle {
            anchors.left: rail.right; anchors.leftMargin: 14
            anchors.top: rail.top; anchors.topMargin: 5
            anchors.bottom: parent.bottom; anchors.bottomMargin: 22
            width: 1; color: "#141417"
        }
        // Reparented native StackLayout lives here. No fake settings controls,
        // duplicate Save button, duplicate page title or substitute backend.
        Item {
            id: bsiPageBody
            objectName: "bsiSettingsPageContainer"
            anchors.left: rail.right; anchors.leftMargin: 40
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.top: header.bottom; anchors.topMargin: 8
            anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        }
    }
}
