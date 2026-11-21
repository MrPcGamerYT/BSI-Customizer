// VOID REFERENCE SETTINGS V3: installed native Settings shell + guarded extra tab.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import "BSISettingsIconsV3.js" as Icons

Item {
    id: shell
    objectName: "bsiSettingsShell"
    property var bsiHost
    property var bsiTheme
    readonly property bool bsiCompact: width < 720
    readonly property int bsiRailWidth: bsiCompact ? 58 : 182
    readonly property var nativeTabs: (bsiHost && bsiHost.pSettingsItems) ? bsiHost.pSettingsItems.pSettingElements : []
    readonly property var bsiTabs: tabEntries()
    property bool motionEnabled: Theme.motionEnabled
    function tabEntries() {
        var result = []
        for (var i = 0; i < nativeTabs.length; ++i)
            result.push({entry:nativeTabs[i],nativeIndex:i})
        return result
    }
    readonly property int selectedIndex: bsiHost ? bsiHost.pCurrentTabIndex : 0
    property alias pageContainer: bsiPageBody
    function stylePage(page) { nativeStyle.prepare(page) }

    function iconForEntry(entry) {
        return Icons.kindForTab(entry.pButtonId, entry.pQmlSource || "", entry.pButtonText)
    }
    function fitHost() {
        var host=bsiHost
        if (!host || !host.screen) return
        var area={x:host.screen.virtualX,y:host.screen.virtualY,width:host.screen.desktopAvailableWidth,height:host.screen.desktopAvailableHeight}
        if (!(area.width>0 && area.height>0)) return
        // Enlarge small Settings windows; never shrink a user-sized window or
        // change Windows scaling. Keep a desktop margin on smaller monitors.
        var w=Math.min(1160,area.width-48), h=Math.min(780,area.height-64)
        if (w>host.width) host.width=w
        if (h>host.height) host.height=h
        host.x=Math.max(area.x,Math.min(host.x,area.x+area.width-host.width))
        host.y=Math.max(area.y,Math.min(host.y,area.y+area.height-host.height))
    }
    onBsiHostChanged: {SurfaceRegistry.attach(shell, bsiHost);if(bsiHost)fitHost()}
    Connections {
        target:shell.bsiHost
        ignoreUnknownSignals:true
        function onVisibleChanged(){if(target && target.visible){fitHost();Theme.sync();nativeStyle.prepare(bsiPageBody)}}
    }
    Component.onDestruction: SurfaceRegistry.detach(shell)
    Component.onCompleted: { fitHost(); SurfaceRegistry.attach(shell, bsiHost) }
    component BsiOutlineIcon: ThemeIcon {
        property string kind: "preferences"
        property color stroke: Theme.text
        sourceSize.width: 48; sourceSize.height: 48
        fillMode: Image.PreserveAspectFit
        source: Icons.pngUrl(kind)
        tint: stroke
        opacity: Qt.colorEqual(stroke,Theme.muted) ? 0.75 : 1
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.corner
        clip: true
        color: Theme.background
        border.width: 1
        border.color: Theme.border
        Rectangle {
            anchors.fill: parent; anchors.margins: 1
            radius: Math.max(0,Theme.corner-1); color: "transparent"
            border.width: 1; border.color: Theme.surface
        }
        Item {
            id: header
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 76
            Text {
                anchors.left: parent.left; anchors.leftMargin: 18; anchors.top: parent.top; anchors.topMargin: 9
                text: "𝓥𝓞𝓘𝓓  /  NATIVE CONTROL CENTER"; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: 9; font.letterSpacing: 1.3
            }
            Text {
                objectName: "voidFullUiVersion"
                anchors.right: appearanceButton.left; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                text: "OLED STUDIO 10  /  "+Theme.name; color: Theme.muted; font.family: Theme.fontFamily; font.pixelSize: 9; font.letterSpacing: 1.2
            }
            Rectangle {
                anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 18
                height: 1; color: Theme.border
                SequentialAnimation on opacity {
                    running: shell.motionEnabled && shell.visible; loops: Animation.Infinite
                    NumberAnimation { from: 0.25; to: 0.65; duration: 2600; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.65; to: 0.25; duration: 2600; easing.type: Easing.InOutSine }
                }
            }
            MouseArea {
                anchors.fill: parent; anchors.rightMargin: 200
                onPressed: if (shell.bsiHost) shell.bsiHost.startSystemMove()
            }
            Text {
                anchors.left: parent.left; anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter; anchors.verticalCenterOffset: 8
                text: qsTranslate("QObject", "Settings"); font.family: Theme.fontFamily
                color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold
            }
            ToolButton {
                id: appearanceButton
                objectName: "openAppearance"
                anchors.right: closeButton.left;anchors.rightMargin:12;anchors.verticalCenter:closeButton.verticalCenter
                width:94;height:30;text:"Themes";font.family:Theme.fontFamily;font.pixelSize:12
                palette.buttonText:Theme.accent
                background:Rectangle {radius:8;color:appearanceButton.hovered?Theme.raised:Theme.surface;border.width:1;border.color:Theme.border}
                onClicked:appearance.open()
            }
            ToolButton {
                id: closeButton
                objectName: "bsiSettingsClose"
                anchors.right: parent.right; anchors.rightMargin: 22
                anchors.top: parent.top; anchors.topMargin: 23
                width: 30; height: 30; hoverEnabled: true
                padding: 6
                Accessible.name: qsTranslate("QObject", "Close")
                contentItem: BsiOutlineIcon { kind: "close"; stroke: closeButton.hovered ? Theme.text : Theme.muted }
                background: Rectangle {
                    radius: 7
                    color: closeButton.hovered ? Theme.raised : "transparent"
                    Behavior on color { enabled:shell.motionEnabled; ColorAnimation { duration: Theme.duration } }
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
            Rectangle {
                anchors.fill:parent;anchors.margins:-5
                radius:Math.max(0,Theme.corner-2);color:Theme.surface;border.width:1;border.color:Theme.border
            }
            Text {
                x:12;y:9;text:shell.bsiCompact?"UI":"WORKSPACE"
                color:Theme.muted;font.family:Theme.fontFamily;font.pixelSize:9;font.letterSpacing:1.3
            }
            Rectangle {
                anchors.left:parent.left;anchors.right:parent.right;anchors.bottom:parent.bottom
                anchors.margins:8;height:46;radius:Math.min(10,Theme.corner);color:Theme.raised
                Rectangle {x:10;anchors.verticalCenter:parent.verticalCenter;width:6;height:6;radius:3;color:Theme.accent}
                Text {x:24;anchors.verticalCenter:parent.verticalCenter;width:parent.width-28;elide:Text.ElideRight;text:Theme.name;visible:!shell.bsiCompact;color:Theme.text;font.family:Theme.fontFamily;font.pixelSize:11}
                MouseArea {anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:appearance.open()}
            }
            ListView {
                id: navigation
                objectName: "bsiSettingsNavigation"
                anchors.fill: parent;anchors.topMargin:32;anchors.bottomMargin:64
                model: shell.bsiTabs
                currentIndex: shell.selectedIndex
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex,ListView.Contain)
                clip: true; spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 3 }
                delegate: ItemDelegate {
                    id: tab
                    objectName: "bsiSettingsTab_" + index
                    width: navigation.width; height: 44
                    property var nativeRow: model.modelData
                    property var nativeEntry: nativeRow.entry
                    readonly property bool selected: index === shell.selectedIndex
                    scale: down ? 0.985 : 1
                    Behavior on scale { enabled: shell.motionEnabled; NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
                    Accessible.name: nativeEntry.pButtonText
                    contentItem: Item {
                        Rectangle {
                            id: tabIcon
                            anchors.left: parent.left
                            anchors.leftMargin: shell.bsiCompact ? 14 : 14
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28; height: 28;radius:8;color:tab.selected?Theme.accent_transparent:Theme.raised
                            BsiOutlineIcon {
                                anchors.centerIn: parent; width: 20; height: 20
                                kind: shell.iconForEntry(tab.nativeEntry)
                                stroke: tab.selected || tab.hovered ? Theme.accent : Theme.muted
                                scale: 1
                                Behavior on scale { enabled: shell.motionEnabled; NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
                            }
                        }
                        Text {
                            id: tabLabel
                            anchors.left: tabIcon.right; anchors.leftMargin: 9
                            anchors.right: parent.right; anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !shell.bsiCompact
                            text: tab.nativeEntry.pButtonText
                            font.family: Theme.fontFamily; font.pixelSize: 13; font.weight: tab.selected ? Font.Medium : Font.Normal
                            color: tab.selected || tab.hovered ? Theme.text : Theme.muted
                            elide: Text.ElideRight
                        }
                    }
                    background: Rectangle {
                        radius: 10
                        border.width: tab.selected || tab.hovered || tab.activeFocus ? 1 : 0
                        border.color: tab.selected || tab.activeFocus ? Theme.accent : tab.hovered ? Theme.border : "transparent"
                        gradient: Gradient {
                            GradientStop { position: 0; color: tab.selected ? Theme.accent_transparent : tab.hovered ? Theme.raised : Theme.surface }
                            GradientStop { position: 1; color: tab.selected ? Theme.raised : Theme.surface }
                        }
                        Behavior on border.color { enabled:shell.motionEnabled; ColorAnimation { duration: Theme.duration } }
                        Rectangle {
                            anchors.left: parent.left; anchors.leftMargin: 5
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1; height: 20; radius: 0.5
                            color: Theme.accent; visible: tab.selected
                        }
                        Rectangle {
                            width: 5; height: 5; radius: 2.5
                            anchors.right: parent.right; anchors.rightMargin: 6
                            anchors.top: parent.top; anchors.topMargin: 6
                            color: Theme.text; visible: !!tab.nativeEntry.pShowRedDot
                        }
                    }
                    // Native code alone decides whether an unsaved page can be left.
                    onClicked: if (shell.bsiHost) {
                        shell.bsiHost.fVoidSelectNative(nativeRow.nativeIndex)
                    }
                    ToolTip.visible: hovered && (shell.bsiCompact || tabLabel.truncated)
                    ToolTip.text: nativeEntry.pButtonText; ToolTip.delay: 400
                }
            }
        }
        Rectangle {
            anchors.left: rail.right; anchors.leftMargin: 14
            anchors.top: rail.top; anchors.topMargin: 5
            anchors.bottom: parent.bottom; anchors.bottomMargin: 22
            width: 1; color: Theme.surface
        }
        Rectangle {
            anchors.fill:bsiPageBody;anchors.margins:-6
            color:Theme.background;radius:Math.max(0,Theme.corner-2);border.width:1;border.color:Theme.border
        }
        // Reparented native StackLayout lives here. No fake settings controls,
        // duplicate Save button, duplicate page title or substitute backend.
        Item {
            id: bsiPageBody
            objectName: "bsiSettingsPageContainer"
            anchors.left: rail.right; anchors.leftMargin: 24
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.top: header.bottom; anchors.topMargin: 12
            anchors.bottom: parent.bottom; anchors.bottomMargin: 22
        }
        AppearancePanel {id:appearance;parent:shell;x:(shell.width-width)/2;y:(shell.height-height)/2}
        NativeControlStyle { id: nativeStyle;stylePanels:true; objectName: "voidNativeControlStyle"; pages: bsiPageBody; visible: false }
    }
}
