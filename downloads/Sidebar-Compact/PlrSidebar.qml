/*
 * Copyright (C) 2020-2021 BlueStack Systems, Inc.
 * All Rights Reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF BLUESTACK SYSTEMS, INC.
 * The copyright notice above does not evidence any actual or intended
 * publication of such source code.
 */
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import Qt.labs.platform 1.1
import QtQuick.Shapes 1.12
import RecordModeEnum 1.0
import UiToolTipControl 1.0
import UiShortcutToolTip 1.0
import DialogButtonModel 1.0
import DialogButtonModelList 1.0
import UiDialogWindow 1.0
import UiFolderDialog 1.0
import UiQuickWindow 1.0
Rectangle {
    // BSI SIDEBAR INLINE V1 COMPACT — complete visual skin, no external files.
component BsiSidebarInlineVisual: Item {
readonly property var iconShapes: ({"back":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"m12 5-7 7 7 7M5 12h15\"/></svg>","fullscreen":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M9 3H3v6M15 3h6v6M3 15v6h6M21 15v6h-6M8 8l-5-5M16 16l5 5\"/></svg>","home":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"m3 10 9-7 9 7M5 9v12h14V9M10 21v-7h4v7\"/></svg>","install":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M5 9v12h14V9M3 9h18M12 2v12M8 10l4 4 4-4\"/></svg>","keymap":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><rect x=\"2\" y=\"5\" width=\"20\" height=\"14\" rx=\"3\"/><path d=\"M6 9h1M11 9h1M16 9h2M6 13h1M11 13h1M16 13h2M8 16h8\"/></svg>","mute":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M3 9h4l5-5v16l-5-5H3zM17 9l5 6M22 9l-5 6\"/></svg>","recent":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><rect x=\"7\" y=\"3\" width=\"14\" height=\"15\" rx=\"3\"/><path d=\"M3 7v11a3 3 0 0 0 3 3h11M11 7h6\"/></svg>","settings":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M4 6h16M4 12h16M4 18h16\"/><circle cx=\"9\" cy=\"6\" r=\"2\" fill=\"ICON_COLOR\"/><circle cx=\"16\" cy=\"12\" r=\"2\" fill=\"ICON_COLOR\"/><circle cx=\"8\" cy=\"18\" r=\"2\" fill=\"ICON_COLOR\"/></svg>","volume":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M3 9h4l5-5v16l-5-5H3zM16 8a6 6 0 0 1 0 8M19 5a10 10 0 0 1 0 14\"/></svg>","memory":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M8 3v3m4-3v3m4-3v3M8 18v3m4-3v3m4-3v3M3 8h3m-3 4h3m-3 4h3m12-8h3m-3 4h3m-3 4h3\"/><rect x=\"6\" y=\"6\" width=\"12\" height=\"12\" rx=\"2\"/><path d=\"M10 10h4v4h-4z\"/></svg>","more":"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"ICON_COLOR\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><circle cx=\"12\" cy=\"5\" r=\"1\"/><circle cx=\"12\" cy=\"12\" r=\"1\"/><circle cx=\"12\" cy=\"19\" r=\"1\"/></svg>"})

id: skin
property string mode: "button"
property string elementId: ""
property string assetName: ""
property bool hovered: false
property bool pressed: false
property bool available: true
property bool active: false
property bool animating: false
    // The caller removes the native glyph ONLY after its replacement has loaded.
readonly property bool known: glyph.status === Image.Ready && mode === "button"
readonly property string iconKey: resolveIcon()
readonly property color ink: !available ? "#665774" : pressed ? "#ffffff" :
hovered || active ? "#f0e5ff" : "#c3aedc"
readonly property string svg: iconShapes[iconKey] || ""

function resolveIcon() {
var id = elementId.toLowerCase().replace(/[^a-z0-9]/g, "");
if (id === "volume") return assetName.toLowerCase().indexOf("mute") >= 0 ? "mute" : "volume";
var names = {fullscreen:"fullscreen",gamecontrol:"keymap",memorytrim:"memory",installapk:"install",more:"more",settings:"settings",back:"back",home:"home",recent:"recent"};
return names[id] || "";
}

Rectangle {
anchors.fill: parent
visible: skin.mode === "rail"
gradient: Gradient {
GradientStop { position: 0; color: "#291737" }
GradientStop { position: 0.42; color: "#20122d" }
GradientStop { position: 1; color: "#170f21" }
}
Rectangle { width: 1; height: parent.height; color: "#624078"; opacity: 0.55 }
Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#110a19" }
}

Item {
id: face
anchors.fill: parent
visible: skin.mode === "button"
scale: skin.pressed ? 0.93 : 1
Behavior on scale { NumberAnimation { duration: 85 } }
Rectangle {
anchors.fill: parent
anchors.margins: 3
radius: 7
            // Unknown icons must remain visible underneath this passive overlay.
color: skin.pressed ? "#75459a" : skin.active ? "#573374" : "#402653"
opacity: !skin.available ? 0 : !skin.known ? (skin.pressed ? 0.14 : skin.hovered ? 0.08 : 0) :
skin.pressed ? 0.96 : skin.active ? 0.85 : skin.hovered ? 0.8 : 0
border.width: 1
border.color: skin.pressed ? "#b78bd9" : "#795494"
Behavior on opacity { NumberAnimation { duration: 120 } }
}
Rectangle {
width: 2; height: 12; radius: 1
anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
color: "#c18aee"
opacity: skin.active && skin.available ? 1 : 0
Behavior on opacity { NumberAnimation { duration: 120 } }
}
Image {
id: glyph
objectName: "bsiOutlineGlyph"
anchors.centerIn: parent
width: 19; height: 19
sourceSize.width: 38; sourceSize.height: 38
fillMode: Image.PreserveAspectFit
smooth: true
source: skin.svg ? "data:image/svg+xml;charset=utf-8," +
encodeURIComponent(skin.svg.replace(/ICON_COLOR/g, skin.ink.toString())) : ""
RotationAnimation on rotation {
from: 0; to: 360; duration: 1100; loops: Animation.Infinite
running: skin.animating && skin.available && skin.visible && skin.mode === "button"
onRunningChanged: if (!running) glyph.rotation = 0
}
}
}

}

    // Inline visual rail; native model and callbacks below are retained.
BsiSidebarInlineVisual { id: bsiSidebarSkinRail;
objectName: "bsiSidebarInlineRail"
anchors.fill: parent; mode: "rail" }

id: iSidebar
objectName: "sidebar"
property int pSidebarElementSize: 32
property int pVisibleSidebarElementsCount: 0
property KmmEditorWindow pKmmEditorWindow: null
property bool pGameControlsEnabled: true
property bool pIsGameControlsOverlayEnabled: true
property bool pGameControlsSensitivityEnabled: false
property bool pGameControlsPopupLoaded: false
property bool pGamepadControlsPresent: false
property bool pGameControlsGamepadSelected: true
property bool pIsMacroEnabled: true
property bool pMouseCursorLocked: false
property bool pIsEcoModeSoundEnabled: false
property bool pRunTrimAnimation: false
property UiMessageDialog pMediaPrompt
property UiMessageDialog pRestartGamePrompt
property string pScreenshotFilename
property string pScreenShotNoticeText
property string pGameOrientationText
property bool pIsSyncEnable: true
property bool pIsSyncColorAsset: false
property bool pIsMuted: false
property int pCurrentVolumeLevel: 7
property int pMaxVolumeLevel: 15
property bool pIsAirplaneModeOn: false
property Timer pVolumePopupTimer: null
property bool pMediaLoaderRunning: false
component PlrSidebarElement: QtObject {
property string pElementId:""
property string pAsset:""
property string pTooltipText:""
property var pConfShortcutProperty: [{}]
property bool pIsFeatureEnable: true
property bool pIsEnable: true
property bool pShowViaRepeaterOnly: true
property bool pShowOnKebabMenu: true
property bool pRunImageAnimation: false
property int pAcceptedMouseButtons: Qt.LeftButton
property string pLoaderAsset: ""
property bool pLoaderRunning: false
property string pExtn: ".svg"
property bool pVisible: true
property bool pShade: false
signal clicked(var mouseButton, var mouseModifier)
signal mouseReleased(var mouseButton, var mouseModifier)
signal positionChanged()
}
PlrSidebarProperties{id:iSidebarProperties}
readonly property string pFullscreen: "fullscreen"
readonly property string pVolume: "volume"
readonly property string pLockCursor: "lockCursor"
readonly property string pGameControl: "gameControl"
readonly property string pSync: "sync"
readonly property string pMacro: "macro"
readonly property string pMemoryTrim: "memoryTrim"
readonly property string pInstallApk: "installApk"
readonly property string pScreenshot: "screenShot"
readonly property string pScreenRecord: "screenRecord"
readonly property string pMediaFolder: "mediaFolder"
readonly property string pRotate: "rotate"
readonly property string pSetLocation: "setLocation"
readonly property string pShake: "shake"
readonly property string pMim: "mim"
readonly property string pEcoMode: "ecoMode"
readonly property string pSidePanel: "sidePanel"
readonly property string pImapAbort: "imapAbort"
readonly property string pSettings: "settings"
readonly property string pBack: "back"
readonly property string pHome: "home"
readonly property string pRecent: "recent"
readonly property string pAirplaneMode: "airplaneMode"
readonly property string pChat: "chat"
readonly property string pLiveStream: "liveStream"
signal sidebarSettingClicked
property UiWindowedPopup pKebabMenuWin: null
property UiWindowedPopup pVolumeWin: null
property UiWindowedPopup pEcoModeWin: null
property UiWindowedPopup pGameControlsWin: null
property UiWindowedPopup pRotateControlsWin: null
property UiQuickWindow pFullScreenBlurbWin: null
property UiQuickWindow pGameControlBlurbWin: null
property UiWindowedPopup pRecordingSavedWin: null
property UiWindowedPopup pScreenRecordWin: null
property UiWindowedPopup pRecordingCompleteWin: null
property UiQuickWindow pImapAbortWin: null
property list<QtObject> pAllSidebarItems: [
PlrSidebarElement {
pElementId: pFullscreen
pAsset: "SidebarFullScreen"
pTooltipText: qsTranslate("QObject", "Toggle fullscreen")
pConfShortcutProperty: [{ value: "toggle_fullscreen"}]
pIsFeatureEnable: iSidebarProperties.pIsFullScreenEnabled
pIsEnable: iAppwin.pIsBootComplete
onClicked: {
plrBackend.logUIAction("Sidebar", "ToggleFullscreen")
plrBackend.checkSplitAdAndSetFullScreen(true)
}
},
PlrSidebarElement {
pElementId: pVolume
pAsset: pIsMuted ? "SidebarMute": "SidebarSound"
pIsFeatureEnable: iSidebarProperties.pIsVolumeEnabled
pConfShortcutProperty: [
{ text: qsTranslate("QObject", "Increase volume"), value: "increase_volume" },
{ text: qsTranslate("QObject", "Decrease volume"), value: "decrease_volume" }
]
pIsEnable: iAppwin.pIsBootComplete || iAppwin.pShowAndroidAd
onClicked: {
plrBackend.logUIAction("Sidebar", "Volume")
fOpenVolumePopup(false)
}
},
PlrSidebarElement {
pElementId: pLockCursor
pAsset: pMouseCursorLocked ? "SidebarLockCursorActive" : "SidebarLockCursor"
pIsFeatureEnable: false
pTooltipText: qsTranslate("QObject", "Lock mouse cursor")
pConfShortcutProperty: [{ value: "toggle_mouse_cursor_lock"}]
pIsEnable: iAppwin.pIsBootComplete
onClicked: {
plrBackend.logUIAction("Sidebar", "LockMouseCursor")
plrBackend.onLockMouseCursorClicked()
}
},
PlrSidebarElement {
pElementId: pGameControl
pAsset: "SidebarControls"
pExtn: uiBackend.oem === "nxt" ? ".png" : ".svg"
pConfShortcutProperty: [{ value: uiBackend.oem === "msi5" ? "open_controls_editor" : ""}]
pIsFeatureEnable: iSidebarProperties.pIsGameControlEnabled
pTooltipText: qsTranslate("QObject", "Game controls")
pIsEnable: iAppwin.pIsBootComplete
pAcceptedMouseButtons: Qt.LeftButton | Qt.RightButton
onMouseReleased: function(mouseButton, mouseModifier) {
plrBackend.logUIAction("Sidebar", "GameControls")
if(mouseButton === Qt.LeftButton)
fOpenGameControlsPopup()
else {
if (mouseButton === Qt.RightButton) {
if((mouseModifier & Qt.ControlModifier) && (mouseModifier & Qt.AltModifier)) {
fCheckSplitModeAndOpenEditorWindow(true)
}
else
fCheckSplitModeAndOpenEditorWindow(false)
}
}
}
},
PlrSidebarElement {
pElementId: pImapAbort
pAsset: pIsEcoModeEnabled ? "SidebarImapAbortActive" : "SidebarImapAbort"
pTooltipText: qsTranslate("QObject", qsTranslate("QObject", "Gamepad") + "(R3)")
pIsEnable: iAppwin.pIsBootComplete
pVisible: iAppwin.pIsImapAbort
pShade: true
pIsFeatureEnable:false /* BSI CUSTOMIZER: sidebar clean:aVNpZGViYXJQcm9wZXJ0aWVzLnBJc0ltYXBBYm9ydEVuYWJsZWQ= */
onClicked: {
plrBackend.plrImapAbort()
}
},
PlrSidebarElement {
pElementId: pMacro
pIsEnable: pIsMacroEnabled
pAsset: "SidebarMacro"
pTooltipText: qsTranslate("QObject", "Open macro manager")
pConfShortcutProperty: [{ value: "open_macro_recorder"}]
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "OpenMacroManager")
fOpenMacroManagerWindow()
}
},
PlrSidebarElement {
pElementId: pSync
pAsset: pIsSyncColorAsset ? "SidebarSyncActive": "SidebarSync"
pTooltipText: qsTranslate("QObject", "Sync operations")
pConfShortcutProperty: [{ value: "open_sync_operations"}]
pIsEnable: iAppwin.pIsBootComplete && pIsSyncEnable
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "OpenSyncOperationsWindow")
fOpenSyncOperationsWindow()
}
},
PlrSidebarElement {
pElementId: pMemoryTrim
pAsset: "SidebarMemoryTrim"
pTooltipText: qsTranslate("QObject", "Trim memory")
pConfShortcutProperty: [{ value: "trim_memory"}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: iSidebarProperties.pIsTrimMemoryEnabled
pRunImageAnimation: pRunTrimAnimation
onClicked: {
plrBackend.logUIAction("Sidebar", "TrimMemory")
fTrimMemoryClick()
}
},
PlrSidebarElement {
pElementId: pInstallApk
pAsset: "SidebarInstallApk"
pTooltipText: qsTranslate("QObject", "Install APK")
pConfShortcutProperty: [{ value: "install_apk"}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: iSidebarProperties.pIsInstallApkEnabled
onClicked: {
plrBackend.logUIAction("Sidebar", "InstallApk")
fHandleInstallApkBtnClicked()
}
},
PlrSidebarElement {
pElementId: pScreenshot
pAsset: "SidebarScreenshot"
pTooltipText: qsTranslate("QObject", "Take screenshot")
pConfShortcutProperty: [{ value: "take_screenshot"}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "TakeScreenshot")
fHandleScreenshotBtnClicked()
}
},
PlrSidebarElement {
pElementId: pScreenRecord
pAsset: nowggBackend.pIsScreenRecorderOn ? "SidebarScreenRecordActive" : "SidebarScreenRecord"
pTooltipText: qsTranslate("QObject", "Screen Recording")
pConfShortcutProperty:[{ value: uiBackend.oem === "msi5" ? "record_screen" : ""}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "RecordScreen")
fOpenScreenRecordPopup()
}
},
PlrSidebarElement {
pElementId: pLiveStream
pAsset: lsgBackend.pSidebarScreenStreamActive ? "SidebarStreamActive" : "SidebarStream"
pTooltipText: qsTranslate("QObject", "Play together")
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: false /* BSI CUSTOMIZER: sidebar clean:aVNpZGViYXJQcm9wZXJ0aWVzLnBJc0xpdmVTdHJlYW1FbmFibGVkICYmIHVpQmFja2VuZC5jb25mR2V0Qm9vbCgiYnN0LmZlYXR1cmUubGl2ZV9zdHJlYW0iKQ== */
onClicked: {
plrBackend.logUIAction("Sidebar", "LiveStream")
fHandleScreenStreamingClicked(true)
}
},
PlrSidebarElement {
pElementId: pMediaFolder
pAsset: "SidebarMediaFolder"
pTooltipText: qsTranslate("QObject", "Media Gallery")
pConfShortcutProperty: [{ value: "open_media_folder"}]
pIsFeatureEnable: false
pLoaderAsset: "LoaderYellow"
pLoaderRunning: pMediaLoaderRunning
pIsEnable: iAppwin.pIsBootComplete
onClicked: {
plrBackend.logUIAction("Sidebar", "OpenMediaFolder")
fOpenMediaGalleryWindow()
if (pRecordingSavedWin != null)
pRecordingSavedWin.close()
}
onPositionChanged: {
if (pRecordingSavedWin != null)
pRecordingSavedWin.close()
}
},
PlrSidebarElement {
pElementId: pAirplaneMode
pAsset: pIsAirplaneModeOn ? "AirplaneModeActive" : "AirplaneModeOff"
pTooltipText: qsTranslate("QObject", "Airplane Mode")
pConfShortcutProperty: [{ value: "airplane_mode"}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "AirplaneMode")
pIsAirplaneModeOn = !pIsAirplaneModeOn
plrBackend.setAirplaneModeSetting(pIsAirplaneModeOn)
}
},
PlrSidebarElement {
pElementId: pRotate
pAsset: "SidebarRotate"
pTooltipText: qsTranslate("QObject", "Rotate")
pConfShortcutProperty: [{ value: "rotate"}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "Rotate")
fOpenRotatePopUp()
}
},
PlrSidebarElement {
pElementId: pShake
pAsset: "SidebarShake"
pTooltipText: qsTranslate("QObject", "Shake")
pConfShortcutProperty: [{ value: "shake"}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "Shake")
plrBackend.shake()
}
},
PlrSidebarElement {
pElementId: pSetLocation
pAsset: "SidebarLocation"
pTooltipText: qsTranslate("QObject", "Set location")
pConfShortcutProperty: [{ value: "open_location_provider"}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "SetLocation")
plrBackend.openLocationProvider()
}
},
PlrSidebarElement {
pElementId: pMim
pAsset: "SidebarInstanceManager"
pTooltipText: qsTranslate("QObject", "Multi-instance manager")
pConfShortcutProperty: [{ value: "open_mim"}]
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "MultiInstanceManager")
plrBackend.launchMultiInstanceManager()
}
},
PlrSidebarElement {
pElementId: pEcoMode
pAsset: pIsEcoModeEnabled ? "SidebarEcoModeActive" : "SidebarEcoMode"
pTooltipText: qsTranslate("QObject", "Eco mode")
pConfShortcutProperty: [{ value: "toggle_eco_mode"}]
pIsEnable: iAppwin.pIsBootComplete
pIsFeatureEnable: false
onClicked: {
plrBackend.logUIAction("Sidebar", "EcoMode")
fOpenEcoModePopup()
}
},
PlrSidebarElement {
pElementId: pSidePanel
pAsset: "SidebarSidePanel"
pTooltipText: qsTranslate("QObject", "Side panel")
pIsEnable: iAppwin.pIsBootComplete && rightPanelBackend.pSidePanelEnabled
pIsFeatureEnable: false /* BSI CUSTOMIZER: sidebar clean:aVNpZGViYXJQcm9wZXJ0aWVzLnBJc1NpZGVQYW5lbEVuYWJsZWQgJiYgdWlCYWNrZW5kLmNvbmZHZXRCb29sKCJic3QuZmVhdHVyZS5zaWRlX3BhbmVsIik= */
onClicked: {
plrBackend.logUIAction("Sidebar", "SidePanel")
rightPanelBackend.toggleRightPanelVisibility()
}
},
PlrSidebarElement {
pElementId: pChat
pAsset: "SidebarChat"
pExtn: ".png"
pTooltipText: qsTranslate("QObject", "Ana AI")
pShowViaRepeaterOnly: false
pIsEnable: iAppwin.pIsBootComplete
pShowOnKebabMenu: !iChatBtn.visible
pIsFeatureEnable: false /* BSI CUSTOMIZER: sidebar clean:aVNpZGViYXJQcm9wZXJ0aWVzLnBJc0FpQ2hhdEVuYWJsZWQgJiYgdWlCYWNrZW5kLmNvbmZHZXRCb29sKCJic3QuZmVhdHVyZS5haV9jaGF0Iik= */
onClicked: {
plrBackend.logUIAction("Sidebar", "Chat")
plrBackend.showChatWindow()
}
},
PlrSidebarElement {
pIsFeatureEnable: false // BSI CUSTOMIZER: sidebar clean
pElementId: pSettings
pAsset: "SidebarSettings"
pTooltipText: qsTranslate("QObject", "Settings")
pConfShortcutProperty: [{ value: "open_settings"}]
pShowViaRepeaterOnly: false
pShowOnKebabMenu: !iSettingsBtn.visible
onClicked: {
plrBackend.logUIAction("Sidebar", "Settings")
sidebarSettingClicked()
}
},
PlrSidebarElement {
pIsFeatureEnable: false // BSI CUSTOMIZER: sidebar clean
pElementId: pBack
pAsset: "SidebarBack"
pTooltipText: qsTranslate("QObject", "Back")
pConfShortcutProperty: [{ value: "go_back"}]
pIsEnable: iAppwin.pIsBootComplete
pShowViaRepeaterOnly: false
pShowOnKebabMenu: !iBackBtn.visible
onClicked: {
plrBackend.logUIAction("Sidebar", "Back")
plrBackend.onBackClicked()
}
},
PlrSidebarElement {
pIsFeatureEnable: false // BSI CUSTOMIZER: sidebar clean
pElementId: pHome
pAsset: "SidebarHome"
pTooltipText: qsTranslate("QObject", "Home")
pConfShortcutProperty: [{ value: "go_home"}]
pIsEnable: iAppwin.pIsBootComplete
pShowViaRepeaterOnly: false
pShowOnKebabMenu: !iHomeBtn.visible
onClicked: {
plrBackend.logUIAction("Sidebar", "Home")
plrBackend.onHomeClicked()
}
},
PlrSidebarElement {
pIsFeatureEnable: false // BSI CUSTOMIZER: sidebar clean
pElementId: pRecent
pAsset: "SidebarRecents"
pTooltipText: qsTranslate("QObject", "Recent apps")
pConfShortcutProperty: [{ value: "open_recent_apps"}]
pIsEnable: iAppwin.pIsBootComplete
pShowViaRepeaterOnly: false
pShowOnKebabMenu: !iRecentBtn.visible
onClicked: {
plrBackend.logUIAction("Sidebar", "RecentApps")
plrBackend.onRecentAppsClicked()
}
}
]
property list<QtObject> pSidebarItems
Component.onCompleted: {
pIsGameControlsOverlayEnabled = uiBackend.confGetBool("bst.key_controls_overlay_enabled")
pIsAirplaneModeOn = uiBackend.confGetInstanceBool("airplane_mode_active")
pGameControlsEnabled = uiBackend.confGetInstanceBool("game_controls_enabled")
}
onHeightChanged: {
fResetSidebar()
}
function fResetSidebar() {
var availableHeight = iSidebar.height
        // accomodate for iKebabMenuBtn when sidebar items are hidden
if(iSidebar.height < 32 * 7)
availableHeight = availableHeight - iKebabMenuBtn.height
if(availableHeight >= iRecentBtn.height) {
iRecentBtn.visible = true
availableHeight = availableHeight - iRecentBtn.height
} else { iRecentBtn.visible = false }
if(availableHeight >= iHomeBtn.height) {
iHomeBtn.visible = true
availableHeight = availableHeight - iHomeBtn.height
} else { iHomeBtn.visible = false }
if(availableHeight >= iBackBtn.height) {
iBackBtn.visible = true
availableHeight = availableHeight - iBackBtn.height
} else { iBackBtn.visible = false }
if(availableHeight >= iSettingsBtn.height) {
iSettingsBtn.visible = true
availableHeight = availableHeight - iSettingsBtn.height
} else { iSettingsBtn.visible = false }
if(uiBackend.confGetBool("bst.feature.ai_chat") && iAppwin.pIsBootComplete && availableHeight >= iChatBtn.height) {
iChatBtn.visible = true
availableHeight = availableHeight - iChatBtn.height
} else {
iChatBtn.visible = false
}
if(uiBackend.confGetString("bst.feature.discord_url") != "" && iAppwin.pIsBootComplete && availableHeight >= iDiscordBtn.height) {
iDiscordBtn.visible = true
availableHeight = availableHeight - iDiscordBtn.height
} else {
iDiscordBtn.visible = false
}
pSidebarItems = []
if(availableHeight > 0) {
var repeaterElements = 0
for (var i = 0; i < pAllSidebarItems.length; i++)
if(pAllSidebarItems[i].pShowViaRepeaterOnly && pAllSidebarItems[i].pIsFeatureEnable && pAllSidebarItems[i].pVisible)
{
pSidebarItems.push(pAllSidebarItems[i]);
repeaterElements++
}
var count = Math.floor(availableHeight / pSidebarElementSize)
if(count >= repeaterElements)
{
iKebabMenuBtn.visible = false
pVisibleSidebarElementsCount = repeaterElements
} else {
iKebabMenuBtn.visible = true
pVisibleSidebarElementsCount = count-1
}
}
}
Component {
id: iFileDialogComponent
FileDialog {
id: iFileDialog
title: qsTranslate("QObject", "Please choose a file")
nameFilters: ["Android Files (*.apk *.xapk)"]
fileMode: FileDialog.OpenFiles
visible: false
onAccepted: {
iFileDialog.close()
iFileDialog.destroy()
for (var i = 0; i < iFileDialog.files.length; ++i) {
var path = iFileDialog.files[i].toString()
path = path.replace(/^(file:\/{3})|(qrc:\/{2})|(http:\/{2})/, "")
plrBackend.onInstallApkClicked(path)
}
}
}
}
Component {
id: iSelectMediaFolderDialogComponent
UiFolderDialog {
id: iSelectMediaFolderDialog
visible: false
folder: StandardPaths.standardLocations(StandardPaths.PicturesLocation)[0]
onAccepted: {
iSelectMediaFolderDialog.close()
iSelectMediaFolderDialog.destroy()
var currentPath = currentFolder.toString()
currentPath = currentPath.replace( /^(file:\/{3})|(qrc:\/{2})|(http:\/{2})/, "")
if (plrBackend.isDirWritable(currentPath) && !plrBackend.isDirInInstallationFolder(currentPath)) {
pMediaPrompt.close()
pMediaPrompt.destroy()
uiBackend.confSetString("bst.media_folder", currentPath)
uiBackend.confCommit()
plrBackend.takeScreenshot(currentPath)
} else {
pScreenShotNoticeText = qsTranslate("QObject", "%1 does not have the required permissions to store in the folder you have selected").arg(uiBackend.productName)
}
}
}
}
Component {
id: iSelectMediaPrompt
UiMessageDialog {
title: qsTranslate("QObject", "Media Gallery")
pDescription: qsTranslate("QObject", "Screenshots will be automatically saved in the Android Gallery and in the folder mentioned below. You may also choose a custom folder.")
pDialogType: UiDialogWindow.Simple
pNotice: pScreenShotNoticeText
pButtonsList: DialogButtonModelList {
pButtons: [
DialogButtonModel {
pColor: UiTheme.buttons.colors.accent
pText: qsTranslate("QObject", "Choose custom")
onClicked: {
iSelectMediaFolderDialogComponent.createObject().open()
}
},
DialogButtonModel {
pColor: UiTheme.buttons.colors.secondary
pText: qsTranslate("QObject", "Use current")
onClicked: {
plrBackend.takeScreenshot("")
close()
}
}
]
}
}
}
Component {
id: iRestartGamePrompt
UiMessageDialog {
title: qsTranslate("QObject", "Restart App")
pDescription: qsTranslate("QObject", "In order to apply and save this change, app restart is required.")
pDialogType: UiDialogWindow.Simple
pButtonsList: DialogButtonModelList {
pButtons: [
DialogButtonModel {
pColor: UiTheme.buttons.colors.accent
pText: qsTranslate("QObject", "Save and Restart")
onClicked: {
plrBackend.setGameOrientation(pGameOrientationText)
close()
}
},
DialogButtonModel {
pColor: UiTheme.buttons.colors.secondary
pText: qsTranslate("QObject", "Cancel")
onClicked: {
close()
}
}
]
}
}
}
Component {
id: iVolumePopupTimerComponent
Timer {
repeat: false
interval: 2000
running: false
onTriggered: {
fCloseVolumePopup()
}
}
}
function fStartVolumePopupTimer() {
if (pVolumePopupTimer == null)
pVolumePopupTimer = iVolumePopupTimerComponent.createObject()
pVolumePopupTimer.restart()
}
function fCloseVolumePopup() {
if (pVolumeWin != null && pVolumeWin.visible)
pVolumeWin.close()
if (pVolumePopupTimer != null)
pVolumePopupTimer.destroy()
}
Component {
id: iVolumePopupComponent
UiWindowedPopup {
contentWidth: 180
contentHeight: iVolumePopupLayout.implicitHeight
visible: false
UiColumnLayout {
id: iVolumePopupLayout
spacing: 8
color: UiTheme.colors.primary70
padding: 8
border.width: 1
border.color: UiTheme.colors.primary60
anchors.fill: parent
Layout.margins: 8
MouseArea {
Layout.preferredHeight: iVolumeSliderLayout.implicitHeight
hoverEnabled: true
onEntered: {
if (pVolumePopupTimer != null)
pVolumePopupTimer.stop()
}
onExited: {
fStartVolumePopupTimer()
}
UiRowLayout {
id: iVolumeSliderLayout
spacing: 4
color: UiTheme.colors.primary70
UiImageButton {
asset: pIsMuted ? "VolumeMute" : "Volume"
pImageHeight: 16
pImageWidth: 16
pUseShortcutTooltip: true
UiShortcutToolTip.text: qsTranslate("QObject", "Mute/Unmute")
UiShortcutToolTip.shortcutMap: [{ value: "mute_unmute" }]
onClicked: {
fRequestMuteUnmute()
}
}
UiSlider {
pSize: UiTheme.sliders.sizes.small
from: 0
to: pMaxVolumeLevel
stepSize: 1
live: false
value: pCurrentVolumeLevel
Layout.preferredWidth: 144
onValueChanged: {
if (pCurrentVolumeLevel !== parseInt(value))
fVolumeSliderChanged(parseInt(value))
}
}
}
}
Rectangle {
height: 1
color: UiTheme.colors.primary60
Layout.fillWidth: true
}
UiButton {
id: iMuteAllInstancesTxt
Layout.maximumWidth: parent.width
text: qsTranslate("QObject", "Mute all instances")
pSize: UiTheme.buttons.sizes.small
pColor: UiTheme.buttons.colors.outline
Layout.alignment: Qt.AlignLeft
onClicked: {
plrBackend.enableMuteModeForRunningInstances(true)
}
}
}
}
}
Component {
id: iEcoModePopupComponent
UiWindowedPopup {
id: iEcoModePopup
contentWidth: 180
contentHeight: 100
visible: false
Rectangle {
anchors.centerIn: parent
anchors.fill: parent
color: UiTheme.colors.primary80
border.width: 1
border.color: UiTheme.colors.primary60
ColumnLayout {
spacing: 0
anchors.fill: parent
anchors.margins: 10
RowLayout {
spacing: 0
Layout.fillWidth: true
Layout.alignment: Qt.AlignTop
Text {
text: qsTranslate("QObject", "Eco mode")
font: UiTheme.fonts.titleExtraSmall
color: UiTheme.colors.primary10
Layout.alignment: Qt.AlignLeft
horizontalAlignment: Text.AlignLeft
}
Item { Layout.fillWidth: true }
UiImageButton {
asset: "MenuHelp"
pImageHeight: 16
pImageWidth: 16
Layout.rightMargin: 8
UiToolTip.text: qsTranslate("QObject", "Help")
onClicked: { uiBackend.openHelpArticle("EcoMode_help") }
pMouseCursor: hovered ? Qt.PointingHandCursor : Qt.ArrowCursor
}
UiToggleButton {
id: iEcoModeToggleBtn
checked: pIsEcoModeEnabled
pSize: UiTheme.toggleButtons.sizes.small
onCheckedChanged: {
if(pIsEcoModeEnabled !== checked) {
pIsEcoModeEnabled = checked
if (!checked)
pIsEcoModeSoundEnabled = false
var fps = pIsEcoModeEnabled ? parseInt(iEcoModeFpsSpinBox.value) : 0
plrBackend.enableEcoMode(pIsEcoModeEnabled, fps)
}
}
}
}
Rectangle {
height: 1
color: UiTheme.colors.primary60
Layout.fillWidth: true
}
RowLayout {
id: iSoundRow
enabled: pIsEcoModeEnabled
Layout.fillWidth: true
Layout.topMargin: 5
Text {
text: qsTranslate("QObject", "Sound")
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary10
opacity: enabled ? 1 : 0.5
Layout.preferredHeight: 20
horizontalAlignment: Text.AlignLeft
}
Item { Layout.fillWidth: true }
UiToggleButton {
id: iEcoModeSoundToggleBtn
checked: pIsEcoModeSoundEnabled
pSize: UiTheme.toggleButtons.sizes.small
onCheckedChanged: {
if(pIsEcoModeSoundEnabled != checked) {
pIsEcoModeSoundEnabled = checked
plrBackend.forceMuteUnmute(!pIsEcoModeSoundEnabled)
}
}
}
}
RowLayout {
id: iEcoFPSRow
enabled: pIsEcoModeEnabled
Layout.fillWidth: true
Layout.topMargin: 5
Text {
text: qsTranslate("QObject", "Eco FPS(1-30)")
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary10
opacity: enabled ? 1 : 0.5
Layout.preferredHeight: 20
horizontalAlignment: Text.AlignLeft
}
Item { Layout.fillWidth: true }
UiSpinBox {
id: iEcoModeFpsSpinBox
realFrom: 1
realTo: 30
realStepSize: 1
realValue: plrBackend.getEcoModeMaxFps()
decimalPlaces: 0
inputMethodHints: Qt.ImhDigitsOnly
validator: RegularExpressionValidator { regularExpression: /([1-9]|[12][0-9]|3[0])/ }
}
}
}
}
onClosePopup: {
if (pIsEcoModeEnabled) {
var ecoModeFps = parseInt(iEcoModeFpsSpinBox.value)
if (ecoModeFps != plrBackend.getEcoModeMaxFps()) plrBackend.setEcoModeMaxFps(ecoModeFps)
}
}
}
}
Component {
id: iImapAbortPopupComponent
PlrImapAbortWindow { }
}
Component {
id: iGameControlsPopupComponent
UiWindowedPopup {
id: iGameControlsPopup
contentWidth: iContentRect.width
contentHeight: iContentRect.height
visible: false
property alias schemeModel: iSchemeCombobox.model
property alias schemeIdx: iSchemeCombobox.currentIndex
onSchemeModelChanged: {
if (schemeModel.length < 1)
iSchemeColumn.visible = false
if(schemeModel.length > 1)
iSchemeShortcutText.visible = true
else
iSchemeShortcutText.visible = false
}
onWidthChanged: {
iGameControlsPopup.x = iSidebar.mapToGlobal(0, 0).x - iGameControlsPopup.width + iGameControlsPopup.shadowThickness
}
function updateSenstivity() {
iMouseSensitivityXSpinBox.realValue = kmmBackend.mouseSensitivityX
iMouseSensitivityYSpinBox.realValue = kmmBackend.mouseSensitivityY
}
Rectangle {
id: iContentRect
width: iContentLayout.width + iContentLayout.anchors.leftMargin + iContentLayout.anchors.rightMargin
height: iContentLayout.height + iContentLayout.anchors.topMargin + iContentLayout.anchors.bottomMargin
color: UiTheme.colors.primary70
border.width: 1
border.color: UiTheme.colors.primary60
ColumnLayout {
id: iContentLayout
width: Math.max(164, childrenRect.width)
anchors.centerIn: parent
anchors.margins: 8
spacing: 8
RowLayout {
spacing: 4
Layout.fillWidth: true
Layout.preferredHeight: childrenRect.height
Text {
text: qsTranslate("QObject", "Game controls")
font: UiTheme.fonts.titleExtraSmall
color: UiTheme.colors.primary10
horizontalAlignment: Text.AlignLeft
}
Item { Layout.fillWidth: true }
RowLayout {
Layout.preferredWidth: childrenRect.width
Layout.preferredHeight: childrenRect.height
spacing: 4
UiImageButton {
asset: "MenuHelp"
pImageHeight: 16
pImageWidth: 16
UiToolTip.text: qsTranslate("QObject", "Help")
onClicked: uiBackend.openHelpArticle("game_controls_help")
pMouseCursor: hovered ? Qt.PointingHandCursor : Qt.ArrowCursor
}
UiToggleButton {
id: iGameControlsToggleBtn
checked: pGameControlsEnabled
pSize: UiTheme.toggleButtons.sizes.small
pUseShortcutTooltip: true
UiShortcutToolTip.text: qsTranslate("QObject", "Toggle game controls")
UiShortcutToolTip.shortcutMap: [{ value: "enable_disable_game_controls" }]
onToggled: {
if (pGameControlsPopupLoaded) {
kmmBackend.setKeyMappingState(checked)
kmmBackend.toggleOverlayVisibility(checked ? iOverlayToggleBtn.checked : checked, false)
}
}
}
}
}
Rectangle {
Layout.fillWidth: true
Layout.preferredHeight: 1
color: UiTheme.colors.primary60
}
UiColumnLayout {
Layout.fillWidth: true
spacing: 2
RowLayout {
id: iOverlayRow
Layout.fillWidth: true
Layout.preferredHeight: childrenRect.height
spacing: 4
enabled: iGameControlsToggleBtn.checked
opacity: enabled ? 1 : 0.5
Text {
text: qsTranslate("QObject", "On-screen controls")
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary20
horizontalAlignment: Text.AlignLeft
}
Item { Layout.fillWidth: true }
UiToggleButton {
id: iOverlayToggleBtn
pSize: UiTheme.toggleButtons.sizes.small
checked: pIsGameControlsOverlayEnabled
pUseShortcutTooltip: true
UiShortcutToolTip.text: qsTranslate("QObject", "Show/Hide on-screen controls")
UiShortcutToolTip.shortcutMap: [{ value: "show_hide_on_screen_controls" }]
onToggled: {
pIsGameControlsOverlayEnabled = checked
kmmBackend.toggleOverlayVisibility(pIsGameControlsOverlayEnabled, true)
}
}
}
Text {
Layout.fillWidth: true
text: plrBackend.getDisplayComboFromNameCombo(uiBackend.confGetString("bst.shortcut.show_hide_on_screen_controls"))
font: UiTheme.fonts.overlineSmall
color: UiTheme.colors.primary30
horizontalAlignment: Text.AlignRight
}
}
RowLayout {
Layout.fillWidth: true
Layout.preferredHeight: childrenRect.height
spacing: 4
enabled: iOverlayRow.enabled && iOverlayToggleBtn.checked
Text {
text: qsTranslate("QObject", "Opacity")
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary20
horizontalAlignment: Text.AlignLeft
opacity: enabled ? 1 : 0.5
}
Item { Layout.fillWidth: true }
UiSlider {
pSize: UiTheme.sliders.sizes.small
from: 0
to: 100
stepSize: 1
value: uiBackend.confGetI32("bst.key_controls_overlay_opacity")
Layout.preferredWidth: 80
onValueChanged: kmmBackend.setOverlayTransparency(parseInt(value))
onPressedChanged: kmmBackend.saveOverlayOpacityInConf(parseInt(value))
}
}
RowLayout {
Layout.fillWidth: true
Layout.preferredHeight: childrenRect.height
spacing: 8
enabled: iOverlayRow.enabled && iOverlayToggleBtn.checked
Text {
text: qsTranslate("QObject", "Controls for")
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary20
horizontalAlignment: Text.AlignLeft
opacity: enabled ? 1 : 0.5
}
Item { Layout.fillWidth: true }
UiImageButton {
asset: pGameControlsGamepadSelected ? "Keyboard" : "Keyboard_click"
pDisableClickAndHoveredState: !pGameControlsGamepadSelected
pImageHeight: 16
pImageWidth: 16
UiToolTip.text: qsTranslate("QObject", "Keyboard and mouse")
onClicked: {
pGameControlsGamepadSelected = false
kmmBackend.gamepadControlsSelected = false
}
}
UiImageButton {
asset: (enabled && pGameControlsGamepadSelected) ? "Gamepad_click" : "Gamepad"
pDisableClickAndHoveredState: enabled && pGameControlsGamepadSelected
pImageHeight: 16
pImageWidth: 16
UiToolTip.text: qsTranslate("QObject", "Gamepad")
enabled: pGamepadControlsPresent
onClicked: {
pGameControlsGamepadSelected = true
kmmBackend.gamepadControlsSelected = true
}
}
}
Rectangle {
Layout.fillWidth: true
Layout.preferredHeight: 1
color: UiTheme.colors.primary60
visible: iSchemeColumn.visible
}
ColumnLayout {
id: iSchemeColumn
Layout.fillWidth: true
Layout.preferredHeight: childrenRect.height
spacing: 4
enabled: iOverlayRow.enabled
opacity: enabled ? 1 : 0.5
Text {
text: qsTranslate("QObject", "Scheme")
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary10
}
UiComboBox {
id: iSchemeCombobox
pSize: UiTheme.comboBoxes.sizes.small
Layout.preferredHeight: 24
Layout.fillWidth: true
onActivated: {
if (pGameControlsPopupLoaded) {
kmmBackend.setActiveScheme(model[currentIndex]);
pGameControlsSensitivityEnabled = kmmBackend.hasPanControl();
pGamepadControlsPresent = kmmBackend.hasGamepadControls
if(pGameControlsSensitivityEnabled)
updateSenstivity()
}
}
}
}
Rectangle {
id: iSchemeShortcutText
Layout.fillWidth: true
color: UiTheme.colors.primary70
border.width: 1
border.color: UiTheme.colors.primary60
implicitHeight: contentLayout.implicitHeight + 8
UiRowLayout {
id: contentLayout
anchors.fill: parent
anchors.margins: 4
spacing: 4
UiImage {
Layout.topMargin: 2
pImageWidth: 12
pImageHeight: 12
asset: "InfoSmall"
Layout.alignment: Qt.AlignTop
}
Text {
wrapMode: Text.Wrap
textFormat: Text.RichText
verticalAlignment: Text.AlignTop
Layout.fillWidth: true
font: UiTheme.fonts.bodySmallest
color: UiTheme.colors.primary30
text: {
let shortcut = uiBackend.confGetString("bst.shortcut.switch_scheme");
let styledShortcut = "<font color='" + UiTheme.colors.primary20 + "'>" + shortcut + "</font>";
return qsTranslate("QObject", "Quick switch scheme in-game: %1").arg(styledShortcut);
}
}
}
}
ColumnLayout {
Layout.fillWidth: true
Layout.preferredHeight: childrenRect.height
spacing: 4
enabled: iOverlayRow.enabled
visible: pGameControlsSensitivityEnabled
opacity: enabled ? 1 : 0.5
Text {
text: qsTranslate("QObject", "Mouse sensitivity")
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary20
}
RowLayout {
spacing: 16
Layout.preferredWidth: childrenRect.width
Layout.preferredHeight: childrenRect.height
RowLayout {
Layout.preferredWidth: childrenRect.width
Layout.preferredHeight: childrenRect.height
spacing: 8
Text {
text: "X"
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary20
horizontalAlignment: Text.AlignLeft
}
UiSpinBox {
id: iMouseSensitivityXSpinBox
Layout.preferredWidth: 58
Layout.preferredHeight: 24
realFrom: 0.01
realTo: 10
realStepSize: 0.01
decimalPlaces: 2
inputMethodHints: Qt.ImhFormattedNumbersOnly
validator: RegularExpressionValidator { regularExpression: /^(?=.*[0-9])\d{1}(?:\.\d{0,2})?$|^10$/}
onValueChanged: sensitivityChanged()
}
}
RowLayout {
Layout.preferredWidth: childrenRect.width
Layout.preferredHeight: childrenRect.height
spacing: 8
Text {
text: "Y"
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary20
horizontalAlignment: Text.AlignLeft
}
UiSpinBox {
id: iMouseSensitivityYSpinBox
Layout.preferredWidth: 58
Layout.preferredHeight: 24
realFrom: 0.01
realTo: 10
realStepSize: 0.01
decimalPlaces: 2
inputMethodHints: Qt.ImhFormattedNumbersOnly
validator: RegularExpressionValidator { regularExpression: /^(?=.*[0-9])\d{1}(?:\.\d{0,2})?$|^10$/}
onValueChanged: sensitivityChanged()
}
}
}
}
Rectangle {
Layout.fillWidth: true
Layout.preferredHeight: 1
color: UiTheme.colors.primary60
}
Text {
id: iOpenEditorTxt
text: qsTranslate("QObject", "Controls editor")
font: UiTheme.fonts.bodySmall
color: UiTheme.colors.primary20
horizontalAlignment: Text.AlignLeft
enabled: iGameControlsToggleBtn.checked
opacity: enabled ? 1 : 0.5
MouseArea {
anchors.fill: parent
hoverEnabled: true
onEntered: {
cursorShape = Qt.PointingHandCursor
iOpenEditorTxt.color = UiTheme.colors.primary10
}
onExited: {
cursorShape = Qt.ArrowCursor
iOpenEditorTxt.color = UiTheme.colors.primary20
}
onClicked: {
cursorShape = Qt.ArrowCursor
iGameControlsPopup.destroy()
fCheckSplitModeAndOpenEditorWindow(false)
}
}
}
}
}
onClosePopup: {
sensitivityChanged();
if (pGameControlsPopupLoaded)
kmmBackend.kmmPopupCommit()
pGameControlsPopupLoaded = false
pGameControlsEnabled = iGameControlsToggleBtn.checked
uiBackend.confSetInstanceBool("game_controls_enabled", pGameControlsEnabled)
uiBackend.confCommit()
}
function sensitivityChanged() {
if (pGameControlsPopupLoaded && pGameControlsSensitivityEnabled) {
kmmBackend.setMouseSensitivity(
iMouseSensitivityXSpinBox.value / iMouseSensitivityXSpinBox.factor,
iMouseSensitivityYSpinBox.value / iMouseSensitivityYSpinBox.factor)
}
}
}
}
Component {
id: iKebabMenuPopupComponent
UiWindowedPopup {
id: iKebabMenuPopup
property int pKebabMenuElementsCount: 0
contentWidth: pSidebarElementSize * Math.ceil(pKebabMenuElementsCount / 4)
contentHeight: Math.min(pSidebarElementSize * 4, pSidebarElementSize * pKebabMenuElementsCount)
Rectangle {
anchors.fill: parent
color: UiTheme.colors.primary80
border.width: 1
border.color: UiTheme.colors.primary60
Flow {
spacing: 0
anchors.fill: parent
flow: Flow.TopToBottom
Repeater {
model: pSidebarItems
delegate: UiImageButton {
Component.onCompleted: if(visible) pKebabMenuElementsCount++
asset: model.modelData.pAsset
extn: model.modelData.pExtn
pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
enabled: model.modelData.pIsEnable
pUseShortcutTooltip: true
UiShortcutToolTip.text: model.modelData.pTooltipText
UiShortcutToolTip.shortcutMap: modelData.pConfShortcutProperty
pImageAnimationRunning: model.modelData.pRunImageAnimation
visible: (index > pVisibleSidebarElementsCount-1) && model.modelData.pIsFeatureEnable && model.modelData.pShowOnKebabMenu && model.modelData.pVisible
MouseArea {
anchors.fill: parent
acceptedButtons: model.modelData.pAcceptedMouseButtons
onPressed: function(mouse) {
UiShortcutToolTip.visible = false
model.modelData.clicked(mouse.button, mouse.modifiers)
}
onReleased: function(mouse) {
if(pKebabMenuWin != null)
pKebabMenuWin.close()
model.modelData.mouseReleased(mouse.button, mouse.modifiers)
}
}
}
}
}
}
}
}
Component {
id: iRotatePopupComponent
UiWindowedPopup {
id: iRotatePopup
contentWidth: iRotatePopupRect.width
contentHeight: iRotatePopupRect.height
visible: false
onWidthChanged: {
iRotatePopup.x = iSidebar.mapToGlobal(0, 0).x - iRotatePopup.width + iRotatePopup.shadowThickness
}
Rectangle {
id: iRotatePopupRect
width: iRotateContentLayout.width
height: iRotateContentLayout.height
color: UiTheme.colors.primary70
border.width: 1
border.color: UiTheme.colors.primary60
ColumnLayout {
id: iRotateContentLayout
anchors.centerIn: parent
anchors.margins: 8
UiImageLabel {
Layout.fillWidth: true
Layout.preferredHeight: 32
pText: qsTranslate("QObject", "Rotate")
pImageAsset: "RotateIcon"
leftPadding: 8
rightPadding: 8
pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
onClicked: { fHandleRotateButtonClicked("") }
}
UiImageLabel {
Layout.fillWidth: true
Layout.preferredHeight: 32
pText: qsTranslate("QObject", "Portrait")
pImageAsset: "PortraitIcon"
leftPadding: 8
rightPadding: 8
enabled: plrBackend.isGameOrientationSupported()
pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
onClicked: { fHandleRotateButtonClicked("Portrait") }
}
UiImageLabel {
Layout.fillWidth: true
Layout.preferredHeight: 32
pText: qsTranslate("QObject", "Landscape")
pImageAsset: "LandscapeIcon"
leftPadding: 8
rightPadding: 8
enabled: plrBackend.isGameOrientationSupported()
pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
onClicked: { fHandleRotateButtonClicked("Landscape") }
}
}
}
}
}
Component {
id: iScreenRecordComponent
UiWindowedPopup {
id: iScreenRecordPopup
contentWidth: 400
contentHeight: iScreenRecordRect.height
visible: false
onWidthChanged: {
iScreenRecordPopup.x = iSidebar.mapToGlobal(0, 0).x - iScreenRecordPopup.width + iScreenRecordPopup.shadowThickness
}
onClosing: function() {
if (uiBackend.confIsPropModified("bst.default_highlights_mode")) {
console.info("default highlights mode is changed, isSnapshotSelected : " + iHighlightsFpsToggle.isSnapshotSelected)
uiBackend.confCommit()
plrBackend.reloadConfInAllInstances()
if (uiBackend.confGetBool("bst.feature.send_auto_record_stats"))
plrBackend.sendMomentsStats("sr_fps_change", iHighlightsFpsToggle.isSnapshotSelected ? plrBackend.pHighlightSnapshotFps : plrBackend.pHighlightVideoFps)
}
}
Rectangle {
id: iScreenRecordRect
width: iScreenRecordPopup.contentWidth
height: iScreenRecordLayout.implicitHeight
color: UiTheme.colors.primary70
border.width: 1
border.color: UiTheme.colors.primary60
UiColumnLayout {
id: iScreenRecordLayout
anchors.fill: parent
UiRowLayout {
Layout.fillWidth: true
leftPadding: 12
rightPadding: 12
topPadding: 8
bottomPadding: 8
spacing: 8
Text {
elide: Text.ElideRight
text: qsTranslate("QObject", "Screen recording")
font: UiTheme.fonts.titleSmall
color: UiTheme.colors.primary10
horizontalAlignment: Text.AlignLeft
}
Item { Layout.fillWidth: true }
UiImageButton {
asset: "AccountLoggedIn"
pImageWidth: 16
pImageHeight: 16
visible: uiBackend.confGetBool("bst.nowgg_signin_completed") || uiBackend.confGetBool("bst.guest_login_completed")
onClicked: {
pScreenRecordWin.close()
if (uiBackend.confGetBool("bst.nowgg_signin_completed"))
fShowSignOutDialog()
else {
nowggBackend.setLinkAccountSource("profile_icon")
fShowLinkNowggAccountDialog("", false)
}
}
pMouseCursor: hovered ? Qt.PointingHandCursor : Qt.ArrowCursor
}
UiButton {
asset: "MediaGallery"
text: qsTranslate("QObject", "My Videos")
pSize: UiTheme.buttons.sizes.small
pColor: UiTheme.buttons.colors.tertiary
opacity: enabled ? 1 : 0.4
onClicked: {
pScreenRecordWin.close()
fOpenMediaGalleryWindow()
}
}
}
Rectangle {
height: 1
color: UiTheme.colors.primary50
Layout.fillWidth: true
}
UiRowLayout {
Layout.fillWidth: true
leftPadding: 12
rightPadding: 12
topPadding: 12
bottomPadding: 12
spacing: 12
visible: uiBackend.confGetBool("bst.feature.show_moments")
UiImage {
pImageWidth: 48
pImageHeight: 48
asset: "MomentsIcon"
extn: ".png"
Layout.alignment: Qt.AlignTop
}
UiColumnLayout {
Layout.alignment: Qt.AlignTop
Layout.fillWidth: true
Text {
elide: Text.ElideRight
text: qsTranslate("QObject", "Moments Mode")
font: UiTheme.fonts.titleSmall
color: UiTheme.colors.primary10
horizontalAlignment: Text.AlignLeft
}
Text {
Layout.preferredWidth: 275
text: (nowggBackend.pRecordMode === RecordMode.MOMENT) ? qsTranslate("QObject", "Press %1 to save moment").arg(uiBackend.confGetString("bst.shortcut.record_moment")) : qsTranslate("QObject", "Never miss a moment that matters. Save the last 60 seconds of your gameplay anytime, with a simple shortcut.")
font: UiTheme.fonts.bodyMedium
color: UiTheme.colors.primary20
horizontalAlignment: Text.AlignLeft
wrapMode: Text.Wrap
}
UiButton {
Layout.topMargin: 8
text: qsTranslate("QObject", "Save Moment")
pSize: UiTheme.buttons.sizes.small
pColor: UiTheme.buttons.colors.accent
visible: nowggBackend.pIsScreenRecorderOn && (nowggBackend.pRecordMode === RecordMode.MOMENT)
onClicked: {
pScreenRecordWin.close()
nowggBackend.addScreenMoment()
}
}
}
UiColumnLayout {
Layout.fillHeight: true
UiToggleButton {
id: iMomentsToggleButton
Layout.alignment: Qt.AlignTop
checked: (nowggBackend.pRecordMode === RecordMode.MOMENT)
onToggled: {
pScreenRecordWin.close()
nowggBackend.handleMomentsToggled(checked)
}
}
UiImageButton {
Layout.alignment: Qt.AlignRight | Qt.AlignBottom
asset: "MenuHelp"
pImageWidth: 16
pImageHeight: 16
UiToolTip.text: qsTranslate("QObject", "Help")
onClicked: uiBackend.openHelpArticle("Moments_Help")
pMouseCursor: hovered ? Qt.PointingHandCursor : Qt.ArrowCursor
}
}
}
Rectangle {
height: 1
color: UiTheme.colors.primary50
Layout.fillWidth: true
}
UiRowLayout {
Layout.fillWidth: true
leftPadding: 12
rightPadding: 12
topPadding: 12
bottomPadding: 12
spacing: 12
UiImage {
pImageWidth: 48
pImageHeight: 48
asset: "RecordingIcon"
Layout.alignment: Qt.AlignTop
}
UiColumnLayout {
Layout.alignment: Qt.AlignTop
Layout.fillWidth: true
Text {
elide: Text.ElideRight
text: qsTranslate("QObject", "Record Gameplay")
font: UiTheme.fonts.titleSmall
color: UiTheme.colors.primary10
horizontalAlignment: Text.AlignLeft
}
Text {
elide: Text.ElideRight
text: qsTranslate("QObject", "Record your current gameplay")
font: UiTheme.fonts.bodyMedium
color: UiTheme.colors.primary20
horizontalAlignment: Text.AlignLeft
wrapMode: Text.Wrap
}
UiButton {
Layout.topMargin: 8
text: qsTranslate("QObject", "Start Recording")
pSize: UiTheme.buttons.sizes.small
pColor: UiTheme.buttons.colors.accent
opacity: enabled ? 1 : 0.4
visible: !nowggBackend.pIsScreenRecorderOn || (nowggBackend.pRecordMode === RecordMode.MOMENT)
onClicked: {
pScreenRecordWin.close()
nowggBackend.startRecording(true)
}
}
UiButton {
Layout.topMargin: 8
text: qsTranslate("QObject", "Stop Recording")
pSize: UiTheme.buttons.sizes.small
pColor: UiTheme.buttons.colors.danger
opacity: enabled ? 1 : 0.4
visible: nowggBackend.pIsScreenRecorderOn && (nowggBackend.pRecordMode === RecordMode.NORMAL)
onClicked: {
pScreenRecordWin.close()
nowggBackend.startRecording(false)
}
}
}
UiImageButton {
Layout.alignment: Qt.AlignBottom
asset: "MenuHelp"
pImageWidth: 16
pImageHeight: 16
UiToolTip.text: qsTranslate("QObject", "Help")
onClicked: uiBackend.openHelpArticle("Screen_Recording_Help")
pMouseCursor: hovered ? Qt.PointingHandCursor : Qt.ArrowCursor
}
}
Rectangle {
height: 1
color: UiTheme.colors.primary50
Layout.fillWidth: true
visible: uiBackend.confGetBool("bst.feature.show_ai_highlights")
}
UiRowLayout {
Layout.fillWidth: true
leftPadding: 12
rightPadding: 12
topPadding: 12
bottomPadding: 12
spacing: 12
visible: uiBackend.confGetBool("bst.feature.show_ai_highlights")
UiImage {
id: iHighlightsImg
pImageWidth: 48
pImageHeight: 48
asset: uiBackend.confGetBool("bst.enable_ai_highlights") ? "AIHighlight" : "AIHighlightDisabled"
Layout.alignment: Qt.AlignTop
}
UiColumnLayout {
Layout.alignment: Qt.AlignTop
Layout.fillWidth: true
spacing: 4
UiRowLayout {
Layout.fillWidth: true
Text {
id: iHighlightsTxt
elide: Text.ElideRight
text: qsTranslate("QObject", "AI Highlights")
font: UiTheme.fonts.titleSmall
color: uiBackend.confGetBool("bst.enable_ai_highlights") ? UiTheme.colors.primary10 : UiTheme.colors.primary30
horizontalAlignment: Text.AlignLeft
}
Rectangle {
id: iHighlightsFpsToggle
width: 140
height: 22
radius: 11
color: UiTheme.colors.primary90
border.width: 1
border.color: UiTheme.colors.primary50
Layout.alignment: Qt.AlignRight
visible: uiBackend.confGetBool("bst.enable_ai_highlights")
property bool isSnapshotSelected: uiBackend.confGetString("bst.default_highlights_mode") === "snapshot"
Rectangle {
id: selectionRect
x: iHighlightsFpsToggle.isSnapshotSelected ? 0 : iHighlightsFpsToggle.width / 2
z: 1
width: parent.width / 2
height: parent.height
radius: parent.radius
color: UiTheme.colors.accentNormal
Behavior on x {
NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
}
}
Row {
anchors.fill: parent
z: 2
spacing: 0
Text {
width: parent.width / 2
height: parent.height
text: qsTranslate("QObject", "Snapshot")
font: UiTheme.fonts.bodySmall
horizontalAlignment: Text.AlignHCenter
verticalAlignment: Text.AlignVCenter
color: iHighlightsFpsToggle.isSnapshotSelected ? UiTheme.colors.white : "#AAB4BE"
}
Text {
width: parent.width / 2
height: parent.height
text: qsTranslate("QObject", "Video")
font: UiTheme.fonts.bodySmall
horizontalAlignment: Text.AlignHCenter
verticalAlignment: Text.AlignVCenter
color: !iHighlightsFpsToggle.isSnapshotSelected ? UiTheme.colors.white : "#AAB4BE"
}
}
MouseArea {
anchors.fill: parent
onClicked: function(mouse) {
var midpoint = parent.width / 2
if (mouse.x < midpoint) {
iHighlightsFpsToggle.isSnapshotSelected = true
uiBackend.confSetString("bst.default_highlights_mode", "snapshot")
}
else {
iHighlightsFpsToggle.isSnapshotSelected = false
uiBackend.confSetString("bst.default_highlights_mode", "video")
}
}
}
}
}
UiRowLayout {
Layout.fillWidth: false
visible: !iHighlightsFpsToggle.isSnapshotSelected && uiBackend.confGetBool("bst.enable_ai_highlights")
spacing: 4
UiImage {
asset: "InfoWarning"
pImageWidth: 12
pImageHeight: 12
}
Text {
text: qsTranslate("QObject", "Video highlights might affect performance")
font: UiTheme.fonts.bodyExtraSmall
color: UiTheme.colors.warning
horizontalAlignment: Text.AlignLeft
}
}
UiRowLayout {
spacing: 8
UiColumnLayout {
Layout.alignment: Qt.AlignLeft
spacing: 8
Text {
id: iAutomaticTxt
Layout.preferredWidth: 300
text: qsTranslate("QObject", "Curates your standout gameplay moments into highlights.")
font: UiTheme.fonts.bodyMedium
color: uiBackend.confGetBool("bst.enable_ai_highlights") ? UiTheme.colors.primary20 : UiTheme.colors.primary40
horizontalAlignment: Text.AlignLeft
wrapMode: Text.Wrap
}
UiButton {
id: iEnableHighlightsBtn
text: qsTranslate("QObject", "Enable Highlights")
pSize: UiTheme.buttons.sizes.small
pColor: UiTheme.buttons.colors.accent
Layout.alignment: Qt.AlignLeft
visible: !uiBackend.confGetBool("bst.enable_ai_highlights")
onClicked: {
iHighlightsTxt.color = UiTheme.colors.primary10
iHighlightsImg.asset = "AIHighlight"
iAutomaticTxt.color = UiTheme.colors.primary20
iHighlightsFpsToggle.visible = true
iEnableHighlightsBtn.visible = false
uiBackend.confSetBool("bst.enable_ai_highlights", true)
uiBackend.confCommit()
plrBackend.reloadConfInAllInstances()
}
}
}
UiImageButton {
Layout.alignment: Qt.AlignRight | Qt.AlignBottom
asset: "MenuHelp"
pImageWidth: 16
pImageHeight: 16
UiToolTip.text: qsTranslate("QObject", "Help")
onClicked: uiBackend.openHelpArticle("AI_Highlights_Help")
pMouseCursor: hovered ? Qt.PointingHandCursor : Qt.ArrowCursor
}
}
}
}
}
}
}
}
Component {
id: iFullScreenBlurbPrompt
PlrFullScreenBlurbWindow { }
}
Component {
id: iGameControlBlurbPrompt
PlrGameControlBlurbWindow { }
}
Component {
id: iScreenRecorderPopupComponent
UiHelpPopup {
id: iScreenRecorderPopup
contentWidth: pInnerLayout.implicitWidth
arrowPosition: UiHelpPopup.ArrowPosition.RightCenter
visible: true
flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
pInnerLayout {
color: UiTheme.colors.primary40
border.color: UiTheme.colors.primary40
border.width: 1
topPadding: 4
bottomPadding: 4
leftPadding: 8
rightPadding: 8
}
property string popupMsg: ""
UiColumnLayout {
spacing: 8
Text {
text: popupMsg
font: UiTheme.fonts.bodyExtraSmall
color: UiTheme.colors.primary10
}
Text {
text: qsTranslate("QObject", "View it from here.")
font: UiTheme.fonts.bodyExtraSmall
color: UiTheme.colors.primary10
}
}
Timer {
repeat: false
running: iScreenRecorderPopup.visible
interval: 2000
onTriggered: {
if (pRecordingSavedWin != null)
pRecordingSavedWin.close()
}
}
}
}
UiColumnLayout {
color: "transparent" // Let the violet rail show through.

anchors.fill: parent
Repeater {
model: pSidebarItems
delegate: Component {
Rectangle {
id: iRootRect
height: pSidebarElementSize
width: pSidebarElementSize
color: "transparent"
visible: (index <= pVisibleSidebarElementsCount-1) && model.modelData.pIsFeatureEnable && model.modelData.pShowViaRepeaterOnly && model.modelData.pVisible
onXChanged: {
model.modelData.positionChanged()
}
onYChanged: {
model.modelData.positionChanged()
}
Connections {
target: iAppwin
function onXChanged() {
model.modelData.positionChanged()
}
function onYChanged() {
model.modelData.positionChanged()
}
}
UiImageButton {
width: pSidebarElementSize
height: pSidebarElementSize

BsiSidebarInlineVisual {
id: bsiSidebarSkin0
anchors.fill: parent
elementId: model.modelData.pElementId
assetName: (
model.modelData.pAsset
)
hovered: iBtn.hovered
pressed: bsiSidebarSkin0Hit.pressed
available: iBtn.enabled
active: /Active/.test(bsiSidebarSkin0.assetName)
animating: model.modelData.pRunImageAnimation
}

id: iBtn
asset: bsiSidebarSkin0.known ? "" : (
model.modelData.pAsset
)
extn: model.modelData.pExtn
pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
enabled: model.modelData.pIsEnable
pUseShortcutTooltip: true
UiShortcutToolTip.text: model.modelData.pTooltipText
UiShortcutToolTip.shortcutMap: modelData.pConfShortcutProperty
pImageAnimationRunning: model.modelData.pRunImageAnimation
MouseArea {
id: bsiSidebarSkin0Hit

anchors.fill: parent
acceptedButtons: model.modelData.pAcceptedMouseButtons
onPressed: function(mouse) {
UiShortcutToolTip.visible = false
model.modelData.clicked(mouse.button, mouse.modifiers)
if (model.modelData.pShade) {
iBtn.showShade()
}
}
onReleased: function(mouse) {
model.modelData.mouseReleased(mouse.button, mouse.modifiers)
}
}
Rectangle {
id: iShade
z: 1
anchors.fill: parent
color: "#88000000"
visible: false
}
Timer {
id: iShadeTimer
interval: 500
onTriggered: iShade.visible = false
}
function showShade() {
iShade.visible = true
iShadeTimer.start()
}
}
UiImage {
id: iLoader
asset: model.modelData.pLoaderAsset
pImageWidth: 8
pImageHeight: 8
anchors.right: iRootRect.right
anchors.bottom: iRootRect.bottom
anchors.rightMargin: 9
anchors.bottomMargin: 9
visible: model.modelData.pLoaderAsset !== "" && model.modelData.pLoaderRunning
RotationAnimation on rotation {
loops: Animation.Infinite
duration: 1000
from: 0
to: 360
direction: RotationAnimation.Clockwise
running: model.modelData.pLoaderRunning
}
}
}
}
}
UiImageButton {
width: pSidebarElementSize
height: pSidebarElementSize

BsiSidebarInlineVisual {
id: bsiSidebarSkin1
anchors.fill: parent
elementId: "more"
assetName: (
pKebabMenuWin == null? "SidebarKebabClosed" : "SidebarKebabOpen"
)
hovered: iKebabMenuBtn.hovered
pressed: ("pressed" in iKebabMenuBtn && typeof iKebabMenuBtn.pressed === "boolean") ? iKebabMenuBtn.pressed : false
available: iKebabMenuBtn.enabled
active: /Active/.test(bsiSidebarSkin1.assetName)
animating: false
}

id: iKebabMenuBtn
Layout.alignment: Qt.AlignHCenter
pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
asset: bsiSidebarSkin1.known ? "" : (
pKebabMenuWin == null? "SidebarKebabClosed" : "SidebarKebabOpen"
)
onPressed: {
if (pKebabMenuWin != null)
{
if (pKebabMenuWin.visible)
pKebabMenuWin.close()
return
}
pKebabMenuWin = iKebabMenuPopupComponent.createObject(iAppwin)
pKebabMenuWin.x = iSidebar.mapToGlobal(0, 0).x - pKebabMenuWin.width + pKebabMenuWin.shadowThickness
pKebabMenuWin.y = iKebabMenuBtn.mapToGlobal(0, 0).y - pKebabMenuWin.shadowThickness
pKebabMenuWin.show()
}
}
Item {
Layout.fillHeight: true
}
UiImageButton {
width: pSidebarElementSize
height: pSidebarElementSize

BsiSidebarInlineVisual {
id: bsiSidebarSkin2
anchors.fill: parent
elementId: ""
assetName: (
uiBackend.oem === "nxt" ? "DiscordButton" : ""
)
hovered: iDiscordBtn.hovered
pressed: ("pressed" in iDiscordBtn && typeof iDiscordBtn.pressed === "boolean") ? iDiscordBtn.pressed : false
available: iDiscordBtn.enabled
active: /Active/.test(bsiSidebarSkin2.assetName)
animating: false
}

id: iDiscordBtn
enabled: iSidebarProperties.pIsDiscordEnabled
Layout.preferredWidth: pSidebarElementSize
Layout.preferredHeight: pSidebarElementSize
pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
pUseImgSourceSize: false
Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
visible: iAppwin.pIsBootComplete && uiBackend.confGetString("bst.feature.discord_url") != ""
UiToolTip.text: qsTranslate("QObject", "Discord")
asset: bsiSidebarSkin2.known ? "" : (
uiBackend.oem === "nxt" ? "DiscordButton" : ""
)
onClicked: {
plrBackend.logUIAction("Sidebar", "Discord")
plrBackend.onDiscrodButtonClicked()
}
}
UiImageButton {
width: pSidebarElementSize
height: pSidebarElementSize

BsiSidebarInlineVisual {
id: bsiSidebarSkin3
anchors.fill: parent
elementId: ""
assetName: (
uiBackend.oem === "nxt" ? "SidebarChat" : ""
)
hovered: iChatBtn.hovered
pressed: ("pressed" in iChatBtn && typeof iChatBtn.pressed === "boolean") ? iChatBtn.pressed : false
available: iChatBtn.enabled
active: /Active/.test(bsiSidebarSkin3.assetName)
animating: false
}

id: iChatBtn
Layout.preferredWidth: pSidebarElementSize
Layout.preferredHeight: pSidebarElementSize
enabled: iSidebarProperties.pIsAiChatEnabled
pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
pUseImgSourceSize: false
Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
visible: iAppwin.pIsBootComplete && uiBackend.confGetBool("bst.feature.ai_chat")
UiToolTip.text: qsTranslate("QObject", "Ana AI")
asset: bsiSidebarSkin3.known ? "" : (
uiBackend.oem === "nxt" ? "SidebarChat" : ""
)
extn: ".png"
onClicked: {
plrBackend.logUIAction("Sidebar", "Chat")
plrBackend.showChatWindow()
}
}
Item {
id: iSettingsBtn
Layout.preferredWidth: pSidebarElementSize
Layout.preferredHeight: pSidebarElementSize
Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
UiImageButton {
width: pSidebarElementSize
height: pSidebarElementSize

BsiSidebarInlineVisual {
id: bsiSidebarSkin4
anchors.fill: parent
elementId: "settings"
assetName: (
"SidebarSettings"
)
hovered: bsiSidebarSkinButton4.hovered
pressed: ("pressed" in bsiSidebarSkinButton4 && typeof bsiSidebarSkinButton4.pressed === "boolean") ? bsiSidebarSkinButton4.pressed : false
available: bsiSidebarSkinButton4.enabled
active: /Active/.test(bsiSidebarSkin4.assetName)
animating: false
}

id: bsiSidebarSkinButton4

pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
asset: bsiSidebarSkin4.known ? "" : (
"SidebarSettings"
)
pUseShortcutTooltip: true
UiShortcutToolTip.text: qsTranslate("QObject", "Settings")
UiShortcutToolTip.shortcutMap: [{ value: "open_settings" }]
onClicked: sidebarSettingClicked()
}
Loader {
visible: sourceComponent != undefined
sourceComponent: plrBackend.pDiskOptimizationOnboarding ? iRedDotComponent : undefined
anchors.top: iSettingsBtn.top
anchors.right: iSettingsBtn.right
anchors.topMargin: 2
anchors.rightMargin: 4
}
}
UiImageButton {
width: pSidebarElementSize
height: pSidebarElementSize

BsiSidebarInlineVisual {
id: bsiSidebarSkin5
anchors.fill: parent
elementId: "back"
assetName: (
"SidebarBack"
)
hovered: iBackBtn.hovered
pressed: ("pressed" in iBackBtn && typeof iBackBtn.pressed === "boolean") ? iBackBtn.pressed : false
available: iBackBtn.enabled
active: /Active/.test(bsiSidebarSkin5.assetName)
animating: false
}

id: iBackBtn
pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
enabled: iAppwin.pIsBootComplete || iAppwin.pShowAndroidAd
asset: bsiSidebarSkin5.known ? "" : (
"SidebarBack"
)
pUseShortcutTooltip: true
UiShortcutToolTip.text: qsTranslate("QObject", "Back")
UiShortcutToolTip.shortcutMap: [{ value: "go_back" }]
onClicked: plrBackend.onBackClicked()
}
UiImageButton {
width: pSidebarElementSize
height: pSidebarElementSize

BsiSidebarInlineVisual {
id: bsiSidebarSkin6
anchors.fill: parent
elementId: "home"
assetName: (
"SidebarHome"
)
hovered: iHomeBtn.hovered
pressed: ("pressed" in iHomeBtn && typeof iHomeBtn.pressed === "boolean") ? iHomeBtn.pressed : false
available: iHomeBtn.enabled
active: /Active/.test(bsiSidebarSkin6.assetName)
animating: false
}

id: iHomeBtn
pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
enabled: iAppwin.pIsBootComplete || iAppwin.pShowAndroidAd
asset: bsiSidebarSkin6.known ? "" : (
"SidebarHome"
)
pUseShortcutTooltip: true
UiShortcutToolTip.text: qsTranslate("QObject", "Home")
UiShortcutToolTip.shortcutMap: [{ value: "go_home" }]
onClicked: plrBackend.onHomeClicked()
}
UiImageButton {
width: pSidebarElementSize
height: pSidebarElementSize

BsiSidebarInlineVisual {
id: bsiSidebarSkin7
anchors.fill: parent
elementId: "recent"
assetName: (
"SidebarRecents"
)
hovered: iRecentBtn.hovered
pressed: ("pressed" in iRecentBtn && typeof iRecentBtn.pressed === "boolean") ? iRecentBtn.pressed : false
available: iRecentBtn.enabled
active: /Active/.test(bsiSidebarSkin7.assetName)
animating: false
}

id: iRecentBtn
pImageHeight: pSidebarElementSize
pImageWidth: pSidebarElementSize
Layout.alignment: Qt.AlignHCenter | Qt.AlignBottom
enabled: iAppwin.pIsBootComplete || iAppwin.pShowAndroidAd
asset: bsiSidebarSkin7.known ? "" : (
"SidebarRecents"
)
pUseShortcutTooltip: true
UiShortcutToolTip.text: qsTranslate("QObject", "Recent apps")
UiShortcutToolTip.shortcutMap: [{ value: "open_recent_apps" }]
onClicked: plrBackend.onRecentAppsClicked()
}
}
function fGetFullScreenBtbEnabled() {
return iSidebarProperties.pIsFullScreenEnabled
}
function fGetVolumeBtnEnabled() {
return iSidebarProperties.pIsVolumeEnabled
}
function fGetLockMouseCursorBtnEnabled() {
return iSidebarProperties.pIsLockMouseCurosrEnabled
}
function fGetGameControlBtnEnabled() {
return iSidebarProperties.pIsGameControlEnabled
}
function fGetSyncBtnEnabled() {
return iSidebarProperties.pIsSyncEnabled && pIsSyncEnable
}
function fGetTrimMemoryBtnEnabled() {
return iSidebarProperties.pIsTrimMemoryEnabled
}
function fGetInstallApkBtnEnabled() {
return iSidebarProperties.pIsInstallApkEnabled
}
function fGetScreenshotBtnEnabled() {
return iSidebarProperties.pIsScreenshotEnabled
}
function fGetOpenMediaFolderBtnEnabled() {
return iSidebarProperties.pIsOpenMediaFolderEnabled
}
function fGetRotateBtnEnabled() {
return iSidebarProperties.pIsRotateEnabled
}
function fGetShakeBtnEnabled() {
return iSidebarProperties.pIsShakeEnabled
}
function fGetLocationBtnEnabled() {
return iSidebarProperties.pIsSetLocationEnabled
}
function fGetMimBtnEnabled() {
        // console.info("fGetMimBtnEnabled"+iSidebarProperties.pIsMimEnabled)
return iSidebarProperties.pIsMimEnabled
}
function fGetEcoModeBtnEnabled() {
return iSidebarProperties.pIsEcoModeEnabled
}
function fGetMacroBtnEnabled() {
        //console.info("fGetMacroBtnEnabled"+iSidebarProperties.pIsMimEnabled)
return pIsMacroEnabled && iSidebarProperties.pIsMacroEnabled
}
function fUpdateMouseCursorImage(mouseStateLocked) {
pMouseCursorLocked = mouseStateLocked
}
function fUpdateMacroBtnEnabled(enable) {
pIsMacroEnabled = enable
}
function fTrimMemoryClick() {
pRunTrimAnimation = true
plrBackend.onTrimMemoryClicked()
}
function fHideTrimMemoryLoader() {
pRunTrimAnimation = false
}
function fHandleInstallApkBtnClicked() {
iFileDialogComponent.createObject().open()
}
function fHandleScreenshotBtnClicked() {
var mediaFolder = uiBackend.confGetString("bst.media_folder")
if (mediaFolder === "") {
mediaFolder = plrBackend.getMediaFolder()
pScreenShotNoticeText = mediaFolder
pMediaPrompt = iSelectMediaPrompt.createObject(iAppwin, { "transientParent": iAppwin })
pMediaPrompt.show()
} else { plrBackend.takeScreenshot(mediaFolder) }
}
function fOpenVolumePopup(startTimer) {
if (startTimer)
fStartVolumePopupTimer()
if (pVolumeWin != null)
{
if (pVolumeWin.visible)
pVolumeWin.closePopup()
return
}
pVolumeWin = iVolumePopupComponent.createObject(iAppwin)
pVolumeWin.x = iSidebar.mapToGlobal(0, 0).x - pVolumeWin.width + pVolumeWin.shadowThickness
pVolumeWin.y = fGetPopupTopPoint(pVolume) - pVolumeWin.shadowThickness
pVolumeWin.show()
}
function fOpenEcoModePopup() {
if (pEcoModeWin != null)
{
if (pEcoModeWin.visible)
pEcoModeWin.close()
return
}
pEcoModeWin = iEcoModePopupComponent.createObject(iAppwin)
pEcoModeWin.x = iSidebar.mapToGlobal(0, 0).x - pEcoModeWin.width + pEcoModeWin.shadowThickness
pEcoModeWin.y = fGetPopupTopPoint(pEcoMode) - pEcoModeWin.shadowThickness
pEcoModeWin.show()
}
function fOpenImapAbortPopup(visible, popup) {
iAppwin.pIsImapAbort = visible
fResetSidebar()
if (!popup) {
return;
}
var applicableIndex = -1
var isIconFound = false
for (var i = 0; i < pSidebarItems.length; i++) {
applicableIndex += 1
if(pSidebarItems[i].pElementId === pImapAbort && applicableIndex <= pVisibleSidebarElementsCount-1) {
isIconFound = true
break
}
}
if(!isIconFound) {
return
}
if (iAppwin.visibility != ApplicationWindow.Minimized || pImapAbortWin != null) {
pImapAbortWin = iImapAbortPopupComponent.createObject(iAppwin, {
                "transientParent": iAppwin,
                "x": Qt.binding(function() { return iAppwin.x + iAppwin.pBorderWidth + iSidebar.x - this.width + pSidebarElementSize + this.shadowThickness }),
                "y": Qt.binding(function() { return iAppwin.y + iAppwin.pBorderWidth + iSidebar.y + (applicableIndex * pSidebarElementSize) - this.shadowThickness })
})
pImapAbortWin.show()
uiBackend.confSetBool("bst.imap_abort_shown", true)
uiBackend.confCommit()
}
}
function fOpenGameControl(mouse) {
if(mouse.button === Qt.RightButton)
fCheckSplitModeAndOpenEditorWindow(false)
else
fOpenGameControlsPopup()
}
function fOpenGameControlsPopup() {
kmmBackend.kmmPopupStart()
if (pGameControlsWin != null)
{
if (pGameControlsWin.visible)
pGameControlsWin.close()
return
}
pGameControlsWin = iGameControlsPopupComponent.createObject(iAppwin)
        //This is a variant list with index of the selected scheme
        //stored at 0th location and scheme list at 1st.
var schemeInfo = kmmBackend.popupSchemes
pGameControlsWin.schemeModel = schemeInfo[1]
pGameControlsWin.schemeIdx = schemeInfo[0]
pGameControlsSensitivityEnabled = kmmBackend.hasPanControl()
pGamepadControlsPresent = kmmBackend.hasGamepadControls
pGameControlsGamepadSelected = kmmBackend.gamepadControlsSelected
if(pGameControlsSensitivityEnabled)
pGameControlsWin.updateSenstivity()
pGameControlsWin.x = iSidebar.mapToGlobal(0, 0).x - pGameControlsWin.width + pGameControlsWin.shadowThickness
pGameControlsWin.y = fGetPopupTopPoint(pGameControl) - pGameControlsWin.shadowThickness
pGameControlsPopupLoaded = true
pGameControlsWin.show()
}
function fCheckSplitModeAndOpenEditorWindow(isDevModeEnabled = false) {
if (pKmmEditorWindow != null) return
kmmBackend.devModeEnabled = isDevModeEnabled
if(uiBackend.confGetInstanceBool("split_ad_enabled")) {
plrBackend.temporarilyToggleSplitAds(false, true)
}
else {
fOpenControlsEditorWindow(isDevModeEnabled)
}
}
function fOpenControlsEditorWindow(isDevModeEnabled = false) {
if (pKmmEditorWindow != null) return
kmmBackend.devModeEnabled = isDevModeEnabled
var component = Qt.createComponent("KmmEditorWindow.qml")
if (component.status === Component.Ready) {
pKmmEditorWindow = component.createObject(iAppwin)
if (pKmmEditorWindow != null) {
adsBackend.editorWindowVisible = true
kmmBackend.kmmEditorStart()
var schemeInfo = kmmBackend.editorSchemes
pKmmEditorWindow.schemeModel = schemeInfo[1]
pKmmEditorWindow.schemeIdx = schemeInfo[0]
pKmmEditorWindow.pBuiltinIndex = schemeInfo[2]
pKmmEditorWindow.width = iAppwin.width - iAppwin.pSidebarWidth + pKmmEditorWindow.pKmmEditorPanelWidth
pKmmEditorWindow.height = iAppwin.height
pKmmEditorWindow.x = iAppwin.x
pKmmEditorWindow.y = iAppwin.y
pKmmEditorWindow.show()
if(kmmBackend.gamepadTabSelected && !uiBackend.confGetBool("bst.gamepad_editing_onboarding_done", false)) {
pKmmEditorWindow.fOpenGamepadInfoPopup()
uiBackend.confSetBool("bst.gamepad_editing_onboarding_done", true)
uiBackend.confCommit()
}
}
}
}
function fResizeControlsEditorWindow() {
if (pKmmEditorWindow == null) return
kmmBackend.kmmEditorStartAdjust()
pKmmEditorWindow.width = iAppwin.width - iAppwin.pSidebarWidth + pKmmEditorWindow.pKmmEditorPanelWidth
pKmmEditorWindow.height = iAppwin.height
pKmmEditorWindow.x = iAppwin.x
pKmmEditorWindow.y = iAppwin.y
pKmmEditorWindow.show()
}
function fToggleGameControlsState(isEnabled) {
pGameControlsEnabled = isEnabled
kmmBackend.toggleOverlayVisibility(isEnabled ? pIsGameControlsOverlayEnabled : isEnabled, false)
uiBackend.confSetInstanceBool("game_controls_enabled", pGameControlsEnabled)
uiBackend.confCommit()
}
function fToggleGameControlsVisibility(isVisible) {
pIsGameControlsOverlayEnabled = isVisible
}
function fToggleGamepadControlsState(isSelected) {
pGameControlsGamepadSelected = isSelected
kmmBackend.gamepadControlsSelected = isSelected
}
function fGetPopupTopPoint(elementId) {
var applicableIndex = -1
for (var i = 0; i < pSidebarItems.length; i++) {
applicableIndex += 1
if(pSidebarItems[i].pElementId === elementId && applicableIndex <= pVisibleSidebarElementsCount-1) {
return iSidebar.mapToGlobal(0, 0).y + (applicableIndex * pSidebarElementSize)
}
}
return iKebabMenuBtn.mapToGlobal(0, 0).y
}
function fVolumeChangedCb(isMute, vol) {
if (isMute !== pIsMuted)
pIsMuted = isMute
if (vol !== pCurrentVolumeLevel)
pCurrentVolumeLevel = vol
pIsEcoModeSoundEnabled = pIsEcoModeEnabled ? !isMute : false
}
function fInitVolume(isMute, vol, maxVol) {
pIsMuted = isMute
pCurrentVolumeLevel = vol
pMaxVolumeLevel = maxVol
}
function fRequestMuteUnmute() {
pIsMuted = !pIsMuted
plrBackend.requestSetVolume(pIsMuted, pCurrentVolumeLevel, pMaxVolumeLevel)
}
function fVolumeSliderChanged(vol) {
plrBackend.requestSetVolume(false, vol, pMaxVolumeLevel)
}
function fCloseAllPopupsOnOrientationChange() {
fCloseVolumePopup();
if (pGameControlsWin != null && pGameControlsWin.visible)
pGameControlsWin.close()
if (pEcoModeWin != null && pEcoModeWin.visible)
pEcoModeWin.close()
if (pKebabMenuWin != null && pKebabMenuWin.visible)
pKebabMenuWin.close()
}
function fOpenRotatePopUp() {
if (pRotateControlsWin != null)
{
if (pRotateControlsWin.visible)
pRotateControlsWin.close()
return
}
pRotateControlsWin = iRotatePopupComponent.createObject(iAppwin)
pRotateControlsWin.x = iSidebar.mapToGlobal(0, 0).x - pRotateControlsWin.width + pRotateControlsWin.shadowThickness
pRotateControlsWin.y = fGetPopupTopPoint(pRotate) - pRotateControlsWin.shadowThickness
pRotateControlsWin.show()
}
function fHandleRotateButtonClicked(mode) {
if(pRotateControlsWin != null)
{
if (pRotateControlsWin.visible)
pRotateControlsWin.close()
}
pGameOrientationText = mode
if (mode === "Landscape" || mode === "Portrait") {
pRestartGamePrompt = iRestartGamePrompt.createObject(iAppwin, { "transientParent": iAppwin })
pRestartGamePrompt.show()
} else {
plrBackend.onRotateClicked()
}
}
function fToggleAirplaneModeSetting(enable) {
pIsAirplaneModeOn = enable
}
function fShowFullScreenBlurbPrompt() {
var applicableIndex = -1
var isIconFound = false
for (var i = 0; i < pSidebarItems.length; i++) {
applicableIndex += 1
if(pSidebarItems[i].pElementId === pFullscreen && applicableIndex <= pVisibleSidebarElementsCount-1) {
isIconFound = true
break
}
}
if(!isIconFound) {
plrBackend.showGameControlsBlurb()
return
}
if (iAppwin.visibility != ApplicationWindow.Minimized || pFullScreenBlurbWin != null) {
pFullScreenBlurbWin = iFullScreenBlurbPrompt.createObject(iAppwin, {
                "transientParent": iAppwin,
                "x": Qt.binding(function() { return iAppwin.x + iAppwin.pBorderWidth + iSidebar.x - this.width + pSidebarElementSize + this.shadowThickness }),
                "y": Qt.binding(function() { return iAppwin.y + iAppwin.pBorderWidth + iSidebar.y + (applicableIndex * pSidebarElementSize) - this.shadowThickness })
})
pFullScreenBlurbWin.show()
uiBackend.confSetBool("bst.fullscreen_blurb_shown", true)
uiBackend.confCommit()
}
}
function fShowGameControlsBlurbPrompt() {
var applicableIndex = -1
var isIconFound = false
for (var i = 0; i < pSidebarItems.length; i++) {
applicableIndex += 1
if(pSidebarItems[i].pElementId === pGameControl && applicableIndex <= pVisibleSidebarElementsCount-1) {
isIconFound = true
break
}
}
if(!isIconFound)
return
if (iAppwin.visibility != ApplicationWindow.Minimized || pGameControlBlurbWin != null) {
plrBackend.pGameControlsBlurbVisible = true
pGameControlBlurbWin = iGameControlBlurbPrompt.createObject(iAppwin, {
                "transientParent": iAppwin,
                "x": Qt.binding(function() { return iAppwin.x + iAppwin.pBorderWidth + iSidebar.x - this.width + pSidebarElementSize + this.shadowThickness }),
                "y": Qt.binding(function() { return iAppwin.y + iAppwin.pBorderWidth + iSidebar.y + (applicableIndex * pSidebarElementSize) - this.shadowThickness })
})
pGameControlBlurbWin.show()
}
}
function fCloseIntroductionBlurbs() {
if (pFullScreenBlurbWin != null && pFullScreenBlurbWin.visible)
pFullScreenBlurbWin.close()
if (pGameControlBlurbWin != null && pGameControlBlurbWin.visible)
pGameControlBlurbWin.close()
}
function fToggleScreenRecordingStatus(started) {
if(started && pRecordingSavedWin != null)
pRecordingSavedWin.close()
if (nowggBackend.pRecordMode === RecordMode.MOMENT) {
fShowMomentsNotificationWindow(qsTranslate("QObject", "Moments Mode is now activated. Press “%1” to save last 60 sec of your gameplay").arg(uiBackend.confGetString("bst.shortcut.record_moment")))
}
else {
fShowScreenRecordingToast(nowggBackend.pIsScreenRecorderOn)
}
}
function fShowScreenRecordingToast(screenRecordingStarted) {
var message = screenRecordingStarted ? qsTranslate("QObject", "Recording Started") : qsTranslate("QObject", "Recording Stopped")
var toast = iToastComponent.createObject(iAppwin, { "pToastMessage": message,
            "pBackground.border.color": UiTheme.colors.primary50,
            "pBackground.color": UiTheme.colors.overlayPrimary,
            "pBackground.radius": 0,
            "pBackground.opacity": 0.85,
            "pBackground.leftPadding": 40,
            "pBackground.rightPadding": 40,
            "pBackground.topPadding": 12,
            "pBackground.bottomPadding": 12,
            "pToastTimer.interval": 3000})
toast.fShowMessage()
}
function fShowToast(strMsg) {
var message = qsTranslate("QObject", strMsg)
var toast = iToastComponent.createObject(iAppwin, { "pToastMessage": message,
            "color": UiTheme.colors.overlayPrimary,
            "pBackground.border.color": UiTheme.colors.primary50,
            "pBackground.color": UiTheme.colors.overlayPrimary,
            "pBackground.radius": 0,
            "pBackground.opacity": 0.85,
            "pBackground.leftPadding": 40,
            "pBackground.rightPadding": 40,
            "pBackground.topPadding": 12,
            "pBackground.bottomPadding": 12,
            "pToastTimer.interval": 3000})
toast.fShowMessage()
}
function fOpenScreenRecordPopup() {
if (pScreenRecordWin != null)
{
if (pScreenRecordWin.visible)
pScreenRecordWin.close()
return
}
pScreenRecordWin = iScreenRecordComponent.createObject(iAppwin)
pScreenRecordWin.x = iSidebar.mapToGlobal(0, 0).x - pScreenRecordWin.width + pScreenRecordWin.shadowThickness
pScreenRecordWin.y = fGetPopupTopPoint(pScreenRecord) - pScreenRecordWin.shadowThickness
pScreenRecordWin.show()
}
function fShowMomentRunningToast(msg) {
var toast = iToastComponent.createObject(iAppwin, { "pToastMessage": msg,
            "color": UiTheme.colors.overlayPrimary,
            "pBackground.border.color": UiTheme.colors.dangerDark,
            "pBackground.color": UiTheme.colors.dangerTranslucent,
            "pBackground.radius": 0,
            "pBackground.opacity": 0.8,
            "pBackground.leftPadding": 40,
            "pBackground.rightPadding": 40,
            "pBackground.topPadding": 12,
            "pBackground.bottomPadding": 12,
            "pToastTimer.interval": 2000})
toast.fShowMessage({verticalOffsetRatio:0.76})
}
function fToggleMediaLoaderState(state, popupMsg) {
pMediaLoaderRunning = state
if (state === false && popupMsg !== "") {
fShowMediaSavedPopup(popupMsg)
}
}
function fShowMediaSavedPopup(popupMsg) {
if(pRecordingSavedWin != null || iAppwin.visibility === ApplicationWindow.FullScreen)
return
pRecordingSavedWin = iScreenRecorderPopupComponent.createObject(iAppwin, {"popupMsg": popupMsg})
pRecordingSavedWin.x = iSidebar.mapToGlobal(0, 0).x - pRecordingSavedWin.width + pRecordingSavedWin.shadowThickness
pRecordingSavedWin.y = fGetPopupTopPoint(pMediaFolder) - pRecordingSavedWin.shadowThickness - 6
pRecordingSavedWin.show()
}
function fHandleScreenRecordingShortcut() {
nowggBackend.startRecording(!nowggBackend.pIsScreenRecorderOn)
}
function fCheckIfMomentsNotificationIsVisible() {
return nowggBackend.pRecordMode === RecordMode.MOMENT
}
function fHandleScreenStreamingClicked(start, isAlreadyActive) {
var jsonString = '{ "stream_active" : ' + (lsgBackend.pSidebarScreenStreamActive ? 'true' : 'false') + ' }'
lsgBackend.sendLiveStreamStats("side_bar_clicked", jsonString)
if(lsgBackend.pSidebarScreenStreamActive) {
fShowChatRoomWindow()
return
}
if(start)
fShowStreamingOnboardingMsg()
else
lsgBackend.startStreaming(false, "")
}
function fCloseGameControlsBlurb() {
if (pGameControlBlurbWin != null && pGameControlBlurbWin.visible) {
pGameControlBlurbWin.close()
}
}
}
