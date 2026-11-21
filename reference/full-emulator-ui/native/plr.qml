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
import DialogButtonModel 1.0
import DialogButtonModelList 1.0
import UiDialogWindow 1.0
import SettingElementModel 1.0
import SettingElementModelList 1.0
import UiLaunchAppWindow 1.0
import UiOverlayControl 1.0
import BannerAdUiModel 1.0
import BannerAdUi 1.0
import UiQuickWindow 1.0
import UiToolTipControl 1.0

ApplicationWindow {property bool pBootVideoDone:false;Timer{id:bvDone;interval:3000;running:pIsBootComplete&&!pBootVideoDone;repeat:false;onTriggered:pBootVideoDone=true}
    id: iAppwin
    x: 10
    y: 10
    width: 960
    height: 540
    flags: Qt.Window | Qt.FramelessWindowHint
    color: uiBackend.oem === "msi5" ? UiTheme.colors.overLayTint : UiTheme.colors.primary60

    property bool pCompLoaded: false
    property int pBorderWidth: 1
    property int pSidebarWidth: 32
    property bool pIsEcoModeEnabled: false
    property bool pSidebarVisibleBeforeFullscreen: true
    property bool pIsMacroRunning: false
    property bool pIsMacroPause: false
    property bool pIsRawMode: false
    property bool pIsBootComplete: false
    property bool pShowAndroidAd: false
    property string pArticleKey: ""
    property bool pIsTroubleSigningInBtnShown: false
    property bool pIsQuestNotificationOpen: false
    property bool pIsImapAbort: false

    property UiSettingsWindow pSettingsWindow: null
    property UiLoadingScreen pLoadingScreenControl: null
    property PlrFullScreenPopup pFullScreenPopupControl: null
    property Window pMacroManagerWindow: null
    property Window pSyncOperationsWindow: null
    property UiLaunchAppWindow pFlePopupWindow: null
    property Window pNotificationBarWindow: null
    property Window pScreenTranslateWindow: null
    property UiQuickWindow pLaunchBsxWindow: null
    property UiQuickWindow pMediaGalleryWindow: null
    property UiMessageDialog pGamepadDetectedDialog: null
    property UiMessageDialog pGameControlsUpdatedDialog: null
    property UiMessageDialog pMultipleCamerasDetectedDialog: null
    property UiMessageDialog pChargerWarningPopupWindow: null
    property UiMessageDialog pSigninSuccessfulPopupWindow: null
    property UiMessageDialog pDisableMomentsPopupWindow: null
    property UiMessageDialog pDisableRecordingPopupWindow: null
    property UiMessageDialog pCheckBrowserPopupWindow: null
    property UiMessageDialog pLowDiskSpacePopupWindow: null
    property UiMessageDialog pSignOutPopupWindow: null
    property UiMessageDialog pSignOutConfirmationPopupWindow: null
    property UiWindowedPopup pTroubleSigningInBtn: null
    property UiWindowedPopup pSignInScrollBtn: null
    property Window pBrowserWindow: null
    property Window pBlueStacksStoreWindow: null
    property PlrExitFullScreenPopup pExitFullScreenControl: null
    property Window pSigninRequiredWindow: null
    property Window pLinkNowggAccountWindow: null
    property Window pInterstitialAdsInfoWindow: null
    property UiQuickWindow pNowbuxSigninSuccessfulPopupWindow: null
    property UiQuickWindow pNowggSigninpopupWindow: null
    property Window pSplitScreenOverlayWindow: null
    property Window pStartLiveStreamingWindow: null
    property Window pGameRoomChatWindow: null
    property Window pBrowserWin: null
    property UiDialogWindow pLiveStreamInfoWindow: null
    property UiDialogWindow pStreamMsgWindow: null
    property UiDialogWindow pLiveStreamOnboardingWindow: null
    property UiDialogWindow pLiveStreamExploreWindow: null

    property real pBootStartTime: new Date().getTime()

    // Enabling pinToTop on maximize breaks the restore functionality
    // Saving app geometry for restoring
    property int pX: 0
    property int pY: 0
    property int pWidth: 0
    property int pHeight: 0

    property bool pIsShowMaximizedCalled: false
    property int pPreviousWindowState: 2
    property int pWindowStateBeforeMinimized: -1

    property int pWindowStateBeforeBrowserPopup: 2

    onVisibilityChanged: function(visibility) {
        if (visibility === ApplicationWindow.Hidden)
            return

        plrBackend.setWindowMinimizedState(visibility === ApplicationWindow.Minimized)

        //setting variables required to set the correct windowState when restored from minimized windowState
        if (visibility === ApplicationWindow.Minimized)
            pWindowStateBeforeMinimized = pPreviousWindowState
        else
            pPreviousWindowState = visibility

        //handling for window automatically going to fullscreen on maximizing when window geometry is equal to screen geometry
        if (visibility === ApplicationWindow.FullScreen && pIsShowMaximizedCalled) {
            iAppwin.visibility = ApplicationWindow.Maximized
            pIsShowMaximizedCalled = false
        }

        //handling to set the correct windowState when restored from minimized state
        if ((pWindowStateBeforeMinimized == ApplicationWindow.Maximized || pWindowStateBeforeMinimized == ApplicationWindow.FullScreen)
                && (visibility === ApplicationWindow.Windowed || visibility === ApplicationWindow.FullScreen)) {
            pWindowStateBeforeMinimized = -1
            pIsShowMaximizedCalled = false
            if (pFullScreenPopupControl != null)
                pFullScreenPopupControl.fClosePopup()
            if (pExitFullScreenControl != null)
                pExitFullScreenControl.fClosePopup()
            iAppwin.showMaximized()
            pIsShowMaximizedCalled = true
        }
    }

    onClosing: function(close) {
        console.info("plr onClosing signal called")
        if(uiBackend.isOsShuttingDown())
        {
            plrBackend.onCloseCalled("OS_shutting_down");
        }
        else
        {
            close.accepted = false
            iTopbar.fShowClosePlrMsg()
        }
    }

    Component.onCompleted: {
        if (!pCompLoaded)
            fSetUiParams(iSidebar.visible)

        pCompLoaded = true
    }

    Connections {
        target: pSettingsWindow

        function onShowRestartLoader() {
            pLoadingScreenControl = iLoadingScreen.createObject(iAppwin)
            pLoadingScreenControl.show()
        }

        function onGamepadDetectionEnabled() {
            fGamepadDetectionMessage()
        }
    }

    Connections {
        target: pSyncOperationsWindow

        function onClosing() {
            pSyncOperationsWindow.destroy()
        }
    }

    Connections {
        target: pScreenTranslateWindow

        function onClosing() {
            pScreenTranslateWindow.destroy()
            uiBackend.pIsShortcutsEnabled = true
        }
    }
    Connections {
        target: lsgBackend
        function onTiktokLsgConnectionReady(success) {
            if(success)
                lsgBackend.startTiktokLsgEventListening()
            else {
                fCloseStreamingPopups()
            }
        }
    }

    Connections {
        target: pGameRoomChatWindow
        function onShowGameRoomExitWarning() {
            fShowGameRoomCloseWarningMsg()
        }
    }
    Component {
        id: iLoadingScreen
        UiLoadingScreen {
            width: iAppwin.width
            height: iAppwin.height
        }
    }

    Component {
        id: iToastComponent
        UiToastPopup { }
    }

    Component {
        id:iFullScreenPopupControlComponent
        PlrFullScreenPopup { }
    }

    Component {
        id:iExitFullScreenPopupControlComponent
        PlrExitFullScreenPopup { }
    }

    Component {
        id: iBootProgressComponent
        PlrBootProgressBar {}
    }

    Component {
        id: iFeedbackPillComponent

        UiWindowedPopup {
            id: iFeedbackPillWindow
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
            x: iAppwin.x + plrBackend.inputWinOffsets[0] + 32
            y: iAppwin.y + plrBackend.inputWinOffsets[1] + plrBackend.inputWinOffsets[3] - contentHeight - 32

            visible: iAppwin.height > 260
            contentWidth: iTroubleSigningInBtn.implicitWidth
            contentHeight: iTroubleSigningInBtn.implicitHeight
            closeOnPressOutside: false
            shadowVisibility: false
            UiRowLayout {
                id: iTroubleSigningInBtn
                spacing: 8
                topPadding: 12
                bottomPadding: 12
                leftPadding: 16
                rightPadding: 16
                radius: 24
                color: pMouseArea.containsMouse ? UiTheme.colors.dangerHover : UiTheme.colors.dangerNormal

                pMouseArea {
                    anchors.fill: iTroubleSigningInBtn
                    enabled: true
                    hoverEnabled: true
                    cursorShape: iTroubleSigningInBtn.pMouseArea.containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked:{
                        uiBackend.openHelpArticle("trouble_google_play_sign_in")
                        plrBackend.sendTroubleSigningInBtnStats("trouble_signing_in_clicked")
                    }
                }
                UiImage {
                    asset: "HelpGoogleSignIn"
                    pImageWidth: 24
                    pImageHeight: 24
                }
                Text {
                    text: qsTranslate("QObject", "Trouble signing in?")
                    font: UiTheme.fonts.titleSmall
                    color: UiTheme.colors.primary10
                    visible: iAppwin.width > 360 && iAppwin.height > 360
                }
            }
        }
    }

    Component {
        id: iSignInScrollBtnComponent

        UiWindowedPopup {
            id: iSignInScrollBtnWindow
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
            x: iAppwin.x + plrBackend.inputWinOffsets[0] + (7 / 8 * plrBackend.inputWinOffsets[2]) - (contentWidth / 2)
            y: iAppwin.y + plrBackend.inputWinOffsets[1] + plrBackend.inputWinOffsets[3] - contentHeight - 32

            visible: iAppwin.height > 260
            contentWidth: 48
            contentHeight: 48
            closeOnPressOutside: false
            shadowVisibility: false
            UiRowLayout {
                id: iSignInScrollBtn
                anchors.fill: parent
                radius: 32
                color: UiTheme.colors.secondaryHover

                pMouseArea {
                    anchors.fill: iSignInScrollBtn
                    enabled: true
                    hoverEnabled: true
                    cursorShape: iSignInScrollBtn.pMouseArea.containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked:{
                        plrBackend.handleMouseScrollOnSignIn()
                    }
                }
                UiImage {
                    Layout.alignment: Qt.AlignCenter
                    asset: "DownPointingArrow"
                    pImageWidth: 24
                    pImageHeight: 24
                }
            }
        }
    }

    Component {
        id: iSplitScreenOverlayComponent
        Window {
            id: iSplitScreenOverlayPopup
            x: iAppwin.x + plrBackend.plrWinOffsets[0]
            y: iAppwin.y + plrBackend.plrWinOffsets[2]
            width: iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1]
            height: iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3]
            color: "#000000"
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowTransparentForInput
            screen: transientParent.screen
            opacity: 1
            visible: false

            Connections {
                target: plrBackend
                function onPlrWinPinToTopChanged(enabled) {
                    if (enabled)
                        iSplitScreenOverlayPopup.flags = iSplitScreenOverlayPopup.flags | Qt.WindowStaysOnTopHint
                    else
                        iSplitScreenOverlayPopup.flags = iSplitScreenOverlayPopup.flags & ~Qt.WindowStaysOnTopHint
                }
            }

            onVisibleChanged: {
                if(visible)
                    fadeInAnimation.start()
                else
                    fadeInAnimation.stop()
            }

            Component.onCompleted: {
                if(iTopbar.pCurrentTopmostState)
                    iSplitScreenOverlayPopup.flags = iSplitScreenOverlayPopup.flags | Qt.WindowStaysOnTopHint
            }

            onClosing: {
                fadeInAnimation.stop()
                iSplitScreenOverlayPopup.destroy()
            }

            SequentialAnimation {
                id: fadeInAnimation
                PropertyAnimation {
                    target: iSplitScreenOverlayPopup
                    property: "opacity"
                    from: 1
                    to: 0
                    easing.type: Easing.OutCubic
                    duration: 1500
                }
                onFinished: {
                    iSplitScreenOverlayPopup.hide()
                }
            }
        }
    }

    Component {
        id: iInterstitialAdsInfoComponent
        Window {
            id: iInterstitialAdsInfoPopup
            x: iAppwin.x + iAppwin.width - plrBackend.plrWinOffsets[1] - iInterstitialAdsInfoPopup.width
            y: iAppwin.y + plrBackend.plrWinOffsets[2] + iAppwin.height*0.1
            width: iInterstitialAdsInfoPopupLayout.implicitWidth
            height: iInterstitialAdsInfoPopupLayout.implicitHeight
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            screen: transientParent.screen
            visible: iAppwin.height > 260

            Connections {
                target: plrBackend
                function onPlrWinPinToTopChanged(enabled) {
                    if (enabled)
                        iInterstitialAdsInfoPopup.flags = iInterstitialAdsInfoPopup.flags | Qt.WindowStaysOnTopHint
                    else
                        iInterstitialAdsInfoPopup.flags = iInterstitialAdsInfoPopup.flags & ~Qt.WindowStaysOnTopHint
                }
            }

            Component.onCompleted: {
                if(iTopbar.pCurrentTopmostState)
                    iInterstitialAdsInfoPopup.flags = iInterstitialAdsInfoPopup.flags | Qt.WindowStaysOnTopHint
            }

            onClosing: {
                iInterstitialAdsInfoPopup.destroy()
            }

            UiRowLayout {
                id: iInterstitialAdsInfoPopupLayout
                spacing: 12
                leftPadding: 16
                rightPadding: 16
                topPadding: 10
                bottomPadding: 10
                color: UiTheme.colors.overLayBlack

                pMouseArea {
                    anchors.fill: iInterstitialAdsInfoPopupLayout
                    enabled: true
                    onClicked:{
                        fShowAdsInfoPrompt(false)
                        plrBackend.sendAndroidAdsStats("android_interstitial_ad_info_click")
                    }
                }

                Text {
                    text: qsTranslate("QObject", "App will start after ad")
                    font: UiTheme.fonts.bodyLarge
                    color: UiTheme.colors.primary10
                }
                UiImage {
                    asset: "MenuInfo"
                    pImageHeight: 16
                    pImageWidth: 16
                }
            }
        }
    }

    Component {
        id: iGlContainerComponent
        Rectangle {
            color: "#000000"
        }
    }

    Component {
        id: iSettingsItems
        SettingElementModelList {
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "Performance");     pQmlSource: "UiPerformanceSetting.qml"
                pComponentArgs: { "pVmName": plrBackend.getVmName(), "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "Display");         pQmlSource:"UiDisplaySetting.qml"
                pComponentArgs:{ "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "Graphics");        pQmlSource:"UiGraphicsSetting.qml"
                pComponentArgs:{ "pVmName": plrBackend.getVmName(), "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonId: "gameSettings";                             pButtonText: qsTranslate("QObject", "Game settings");
                pQmlSource:"PlrGameSetting.qml";                       pComponentArgs:{ "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "Devices");           pQmlSource: "PlrAudioSetting.qml"
                pComponentArgs:{ "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "Gamepad");         pQmlSource: "PlrGamepadSetting.qml"
                pComponentArgs:{ "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "Preferences");     pQmlSource:"PlrPreferencesSetting.qml"
                pComponentArgs:{ "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "Phone"); pQmlSource: "PlrDeviceSetting.qml"
                pComponentArgs:{ "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonId: "shortcuts";                                 pButtonText: qsTranslate("QObject", "Shortcuts");
                pQmlSource:"PlrShortcutsSetting.qml";                   pComponentArgs:{ "pParentWindow": "plr"}
            }
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "User data");
                pQmlSource:"PlrUserDataSetting.qml";                    pComponentArgs:{ "pParentWindow": "plr"}
                pShowRedDot: plrBackend.pDiskOptimizationOnboarding
            }
            SettingElementModel {
                pButtonId: "advanceSettings";
                pButtonText: qsTranslate("QObject", "Advanced");        pQmlSource:"PlrAdvancedSetting.qml"
                pComponentArgs:{ "pParentWindow": "plr"}
                pIsVisible : uiBackend.oem !== "wmx"
            }
            SettingElementModel {
                pButtonText: qsTranslate("QObject", "About");           pQmlSource:"PlrAboutSetting.qml"
                pComponentArgs:{ "pParentWindow": "plr"}
            }
        }
    }

    Component {
        id: iRedDotComponent
        Rectangle {
            width: 6
            height: 6
            radius: 3
            color: UiTheme.colors.dangerNormal
        }
    }

    Component {
        id: iCloseGameRoomComponent
        UiMessageDialog {
            id: iCloseGameRoomMsg
            flags: Qt.Tool | Qt.FramelessWindowHint
            pDialogType: UiDialogWindow.Tall
            title: qsTranslate("QObject", "End Gameroom")
            pDescription: qsTranslate("QObject", "Are you sure you want to end the stream and close the Gameroom?")
            pDialogImageSource: "GameRoomWarning"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Keep playing")
                        onClicked: {
                            iCloseGameRoomMsg.close()
                            fShowChatRoomWindow()
                        }
                    },
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.secondary
                        pText: qsTranslate("QObject", "End GameRoom")
                        onClicked: {
                            iCloseGameRoomMsg.close()
                            fCloseStreamingPopups()
                            lsgBackend.startStreaming(false, "")
                        }
                    }
                ]
            }
            onCloseClicked: {
                fShowChatRoomWindow()
            }
        }
    }

    Component {
        id: iLiveStreamOnboardingPrompt
        UiDialogWindow {
            id: iMainWin
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            modality: Qt.ApplicationModal
            width: 320 + 2 * iShadow.shadowThickness
            height: iOnboardingContentLayout.implicitHeight + 2 * iShadow.shadowThickness
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + plrBackend.plrWinOffsets[0] - (adsBackend.pExtendedBannerAdWidth > 0 ? iAdRepeater.itemAt(0).width : 0),
                                           iAppwin.y + plrBackend.plrWinOffsets[2],
                                           iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] + (adsBackend.pExtendedBannerAdWidth > 0 ? iAdRepeater.itemAt(0).width : 0),
                                           iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3])
            onClosed: pLiveStreamOnboardingWindow.destroy()

            UiShadowContainer {
                id: iShadow
                UiColumnLayout {
                    id: iOnboardingContentLayout
                    Layout.minimumWidth: 420
                    anchors.fill: parent
                    padding: 16
                    color: UiTheme.colors.primary70
                    border {
                        color: UiTheme.colors.primary50
                        width: 1
                    }
                    UiImageButton {
                        asset: "CloseIcon"
                        pImageWidth: 16
                        pImageHeight: 16
                        Layout.alignment: Qt.AlignRight
                        onClicked: {
                            if (pLiveStreamOnboardingWindow != null) {
                                lsgBackend.pIsScreenStreamingOn = false
                                pLiveStreamOnboardingWindow.close()
                            }
                        }
                    }

                    UiColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8
                        UiImage {
                            asset: "StreamOnboarding"
                            Layout.alignment: Qt.AlignCenter
                            pImageWidth: 72
                            pImageHeight: 72
                        }
                        UiText {
                            text: qsTranslate("QObject", "Play together")
                            font: UiTheme.fonts.titleMedium
                            color: UiTheme.colors.primary10
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        UiText {
                            text: qsTranslate("QObject", "Invite your friends to your live stream—play, chat, and let them control your game.")
                            font: UiTheme.fonts.bodyLarge
                            color: UiTheme.colors.primary20
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        UiColumnLayout {
                            Layout.fillHeight: true
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            padding: 12
                            border.color: "#1AFFFFFF"
                            border.width: 1
                            gradient: Gradient {
                                orientation: Gradient.Vertical
                                GradientStop { position: 0; color: "#33A85959" }
                                GradientStop { position: 1; color: "#3342233A" }
                            }
                            UiText {
                                text: qsTranslate("QObject", "How it works")
                                font: UiTheme.fonts.titleSmallest
                                color: UiTheme.colors.primary10
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Rectangle {
                                height: 1
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: "transparent" }
                                    GradientStop { position: 0.5; color: "#1AFFFFFF" }
                                    GradientStop { position: 1; color: "transparent" }
                                }
                            }
                            UiRowLayout {
                                Layout.topMargin: 8
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                spacing: 4
                                UiColumnLayout {
                                    color: UiTheme.colors.primary60
                                    Layout.minimumHeight: 20
                                    Layout.minimumWidth: 20
                                    radius: 24
                                    Text {
                                        text: qsTranslate("QObject", "1")
                                        leftPadding: 6
                                        topPadding: 3
                                        bottomPadding: 3
                                        rightPadding: 6
                                        horizontalAlignment: Text.AlignHCenter
                                        color: UiTheme.colors.primary10
                                        font: UiTheme.fonts.bodyExtraSmall
                                    }
                                }
                                UiText {
                                    text: qsTranslate("QObject", "Enter your Gameroom name")
                                    font: UiTheme.fonts.bodySmall
                                    color: UiTheme.colors.primary10
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                            UiRowLayout {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                spacing: 4
                                UiColumnLayout {
                                    color: UiTheme.colors.primary60
                                    Layout.minimumHeight: 20
                                    Layout.minimumWidth: 20
                                    radius: 24
                                    Text {
                                        text: qsTranslate("QObject", "2")
                                        leftPadding: 6
                                        topPadding: 3
                                        bottomPadding: 3
                                        rightPadding: 6
                                        horizontalAlignment: Text.AlignHCenter
                                        color: UiTheme.colors.primary10
                                        font: UiTheme.fonts.bodyExtraSmall
                                    }
                                }
                                UiText {
                                    text: qsTranslate("QObject", "Instantly start your stream")
                                    font: UiTheme.fonts.bodySmall
                                    color: UiTheme.colors.primary10
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                            UiRowLayout {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                spacing: 4
                                Layout.topMargin: 8
                                UiColumnLayout {
                                    color: UiTheme.colors.primary60
                                    Layout.minimumHeight: 20
                                    Layout.minimumWidth: 20
                                    radius: 24
                                    Text {
                                        text: qsTranslate("QObject", "3")
                                        leftPadding: 6
                                        topPadding: 3
                                        bottomPadding: 3
                                        rightPadding: 6
                                        horizontalAlignment: Text.AlignHCenter
                                        color: UiTheme.colors.primary10
                                        font: UiTheme.fonts.bodyExtraSmall
                                    }
                                }
                                UiText {
                                    text: qsTranslate("QObject", "Invite your friends")
                                    font: UiTheme.fonts.bodySmall
                                    color: UiTheme.colors.primary10
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                            UiRowLayout {
                                Layout.fillHeight: true
                                Layout.fillWidth: true
                                spacing: 4
                                Layout.topMargin: 8
                                UiColumnLayout {
                                    color: UiTheme.colors.primary60
                                    Layout.minimumHeight: 20
                                    Layout.minimumWidth: 20
                                    radius: 24
                                    Text {
                                        text: qsTranslate("QObject", "4")
                                        leftPadding: 6
                                        topPadding: 3
                                        bottomPadding: 3
                                        rightPadding: 6
                                        horizontalAlignment: Text.AlignHCenter
                                        color: UiTheme.colors.primary10
                                        font: UiTheme.fonts.bodyExtraSmall
                                    }
                                }
                                UiText {
                                    text: qsTranslate("QObject", "Play, chat, and let them control your game.")
                                    font: UiTheme.fonts.bodySmall
                                    color: UiTheme.colors.primary10
                                    wrapMode: Text.Wrap
                                    Layout.fillWidth: true
                                }
                            }
                        }
                        UiButton {
                            Layout.topMargin: 8
                            Layout.bottomMargin: 8
                            Layout.alignment: Qt.AlignCenter
                            pImageAfterText: true
                            asset: "Continue"
                            pSize: UiTheme.buttons.sizes.medium
                            pColor: UiTheme.buttons.colors.accent
                            text: qsTranslate("QObject", "Continue")
                            onClicked: {
                                if (pLiveStreamOnboardingWindow != null) {
                                    pLiveStreamOnboardingWindow.close()
                                }
                                lsgBackend.sendLiveStreamStats("continue_button_clicked","")
                                fShowGameRoomInfoWindow()
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iLiveStreamUrlPrompt
        Window {
            id: iMainWin
            color: "transparent"
            flags: Qt.Tool | Qt.FramelessWindowHint
            modality: Qt.ApplicationModal
            width: 405
            height: iContentLayout.implicitHeight
            screen: transientParent.screen

            property string streamUrl: ""
            property string streamerName: ""
            onClosing: pStartLiveStreamingWindow.destroy()
            UiColumnLayout {
                id: iContentLayout
                anchors.fill: parent
                topPadding: 32
                leftPadding: 32
                rightPadding: 32
                bottomPadding: 32
                color: UiTheme.colors.primary70
                border {
                    color: UiTheme.colors.primary50
                    width: 1
                }
                spacing: 8
                Layout.minimumHeight: 368
                UiText {
                    text: qsTranslate("QObject", "%1's Gameroom").arg(streamerName)
                    font: UiTheme.fonts.titleLarge
                    color: UiTheme.colors.primary10
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                UiText {
                    text: qsTranslate("QObject", "You’re now live streaming your game. Invite your friends to play together.")
                    font: UiTheme.fonts.bodyLarge
                    color: UiTheme.colors.primary20
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                UiTextField {
                    Layout.topMargin: 16
                    id: iUrlTextField
                    text: streamUrl
                    pSize: UiTheme.textFields.sizes.medium
                    Layout.preferredWidth: 340
                    readOnly: true
                    onTextChanged: {
                        iUrlTextField.cursorPosition = 0
                    }
                }
                UiHelpPopup {
                    id: iCopyUrlPopup
                    contentWidth: 138
                    visible: false
                    pInnerLayout {
                        border.width: 1
                        border.color: UiTheme.colors.primary40
                        color: UiTheme.colors.primary90
                        padding: 8
                    }

                    Text {
                        text: qsTranslate("QObject", "Link copied to clipboard")
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.primary10
                        verticalAlignment: Text.AlignVCenter
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }
                    Timer {
                        id: iPopupTimer
                        repeat: false
                        interval: 1000
                        running: false
                        onTriggered: {
                            iPopupTimer.stop()
                            iCopyUrlPopup.visible = false
                            iCopyUrlPopup.close()
                        }
                    }
                }
                UiButton {
                    id: iCopyUrlBtn
                    text: qsTranslate("QObject", "Copy URL")
                    pSize: UiTheme.buttons.sizes.medium
                    pColor: UiTheme.buttons.colors.tertiary
                    asset: "GameRoomCopyUrl"
                    onClicked: {
                        iCopyUrlPopup.visible = true
                        iCopyUrlPopup.x = iCopyUrlBtn.mapToGlobal(0,0).x - (iCopyUrlPopup.width - iCopyUrlBtn.width)/2
                        iCopyUrlPopup.y = iCopyUrlBtn.mapToGlobal(0,0).y - iCopyUrlPopup.height
                        lsgBackend.copyStreamingUrl(iUrlTextField.text)
                        iPopupTimer.start()
                        var jsonString = '{ "location" : "gameroom" }'
                        lsgBackend.sendLiveStreamStats("copy_url_clicked", jsonString)
                    }
                }
                Item {
                    Layout.fillHeight: true
                }
                UiButton {
                    Layout.topMargin: 32
                    text: qsTranslate("QObject", "Continue")
                    pSize: UiTheme.buttons.sizes.medium
                    pColor: UiTheme.buttons.colors.accent
                    pImageAfterText: true
                    asset: "Continue"
                    onClicked: {
                        if (pStartLiveStreamingWindow != null) {
                            pStartLiveStreamingWindow.close()
                        }
                        fGameRoomChatPublish("", "", "", iUrlTextField.text)
                    }
                }
            }
        }
    }

    Component {
        id: iShowGameRoomInfoPrompt
        UiDialogWindow {
            id: iMainWin
            color: "transparent"
            flags: Qt.Dialog | Qt.FramelessWindowHint
            modality: Qt.ApplicationModal
            x: iMainWin.transientParent.x + plrBackend.plrWinOffsets[0] + (iMainWin.transientParent.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] - iMainWin.width) / 2
            y: iMainWin.transientParent.y + plrBackend.plrWinOffsets[2] + (iMainWin.transientParent.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3] - iMainWin.height) / 2
            width: iCreateRoomBtn.implicitWidth + 32 + 2 * iShadow.shadowThickness < 320 + 2 * iShadow.shadowThickness ? 320 + 2 * iShadow.shadowThickness : iCreateRoomBtn.implicitWidth + 32 + 2 * iShadow.shadowThickness
            height: 320 + 2 * iShadow.shadowThickness
            onClosed: {
                pLiveStreamInfoWindow.destroy()
            }
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + plrBackend.plrWinOffsets[0] - (adsBackend.pExtendedBannerAdWidth > 0 ? iAdRepeater.itemAt(0).width : 0),
                                           iAppwin.y + plrBackend.plrWinOffsets[2],
                                           iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] + (adsBackend.pExtendedBannerAdWidth > 0 ? iAdRepeater.itemAt(0).width : 0),
                                           iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3])
            UiShadowContainer {
                id: iShadow
                UiColumnLayout {
                    anchors.fill: parent
                    padding: 16
                    color: UiTheme.colors.primary80
                    border {
                        color: UiTheme.colors.primary60
                        width: 1
                    }
                    UiColumnLayout {
                        id: iStreamRoomInfoLayout
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 10
                        UiImageButton {
                            asset: "CloseIcon"
                            pImageWidth: 16
                            pImageHeight: 16
                            Layout.alignment: Qt.AlignRight
                            onClicked: {
                                if (pLiveStreamInfoWindow != null) {
                                    lsgBackend.pIsScreenStreamingOn = false
                                    pLiveStreamInfoWindow.close()
                                }
                            }
                        }
                        UiImage {
                            asset: "StreamOnboarding"
                            Layout.alignment: Qt.AlignCenter
                            pImageWidth: 72
                            pImageHeight: 72
                        }
                        UiText {
                            text: qsTranslate("QObject", "Create Gameroom")
                            font: UiTheme.fonts.titleMedium
                            color: UiTheme.colors.primary10
                            horizontalAlignment: Qt.AlignHCenter
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }
                        UiText {
                            text: qsTranslate("QObject", "Enter the name your friends will see when they join your Gameroom")
                            font: UiTheme.fonts.bodyLarge
                            color: UiTheme.colors.primary20
                            horizontalAlignment: Qt.AlignHCenter
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                        }

                        UiTextField {
                            id: iStreamerNameTxtField
                            Layout.minimumWidth: 208
                            pSize: UiTheme.textFields.sizes.medium
                            placeholderText: qsTranslate("QObject", "Enter your name")
                            horizontalAlignment: Text.AlignHCenter
                            placeholderTextColor: UiTheme.colors.primary30
                            Layout.preferredWidth: iCreateRoomBtn.implicitWidth
                            Layout.alignment: Qt.AlignHCenter
                            onTextChanged: {
                                if (iStreamerNameTxtField.text.length > 25)
                                    iStreamerNameTxtField.text = iStreamerNameTxtField.text.substring(0, 25)
                            }
                        }
                        UiButton {
                            id: iCreateRoomBtn
                            Layout.minimumWidth: 208
                            text: qsTranslate("QObject", "Create Gameroom")
                            pSize: UiTheme.buttons.sizes.medium
                            pColor: UiTheme.buttons.colors.accent
                            Layout.alignment: Qt.AlignCenter
                            Layout.bottomMargin: 20
                            enabled: iStreamerNameTxtField.text != ""
                            opacity: enabled ? 1 : 0.5
                            asset: "Continue"
                            pImageAfterText: true
                            onClicked: {
                                iStreamRoomInfoLayout.visible = false
                                iLoadingGameRoomLayout.visible = true
                                lsgBackend.sendLiveStreamStats("create_game_room_button_clicked","")
                                lsgBackend.startStreaming(true, iStreamerNameTxtField.text)

                            }
                        }
                    }

                    UiColumnLayout {
                        id: iLoadingGameRoomLayout
                        Layout.fillWidth: true
                        visible: false
                        spacing: 20
                        UiImage {
                            id: iLoader
                            asset: "GameRoomLoading"
                            pImageWidth: 20
                            pImageHeight: 20
                            Layout.alignment: Qt.AlignCenter
                            Layout.topMargin: 20
                            RotationAnimation on rotation {
                                loops: Animation.Infinite
                                duration: 1000
                                from: 0
                                to: 360
                                direction: RotationAnimation.Clockwise
                                running: true
                            }
                        }
                        UiText {
                            text: qsTranslate("QObject", "Creating Gameroom")
                            font: UiTheme.fonts.bodyLarge
                            color: UiTheme.colors.primary20
                            horizontalAlignment: Qt.AlignHCenter
                            verticalAlignment: Qt.AlignVCenter
                            wrapMode: Text.Wrap
                            Layout.bottomMargin: 20
                            Layout.fillWidth: true
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iApkInstallFailurePrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Installation error")
            pDescription: qsTranslate(
                              "QObject",
                              "Unfortunately, this app could not be installed. Please verify the installation file before trying again.")
            pDialogType: UiDialogWindow.Simple
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Ok")
                        onClicked: {
                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iGamepadDetectionMessageComponent
        UiMessageDialog {
            id: iGamepadMessage
            pDialogType: UiDialogWindow.Tall
            title: qsTranslate("QObject", "Gamepad detected")
            pDescription: qsTranslate("QObject", "You can play your favorite games using gamepad on %1.").arg(uiBackend.productName)
            pHyperlink: qsTranslate("QObject", "Learn more")
            onHyperlinkClicked: uiBackend.openHelpArticle("gamepad_detection")
            pDialogImageSource: "GamepadLarge"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Okay")
                        onClicked: {
                            iGamepadMessage.close()
                        }
                    }
                ]
            }
            pCheckBoxText: qsTranslate("QObject", "Do not show again")
            onCheckBoxClicked: {
                uiBackend.confSetBool("bst.show_gamepad_detection_message", !checked)
                uiBackend.confCommit()
            }
        }
    }

    Component {
        id: iGameControlsUpdatedPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Game controls updated")
            pDescription: qsTranslate(
                              "QObject",
                              "The default game controls have been updated. If you have created your own controls, they won't be affected.")
            pDialogType: UiDialogWindow.Descriptive
            pDialogImageSource: "Info"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: pArticleKey === "" ? qsTranslate("QObject", "Okay") : qsTranslate("QObject", "Know more")
                        onClicked: {
                            if(pArticleKey !== "")
                                Qt.openUrlExternally(pArticleKey)
                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iMultipleCamerasConnectedPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Multiple cameras detected")
            pDescription: qsTranslate(
                              "QObject",
                              "You can change your default camera from Settings > Devices > Camera")
            pDialogType: UiDialogWindow.Simple
            pCloseBtnVisible: false
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Okay")
                        onClicked: close()
                    }
                ]
            }
            pCheckBoxText: qsTranslate("QObject", "Do not show this again")
            onCheckBoxClicked: {
                uiBackend.confSetBool("bst.show_camera_detection_message", !checked)
                uiBackend.confCommit()
            }
        }
    }

    Component {
        id: iChargerWarningComponent
        UiMessageDialog {
            id: iChargerWarningPopup
            pDialogType: UiDialogWindow.Graphic
            title: qsTranslate("QObject", "Connect your charger")
            pDescription: qsTranslate("QObject", "If you want to get the most out of your gaming sessions, make sure your laptop is plugged in. You'll have a smoother, more enjoyable experience.")
            pDialogImageSource: "Charger"
            pCloseBtnVisible: false
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Okay, got it")
                        onClicked: {
                            if (iChargerWarningPopup.pCheckBoxState) {
                                uiBackend.confSetBool("bst.show_charger_warning", false)
                                uiBackend.confCommit()
                            }
                            iChargerWarningPopup.close()
                        }
                    }
                ]
            }
            pCheckBoxText: qsTranslate("QObject", "Do not show this again")
            onClosed: {
                pChargerWarningPopupWindow = null
                plrBackend.releaseDisplayChargerWarningLock()
            }
        }
    }

    Component {
        id: iAdsInfoPrompt
        UiMessageDialog {
            id: iAdsInfo
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + pBorderWidth + iMainContentLayout.x,
                                           iAppwin.y + pBorderWidth + iMainContentLayout.y,
                                           iMainContentLayout.width,
                                           iMainContentLayout.height)
            pDialogType: UiDialogWindow.Graphic
            title: qsTranslate("QObject", "Ads on %1").arg(uiBackend.productName)
            pDescription: uiBackend.confGetBool("bst.feature.show_programmatic_ads_preference")
                          ? qsTranslate("QObject", "%1 stays free because of ads. Support us by keeping Ads enabled. You can choose to hide them at any time from Settings.").arg(uiBackend.productName)
                          : qsTranslate("QObject", "%1 stays free because of ads. Please support us.").arg(uiBackend.productName)
            pDialogImageSource: "AdsPopupInfoImage"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Okay")
                        onClicked: {
                            iAdsInfo.close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iAdsInfoComponent
        UiQuickWindow {
            id: iAdsInfoWindow
            color: "transparent"
            property bool isFromHtmlAds: false
            flags: Qt.Window | Qt.FramelessWindowHint
            width: 360 + 2 * shadowContainer.shadowThickness
            height: iMainContentLayout.implicitHeight + 2 * shadowContainer.shadowThickness
            modality: Qt.ApplicationModal
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + pBorderWidth + iMainContentLayout.x,
                                           iAppwin.y + pBorderWidth + iMainContentLayout.y,
                                           iAppwin.width,
                                           iAppwin.height)

            onClosed: {
                iAdsInfoWindow.destroy()
            }

            onWindowCompleted: {
                uiBackend.sendSettingStats("programmatic_ads_info_popup", "ads_free", "popup_shown", isFromHtmlAds ? "html_ads" : "android_ads")
            }

            UiShadowContainer {
                id: shadowContainer
                Rectangle {
                    clip: true
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: iMainContentLayout.implicitHeight
                    border.color: UiTheme.colors.primary60
                    border.width: 1
                    color: UiTheme.colors.primary70

                    UiColumnLayout {
                        id: iMainContentLayout
                        anchors.left: parent.left
                        anchors.right: parent.right

                        Item {
                            Layout.fillWidth: true
                            Layout.preferredHeight: iAdsImg.height
                            UiColumnLayout {
                                anchors.centerIn: parent
                                UiImage {
                                    id: iAdsImg
                                    asset: "AdsPopupInfoImage"
                                    pImageWidth: 250
                                    pImageHeight: 140
                                }
                            }
                            UiImageButton {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.rightMargin: 20
                                anchors.topMargin: 20
                                asset: "CloseIcon"
                                pImageWidth: 16
                                pImageHeight: 16
                                onClicked: {
                                    iAdsInfoWindow.close()
                                }
                            }
                        }

                        UiColumnLayout {
                            Layout.fillWidth: true
                            padding: 24

                            Text {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: adsInfoPopup.popupData.title
                                font: UiTheme.fonts.titleLarge
                                color: UiTheme.dialogs.colors.accent.title
                                wrapMode: Text.Wrap
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.topMargin: 8
                                horizontalAlignment: Text.AlignHCenter
                                text: adsInfoPopup.popupData.description
                                font: UiTheme.fonts.bodyLarge
                                color: UiTheme.colors.primary20
                                wrapMode: Text.Wrap
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: iGoPrimeLayout.height + 2
                                Layout.topMargin: 16
                                color: adsInfoPopup.popupData.prime_section.background_gradient.border_color
                                visible: adsInfoPopup.popupData.prime_section.visible

                                Rectangle {
                                    width: parent.width - 2
                                    height: iGoPrimeLayout.height
                                    anchors.centerIn: parent
                                    color: adsInfoPopup.popupData.prime_section.background_gradient.border_color
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: adsInfoPopup.popupData.prime_section.background_gradient.start }
                                        GradientStop { position: 1; color: adsInfoPopup.popupData.prime_section.background_gradient.end }
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        color: Qt.rgba(0, 0, 0, 0.7)

                                        UiColumnLayout {
                                            id: iGoPrimeLayout
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            padding: 16

                                            UiRowLayout {
                                                spacing: 8

                                                UiImage {
                                                    asset: "PrimeIcon"
                                                    pImageWidth: 20
                                                    pImageHeight: 20
                                                }

                                                Text {
                                                    text: adsInfoPopup.popupData.prime_section.name
                                                    color: UiTheme.colors.secondaryHover
                                                    font: UiTheme.fonts.titleSmall
                                                }
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                Layout.topMargin: 4
                                                text: adsInfoPopup.popupData.prime_section.benefits
                                                color: UiTheme.colors.overLayBright
                                                font: UiTheme.fonts.bodyLarge
                                                wrapMode: Text.Wrap
                                            }

                                            UiButton {
                                                pSize: UiTheme.buttons.sizes.medium
                                                pColor: adsInfoPopup.popupData.prime_section.button.type === "nowbuxButton" ?
                                                            UiTheme.buttons.colors.nowbuxButton : adsInfoPopup.popupData.prime_section.button.type === "accent" ?
                                                                UiTheme.buttons.colors.accent : adsInfoPopup.popupData.prime_section.button.type === "secondary" ?
                                                                    UiTheme.buttons.colors.secondary : adsInfoPopup.popupData.prime_section.button.type === "tertiary" ?
                                                                        UiTheme.buttons.colors.tertiary : adsInfoPopup.popupData.prime_section.button.type === "danger" ?
                                                                            UiTheme.buttons.colors.danger : adsInfoPopup.popupData.prime_section.button.type === "transparent" ?
                                                                                UiTheme.buttons.colors.transparent : adsInfoPopup.popupData.prime_section.button.type === "outline" ?
                                                                                    UiTheme.buttons.colors.outline : adsInfoPopup.popupData.prime_section.button.type === "accentWhiteBorder" ?
                                                                                        UiTheme.buttons.colors.accentWhiteBorder : UiTheme.buttons.colors.accentWhiteBorder

                                                text: adsInfoPopup.popupData.prime_section.button.text
                                                Layout.fillWidth: true
                                                Layout.topMargin: 12
                                                onClicked: {
                                                    plrBackend.handleAdsFreeBtnClick(adsInfoPopup.popupData.prime_section.button.action, adsInfoPopup.popupData.prime_section.button.value)
                                                    uiBackend.sendSettingStats("programmatic_ads_info_popup", "ads_free", "prime_btn_clicked",  isFromHtmlAds ? "html_ads" : "android_ads")
                                                    iAdsInfoWindow.close()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            UiButton {
                                pSize: UiTheme.buttons.sizes.medium
                                pColor: adsInfoPopup.popupData.buttons[0].type === "accent" ?
                                            UiTheme.buttons.colors.accent : adsInfoPopup.popupData.buttons[0].type === "nowbuxButton" ?
                                                UiTheme.buttons.colors.nowbuxButton : adsInfoPopup.popupData.buttons[0].type === "secondary" ?
                                                    UiTheme.buttons.colors.secondary : adsInfoPopup.popupData.buttons[0].type === "tertiary" ?
                                                        UiTheme.buttons.colors.tertiary : adsInfoPopup.popupData.buttons[0].type === "danger" ?
                                                            UiTheme.buttons.colors.danger : adsInfoPopup.popupData.buttons[0].type === "transparent" ?
                                                                UiTheme.buttons.colors.transparent : adsInfoPopup.popupData.buttons[0].type === "outline" ?
                                                                    UiTheme.buttons.colors.outline : adsInfoPopup.popupData.buttons[0].type === "accentWhiteBorder" ?
                                                                        UiTheme.buttons.colors.accentWhiteBorder : UiTheme.buttons.colors.accentWhiteBorder
                                text: adsInfoPopup.popupData.buttons[0].text
                                Layout.fillWidth: true
                                Layout.topMargin: 12
                                onClicked: {
                                    iAdsInfoWindow.close()
                                }
                            }

                            Text {
                                text: adsInfoPopup.popupData.footer_links[0].text
                                font: UiTheme.fonts.bodySmall
                                color: UiTheme.colors.overLayBright
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                Layout.topMargin: 12

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: iAdsInfoWindow.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iAdsEnableOnBootPrompt
        UiMessageDialog {
            id: iAdsEnableOnBoot
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + pBorderWidth + iMainContentLayout.x,
                                           iAppwin.y + pBorderWidth + iMainContentLayout.y,
                                           iMainContentLayout.width,
                                           iMainContentLayout.height)
            pDialogType: UiDialogWindow.Graphic
            title: qsTranslate("QObject", "Help Keep %1 Free and Awesome").arg(uiBackend.productName)
            pDescription: qsTranslate("QObject", "Show us some love and keep ads enabled. We depend on ad revenue to keep %1 free for you.").arg(uiBackend.productName)
            pDialogImageSource: "AdsPopupInfoImage"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Enable Ads")
                        onClicked: {
                            uiBackend.confSetBool("bst.enable_programmatic_ads", true)
                            uiBackend.confCommit()
                            plrBackend.enableAndroidAds(true)
                            plrBackend.sendDisplayAdsStats("reset_setting_popup_click", "enable")
                            iAdsEnableOnBoot.close()
                        }
                    },
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.tertiary
                        pText: qsTranslate("QObject", "No, Thanks")
                        onClicked: {
                            plrBackend.sendDisplayAdsStats("reset_setting_popup_click", "disable")
                            iAdsEnableOnBoot.close()
                        }
                    }
                ]
            }
            onCloseClicked: {
                plrBackend.sendDisplayAdsStats("reset_setting_popup_click", "close")
            }
        }
    }

    Component {
        id: iSignInRequiredPrompt
        Window {
            id: iSignInRequiredPromptWin
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            modality: Qt.ApplicationModal
            width: 360 + 2 * iShadow.shadowThickness
            height: 400 + 2 * iShadow.shadowThickness
            screen: transientParent.screen

            onClosing: pSigninRequiredWindow.destroy()
            property string pLoginTrigger: ""
            UiShadowContainer {
                id: iShadow
                UiColumnLayout {
                    anchors.fill: parent
                    leftPadding: 20
                    rightPadding: 20
                    topPadding: 20
                    bottomPadding: 40
                    color: UiTheme.colors.primary70
                    border {
                        color: UiTheme.colors.primary60
                        width: 1
                    }

                    UiImageButton {
                        asset: "CloseIcon"
                        pImageWidth: 16
                        pImageHeight: 16
                        Layout.alignment: Qt.AlignRight
                        onClicked: {
                            if (pSigninRequiredWindow != null) {
                                pSigninRequiredWindow.close()
                                nowggBackend.sendMomentsStats("moments_signin_popup_closed")
                            }
                        }
                    }

                    UiColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        UiImage {
                            asset: "ScreenRecordingActive"
                            pImageWidth: 72
                            pImageHeight: 72
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: qsTranslate("QObject", "Sign in required")
                            font: UiTheme.fonts.titleMedium
                            color: UiTheme.colors.primary10
                            wrapMode: Text.NoWrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            Layout.topMargin: 20
                        }
                        Text {
                            text: qsTranslate("QObject", "Moments & Recording are powered by now.gg cloud. Please log in to continue.")
                            font: UiTheme.fonts.bodyLarge
                            color: UiTheme.colors.primary20
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            horizontalAlignment: Text.AlignHCenter
                            Layout.topMargin: 8
                        }
                        UiColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 20
                            spacing: 16
                            UiButton {
                                asset: "NowggIcon"
                                extn: ".png"
                                text: qsTranslate("QObject", "Sign in with Now.gg")
                                pSize: UiTheme.buttons.sizes.large
                                pColor: UiTheme.buttons.colors.accent
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: {
                                    if (pSigninRequiredWindow != null) {
                                        pSigninRequiredWindow.close()
                                        nowggBackend.signinToNowGG(iSignInRequiredPromptWin.pLoginTrigger)
                                    }
                                }
                            }
                            UiRowLayout {
                                spacing: 20
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignHCenter
                                Rectangle {
                                    height: 1
                                    Layout.fillWidth: true
                                    color: UiTheme.colors.primary50
                                }
                                Text {
                                    text: qsTranslate("QObject", "Or")
                                    font: UiTheme.fonts.bodySmall
                                    color: UiTheme.colors.white
                                }
                                Rectangle {
                                    height: 1
                                    Layout.fillWidth: true
                                    color: UiTheme.colors.primary50
                                }
                            }

                            UiButton {
                                text: qsTranslate("QObject", "Continue as guest")
                                pSize: UiTheme.buttons.sizes.large
                                pColor: UiTheme.buttons.colors.alternate
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: {
                                    if (pSigninRequiredWindow != null) {
                                        pSigninRequiredWindow.close()
                                        nowggBackend.handleGuestLogin(iSignInRequiredPromptWin.pLoginTrigger)
                                        nowggBackend.sendMomentsStats("moments_signin_popup_signin_clicked", "guest_button")
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iSignInSuccessfulPrompt
        UiMessageDialog {
            id: iSigninSuccessfulDialog
            property string pLoginTrigger: ""
            title: iSigninSuccessfulDialog.pLoginTrigger == "recording" ? qsTranslate("QObject", "Login successful") : qsTranslate("QObject", "Sign in successful")
            pDescription: iSigninSuccessfulDialog.pLoginTrigger == "recording" ? qsTranslate("QObject", "You are all set to record your epic gameplay") : qsTranslate("QObject", "You are now ready to use moments mode and record your epic gameplay")
            pDialogType: UiDialogWindow.Tall
            pDialogImageSource: iSigninSuccessfulDialog.pLoginTrigger == "recording" ? "ScreenRecordingActive" :"MomentsIcon"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: iSigninSuccessfulDialog.pLoginTrigger == "moments" ? qsTranslate("QObject", "Turn Moments Mode on") : (iSigninSuccessfulDialog.pLoginTrigger == "recording" ? qsTranslate("QObject", "Turn Recording on") : qsTranslate("QObject", "Okay"))
                        onClicked: {
                            if (iSigninSuccessfulDialog.pLoginTrigger == "moments") {
                                nowggBackend.handleMomentsToggled(true)
                                nowggBackend.sendMomentsStats("moments_signin_enabled")
                            }else if(iSigninSuccessfulDialog.pLoginTrigger == "recording") {
                                nowggBackend.startRecording(true)
                                nowggBackend.sendMomentsStats("moments_signin_enabled")
                            }
                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iNowbuxSignInRewardSuccess
        UiQuickWindow {
            id: iNowbuxSignInRewardSuccessWindow
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            width: 360 + 2 * shadowContainer.shadowThickness
            height: iNowbuxSignInRewardSuccessLayout.implicitHeight + 2 * shadowContainer.shadowThickness
            modality: Qt.ApplicationModal
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + plrBackend.plrWinOffsets[0] - adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.y + plrBackend.plrWinOffsets[2],
                                           iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] + adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3])

            property int pNowbuxRewardWonAmount: 0
            property string pNowbuxRewardWonDescription: ""

            onClosed: {
                iNowbuxSignInRewardSuccessWindow.destroy()
            }

            onVisibleChanged: {
                if (visible) {
                    if (iNowbuxSignInRewardSuccessWindow.transientParent != null) {
                        iNowbuxSignInRewardSuccessWindow.x = Qt.binding(function() {
                            var messageWindowX = iNowbuxSignInRewardSuccessWindow.transientParent.x + (iNowbuxSignInRewardSuccessWindow.transientParent.width - iNowbuxSignInRewardSuccessWindow.width) / 2
                            if (messageWindowX < iNowbuxSignInRewardSuccessWindow.transientParent.screen.virtualX)
                                messageWindowX = iNowbuxSignInRewardSuccessWindow.transientParent.screen.virtualX
                            if(messageWindowX + iNowbuxSignInRewardSuccessWindow.width > iNowbuxSignInRewardSuccessWindow.transientParent.screen.virtualX + iNowbuxSignInRewardSuccessWindow.transientParent.screen.width)
                                messageWindowX = iNowbuxSignInRewardSuccessWindow.transientParent.screen.virtualX + iNowbuxSignInRewardSuccessWindow.transientParent.screen.width - iNowbuxSignInRewardSuccessWindow.width
                            return messageWindowX
                        })
                        iNowbuxSignInRewardSuccessWindow.y = Qt.binding(function() {
                            var messageWindowY = iNowbuxSignInRewardSuccessWindow.transientParent.y + (iNowbuxSignInRewardSuccessWindow.transientParent.height - iNowbuxSignInRewardSuccessWindow.height) / 2
                            if (messageWindowY < iNowbuxSignInRewardSuccessWindow.transientParent.screen.virtualY)
                                messageWindowY = iNowbuxSignInRewardSuccessWindow.transientParent.screen.virtualY
                            if(messageWindowY + iNowbuxSignInRewardSuccessWindow.height > iNowbuxSignInRewardSuccessWindow.transientParent.screen.virtualY + iNowbuxSignInRewardSuccessWindow.transientParent.screen.height)
                                messageWindowY = iNowbuxSignInRewardSuccessWindow.transientParent.screen.virtualY + iNowbuxSignInRewardSuccessWindow.transientParent.screen.height - iNowbuxSignInRewardSuccessWindow.height
                            return messageWindowY
                        })
                    }
                }
            }

            UiShadowContainer {
                id: shadowContainer
                Rectangle {
                    clip: true
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: iNowbuxSignInRewardSuccessLayout.implicitHeight
                    border.color: UiTheme.colors.primary40
                    border.width: 1
                    color: "#17182A"
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0; color: "#272A4A" }
                        GradientStop { position: 1; color: "#17182A" }
                    }
                    AnimatedImage {
                        id: animatedGif
                        anchors.topMargin: -200
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: "qrcAssets/Confetti.gif"
                        width: parent.width * 1.5
                        height: iNowbuxSignInRewardSuccessLayout.implicitHeight * 1.5
                    }
                    UiColumnLayout {
                        id: iNowbuxSignInRewardSuccessLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        padding: 20
                        spacing: 20
                        UiColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: qsTranslate("QObject", "Congratulations!")
                                font: UiTheme.fonts.titleExtraLarge
                                color: "#FBD123"
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            Text {
                                text: qsTranslate("QObject", "You won")
                                font: UiTheme.fonts.titleExtraLarge
                                color: "#FBD123"
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        UiColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            UiImage {
                                asset: "NowBux"
                                pImageWidth: 96
                                pImageHeight: 96
                                Layout.alignment: Qt.AlignHCenter
                            }
                            UiColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    text: iNowbuxSignInRewardSuccessWindow.pNowbuxRewardWonAmount
                                    font: UiTheme.fonts.titleLargest
                                    color: UiTheme.colors.white
                                    wrapMode: Text.Wrap
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTranslate("QObject", "nowBux")
                                    font: UiTheme.fonts.titleExtraLarge
                                    color: "#FBD123"
                                    wrapMode: Text.Wrap
                                    horizontalAlignment: Text.AlignHCenter
                                }
                            }
                        }

                        UiColumnLayout {
                            Layout.fillWidth: true
                            padding: 12
                            spacing: 14
                            border.width: 1
                            border.color: UiTheme.colors.primary70
                            color: "#4C141524"
                            Text {
                                leftPadding: 20
                                rightPadding: 20
                                text: iNowbuxSignInRewardSuccessWindow.pNowbuxRewardWonDescription
                                font: UiTheme.fonts.bodyExtraLarge
                                color: UiTheme.colors.white
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            UiButton {
                                Layout.fillWidth: true
                                text: qsTranslate("QObject", "Claim reward")
                                pSize: UiTheme.buttons.sizes.large
                                pColor: UiTheme.buttons.colors.nowbuxButton
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: {
                                    iNowbuxSignInRewardSuccessWindow.close()
                                    iTopbar.fOpenNowbuxRewardsPopup()
                                    nowggBackend.installPackageAfterLogin()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iNowbuxSignInWithIapRewardSuccess
        UiQuickWindow {
            id: iNowbuxSignInWithIapRewardSuccessWindow
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            width: 336 + 2 * shadowContainer.shadowThickness
            height: iNowbuxSignInWithIapRewardSuccessLayout.implicitHeight + 2 * shadowContainer.shadowThickness
            modality: Qt.ApplicationModal
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + plrBackend.plrWinOffsets[0] - adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.y + plrBackend.plrWinOffsets[2],
                                           iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] + adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3])

            onClosed: {
                iNowbuxSignInWithIapRewardSuccessWindow.destroy()
            }

            onVisibleChanged: {
                if (visible) {
                    if (iNowbuxSignInWithIapRewardSuccessWindow.transientParent != null) {
                        iNowbuxSignInWithIapRewardSuccessWindow.x = Qt.binding(function() {
                            var messageWindowX = iNowbuxSignInWithIapRewardSuccessWindow.transientParent.x + (iNowbuxSignInWithIapRewardSuccessWindow.transientParent.width - iNowbuxSignInWithIapRewardSuccessWindow.width) / 2
                            if (messageWindowX < iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.virtualX)
                                messageWindowX = iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.virtualX
                            if(messageWindowX + iNowbuxSignInWithIapRewardSuccessWindow.width > iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.virtualX + iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.width)
                                messageWindowX = iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.virtualX + iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.width - iNowbuxSignInWithIapRewardSuccessWindow.width
                            return messageWindowX
                        })
                        iNowbuxSignInWithIapRewardSuccessWindow.y = Qt.binding(function() {
                            var messageWindowY = iNowbuxSignInWithIapRewardSuccessWindow.transientParent.y + (iNowbuxSignInWithIapRewardSuccessWindow.transientParent.height - iNowbuxSignInWithIapRewardSuccessWindow.height) / 2
                            if (messageWindowY < iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.virtualY)
                                messageWindowY = iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.virtualY
                            if(messageWindowY + iNowbuxSignInWithIapRewardSuccessWindow.height > iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.virtualY + iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.height)
                                messageWindowY = iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.virtualY + iNowbuxSignInWithIapRewardSuccessWindow.transientParent.screen.height - iNowbuxSignInWithIapRewardSuccessWindow.height
                            return messageWindowY
                        })
                    }
                }
            }

            UiShadowContainer {
                id: shadowContainer
                UiColumnLayout {
                    id: iNowbuxSignInWithIapRewardSuccessLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    border.color: UiTheme.colors.primary40
                    border.width: 1
                    color: "#17182A"
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0; color: "#272A4A" }
                        GradientStop { position: 1; color: "#17182A" }
                    }
                    padding: 20
                    spacing: 20
                    Text {
                        text: qsTranslate("QObject", "Congratulations!")
                        font: UiTheme.fonts.titleExtraLarge
                        color: "#FBD123"
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                    }
                    UiImage {
                        asset: "GiftIcon"
                        pImageWidth: 126
                        pImageHeight: 96
                        Layout.alignment: Qt.AlignHCenter
                    }
                    UiColumnLayout {
                        Layout.fillWidth: true
                        padding: 12
                        spacing: 8
                        border.width: 1
                        border.color: UiTheme.colors.primary70
                        color: UiTheme.colors.primary90
                        Text {
                            leftPadding: 8
                            rightPadding: 8
                            text: qsTranslate("QObject", "Your winnings have been credited to your account.")
                            font: UiTheme.fonts.bodyExtraLarge
                            color: UiTheme.colors.white
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        UiButton {
                            Layout.fillWidth: true
                            text: qsTranslate("QObject", "Okay")
                            pSize: UiTheme.buttons.sizes.large
                            pColor: UiTheme.buttons.colors.nowbuxButton
                            onClicked: {
                                iNowbuxSignInWithIapRewardSuccessWindow.close()
                                iTopbar.fOpenNowbuxRewardsPopup()
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iNowbuxSignInSuccessfulPrompt
        UiQuickWindow {
            id: iNowbuxSigninSuccessfulDialog
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            width: 280 + 2 * shadowContainer.shadowThickness
            height: iNowbuxSigninSuccessfulLayout.implicitHeight + 2 * shadowContainer.shadowThickness
            modality: Qt.ApplicationModal
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + plrBackend.plrWinOffsets[0] - adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.y + plrBackend.plrWinOffsets[2],
                                           iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] + adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3])

            onClosed: {
                iNowbuxSigninSuccessfulDialog.destroy()
            }

            onVisibleChanged: {
                if (visible) {
                    if (iNowbuxSigninSuccessfulDialog.transientParent != null) {
                        iNowbuxSigninSuccessfulDialog.x = Qt.binding(function() {
                            var messageWindowX = iNowbuxSigninSuccessfulDialog.transientParent.x + (iNowbuxSigninSuccessfulDialog.transientParent.width - iNowbuxSigninSuccessfulDialog.width) / 2
                            if (messageWindowX < iNowbuxSigninSuccessfulDialog.transientParent.screen.virtualX)
                                messageWindowX = iNowbuxSigninSuccessfulDialog.transientParent.screen.virtualX
                            if(messageWindowX + iNowbuxSigninSuccessfulDialog.width > iNowbuxSigninSuccessfulDialog.transientParent.screen.virtualX + iNowbuxSigninSuccessfulDialog.transientParent.screen.width)
                                messageWindowX = iNowbuxSigninSuccessfulDialog.transientParent.screen.virtualX + iNowbuxSigninSuccessfulDialog.transientParent.screen.width - iNowbuxSigninSuccessfulDialog.width
                            return messageWindowX
                        })
                        iNowbuxSigninSuccessfulDialog.y = Qt.binding(function() {
                            var messageWindowY = iNowbuxSigninSuccessfulDialog.transientParent.y + (iNowbuxSigninSuccessfulDialog.transientParent.height - iNowbuxSigninSuccessfulDialog.height) / 2
                            if (messageWindowY < iNowbuxSigninSuccessfulDialog.transientParent.screen.virtualY)
                                messageWindowY = iNowbuxSigninSuccessfulDialog.transientParent.screen.virtualY
                            if(messageWindowY + iNowbuxSigninSuccessfulDialog.height > iNowbuxSigninSuccessfulDialog.transientParent.screen.virtualY + iNowbuxSigninSuccessfulDialog.transientParent.screen.height)
                                messageWindowY = iNowbuxSigninSuccessfulDialog.transientParent.screen.virtualY + iNowbuxSigninSuccessfulDialog.transientParent.screen.height - iNowbuxSigninSuccessfulDialog.height
                            return messageWindowY
                        })
                    }
                }
            }

            UiShadowContainer {
                id: shadowContainer
                UiColumnLayout {
                    id: iNowbuxSigninSuccessfulLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    leftPadding: 20
                    rightPadding: 20
                    topPadding: 20
                    bottomPadding: 32
                    color: UiTheme.colors.primary70
                    border.color: UiTheme.colors.primary40
                    border.width: 1
                    spacing: 20

                    UiImage {
                        asset: "SuccessIcon"
                        pImageWidth: 72
                        pImageHeight: 72
                        Layout.alignment: Qt.AlignHCenter
                    }

                    UiColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        leftPadding: 16
                        rightPadding: 16
                        Text {
                            text: qsTranslate("QObject", "Login successful")
                            font: UiTheme.fonts.titleMedium
                            color: UiTheme.colors.primary10
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            text: qsTranslate("QObject", "You have been successfully logged in.")
                            font: UiTheme.fonts.bodyLarge
                            color: UiTheme.colors.primary20
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    UiButton {
                        text: qsTranslate("QObject", "Continue")
                        pSize: UiTheme.buttons.sizes.medium
                        pColor: UiTheme.buttons.colors.accent
                        Layout.alignment: Qt.AlignHCenter
                        onClicked: {
                            visible = false
                            iLoaderLayout.visible = true
                            nowggBackend.sendNowbuxStats("nowbux_signin_claim_rewards_clicked")
                            nowggBackend.checkAndClaimNowbuxReward("NOWBUX_LOGIN")
                        }
                    }

                    UiRowLayout {
                        id: iLoaderLayout
                        Layout.alignment: Qt.AlignHCenter
                        topPadding: 8
                        bottomPadding: 8
                        leftPadding: 20
                        rightPadding: 20
                        spacing: 4
                        color: UiTheme.colors.primary60
                        visible: false
                        UiImage {
                            asset: "Loader_bright"
                            pImageWidth: 16
                            pImageHeight: 16
                            visible: iLoaderLayout.visible
                            cache: false
                            RotationAnimation on rotation {
                                loops: Animation.Infinite
                                duration: 2000
                                from: 0
                                to: 360
                                direction: RotationAnimation.Clockwise
                                running: iLoaderLayout.visible
                            }
                        }
                        Text {
                            text: qsTranslate("QObject", "Please wait")
                            font: UiTheme.fonts.buttonMedium
                            color: UiTheme.colors.primary20
                        }
                    }
                }
            }
        }
    }


    Component {
        id: iNowbuxSignIn
        UiQuickWindow {
            id: iNowbuxSignInWindow
            color: "transparent"
            property bool pSkipLoginEnabled: true
            property string pSigninSource: ""
            property string pPackageName: ""
            flags: Qt.Window | Qt.FramelessWindowHint
            width: 320 + 2 * shadowContainer.shadowThickness
            height: iNowbuxSignInLayout.implicitHeight + 2 * shadowContainer.shadowThickness
            modality: Qt.ApplicationModal
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + plrBackend.plrWinOffsets[0] - adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.y + plrBackend.plrWinOffsets[2],
                                           iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] + adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3])

            onClosed: {
                iNowbuxSignInWindow.destroy()
            }

            onVisibleChanged: {
                if (visible) {
                    if (iNowbuxSignInWindow.transientParent != null) {
                        iNowbuxSignInWindow.x = Qt.binding(function() {
                            var messageWindowX = iNowbuxSignInWindow.transientParent.x + (iNowbuxSignInWindow.transientParent.width - iNowbuxSignInWindow.width) / 2
                            if (messageWindowX < iNowbuxSignInWindow.transientParent.screen.virtualX)
                                messageWindowX = iNowbuxSignInWindow.transientParent.screen.virtualX
                            if(messageWindowX + iNowbuxSignInWindow.width > iNowbuxSignInWindow.transientParent.screen.virtualX + iNowbuxSignInWindow.transientParent.screen.width)
                                messageWindowX = iNowbuxSignInWindow.transientParent.screen.virtualX + iNowbuxSignInWindow.transientParent.screen.width - iNowbuxSignInWindow.width
                            return messageWindowX
                        })
                        iNowbuxSignInWindow.y = Qt.binding(function() {
                            var messageWindowY = iNowbuxSignInWindow.transientParent.y + (iNowbuxSignInWindow.transientParent.height - iNowbuxSignInWindow.height) / 2
                            if (messageWindowY < iNowbuxSignInWindow.transientParent.screen.virtualY)
                                messageWindowY = iNowbuxSignInWindow.transientParent.screen.virtualY
                            if(messageWindowY + iNowbuxSignInWindow.height > iNowbuxSignInWindow.transientParent.screen.virtualY + iNowbuxSignInWindow.transientParent.screen.height)
                                messageWindowY = iNowbuxSignInWindow.transientParent.screen.virtualY + iNowbuxSignInWindow.transientParent.screen.height - iNowbuxSignInWindow.height
                            return messageWindowY
                        })
                    }
                }
            }

            UiShadowContainer {
                id: shadowContainer
                Item {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    UiColumnLayout {
                        id: iNowbuxSignInLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        border.color: UiTheme.colors.primary40
                        border.width: 1
                        color: UiTheme.colors.primary70
                        UiColumnLayout {
                            Layout.fillWidth: true
                            topPadding: 32
                            leftPadding: 20
                            rightPadding: 20
                            spacing: 20

                            UiImage {
                                asset: pSigninSource === "game_center" ? "NowbuxStore" : "BlueStacksLogo"
                                pImageWidth: 56
                                pImageHeight: 61
                                Layout.alignment: Qt.AlignHCenter
                            }
                            Text {
                                text: pSigninSource === "game_center" ? qsTranslate("QObject", "BlueStacks Store") : qsTranslate("QObject", "BlueStacks Login")
                                font: UiTheme.fonts.titleSmall
                                color: "#FFFFFF"
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: "#FFFFFF"
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: "#00FFFFFF" }
                                    GradientStop { position: 0.5; color: "#33FFFFFF" }
                                    GradientStop { position: 1; color: "#00FFFFFF" }
                                }
                            }

                            Text {
                                text: qsTranslate("QObject", "Sign in to BlueStacks account to install games and access BlueStacks Store")
                                font: UiTheme.fonts.bodyMedium
                                color: UiTheme.colors.white
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }

                            UiColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 66
                                Layout.rightMargin: 66
                                spacing: 8
                                UiButton {
                                    text: qsTranslate("QObject", "Sign In")
                                    pSize: UiTheme.buttons.sizes.medium
                                    pColor: UiTheme.buttons.colors.accent
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillWidth: true
                                    onClicked: {
                                        iNowbuxSignInWindow.close()
                                        nowggBackend.signinToNowBux()
                                        nowggBackend.sendNowbuxStats("nowgg_login_popup_signin_clicked", pSigninSource, pPackageName)
                                    }
                                }

                                UiButton {
                                    text: qsTranslate("QObject", "Skip")
                                    pSize: UiTheme.buttons.sizes.medium
                                    pColor: UiTheme.buttons.colors.transparent
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.fillWidth: true
                                    visible: pSkipLoginEnabled
                                    onClicked: {
                                        iNowbuxSignInWindow.close()
                                        nowggBackend.installPackageAfterLogin()
                                        nowggBackend.sendNowbuxStats("nowgg_login_popup_skip_clicked", pSigninSource, pPackageName)
                                    }
                                }
                            }
                        }
                        UiImage {
                            id: iNowbuxLoginLogo
                            asset: "NowbuxLoginLogo"
                            pImageWidth: parent.width
                            pImageHeight: 74
                            Layout.topMargin: pSkipLoginEnabled? -16 : -6
                            Text {
                                text: qsTranslate("QObject", "One account for")
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                font: UiTheme.fonts.bodySmall
                                color: UiTheme.colors.white
                                anchors.bottomMargin: 60
                            }
                        }

                    }

                    UiImageButton {
                        asset: "CloseIcon"
                        UiToolTip.text: qsTranslate("QObject", "Close")
                        pImageWidth: 16
                        pImageHeight: 16
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: 24
                        anchors.rightMargin: 24
                        Layout.alignment: Qt.AlignRight
                        onClicked: {
                            iNowbuxSignInWindow.close()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iDisableMomentPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Record Gameplay")
            pDescription: qsTranslate("QObject", "To start recording, you need to disable Moments first.")
            pDialogType: UiDialogWindow.DescriptiveNarrow
            pDialogHeaderImageSource: "ScreenRecordOutline"
            pDialogImageSource: "ErrorLarge"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Okay")
                        onClicked: {
                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iDisableRecordingPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Record Moments")
            pDescription: qsTranslate("QObject", "To start moments, you need to stop recording first.")
            pDialogType: UiDialogWindow.DescriptiveNarrow
            pDialogHeaderImageSource: "ScreenRecordOutline"
            pDialogImageSource: "ErrorLarge"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Okay")
                        onClicked: {
                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iCheckBrowserPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Check your browser")
            pDescription: qsTranslate("QObject", "Finish logging in from your web browser")
            pDialogType: UiDialogWindow.Tall
            pDialogImageSource: "Browser"
            pNotice: qsTranslate("QObject", "Not seeing the browser tab? Try again")

            property string source: "moments"

            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Try Again")
                        onClicked: {
                            close()
                            if (source === "moments")
                                nowggBackend.signinToNowGG("moments")
                            else if (source === "nowbux")
                                nowggBackend.signinToNowBux()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iLowDiskSpacePrompt
        UiMessageDialog {
            id: iRoot
            title: qsTranslate("QObject", "Warning!")
            pDescription: qsTranslate("QObject", "Your system is running low on disk space. Please free up some space to avoid any issues.")
            pDialogType: UiDialogWindow.DescriptiveNarrow
            pDialogImageSource: "WarningLarge"

            property bool isMoment: true

            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Okay")
                        onClicked: {
                            if (iRoot.isMoment)
                                nowggBackend.startMomentMode()
                            else
                                nowggBackend.recordScreen()

                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iSignOutPrompt
        UiMessageDialog {
            property string userName: "Johnny Vicks"
            property string userEmail: "john@vicks.com"
            title: userName
            pDescription: userEmail
            pDialogType: UiDialogWindow.TallAlert
            pDialogImageSource: "ProfileLogo"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Sign Out")
                        onClicked: {
                            close()
                            fShowSignOutConfirmationDialog()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iSignOutConfirmationPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Confirm sign out")
            pDescription: qsTranslate("QObject", "You will not be able to access your saved moments from the cloud. Are you sure you want to sign out?")
            pDialogType: UiDialogWindow.GraphicSimple
            pDialogImageSource: "WarningLarge"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pSize: UiTheme.buttons.sizes.large
                        pColor: UiTheme.buttons.colors.danger
                        pText: qsTranslate("QObject", "Sign Out")
                        onClicked: {
                            close()
                            if (pSignOutPopupWindow != null)
                                pSignOutPopupWindow.close()

                            nowggBackend.signOutNowggAccount("moments")
                        }
                    },
                    DialogButtonModel {
                        pSize: UiTheme.buttons.sizes.large
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
        id: iLinkNowggAccountPrompt
        Window {
            id: iMainWin
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            modality: Qt.ApplicationModal
            width: 360 + 2 * iShadow.shadowThickness
            height: 380 + 2 * iShadow.shadowThickness
            screen: transientParent.screen

            property string videoId: ""
            property bool pShowDoNotShowAgain: true

            onClosing: pLinkNowggAccountWindow.destroy()

            UiShadowContainer {
                id: iShadow
                UiColumnLayout {
                    anchors.fill: parent
                    leftPadding: 20
                    rightPadding: 20
                    topPadding: 20
                    bottomPadding: 40
                    color: UiTheme.colors.primary70
                    border {
                        color: UiTheme.colors.primary60
                        width: 1
                    }

                    UiImageButton {
                        asset: "CloseIcon"
                        pImageWidth: 16
                        pImageHeight: 16
                        Layout.alignment: Qt.AlignRight
                        onClicked: {
                            if (pLinkNowggAccountWindow != null) {
                                pLinkNowggAccountWindow.close()
                                nowggBackend.sendMomentsStats("moments_link_account_popup_closed", nowggBackend.pLinkAccountSource)
                            }
                        }
                    }

                    UiColumnLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        UiImage {
                            asset: "ScreenRecordingActive"
                            pImageWidth: 72
                            pImageHeight: 72
                            Layout.alignment: Qt.AlignHCenter
                        }
                        Text {
                            text: qsTranslate("QObject", "Link your account")
                            font: UiTheme.fonts.titleMedium
                            color: UiTheme.colors.primary10
                            wrapMode: Text.NoWrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            Layout.topMargin: 20
                        }
                        Text {
                            text: qsTranslate("QObject", "Sign in to link your moments to your account")
                            font: UiTheme.fonts.bodyLarge
                            color: UiTheme.colors.primary20
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            Layout.leftMargin: 20
                            Layout.rightMargin: 20
                            horizontalAlignment: Text.AlignHCenter
                            Layout.topMargin: 8
                        }
                        UiColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.topMargin: 20
                            spacing: 16
                            UiButton {
                                asset: "NowggIcon"
                                extn: ".png"
                                text: qsTranslate("QObject", "Sign in with Now.gg")
                                pSize: UiTheme.buttons.sizes.large
                                pColor: UiTheme.buttons.colors.accent
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: {
                                    if (pLinkNowggAccountWindow != null) {
                                        nowggBackend.sendMomentsStats("moments_link_account_signin_clicked", nowggBackend.pLinkAccountSource)
                                        pLinkNowggAccountWindow.close()
                                        nowggBackend.signinToNowGG()
                                        if (iDoNotShowAgainChkbox.checked) {
                                            uiBackend.confSetBool("bst.do_not_show_link_account_popup", true)
                                            uiBackend.confCommit()
                                            plrBackend.reloadConfInAllInstances()
                                        }
                                    }
                                }
                            }
                            UiButton {
                                text: qsTranslate("QObject", "No, continue as guest")
                                pSize: UiTheme.buttons.sizes.large
                                pColor: UiTheme.buttons.colors.alternate
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: {
                                    if (pLinkNowggAccountWindow != null) {
                                        nowggBackend.sendMomentsStats("moments_link_account_continue_as_guest_click", nowggBackend.pLinkAccountSource)
                                        if (iDoNotShowAgainChkbox.visible && iDoNotShowAgainChkbox.checked) {
                                            nowggBackend.sendMomentsStats("moments_link_account_do_not_show_again_selected", nowggBackend.pLinkAccountSource)
                                            uiBackend.confSetBool("bst.do_not_show_link_account_popup", true)
                                            uiBackend.confCommit()
                                            plrBackend.reloadConfInAllInstances()
                                        }
                                        pLinkNowggAccountWindow.close()
                                        if (iMainWin.videoId !== "")
                                            mediaBackend.playMoments(iMainWin.videoId, false)
                                    }
                                }
                            }
                            UiCheckBox {
                                id: iDoNotShowAgainChkbox
                                visible: iMainWin.pShowDoNotShowAgain
                                pSize: UiTheme.checkBoxes.sizes.small
                                text: qsTranslate("QObject", "Do not show this again")
                                checked: false
                                Layout.alignment: Qt.AlignHCenter
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: iMainContentLayout
        anchors.fill: parent
        anchors.margins: iAppwin.visibility == ApplicationWindow.FullScreen ? 0 : pBorderWidth
        color: uiBackend.oem === "msi5" ? UiTheme.colors.primary80 :"#000000"

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            anchors.left: parent.left
            anchors.right: iSidebar.visible ? iSidebar.left : parent.right
            anchors.top: iTopbar.bottom
            anchors.bottom: parent.bottom

            Loader {
                id: iBootLoader
                anchors.fill: parent
                sourceComponent:((pIsBootComplete&&pBootVideoDone)||pShowAndroidAd)?iGlContainerComponent:iBootProgressComponent
            }

            UiRowLayout {
                id: iProgressLayout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                visible: pIsBootComplete == false
                height: 40
                color: UiTheme.colors.primary70
                leftPadding: 12
                rightPadding: 12
                spacing: 16

                onVisibleChanged: {
                    console.info("Progress layout visibility: " + visible)
                }

                Text {
                    visible: iAppwin.width > 500
                    text: qsTranslate("QObject", "Starting %1").arg(uiBackend.productName)
                    font: UiTheme.fonts.titleSmallest
                    color: UiTheme.colors.primary10
                }
                UiProgressBar {
                    id: iProgressBar
                    Layout.fillWidth: true
                    from: 0
                    to: 99
                    Layout.preferredHeight: 4
                    value: 1
                    backgroundColor: UiTheme.colors.primary100

                    property real lastProgressTime : pBootStartTime

                    Timer {
                        interval: 40
                        repeat: true
                        running: pIsBootComplete ? false : true
                        onTriggered: {
                            iProgressBar.value += 100* (new Date().getTime() - iProgressBar.lastProgressTime)/(plrBackend.getBootDuration() * 1.1)
                            iProgressBar.lastProgressTime = new Date().getTime()

                            if(iProgressBar.value >= 95)
                                iProgressBar.value += 0.025

                            if(iProgressBar.value >= 99)
                                iProgressBar.value = 99
                        }
                    }
                }
                UiButton {
                    id: iBootImageBtn
                    Layout.minimumWidth: 80
                    pSize: UiTheme.buttons.sizes.small
                    pColor: enabled ? UiTheme.buttons.colors.accent : UiTheme.buttons.colors.tertiary
                    enabled: false
                    opacity: enabled ? 1 : 0.6
                    visible: plrBackend.pShowProgrammaticAdsSkipBtn
                    text: qsTranslate("QObject", "Home")
                    asset: uiBackend.oem === "nxt" ? "RightHomeIcon" : ""
                    pImageAfterText: true
                    onClicked:  {
                        plrBackend.onHomeClicked()
                    }

                    Timer {
                        interval: plrBackend.pProgrammaticAdsSkipBtnTimerMSecs
                        repeat: false
                        running: pShowAndroidAd
                        onTriggered: {
                            iBootImageBtn.enabled = true
                        }
                    }
                }
            }
        }

        UiRowLayout {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            Repeater {
                id: iAdRepeater
                model: adsBackend.pLeftAds
                delegate: PlrAdSlot {
                    id: iLeftAdSlot
                    Layout.fillHeight: true
                    Layout.preferredWidth: ((iAppwin.height - dataObject.pAdVerticalOffset) * dataObject.pAdRatio) + dataObject.pAdVerticalOffset
                    Layout.maximumWidth: dataObject.pAdMaxWidth
                    opacity: dataObject.pAdVisible ? 1 : 0
                    pWebEngineView.zoomFactor: Math.min(((iLeftAdSlot.width - dataObject.pAdHorizontalOffset) * 1.0) / dataObject.pAdWidth, ((iLeftAdSlot.height - dataObject.pAdVerticalOffset) * 1.0) / dataObject.pAdHeight)
                    pWebEngineView.url: dataObject.pWebEngineViewUrl
                    pAdSlotBrowserPreference: dataObject.pAdClickBrowserPreference
                    pWebEngineView.webChannel: dataObject.pWebChannel
                    pWebEngineView.profile: dataObject.pWebEngineProfile

                    Component.onCompleted: {
                        dataObject.pExtendedBannerAdWidth = Qt.binding(function(){ return (dataObject.pAdVisible ? iLeftAdSlot.width : 0) })
                    }

                    Connections {
                        target: dataObject
                        function onRunJsFuncInWebEngineView(script) {
                            console.info("fRunJsFunctionInWebView - script: " + script)
                            iLeftAdSlot.pWebEngineView.runJavaScript(script)
                        }
                    }


                    Connections {
                        target: dataObject
                        function onAdCloseClicked() {
                            if (iLeftAdSlot.pWebEngineView != null)
                                iLeftAdSlot.pWebEngineView.destroy()
                        }
                        function onExtendedBannerAdWidthChanged() {
                            if (index === 0)
                                adsBackend.pExtendedBannerAdWidth = dataObject.pExtendedBannerAdWidth
                        }
                    }

                    Connections {
                        target: iLeftAdSlot
                        function onAdSlotClicked(clickUrl) {
                            dataObject.adClicked(clickUrl)
                        }
                    }

                    Connections {
                        target: adsBackend
                        function onEditorWindowVisibleChanged() {
                            if (index === 0) {
                                if (adsBackend.currentBannerAdVisibile) {
                                    if (adsBackend.editorWindowVisible) {
                                        dataObject.adHide()
                                    }
                                    else {
                                        dataObject.adUnhide()
                                    }
                                }
                            }
                        }
                    }

                    Connections {
                        target: lsgBackend
                        function onScreenStreamingChanged() {
                            if (index === 0) {
                                if (adsBackend.currentBannerAdVisibile) {
                                    if (lsgBackend.pIsScreenStreamingOn) {
                                        dataObject.adHide()
                                    }
                                    else {
                                        dataObject.adUnhide()
                                    }
                                }
                            }
                        }
                    }

                    Connections {
                        target: iAppwin
                        function onHeightChanged() {
                            if (index === 0) {
                                if (dataObject.pAdVisible) {
                                    dataObject.adResize()
                                }
                            }
                        }

                        function onVisibilityChanged(visibility) {
                            if (index === 0) {
                                if (adsBackend.currentBannerAdVisibile) {
                                    if (visibility === ApplicationWindow.Maximized || visibility === ApplicationWindow.Windowed) {
                                        if (dataObject.pAdVisible) {
                                            dataObject.adResize()
                                        }
                                        else {
                                            dataObject.adUnhide()
                                        }
                                    }
                                    else if (visibility === ApplicationWindow.FullScreen) {
                                        dataObject.adHide()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            color: UiTheme.colors.primary60
            width: adsBackend.pExtendedBannerAdWidth > 0 ? 1 : 0
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: adsBackend.pExtendedBannerAdWidth
        }

        PlrTopbar {
            id: iTopbar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: adsBackend.pExtendedBannerAdWidth > 0  ? adsBackend.pExtendedBannerAdWidth + pBorderWidth : adsBackend.pExtendedBannerAdWidth
            height: 32
            color: uiBackend.oem === "msi5" ? UiTheme.colors.primary70 : UiTheme.colors.primary80
            onSidebarBtnClicked: {
                iSidebar.visible = true
                fSetUiParams(true)
                if (iAppwin.visibility != ApplicationWindow.Maximized)
                    iAppwin.width += iAppwin.pSidebarWidth
                uiBackend.confSetInstanceBool("show_sidebar", true)
                plrBackend.onSidebarVisibilityChanged()
                iTopbar.fReloadTopbarElements()
            }

            onSidebarHideClicked: {
                iSidebar.visible = false
                fSetUiParams(false)
                if (iAppwin.visibility != ApplicationWindow.Maximized) {
                    iAppwin.width -= iAppwin.pSidebarWidth
                }

                uiBackend.confSetInstanceBool("show_sidebar", false)
                plrBackend.onSidebarVisibilityChanged()
                iTopbar.fReloadTopbarElements()
            }

            onHamburgerSettingClicked: fOpenSettingsWindow()
        }
        PlrSidebar {
            id: iSidebar
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.top: iTopbar.bottom
            color: uiBackend.oem === "msi5" ? UiTheme.colors.primary70 : UiTheme.colors.primary80
            width: pSidebarWidth
            visible: uiBackend.confGetInstanceBool("show_sidebar")
            onSidebarSettingClicked: fOpenSettingsWindow()
        }
    }

    function fCloseAllChildWindows() {
        if (pSettingsWindow != null)
            pSettingsWindow.destroy()

        fDestroyMacroManagerWindow()

        fCloseFlePopup()

        if (pSyncOperationsWindow != null)
            pSyncOperationsWindow.destroy()

        if (pScreenTranslateWindow != null)
            pScreenTranslateWindow.destroy()

        if (pLoadingScreenControl != null)
            pLoadingScreenControl.destroy()

        if (pFullScreenPopupControl != null)
            pFullScreenPopupControl.fClosePopup()

        if (pNotificationBarWindow != null)
            pNotificationBarWindow.close()

        if (pChargerWarningPopupWindow != null)
            pChargerWarningPopupWindow.destroy()

        if(pTroubleSigningInBtn != null)
            pTroubleSigningInBtn.destroy()

        if(pSignInScrollBtn != null)
            pSignInScrollBtn.destroy()

        if (pExitFullScreenControl != null)
            pExitFullScreenControl.fClosePopup()

        if(pMediaGalleryWindow != null)
            pMediaGalleryWindow.destroy()

        if (pNowbuxSigninSuccessfulPopupWindow != null)
            pNowbuxSigninSuccessfulPopupWindow.destroy()

        iSidebar.fCloseIntroductionBlurbs()

        if(pNowggSigninpopupWindow != null)
            pNowggSigninpopupWindow.destroy()


        adsBackend.closeBannerAds()

        if (pBrowserWindow != null)
            pBrowserWindow.close()

        if (pInterstitialAdsInfoWindow != null)
            pInterstitialAdsInfoWindow.destroy()

        if (pSplitScreenOverlayWindow != null)
            pSplitScreenOverlayWindow.destroy()

        plrBackend.closeChatWindow()
        plrBackend.closeGamePopupWindow()

        fCloseStreamingPopups()
    }

    function fSetUiParams(sidebarVisible) {
        plrBackend.setBackendUiParams(
                    pBorderWidth + adsBackend.pExtendedBannerAdWidth + (adsBackend.pExtendedBannerAdWidth > 0 ? pBorderWidth : 0),
                    iTopbar.height + pBorderWidth,
                    pBorderWidth + (sidebarVisible ? iAppwin.pSidebarWidth : 0),
                    pBorderWidth,
                    7 * iTopbar.height + 50 + pBorderWidth * 2)
    }

    function fUiShowNormal() {
        pIsShowMaximizedCalled = false
        iTopbar.visible = true
        if (iAppwin.visibility == ApplicationWindow.FullScreen) {
            iSidebar.visible = pSidebarVisibleBeforeFullscreen
        }
        fSetUiParams(iSidebar.visible)
        if (pFullScreenPopupControl != null)
            pFullScreenPopupControl.fClosePopup()

        if (pExitFullScreenControl != null)
            pExitFullScreenControl.fClosePopup()
        iAppwin.showNormal()
        iAppwin.x = pX
        iAppwin.y =pY
        iAppwin.width = pWidth
        iAppwin.height = pHeight

        plrBackend.onRestoreOrientation()
    }

    function fUiShowMaximized() {
        fUISaveSize()
        pIsShowMaximizedCalled = false
        iTopbar.visible = true
        if (iAppwin.visibility == ApplicationWindow.FullScreen) {
            iSidebar.visible = pSidebarVisibleBeforeFullscreen
        }
        fSetUiParams(iSidebar.visible)
        if (pFullScreenPopupControl != null)
            pFullScreenPopupControl.fClosePopup()
        if (pExitFullScreenControl != null)
            pExitFullScreenControl.fClosePopup()
        iAppwin.showMaximized()

        pIsShowMaximizedCalled = true
    }

    function fUISaveSize() {
        if (iAppwin.visibility === ApplicationWindow.Windowed)
        {
            pX = iAppwin.x
            pY = iAppwin.y
            pWidth = iAppwin.width
            pHeight = iAppwin.height
        }
    }

    function fUiShowFullscreen(showPopup) {
        fUISaveSize()
        iTopbar.visible = false
        pIsShowMaximizedCalled = false
        plrBackend.setBackendUiParams(0, 0, 0, 0, 6 * iTopbar.height + pBorderWidth * 2)
        iSidebar.fCloseIntroductionBlurbs()
        pSidebarVisibleBeforeFullscreen = iSidebar.visible
        iSidebar.visible = false
        iAppwin.showFullScreen()
        plrBackend.updatePlrMouseCoordinates()
        if(showPopup)
        {
            if (pFullScreenPopupControl != null) {
                pFullScreenPopupControl.fClosePopup()
            }
            pFullScreenPopupControl = iFullScreenPopupControlComponent.createObject(iAppwin, {"pMessage": qsTranslate("QObject", "Press {%1} to exit fullscreen").arg(
                                                                                                              plrBackend.getDisplayComboFromNameCombo(uiBackend.confGetString("bst.shortcut.toggle_fullscreen")))})
            pFullScreenPopupControl.fShowMessage(true, plrBackend.showFullScreenSetting())
        }
    }

    function fShowAndroidAdInProgressBar() {
        pShowAndroidAd = true
    }

    function fUiHideBootProgressBar(showAdInfoPopup) {
        console.info("fUiHideBootProgressBar called with showAdInfoPopup: " + showAdInfoPopup)
        pIsBootComplete = true
        fSetUiParams(iSidebar.visible)
        iSidebar.fResetSidebar()

        uiBackend.confSetInstanceI32("boot_duration",(new Date().getTime() - pBootStartTime))
        uiBackend.confCommit()

        if(showAdInfoPopup)
            fShowBootAdsInfoPrompt()
        fGamepadDetectionMessage()
    }

    function fUpdateForRawMode() {
        pIsRawMode = true;
        iTopbar.fReloadTopbarElements()
    }

    function fChangeBootPromotion(imagePath, buttonText, buttonClickMessage) {
        if(!pShowAndroidAd)
            iBootLoader.item.fSetBootPromotion(imagePath, buttonText, buttonClickMessage)
    }

    function fSetDefaultAppSpecificBootBanner(fileName, title, appDetails) {
        if(!pShowAndroidAd)
            iBootLoader.item.fSetDefaultAppSpecificBootBanner(fileName, title, appDetails);
    }

    function fUiEnableMouseCursorLock(enabled) {
        if (enabled) {
            if (pFullScreenPopupControl != null) {
                pFullScreenPopupControl.fClosePopup()
            }
            var keyText = uiBackend.confGetBool("bst.enable_esc_mousecursor") ?
                        plrBackend.getDisplayComboFromNameCombo("Esc") :
                        plrBackend.getDisplayComboFromNameCombo(uiBackend.confGetString("bst.shortcut.toggle_mouse_cursor_lock"))
            pFullScreenPopupControl = iFullScreenPopupControlComponent.createObject(iAppwin, {"pMessage" : qsTranslate("QObject", "Press {%1} to unlock mouse cursor").arg(keyText)})
            pFullScreenPopupControl.fShowMessage(true)
        }
        else {
            if (pFullScreenPopupControl != null)
                pFullScreenPopupControl.fClosePopup()
        }

        iSidebar.fUpdateMouseCursorImage(enabled)
    }

    function fOpenSettingsWindow() {
        if (pSettingsWindow != null)
            return

        var component = Qt.createComponent("UiSettingsWindow.qml")
        if (component.status === Component.Ready) {
            pSettingsWindow = component.createObject(iAppwin, { "pParentWindow": "plr", "pSettingsItems": iSettingsItems.createObject() })
            if (pSettingsWindow != null) {
                var shadowWidthDiff = 2 * pSettingsWindow.pShadowThickness
                var settingsWidth = Math.max(iAppwin.width + shadowWidthDiff,
                                             (840 + shadowWidthDiff))
                var settingsHeight = Math.max(iAppwin.height + shadowWidthDiff,
                                              (475 + shadowWidthDiff))
                var settingsX = iAppwin.x - ((settingsWidth - iAppwin.width) / 2)
                var settingsY = iAppwin.y - ((settingsHeight - iAppwin.height) / 2)

                if (settingsX < pSettingsWindow.screen.virtualX) {
                    if (iAppwin.x < pSettingsWindow.screen.virtualX)
                        settingsX = pSettingsWindow.screen.virtualX
                    else
                        settingsX = iAppwin.x - (shadowWidthDiff / 2)
                }
                if (settingsY < pSettingsWindow.screen.virtualY) {
                    if (iAppwin.y < pSettingsWindow.screen.virtualY)
                        settingsY = pSettingsWindow.screen.virtualY
                    else
                        settingsY = iAppwin.y - (shadowWidthDiff / 2)
                }

                if (iAppwin.visibility != ApplicationWindow.Maximized
                        && ((settingsX + settingsWidth) > pSettingsWindow.screen.virtualX
                            + pSettingsWindow.screen.width)) {
                    settingsX = pSettingsWindow.screen.virtualX
                            + pSettingsWindow.screen.width - settingsWidth
                }
                if (iAppwin.visibility != ApplicationWindow.Maximized
                        && ((settingsY + settingsHeight) > pSettingsWindow.screen.virtualY
                            + pSettingsWindow.screen.height)) {
                    settingsY = pSettingsWindow.screen.virtualY
                            + pSettingsWindow.screen.height - settingsHeight
                }

                pSettingsWindow.width = settingsWidth
                pSettingsWindow.height = settingsHeight
                pSettingsWindow.x = settingsX
                pSettingsWindow.y = settingsY

                pSettingsWindow.show()

                plrBackend.pSettingWindowInitializing = false
            }
        }
    }

    function fOpenMacroManagerWindow() {
        if (pMacroManagerWindow !== null || !iSidebar.pIsMacroEnabled)
            return
        if (!mnsBackend.isMacroPlayingOrRecording()) {
            plrBackend.temporarilyToggleSplitAds(false)
        }

        if (mnsBackend.onOpenMacroManagerWindow()) {
            var component = Qt.createComponent("MnsMacroManagerWindow.qml")
            if (component.status === Component.Ready) {
                var macroX = uiBackend.confGetInstanceI32("macro_win_x")
                var macroY = uiBackend.confGetInstanceI32("macro_win_y")
                if(macroX === -1 || macroY === -1){
                    pMacroManagerWindow = component.createObject(iAppwin, { "pRunningMacroName": iTopbar.pCurrentRunningMacroName })
                }
                else{
                    var macroScreen = iAppwin.screen
                    var screens = Qt.application.screens;
                    for (var i = 0; i < screens.length; ++i){
                        if (screens[i].name === uiBackend.confGetInstanceString("macro_win_screen")){
                            macroScreen = screens[i]
                        }
                    }
                    macroX += macroScreen.virtualX
                    macroY += macroScreen.virtualY
                    var macroHeight = uiBackend.confGetInstanceI32("macro_win_height") < 360 ? 360 : uiBackend.confGetInstanceI32("macro_win_height")

                    pMacroManagerWindow = component.createObject(iAppwin, { "pRunningMacroName": iTopbar.pCurrentRunningMacroName,
                                                                     "x": macroX,
                                                                     "y": macroY,
                                                                     "height": macroHeight,
                                                                     "screen": macroScreen})
                }

                pMacroManagerWindow.stopMacro.connect(fStopMacro);
                pMacroManagerWindow.pauseMacro.connect(fToggleMacroPause);
                pMacroManagerWindow.show()
                pMacroManagerWindow.fAdjustLocation()
            }
        }
        else
        {
            if (pFullScreenPopupControl == null)
            {
                pFullScreenPopupControl = iFullScreenPopupControlComponent.createObject(iAppwin, {"pMessage" : qsTranslate("QObject", "Please stop the recording first")})
                pFullScreenPopupControl.fShowMessage(true)
            }
        }
    }

    function fShowInstallApkFailureMsg() {
        iApkInstallFailurePrompt.createObject(iAppwin, { "transientParent": iAppwin }).show()
    }

    function fStopMacro() {
        iTopbar.fStopMacro()
    }

    function fToggleMacroPause() {
        iTopbar.fToggleMacroPause()
    }

    function fPauseResumeSync() {
        iTopbar.fPauseResumeSync()
    }

    function fToggleEcoMode(enable) {
        pIsEcoModeEnabled = enable
    }

    function fTopbarMacroControlVisibility(visible, isMacroRecording, enableTimer) {
        pIsMacroRunning = visible
        iTopbar.pMacroRecordingInProcess = isMacroRecording
        iTopbar.pMacroTimerRunning = enableTimer
        iTopbar.pEnableTimer = enableTimer
        iTopbar.fReloadTopbarElements()
    }

    function fTopbarMacroControlNameUpdate(macroName, dependentName) {
        iTopbar.pCurrentRunningMacroName = macroName
        iTopbar.pCurrentRunningDependentMacroName = dependentName

        if (pMacroManagerWindow !== null)
            pMacroManagerWindow.pRunningMacroName = macroName
    }

    function fTopbarMacroControlIterationUpdate(count) {
        iTopbar.pMacroIterationText = qsTranslate("QObject", "%1 time").arg(count)
    }

    function fShowMacroMessageToast(code) {
        if(pMacroManagerWindow !== null)
            pMacroManagerWindow.fShowMacroMessageToast(code)
    }

    function fShowMacroImportSuccessToast(code, count) {
        if(pMacroManagerWindow !== null)
            pMacroManagerWindow.fShowMacroImportSuccessToast(code, count)
    }

    function fShowMemoryTrimmedToast(mbMemory) {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "%1 MB memory freed up").arg(mbMemory) } )
        toast.fShowMessage()
        iSidebar.fHideTrimMemoryLoader()
    }

    function fShowFileNotSupported(fileName) {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "%1 file not supported").arg(fileName) } )
        toast.fShowMessage()
    }

    function fShowRawModeWarning(editConf) {
        plrBackend.showRawModeWarningPopup()
        if (editConf) {
            uiBackend.confSetBool("bst.show_raw_mode_warning", false)
            uiBackend.confCommit()
        }
    }

    function fImportMacros() {
        if(pMacroManagerWindow !== null)
            pMacroManagerWindow.fImportMacros()
    }

    function fSetImageName(imageName) {
        iTopbar.fSetImageName(imageName)
    }

    function fUpdateSyncUiState(visible, source) {
        // visible to IsSyncing, source to isSource
        visible = visible === true
        iTopbar.pSyncSource = source === true
        if(!iTopbar.pSyncSource)
            iSidebar.pIsSyncEnable = !visible
        else
            iSidebar.pIsSyncColorAsset = visible

        if(!visible)
            iSidebar.pIsSyncColorAsset = false
        iTopbar.fUpdateSyncState(visible)
        iTopbar.fReloadTopbarElements()

        if(pSyncOperationsWindow !== null)
            pSyncOperationsWindow.fSetSyncState(visible)
    }

    function fOpenSyncOperationsWindow() {
        if(pSyncOperationsWindow !== null || !iSidebar.pIsSyncEnable)
            return
        var component = Qt.createComponent("MnsSyncOperationWindow.qml")
        if (component.status === Component.Ready) {
            pSyncOperationsWindow = component.createObject(iAppwin, {"pIsSyncActive" : iSidebar.pIsSyncColorAsset})
            pSyncOperationsWindow.show()
        }
    }

    function fOpenLaunchBsxWindow() {
        if(pLaunchBsxWindow != null)
            return

        var component = Qt.createComponent("PlrLaunchBsxWindow.qml")
        if (component.status === Component.Ready) {
            pLaunchBsxWindow = component.createObject(iAppwin, { "transientParent": iAppwin })
            pLaunchBsxWindow.show()
        }
    }

    function fCloseSyncWindow() {
        if(pSyncOperationsWindow != null)
            pSyncOperationsWindow.close()
    }

    function fStopSyncOperations() {
        if(pSyncOperationsWindow != null)
            pSyncOperationsWindow.fSyncStopped()
        else
            mnsBackend.onSyncStopped()
    }

    function fShowSettingToast(message) {
        pSettingsWindow.fShowToast(qsTranslate("QObject", message))
    }

    function fCloseMacroManagerWindow() {
        if(pMacroManagerWindow != null)
            pMacroManagerWindow.close()
    }

    function fDestroyMacroManagerWindow() {
        if(pMacroManagerWindow != null) {
            pMacroManagerWindow.fOnCloseOperation()
            pMacroManagerWindow.fSaveMacroWindowGeometry()
            pMacroManagerWindow.destroy()
            pMacroManagerWindow = null
        }
    }

    function fShowImportSuccessPopup() {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "File(s) imported to Media Manager") } )
        toast.fShowMessage()
    }

    function fShowAllImportFailedPopup() {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "Error importing files to Media Manager") } )
        toast.fShowMessage()
    }

    function fShowPartialImportFailedPopup() {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "Error importing some files. Check Media Manager to review") } )
        toast.fShowMessage()
    }

    function fShowImportFolderUnsupportedPopup() {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "Error: Importing a folder is not supported") } )
        toast.fShowMessage()
    }

    function fOpenScreenTranslateWindow() {
        if(pScreenTranslateWindow !== null)
            return

        var component = Qt.createComponent("PlrScreenTranslateWindow.qml")
        if (component.status === Component.Ready) {
            plrBackend.getTranslatedImage()
            pScreenTranslateWindow = component.createObject(iAppwin)
            pScreenTranslateWindow.show()
        }
    }

    function fTranslatedImageResponse(success) {
        if(pScreenTranslateWindow !== null) {
            success = success === true
            if(success)
                pScreenTranslateWindow.pImageVisible = true === true
            else
                pScreenTranslateWindow.fShowErrorMsg()
        }
    }

    function fReOpenScreenTranslateWindow() {
        pScreenTranslateWindow = null
        fOpenScreenTranslateWindow()
    }

    function fUiShowPanControlTooltip(panKey) {
        pFullScreenPopupControl = iFullScreenPopupControlComponent.createObject(iAppwin, {"pMessage" : qsTranslate("QObject", "Press {%1} to use mouse for look around.").arg(panKey)})
        pFullScreenPopupControl.fShowMessage(true)
    }

    function fGamepadDetectionMessage() {
        if (iAppwin.visibility !== ApplicationWindow.Hidden && pIsBootComplete && kmmBackend.gamepadConnectedState) {
            if (iAppwin.visibility === ApplicationWindow.Minimized)
                iAppwin.visibilityChanged.connect(fShowGamepadDetectionMessage);
            else
                fShowGamepadDetectionMessage()
        }
    }

    function fShowGamepadDetectionMessage() {
        if (pGamepadDetectedDialog != null || pFlePopupWindow != null)
            return

        var gamepadDetectionEnabled = uiBackend.confGetBool("bst.enable_gamepad_detection");
        if (gamepadDetectionEnabled) {
            var showGamepadDetectionMessage = uiBackend.confGetBool("bst.show_gamepad_detection_message")
            var isDefaultInstance = plrBackend.getVmName().indexOf("_") === -1
            if(isDefaultInstance && showGamepadDetectionMessage) {
                pGamepadDetectedDialog = iGamepadDetectionMessageComponent.createObject(iAppwin, { "transientParent": iAppwin })
                pGamepadDetectedDialog.show()
            }
            else {
                var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "Gamepad connected") })
                toast.fShowMessage()
            }
        }
        iAppwin.visibilityChanged.disconnect(fShowGamepadDetectionMessage)
    }

    function fShowCheckEffectUpgrade() {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "Checking Effect Update...") } )
        toast.fShowMessage()
    }

    function fShowEffectUpgradeFinished() {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "Effects have been updated") } )
        toast.fShowMessage()
    }

    function fShowGameControlsUpdatedInfo(articleKey) {
        if (pGameControlsUpdatedDialog != null || (!iSidebar.pIsSyncEnable && !iTopbar.pSyncSource))
            return

        pArticleKey = articleKey
        pGameControlsUpdatedDialog = iGameControlsUpdatedPrompt.createObject(iAppwin, { "transientParent": iAppwin })
        pGameControlsUpdatedDialog.show()
    }

    function fShowMultipleCameraDetectedPrompt() {
        if(pMultipleCamerasDetectedDialog != null || pSettingsWindow != null)
            return

        pMultipleCamerasDetectedDialog = iMultipleCamerasConnectedPrompt.createObject(iAppwin, { "transientParent": iAppwin })
        pMultipleCamerasDetectedDialog.show()
    }

    function fCreateFlePopup(packageName, instance) {
        if(pFlePopupWindow != null)
            return

        var component = Qt.createComponent("PlrLaunchAppWindow.qml")
        if (component.status === Component.Ready) {
            pFlePopupWindow = component.createObject(iAppwin, {
                                                         "transientParent": iAppwin,
                                                         "x": Qt.binding(function() { return iAppwin.x + plrBackend.plrWinOffsets[0] - adsBackend.pExtendedBannerAdWidth + (iAppwin.width - plrBackend.plrWinOffsets[0] + adsBackend.pExtendedBannerAdWidth - plrBackend.plrWinOffsets[1] - this.width) / 2 }),
                                                         "y": Qt.binding(function() { return iAppwin.y + plrBackend.plrWinOffsets[2] + (iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3] - this.height) / 2 })
                                                     })
            pFlePopupWindow.loadWindow(packageName, instance)
        }
    }

    function fCloseFlePopup() {
        if(pFlePopupWindow != null)
            pFlePopupWindow.destroy()
    }

    function fShowFlePopup(store, packageName, extraData, source, instance) {
        fCreateFlePopup(packageName, instance)

        pFlePopupWindow.installClicked.connect(function() {
            plrBackend.launchAppStore(store, packageName, extraData, source)
        });

        pFlePopupWindow.show()
    }

    function fShowQuestNotificationWindow(notificationJson, contentTitle, contentText, buttonText, disableSound) {
        if(pNotificationBarWindow != null) {
            if(iSidebar.fCheckIfMomentsNotificationIsVisible())
                pNotificationBarWindow.close()
            else
                return
        }

        var component = Qt.createComponent("PlrNotificationBarWindow.qml")
        if (component.status === Component.Ready) {
            pNotificationBarWindow = component.createObject(iAppwin, {
                                                                "x": Qt.binding(function() { return iAppwin.x + plrBackend.plrWinOffsets[0] }),
                                                                "y": Qt.binding(function() { return iAppwin.y + plrBackend.plrWinOffsets[2] }),
                                                                "width": Qt.binding(function() { return iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] }),
                                                                "pNotificationJson": notificationJson,
                                                                "pContentTitle": contentTitle,
                                                                "pContentText": contentText,
                                                                "pButtonText": buttonText,
                                                                "pEnableSound": !disableSound
                                                            })
            pNotificationBarWindow.buttonClicked.connect(fNotificationBarButtonClicked);
            pNotificationBarWindow.show()
            pIsQuestNotificationOpen = true
        }
    }

    function fNotificationBarButtonClicked(notificationJson) {
        plrBackend.onNotificationBarButtonClicked(notificationJson)
        plrBackend.sendNotificationBarStats("notification_bar_claimBtn_clicked", notificationJson)
    }

    function fShowChargerWarning() {
        if(pChargerWarningPopupWindow != null)
            return

        if(plrBackend.isChargerWarningRequired()) {
            pChargerWarningPopupWindow = iChargerWarningComponent.createObject(iAppwin, { "transientParent": iAppwin })
            pChargerWarningPopupWindow.show()
            plrBackend.sendPopupStats("charger_warning", "impression")
        }
        else {
            plrBackend.releaseDisplayChargerWarningLock()
        }
    }

    function fShowSigninRequiredDialog(loginTrigger) {
        if(pSigninRequiredWindow != null)
            return

        pSigninRequiredWindow = iSignInRequiredPrompt.createObject(iAppwin, {"pLoginTrigger": loginTrigger})
        pSigninRequiredWindow.show()
    }

    function fShowLinkNowggAccountDialog(videoId, showDoNotShowAgain) {
        if(pLinkNowggAccountWindow != null)
            return

        pLinkNowggAccountWindow = iLinkNowggAccountPrompt.createObject(iAppwin, {"videoId": videoId, "pShowDoNotShowAgain": showDoNotShowAgain})
        pLinkNowggAccountWindow.show()
        nowggBackend.sendMomentsStats("moments_link_account_popup_shown", nowggBackend.pLinkAccountSource)
    }

    function fShowNowbuxIapRewardDialog(success, data) {
        if (success)
            iTopbar.fShowNowbuxIapRewardDialog(data)
    }

    function fShowNowbuxSigninRewardDialog(success, data, loginWithIapTransactions) {
        if (pNowbuxSigninSuccessfulPopupWindow != null)
            pNowbuxSigninSuccessfulPopupWindow.close()

        if (success) {
            if (loginWithIapTransactions) {
                iNowbuxSignInWithIapRewardSuccess.createObject(iAppwin, { "transientParent": iAppwin }).show()
            }
            else {
                iNowbuxSignInRewardSuccess.createObject(iAppwin, {
                                                            "transientParent": iAppwin,
                                                            "pNowbuxRewardWonAmount": data.pNowbuxWon,
                                                            "pNowbuxRewardWonDescription": data.pDescription
                                                        }).show()
            }
        }
    }

    function fCloseNowbuxSigninSuccessfulPopupWindow() {
        if (pNowbuxSigninSuccessfulPopupWindow != null)
            pNowbuxSigninSuccessfulPopupWindow.close()
    }

    function fShowNowbuxSigninSuccessfulDialog() {
        if (pCheckBrowserPopupWindow != null)
            pCheckBrowserPopupWindow.close()

        if(pNowbuxSigninSuccessfulPopupWindow != null)
            return

        pNowbuxSigninSuccessfulPopupWindow = iNowbuxSignInSuccessfulPrompt.createObject(iAppwin, { "transientParent": iAppwin })
        pNowbuxSigninSuccessfulPopupWindow.show()
    }

    function fShowSigninSuccessfulDialog(loginTrigger) {
        if (pCheckBrowserPopupWindow != null)
            pCheckBrowserPopupWindow.close()

        if(pSigninSuccessfulPopupWindow != null)
            return

        pSigninSuccessfulPopupWindow = iSignInSuccessfulPrompt.createObject(iAppwin, {
                                                                                "transientParent": iAppwin,
                                                                                "pLoginTrigger": loginTrigger
                                                                            })
        pSigninSuccessfulPopupWindow.show()
    }

    function fShowNowbuxSignInDialog(skipLoginEnabled, signinSource, packageName) {
        if (pNowggSigninpopupWindow != null)
            pNowggSigninpopupWindow.close()

        pNowggSigninpopupWindow = iNowbuxSignIn.createObject(iAppwin, { "transientParent": iAppwin,
                                                                        "pSkipLoginEnabled": skipLoginEnabled,
                                                                        "pSigninSource" : signinSource,
                                                                        "pPackageName": packageName})
        pNowggSigninpopupWindow.show()
        nowggBackend.sendNowbuxStats("nowgg_login_popup_shown", signinSource, packageName)
    }

    function fShowDisableMomentsDialog() {
        if(pDisableMomentsPopupWindow != null)
            return

        pDisableMomentsPopupWindow = iDisableMomentPrompt.createObject(iAppwin, { "transientParent": iAppwin })
        pDisableMomentsPopupWindow.show()
    }

    function fShowDisableRecordingDialog() {
        if(pDisableRecordingPopupWindow != null)
            return

        pDisableRecordingPopupWindow = iDisableRecordingPrompt.createObject(iAppwin, { "transientParent": iAppwin })
        pDisableRecordingPopupWindow.show()
    }

    function fShowCheckBrowserDialog(source) {
        if(pCheckBrowserPopupWindow != null)
            return

        pCheckBrowserPopupWindow = iCheckBrowserPrompt.createObject(iAppwin, {
                                                                        "transientParent": iAppwin,
                                                                        "source": source
                                                                    })
        pCheckBrowserPopupWindow.show()
    }

    function fShowLowDiskSpaceDialog(isMoment) {
        if(pLowDiskSpacePopupWindow != null)
            return

        pLowDiskSpacePopupWindow = iLowDiskSpacePrompt.createObject(iAppwin, {
                                                                        "transientParent": iAppwin,
                                                                        "isMoment": isMoment
                                                                    })
        pLowDiskSpacePopupWindow.show()
    }

    function fShowSignOutDialog() {
        if(pSignOutPopupWindow != null)
            return

        var userName = uiBackend.confGetString("bst.nowgg_username")
        var email = uiBackend.confGetString("bst.nowgg_email")
        if (userName === "")
            userName = "John Wick"
        if (email === "")
            email = "john@wick.com"

        pSignOutPopupWindow = iSignOutPrompt.createObject(iAppwin, {
                                                              "transientParent": iAppwin,
                                                              "userName": userName,
                                                              "userEmail": email
                                                          })
        pSignOutPopupWindow.show()
    }

    function fShowSignOutConfirmationDialog() {
        if(pSignOutConfirmationPopupWindow != null)
            return

        pSignOutConfirmationPopupWindow = iSignOutConfirmationPrompt.createObject(iAppwin, { "transientParent": iAppwin })
        pSignOutConfirmationPopupWindow.show()
    }

    function fShowAdsInfoPrompt(isFromHtmlAds) {
        if (pSettingsWindow == null && iAppwin.visibility != ApplicationWindow.Minimized) {
                if (adsInfoPopup.hasPopupData)
                    iAdsInfoComponent.createObject(iAppwin, { "transientParent": iAppwin, "isFromHtmlAds": isFromHtmlAds }).show()
                else
                    iAdsInfoPrompt.createObject(iAppwin, { "transientParent": iAppwin }).show()
        }
    }

    function fShowBootAdsInfoPrompt() {
        if (pSettingsWindow == null && iAppwin.visibility != ApplicationWindow.Minimized) {
            iAdsEnableOnBootPrompt.createObject(iAppwin, { "transientParent": iAppwin }).show()
        }
    }

    function fShowAirplaneSettingChangedToast(airplaneModeEnabled) {
        var message = airplaneModeEnabled ? qsTranslate("QObject", "Airplane Mode On") :  qsTranslate("QObject", "Airplane Mode Off")
        var toast = iToastComponent.createObject(iAppwin, { "pToastMessage": message,
                                                     "pBackground.border.color" : UiTheme.colors.primary50,
                                                     "pBackground.color" : UiTheme.colors.overlayPrimary,
                                                     "pBackground.radius" : 0,
                                                     "pBackground.opacity" : 0.85,
                                                     "pBackground.leftPadding": 40,
                                                     "pBackground.rightPadding": 40,
                                                     "pBackground.topPadding": 12,
                                                     "pBackground.bottomPadding": 12})

        toast.fShowMessage({verticalOffsetRatio:0.65})
    }

    function fShowTroubleSigningInBtn(isVisible) {
        if(pTroubleSigningInBtn != null)
            pTroubleSigningInBtn.close()

        if(!isVisible)
            return

        pTroubleSigningInBtn = iFeedbackPillComponent.createObject(iAppwin)
        pTroubleSigningInBtn.show()

        if(!pIsTroubleSigningInBtnShown) {
            pIsTroubleSigningInBtnShown = true
            plrBackend.sendTroubleSigningInBtnStats("trouble_signing_in_shown")
        }
    }

    function fShowSignInScrollBtn(isVisible) {
        if(pSignInScrollBtn != null)
            pSignInScrollBtn.close()

        if(!isVisible)
            return

        pSignInScrollBtn = iSignInScrollBtnComponent.createObject(iAppwin)
        pSignInScrollBtn.show()
        plrBackend.bringChatOnTop()
    }

    function fShowExitFullScreenPopup() {
        if(iAppwin.visibility != ApplicationWindow.FullScreen)
            return

        if (pFullScreenPopupControl != null)
            pFullScreenPopupControl.fClosePopup()
        if(pExitFullScreenControl == null) {
            pExitFullScreenControl = iExitFullScreenPopupControlComponent.createObject(iAppwin)
            pExitFullScreenControl.fShowPopup()
        }
    }

    function fOpenMediaGalleryWindow() {
        if (pMediaGalleryWindow != null)
            return

        var component = Qt.createComponent("PlrMediaGalleryWindow.qml")
        if (component.status === Component.Ready) {
            pMediaGalleryWindow = component.createObject(iAppwin, { "transientParent": iAppwin })
            pMediaGalleryWindow.show()
        }
    }

    function fShowMomentsNotificationWindow(contentText) {
        if(pNotificationBarWindow != null)
            return

        var component = Qt.createComponent("PlrNotificationBarWindow.qml")
        if (component.status === Component.Ready) {
            pNotificationBarWindow = component.createObject(iAppwin, {
                                                                "x": Qt.binding(function() { return iAppwin.x + plrBackend.plrWinOffsets[0] }),
                                                                "y": Qt.binding(function() { return iAppwin.y + plrBackend.plrWinOffsets[2] }),
                                                                "width": Qt.binding(function() { return iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] }),
                                                                "pContentText": contentText,
                                                                "pButtonVisible": false,
                                                                "pSendStats": false,
                                                                "pEnableSound": false,
                                                                "pShowProductIcon": false
                                                            })
            pNotificationBarWindow.buttonClicked.connect(fNotificationBarButtonClicked);
            pNotificationBarWindow.show()
        }
    }

    function fShowHighlightsCompleteNotification(notificationJson, contentTitle, contentText, buttonText) {
        if (pNotificationBarWindow != null) {
            pNotificationBarWindow.close()
        }

        var component = Qt.createComponent("PlrNotificationBarWindow.qml")
        if (component.status === Component.Ready) {
            pNotificationBarWindow = component.createObject(iAppwin, {
                                                                "x": Qt.binding(function() { return iAppwin.x + plrBackend.plrWinOffsets[0] }),
                                                                "y": Qt.binding(function() { return iAppwin.y + plrBackend.plrWinOffsets[2] }),
                                                                "width": Qt.binding(function() { return iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] }),
                                                                "pNotificationJson": notificationJson,
                                                                "pContentTitle": contentTitle,
                                                                "pContentText": contentText,
                                                                "pButtonText": buttonText,
                                                                "pSendStats": true,
                                                                "pEnableSound": false,
                                                                "pShowProductIcon": false,
                                                                "pWhiteButton": true
                                                            })
            pNotificationBarWindow.buttonClicked.connect(fHighlightsNotificationClicked);
            pNotificationBarWindow.show()
        }
    }

    function fHighlightsNotificationClicked(notificationJson) {
        plrBackend.onHighlightsNotificationClicked(notificationJson)
    }

    function fCloseMomentsNotificationWindow() {
        if(pNotificationBarWindow != null && !pIsQuestNotificationOpen)
            pNotificationBarWindow.fAnimateAndClose()
    }

    function fCloseMediaGalleryWindow() {
        if (pMediaGalleryWindow != null) {
            pMediaGalleryWindow.close()
            pMediaGalleryWindow = null
        }
    }

    function fOpenBlueStacksStoreWindow(url) {
        if (pBlueStacksStoreWindow == null) {
            var comp = Qt.createComponent("UiBrowserWindow.qml")
            if (comp.status === Component.Ready) {
                pBlueStacksStoreWindow = comp.createObject(null, {
                                                               "x": 300,
                                                               "y": 50,
                                                               "width": 1000,
                                                               "height": 800,
                                                               "pShowTabsOnTopBar": true,
                                                               "pCreateTabBtnVisible": false,
                                                               "visibility": ApplicationWindow.Maximized
                                                           })
            }
        }
        else {
            pBlueStacksStoreWindow.createNewTab()
        }
        pBlueStacksStoreWindow.pCurrentWebView.url = url

        if (pBlueStacksStoreWindow.visibility == Window.Maximized)
            pBlueStacksStoreWindow.showMaximized()
        else
            pBlueStacksStoreWindow.showNormal()

        pBlueStacksStoreWindow.raise()
        pBlueStacksStoreWindow.requestActivate()

        plrBackend.sendWebshopIAPStats(url)
    }

    function fShowInterstitialAdsInfo(enable) {
        if (enable) {
            if(pInterstitialAdsInfoWindow != null)
                return

            pInterstitialAdsInfoWindow = iInterstitialAdsInfoComponent.createObject(iAppwin)
            if(iAppwin.height > 260)
                pInterstitialAdsInfoWindow.show()
        } else {
            if (pInterstitialAdsInfoWindow != null)
                pInterstitialAdsInfoWindow.destroy()
        }
    }

    function fShowSplitScreenOverlay(enable) {
        if (enable) {
            if(pSplitScreenOverlayWindow == null)
                pSplitScreenOverlayWindow = iSplitScreenOverlayComponent.createObject(iAppwin)

            pSplitScreenOverlayWindow.show()
        } else {
            if (pSplitScreenOverlayWindow != null)
                pSplitScreenOverlayWindow.hide()
        }
    }

    function fShowNowbuxRedeemGiftCardDialog(success, errorMsg) {
        iTopbar.fShowNowbuxRedeemGiftCardDialog(success, errorMsg)
    }

    function fShowLiveStreamingUrlPrompt(shareUrl) {
        fCloseCreateRoomLoadingWindow()
        if(pStartLiveStreamingWindow != null)
            return

        pStartLiveStreamingWindow = iLiveStreamUrlPrompt.createObject(iAppwin, {"streamUrl" : shareUrl, "streamerName" : lsgBackend.getStreamerName()})
        pStartLiveStreamingWindow.show()
    }

    function fShowStreamingOnboardingMsg() {
        if(pLiveStreamOnboardingWindow != null || pLiveStreamExploreWindow != null) {
            return
        }

        if(lsgBackend.isAppSupportedForStreaming()) {
            lsgBackend.pIsScreenStreamingOn = true
            pLiveStreamOnboardingWindow = iLiveStreamOnboardingPrompt.createObject(iAppwin, { "transientParent": iAppwin })
            pLiveStreamOnboardingWindow.show()
        }
        else {
            var component = Qt.createComponent("PlrGameRoomExploreWindow.qml")
            if (component.status === Component.Ready) {
                pLiveStreamExploreWindow = component.createObject(iAppwin, { "transientParent": iAppwin })
                pLiveStreamExploreWindow.show()
            }
        }
    }

    function fShowGameRoomInfoWindow() {
        if(pLiveStreamInfoWindow != null) {
            return
        }
        pLiveStreamInfoWindow = iShowGameRoomInfoPrompt.createObject(iAppwin, { "transientParent": iAppwin })
        pLiveStreamInfoWindow.show()
    }

    function fCloseCreateRoomLoadingWindow() {
        if(pLiveStreamInfoWindow != null) {
            pLiveStreamInfoWindow.close()
        }
    }

    function fGameRoomChatPublish(sender, eventType, eventValue, shareUrl) {
        if (pGameRoomChatWindow == null) {
            var component = Qt.createComponent("LsgGameRoomChatWindow.qml")
            if (component.status === Component.Ready) {
                pGameRoomChatWindow = component.createObject(iAppwin, {"pStreamUrl" : shareUrl})
                pGameRoomChatWindow.show()
            }
        }

        if (eventType === "comment"  || eventType === "chat_msg") {
            pGameRoomChatWindow.fPublishChat(sender, eventValue)
        }
    }

    function fCloseGameRoomChatWindow() {
        if(pGameRoomChatWindow != null) {
            pGameRoomChatWindow.destroy()
            pGameRoomChatWindow = null
        }
    }

    function fShowGameRoomCloseWarningMsg() {
        iCloseGameRoomComponent.createObject(iAppwin, { "transientParent": iAppwin }).show()
    }

    function fCloseStreamingPopups() {
        fCloseGameRoomChatWindow()
        fCloseCreateRoomLoadingWindow()
        if(pStartLiveStreamingWindow != null)
            pStartLiveStreamingWindow.close()
    }

    function fShowChatRoomWindow() {
        if(pStartLiveStreamingWindow != null)
            pStartLiveStreamingWindow.show()
        else if(pGameRoomChatWindow != null)
            pGameRoomChatWindow.show()
    }

    function fGameRoomViewerPublish(eventType, name) {
        if (pGameRoomChatWindow != null)
            pGameRoomChatWindow.fUpdateViewersModel(eventType, name)
    }

    function fShowSchemeChangedToast(schemeName) {
        var toast = iToastComponent.createObject(iAppwin, { pToastMessage: qsTranslate("QObject", "Control scheme switched to : %1").arg(schemeName) } )
        toast.fShowMessage({verticalOffsetRatio:0.15})
    }
}
