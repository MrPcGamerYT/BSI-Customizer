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

import UiToolTipControl 1.0
import UiShortcutToolTip 1.0
import QtQuick.Shapes 1.12
import DialogButtonModel 1.0
import DialogButtonModelList 1.0
import UiDialogWindow 1.0
import UiQuickWindow 1.0
import UiOverlayControl 1.0

import RecordModeEnum 1.0

import Qt5Compat.GraphicalEffects

Rectangle {
    id: iRoot
    objectName: "topbar"

    signal sidebarBtnClicked()
    signal sidebarHideClicked()
    signal hamburgerSettingClicked()

    property bool pCompLoaded: false
    property int pIconSize: 32
    property bool pCurrentTopmostState: false
    property bool pFixedWindowSizeState: false
    property real pFixedElementsWidth: (10 + iProductLogo.width) + (pIsMacroRunning ? iMacroTopbarControl.item.pCompFixedWidth : 0) +
                                       iMinimizeBtn.width + (iFixedResizeBtn.visible ? iFixedResizeBtn.width : 0) +
                                       iCloseBtn.width + (iSidebarShowBtn.visible ? iSidebarShowBtn.width : 0) +
                                       (iDisablePinToTopBtn.visible ? iDisablePinToTopBtn.width : 0) + (pIconSize/2) +
                                       (pIsScreenRecording ? iScreenRecordingTopbarControl.item.pCompFixedWidth : 0)

    property var pMacroRecordingStartTime: -1
    property var pMacroRecordingPausedTime: -1
    property var pScreenRecordingStartTime: -1
    property bool pEnableTimer: false
    property bool pMacroRecordingInProcess: false
    property bool pMacroTimerRunning: false
    property string pMacroIterationText: ""
    property string pCurrentRunningMacroName: ""
    property string pCurrentRunningDependentMacroName: ""
    property string pScreenRecordingTimerText: ""
    property bool pIsScreenRecording: false

    property bool pSyncControlVisible: false
    property bool pSyncSource: true

    property string pTopActivityAppLabel: ""
    property string pTopActivityAppPackage: ""
    property bool pShowFeedbackPopup: false
    property bool pShowQuestText: true
    property bool pIsQuestEnabled : false
    property bool pIsNowbuxEnabled: false
    property bool pShowNowbuxRewardsRedDot: false
    property bool pShowNowbuxLabel: true
    property string pLiveStreamingUsername: ""

    property UiWindowedPopup hamburgerWin: null
    property UiWindowedPopup helpWin: null
    property UiHelpPopup nowbuxRewardsWin: null
    property UiWindowedPopup nowggAccountSignoutWin: null
    property UiWindowedPopup nowggAccountSigninWin: null

    property UiQuickWindow pNowbuxAddGiftCardWindow: null

    component PlrTopbarElement: QtObject {
        property bool pVisible: true
        property int pWidth: 0

        signal changeVisibility(var visibility)
    }

    property list<QtObject> pNonFixedTopbarDynamicItems

    property list<QtObject> pNonFixedTopbarItems: [
        PlrTopbarElement {
            pVisible: true
            pWidth: (12 + Math.max(iVersionImgLayout.width, iBstTxt.implicitWidth))
            onChangeVisibility: function(visibility) {
                iVersionImgLayout.visible = visibility
                if((pIsMacroRunning || pIsMacroPause) && iMacroTopbarControl.sourceComponent != undefined)
                    iMacroTopbarControl.visible = true
                else
                    iMacroTopbarControl.visible = false

                if(!visibility) {
                    var pAvailablewidth = iTopbarRow.width  - pFixedElementsWidth
                    if(pIsScreenRecording) {
                        iBstTxt.pPreferredWidth = pAvailablewidth - (24 + (iScreenRecordingTopbarControl.item.pRecordingDetailRowVisible ? iScreenRecordingTopbarControl.item.pRecordingDetailWidth + 8 : 0))
                        if(pIsMacroPause || pIsMacroRunning) {
                            if(pAvailablewidth < 0) {
                                if(iMacroTopbarControl.sourceComponent != undefined)
                                    iMacroTopbarControl.visible = false
                            }
                            else {
                                iBstTxt.pPreferredWidth -= (iMacroTopbarControl.item.pIsMacroDetailVisible ? iMacroTopbarControl.item.pMacroDetailWidth + 8 : 0)
                                        + (iSyncTopbarControl.visible ? iSyncTopbarControl.width + 8 : 0)
                            }
                        }
                    }
                    else if(pIsMacroRunning || pIsMacroPause) {
                        if(iMacroTopbarControl.sourceComponent != undefined)
                            iMacroTopbarControl.visible = true
                        iBstTxt.pPreferredWidth = pAvailablewidth - (24 + (iMacroTopbarControl.item.pIsMacroDetailVisible ? iMacroTopbarControl.item.pMacroDetailWidth + 8 : 0) + (iSyncTopbarControl.visible ? iSyncTopbarControl.width + 8 : 0))
                    }
                    else
                        iBstTxt.pPreferredWidth = pAvailablewidth - 24

                    iBstTxt.visible = iBstTxt.pPreferredWidth > 4 ? true: false
                }
                else {
                    iBstTxt.visible = visibility
                    iBstTxt.pPreferredWidth = iBstTxt.implicitWidth
                }
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:cElzU2NyZWVuUmVjb3JkaW5n */
            pWidth: iScreenRecordingTopbarControl.sourceComponent != undefined ? iScreenRecordingTopbarControl.item.pCompVariableWidth : 0
            onChangeVisibility: function(visibility) {
                if (iScreenRecordingTopbarControl.sourceComponent != undefined)
                    iScreenRecordingTopbarControl.item.pRecordingDetailRowVisible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:cFN5bmNDb250cm9sVmlzaWJsZQ== */
            pWidth: iSyncTopbarControl.width
            onChangeVisibility: function(visibility) {
                iSyncTopbarControl.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:cElzTWFjcm9SdW5uaW5n */
            pWidth: iMacroTopbarControl.sourceComponent != undefined ? iMacroTopbarControl.item.pCompVariableWidth : 0
            onChangeVisibility: function(visibility) {
                if (iMacroTopbarControl.sourceComponent != undefined)
                    iMacroTopbarControl.item.pIsMacroDetailVisible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: true
            pWidth: iBackBtn.width
            onChangeVisibility: function(visibility) {
                iBackBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: true
            pWidth: iHomeBtn.width
            onChangeVisibility: function(visibility) {
                iHomeBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: true
            pWidth: iRecentBtn.width
            onChangeVisibility: function(visibility) {
                iRecentBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:dHJ1ZQ== */
            pWidth: iCfgBtn.width
            onChangeVisibility: function(visibility) {
                iCfgBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:dHJ1ZQ== */
            pWidth: iHelpBtn.width
            onChangeVisibility: function(visibility) {
                iHelpBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:aUFwcHdpbi5wSXNSYXdNb2Rl */
            pWidth: iRawModeWarningBtn.width
            onChangeVisibility: function(visibility) {
                iRawModeWarningBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:cElzTm93YnV4RW5hYmxlZA== */
            pWidth: iNowggAccountBtn.width
            onChangeVisibility: function(visibility) {
                iNowggAccountBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:cElzTm93YnV4RW5hYmxlZA== */
            pWidth: (12 + iNowbuxBtn.width)
            onChangeVisibility: function(visibility) {
                iNowbuxBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:cElzTm93YnV4RW5hYmxlZA== */
            onChangeVisibility: function(visibility) {
                pShowNowbuxLabel = visibility
            }
            Component.onCompleted: {
                pWidth = (iNowbuxBtn.width - 36)
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:cElzUXVlc3RFbmFibGVk */
            pWidth: 32
            onChangeVisibility: function(visibility) {
                iQuestBtn.visible = visibility
            }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:cElzUXVlc3RFbmFibGVk */
            onChangeVisibility: function(visibility) {
                pShowQuestText = visibility
            }
            Component.onCompleted: {
                pWidth = (iQuestBtn.width - 36)
            }
        }
    ]

    Component {
        id: iPlrTopbarIconElementDynamic
        PlrTopbarElement {
            property int pIndex: 0
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:dHJ1ZQ== */
            pWidth: 32
            onChangeVisibility: function(visibility) {
                plrBackend.pTopbarIconsList[pIndex].pVisible = visibility
                if (pIndex == 0)
                    iTopbarIconsList.visible = visibility
            }
        }
    }
    Component {
        id: iPlrTopbarTextElementDynamic
        PlrTopbarElement {
            property int pIndex: 0
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:dHJ1ZQ== */
            onChangeVisibility: function(visibility) {
                plrBackend.pTopbarIconsList[pIndex].pShowIconText = visibility
            }
            Component.onCompleted: {
                pWidth = (iTopbarIconsList.contentItem.children[pIndex].width - 36)
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        property var startingPosition
        property bool isPressed: false

        onPressed: function(mouse) {
            isPressed = true
            startingPosition  = Qt.point(mouse.x, mouse.y)
        }
        onReleased: {
            isPressed = false
            if ( iAppwin.y < Screen.virtualY) {
                iAppwin.y = Screen.virtualY
            }
        }

        onPositionChanged: function(mouse) {
            if (isPressed && iAppwin.visibility !== ApplicationWindow.Maximized) {
                iAppwin.x = mouseProvider.cursorPos().x - startingPosition.x - adsBackend.pExtendedBannerAdWidth
                iAppwin.y = mouseProvider.cursorPos().y - startingPosition.y
            }
        }

        onDoubleClicked: {
            iAppwin.visibility === ApplicationWindow.Windowed ?
                        plrBackend.onMaximizeClicked() :
                        plrBackend.onRestoreClicked()
        }
    }

    Component.onCompleted: {
        if (!pCompLoaded) {
            pCurrentTopmostState = uiBackend.confGetInstanceBool("pin_to_top")
            if (true === pCurrentTopmostState)
                iAppwin.flags = iAppwin.flags | Qt.WindowStaysOnTopHint

            pShowNowbuxRewardsRedDot = uiBackend.confGetInstanceBool("show_nowbux_rewards_red_dot_onboarding")

            pCompLoaded = true
        }
    }

    onWidthChanged: {
        if (pCompLoaded) {
            fReloadTopbarElements()
        }
    }

    UiRowLayout {
        id: iTopbarRow
        anchors.fill: parent
        spacing: 0

        UiImage {
            id: iProductLogo
            asset: "BlueStacksLogo"
            Layout.leftMargin: 10 + adsBackend.pSplitBannerAdWidth
            pImageWidth: 20
            pImageHeight: 20
        }

        UiColumnLayout{
            id: iTitleLayout
            Layout.leftMargin: 12
            Text {
                property int pPreferredWidth : iBstTxt.implicitWidth
                id: iBstTxt
                text: plrBackend.title
                Layout.fillHeight: true
                color: UiTheme.colors.primary10
                font: UiTheme.fonts.bodySmall
                fontSizeMode: Text.Fit
                horizontalAlignment: Text.AlignLeft
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.preferredWidth: pPreferredWidth + 12
            }
            UiRowLayout{
                id: iVersionImgLayout
                Text {
                    id: iVersionTxt
                    text: plrBackend.version
                    Layout.fillHeight: true
                    color: uiBackend.oem === "msi5" ? UiTheme.colors.overLayTint : UiTheme.colors.primary30
                    font: UiTheme.fonts.bodyTiny
                    fontSizeMode: Text.Fit
                    opacity: 0.7
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }

                Text {
                    id: iImageTxt
                    text: "N32"
                    Layout.fillHeight: true
                    color: uiBackend.oem === "msi5" ? UiTheme.colors.overLayTint : UiTheme.colors.primary30
                    font: UiTheme.fonts.bodyTiny
                    fontSizeMode: Text.Fit
                    Layout.leftMargin: 4
                    opacity: 0.7
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        UiImageButton {
            id: iBackBtn
            asset: "SidebarBack"
            Layout.leftMargin: 12
            enabled: iAppwin.pIsBootComplete || iAppwin.pShowAndroidAd
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            pUseShortcutTooltip: true
            UiShortcutToolTip.text: qsTranslate("QObject", "Back")
            UiShortcutToolTip.shortcutMap: [{ value: "go_back" }]
            onClicked: {
                plrBackend.logUIAction("Topbar", "Back")
                plrBackend.onBackClicked()
            }
        }

        UiImageButton {
            id: iHomeBtn
            asset: "SidebarHome"
            enabled: iAppwin.pIsBootComplete || iAppwin.pShowAndroidAd
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            pUseShortcutTooltip: true
            UiShortcutToolTip.text: qsTranslate("QObject", "Home")
            UiShortcutToolTip.shortcutMap: [{ value: "go_home" }]
            onClicked: {
                plrBackend.logUIAction("Topbar", "Home")
                plrBackend.onHomeClicked()
            }
        }

        UiImageButton {
            id: iRecentBtn
            asset: "SidebarRecents"
            enabled: iAppwin.pIsBootComplete || iAppwin.pShowAndroidAd
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            pUseShortcutTooltip: true
            UiShortcutToolTip.text: qsTranslate("QObject", "Recent apps")
            UiShortcutToolTip.shortcutMap: [{ value: "open_recent_apps" }]
            onClicked: {
                plrBackend.logUIAction("Topbar", "RecentApps")
                plrBackend.onRecentAppsClicked()
            }
        }

        UiText {
            text: "ENGG"
            Layout.leftMargin: 12
            font: UiTheme.fonts.titleMedium
            color: UiTheme.colors.dangerHover
            verticalAlignment: Text.AlignVCenter
            visible: plrBackend.pEnggEnvIndicatorVisible ? true : false
        }

        Item {
            Layout.fillWidth: true
        }

        Loader {
            id: iMacroTopbarControl
            Layout.leftMargin: 8
            visible: false /* BSI CUSTOMIZER: clean player UI */ && (sourceComponent != undefined)
            sourceComponent: pIsMacroRunning ? iMacroTopbarComponent : undefined
        }

        Loader {
            id: iSyncTopbarControl
            visible: false /* BSI CUSTOMIZER: clean player UI */ && (sourceComponent != undefined)
            sourceComponent: pSyncControlVisible ? iSyncTopbarComponent : undefined
        }

        Loader {
            id: iScreenRecordingTopbarControl
            visible: false /* BSI CUSTOMIZER: clean player UI */ && (sourceComponent != undefined)
            sourceComponent: pIsScreenRecording ? iScreenRecordingTopbarComponent : undefined
        }

        UiImageButton {
            id: iRawModeWarningBtn
            visible: false /* BSI CUSTOMIZER: clean player UI */ && (false)
            asset: "TitlebarError"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            onClicked: {
                plrBackend.logUIAction("Topbar", "RawModeWarning")
                iAppwin.fShowRawModeWarning(false)
            }
        }

        UiImageLabel {
            id: iQuestBtn
            Layout.preferredHeight: pIconSize
            visible: false /* BSI CUSTOMIZER: clean player UI */ && (pIsQuestEnabled)
            pText: ((iAppwin.width > iAppwin.height) && pShowQuestText) ? qsTranslate("QObject", "Play & Win") : ""
            pImageAsset: "PlayAndWin"
            padding: 8
            spacing: 4
            pBackgroundColor: pHovered ? UiTheme.colors.primary50 : UiTheme.colors.primary70
            pBorderColor: UiTheme.colors.primary80
            pBorderWidth: 1
            font: UiTheme.fonts.bodySmall
            pTextColor: UiTheme.colors.primary10
            UiToolTip.text: qsTranslate("QObject", "Play & Win")
            UiToolTip.visible: ((iAppwin.width < iAppwin.height)||!pShowQuestText) && pHovered
            onClicked: {
                plrBackend.logUIAction("Topbar", "Play & Win")
                plrBackend.onQuestIconClick("play_and_win", "")
            }
        }

        ListView {
            visible: false // BSI CUSTOMIZER: clean player UI
            id: iTopbarIconsList
            Layout.preferredHeight: pIconSize
            model: plrBackend.pTopbarIconsList
            orientation: ListView.Horizontal
            interactive: false
            implicitWidth: contentWidth
            layoutDirection: Qt.RightToLeft
            delegate: UiImageLabel {
                height: pIconSize
                pText: ((iAppwin.width > iAppwin.height) && modelData.pShowIconText) ? modelData.pDescription : ""
                pImageSource: modelData.pIcon
                visible: modelData.pVisible && iAppwin.pIsBootComplete
                padding: 8
                spacing: 4
                pBackgroundColor: pHovered ? modelData.pBackgroundColorHover : modelData.pBackgroundColor
                pBorderColor: UiTheme.colors.primary80
                pBorderWidth: 1
                font: UiTheme.fonts.bodySmall
                pTextColor: UiTheme.colors.primary10
                UiToolTip.text: modelData.pDescription
                UiToolTip.visible: (pText === "") && pHovered
                onClicked: {
                    plrBackend.logUIAction("Topbar", modelData.pDescription)
                    plrBackend.onTopbarIconClicked(modelData)
                }
                Component.onCompleted: {
                    var item = iPlrTopbarIconElementDynamic.createObject(iTopbar)
                    item.pIndex = index
                    pNonFixedTopbarDynamicItems.push(item)
                    var textItem = iPlrTopbarTextElementDynamic.createObject(iTopbar)
                    textItem.pIndex = index
                    pNonFixedTopbarDynamicItems.push(textItem)
                }
            }
        }

        UiRowLayout {
            id: iNowbuxBtn
            visible: false /* BSI CUSTOMIZER: clean player UI */ && (pIsNowbuxEnabled)
            Layout.preferredHeight: pIconSize
            topPadding: 4
            bottomPadding: 4
            leftPadding: 8
            rightPadding: 8
            color: UiTheme.colors.primary70
            border.width: 1
            border.color: UiTheme.colors.primary80
            pMouseArea {
                anchors.fill: iNowbuxBtn
                enabled: true
                onClicked: {
                    plrBackend.logUIAction("Topbar", "nowBux")
                    nowggBackend.sendNowbuxStats("topbar_nowbux_clicked")
                    fOpenNowbuxRewardsPopup()
                }
            }

            UiRowLayout {
                Layout.fillHeight: true
                spacing: 8
                rightPadding: pShowNowbuxLabel ? 16 : 0
                radius: 16
                color: UiTheme.colors.primary90
                UiImage {
                    asset:uiBackend.oem === "nxt" ? "NowBux" : ""
                    pImageWidth: 20
                    pImageHeight: 20
                }
                Text {
                    visible: pShowNowbuxLabel
                    text: nowggBackend.pNowbuxBalance
                    font: UiTheme.fonts.bodyExtraLarge
                    color: UiTheme.colors.white
                }
            }
            Rectangle {
                id: iRedDotNowbux
                visible: pShowNowbuxRewardsRedDot && nowggBackend.pIsNowbuxTabsEnabled
                Layout.alignment: Qt.AlignTop
                Layout.leftMargin: -6
                Layout.preferredWidth: 6
                Layout.preferredHeight: 6
                radius: 3
                color: UiTheme.colors.dangerNormal
            }
        }

        UiImageButton {
            id: iNowggAccountBtn

            property bool pUrlLoadFailed: false
            property string pAvatarUrl: uiBackend.confGetInstanceString("nowgg_userAvatarUrl")

            visible: false /* BSI CUSTOMIZER: clean player UI */ && (pIsNowbuxEnabled)
            horizontalPadding: 6
            verticalPadding: 6
            UiToolTip.text: qsTranslate("QObject", "My account")
            background: Rectangle {
                color: iNowggAccountBtn.down
                       ? UiTheme.colors.primary90
                       : iNowggAccountBtn.hovered
                         ? UiTheme.colors.primary60
                         : UiTheme.colors.primary80
            }

            Connections {
                target: nowggBackend
                function onLinkAccountAvatarUrlChanged(url) {
                    iNowggAccountBtn.pAvatarUrl = url
                }
            }

            onPressed: {
                plrBackend.logUIAction("Topbar", "NowggAccount")
                nowggBackend.sendNowbuxStats("topbar_account_clicked")
                if (uiBackend.confGetInstanceBool("nowbux_signin_completed"))
                    fOpenNowggAccountSignoutPopup()
                else
                    fShowNowbuxSignInDialog(false, "my_account", "")
            }

            contentItem: UiImage {
                anchors.centerIn: parent
                source: uiBackend.oem === "nxt" ? ( iNowggAccountBtn.pAvatarUrl !== "" && !iNowggAccountBtn.pUrlLoadFailed
                        ? iNowggAccountBtn.pAvatarUrl
                        : iNowggAccountBtn.down
                          ? "qrcAssets/TitlebarAccountLogin_click.svg"
                          : iNowggAccountBtn.hovered
                            ? "qrcAssets/TitlebarAccountLogin_hover.svg"
                            : "qrcAssets/TitlebarAccountLogin.svg") : ""
                pImageWidth: 20
                pImageHeight: 20

                onStatusChanged: {
                    if (status == Image.Error)
                        iNowggAccountBtn.pUrlLoadFailed = true
                }

                UiImage {
                    visible: iNowggAccountBtn.pAvatarUrl !== "" && !iNowggAccountBtn.pUrlLoadFailed
                    pImageWidth: 22
                    pImageHeight: 22
                    asset: uiBackend.oem === "nxt" ? ( iNowggAccountBtn.down
                           ? "AvatarMask_click"
                           : iNowggAccountBtn.hovered
                             ? "AvatarMask_hover"
                             : "AvatarMask") : ""
                    extn: ".png"
                    anchors.centerIn: parent
                }
            }
        }

        UiImageButton {
            id: iDisablePinToTopBtn
            asset: "TitlebarDisablePinTop"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            visible: false /* BSI CUSTOMIZER: clean player UI */ && (pCurrentTopmostState)
            UiToolTip.text: qsTranslate("QObject", "Disable Pin to Top")
            onPressed: {
                plrBackend.logUIAction("Topbar", "DisablePinToTop")
                iCfgBtn.visible = true
                pCurrentTopmostState = false
                fHandlePinToTop()
                plrBackend.sendMiniModeStats(true)
            }
        }

        UiImageButton {
            visible: false // BSI CUSTOMIZER: clean player UI
            id: iHelpBtn
            asset: "TitlebarHelp"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            UiToolTip.text: qsTranslate("QObject", "Help and support")
            onPressed: {
                plrBackend.logUIAction("Topbar", "HelpAndSupport")
                fOpenHelpPopup()
            }
        }

        Item {
            visible: false // BSI CUSTOMIZER: clean player UI
            id: iCfgBtn
            Layout.preferredWidth: pIconSize
            Layout.preferredHeight: pIconSize
            UiImageButton {
                asset: "TitlebarMenu"
                pImageWidth: pIconSize
                pImageHeight: pIconSize
                UiToolTip.text: qsTranslate("QObject", "Menu")
                onPressed: {
                    plrBackend.logUIAction("Topbar", "HamburgerMenu")
                    fOpenHamburgerPopup()
                }
            }
            Loader {
                visible: sourceComponent != undefined
                sourceComponent: plrBackend.pDiskOptimizationOnboarding ? iRedDotComponent : undefined
                anchors.top: iCfgBtn.top
                anchors.right: iCfgBtn.right
                anchors.topMargin: 2
                anchors.rightMargin: 4
            }
        }

        UiImageButton {
            id: iMinimizeBtn
            asset: "TitlebarMinimize"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            UiToolTip.text: qsTranslate("QObject", "Minimize")
            onClicked: {
                plrBackend.logUIAction("Topbar", "Minimize")
                iAppwin.showMinimized()
            }
        }

        UiImageButton {
            id: iFixedResizeBtn
            asset: iAppwin.visibility === ApplicationWindow.Windowed ? "TitlebarMaximize" : "TitlebarRestore"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            UiToolTip.text: iAppwin.visibility === ApplicationWindow.Windowed ? qsTranslate("QObject", "Maximize") : qsTranslate("QObject", "Restore")
            onClicked: {
                if (iAppwin.visibility === ApplicationWindow.Windowed) {
                    plrBackend.logUIAction("Topbar", "Maximize")
                    plrBackend.onMaximizeClicked()
                }
                else {
                    plrBackend.logUIAction("Topbar", "Restore")
                    plrBackend.onRestoreClicked()
                }
            }
        }

        UiImageButton {
            id:iCloseBtn
            asset: "TitlebarClose"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            UiToolTip.text: qsTranslate("QObject", "Close")
            onClicked: {
                plrBackend.logUIAction("Topbar", "Close")
                fShowClosePlrMsg()
            }
        }

        UiImageButton {
            visible: false // BSI CUSTOMIZER: clean player UI
            id: iSidebarShowBtn
            asset: iSidebar.visible ? "TitlebarCollapse" : "TitlebarExpand"
            pImageHeight: iAppwin.pSidebarWidth
            pImageWidth: iAppwin.pSidebarWidth
            UiToolTip.text: iSidebar.visible ? qsTranslate("QObject", "Close sidebar") : qsTranslate("QObject", "Open sidebar")
            onClicked: {
                if (iSidebar.visible) {
                    plrBackend.logUIAction("Sidebar", "CollapseSidebar")
                    sidebarHideClicked()
                }
                else {
                    plrBackend.logUIAction("Topbar", "ExpandSidebar")
                    sidebarBtnClicked()
                }
            }
        }
    }

    Connections {
        target: iAppwin
        function onPIsMacroRunningChanged() {
            if (iAppwin.pIsMacroRunning && iAppwin.visibility !== ApplicationWindow.FullScreen)
                iMacroRecordingTooltipComponent.createObject(iAppwin).show()
            else {
                pMacroRecordingStartTime = -1
                pIsMacroPause = false
            }
        }
    }

    Component {
        id: iMacroTopbarComponent
        UiRowLayout {
            height: 26
            color: UiTheme.colors.primary90
            border.color: UiTheme.colors.primary60
            border.width: 1
            spacing: 8
            leftPadding: 8
            rightPadding: 8

            property int pCompFixedWidth: 16 + iMacroRecordingActiveImage.width + 8 + iPlayPauseBtn.width + 8 + iStopBtn.width + 8
            property int pCompVariableWidth: 16 + iMacroRecordingControlText.width + (pMacroRecordingInProcess ? 0 : (16 + iMacroRecordingControlIterationText.width))
            property int pMacroDetailWidth: iMacroDetail.width
            property bool pIsMacroDetailVisible: true

            UiImage {
                id: iMacroRecordingActiveImage
                asset: pMacroRecordingInProcess ? "MacroRecord" : "MacroPlay"
                pImageWidth: 12
                pImageHeight: 12
            }
            UiRowLayout {
                id: iMacroDetail
                spacing: 8
                visible: false /* BSI CUSTOMIZER: clean player UI */ && (pIsMacroDetailVisible)
                Text {
                    id: iMacroRecordingControlText
                    text: pMacroRecordingInProcess ? pMacroIterationText : pCurrentRunningMacroName
                    Layout.preferredWidth: 55
                    color: UiTheme.colors.primary10
                    font: UiTheme.fonts.bodySmall
                    UiToolTip.text: pMacroRecordingInProcess
                                    ? qsTranslate("QObject", "Macro recording in progress")
                                    : pCurrentRunningDependentMacroName === pCurrentRunningMacroName
                                      ? qsTranslate("QObject", "Playing %1").arg(pCurrentRunningMacroName)
                                      : qsTranslate("QObject", "%1 -> %2 in progress").arg(pCurrentRunningMacroName).arg(pCurrentRunningDependentMacroName)
                    UiToolTip.visible: iMacroTextMouseArea.containsMouse
                    elide: Text.ElideRight
                    MouseArea {
                        id: iMacroTextMouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }
                Text {
                    id: iMacroRecordingControlIterationText
                    text: pMacroIterationText
                    color: UiTheme.colors.accentNormal
                    font: UiTheme.fonts.bodyExtraSmall
                    visible: !pMacroRecordingInProcess
                }
            }
            Timer {
                interval: 500
                running: true
                repeat: true
                onTriggered: {
                    if(!pMacroRecordingInProcess)
                    {
                        if(!pIsMacroPause)
                            iMacroRecordingActiveImage.asset = iMacroRecordingActiveImage.asset != "MacroPlay" ? "MacroPlay" : "MacroBase"
                        else
                            iMacroRecordingActiveImage.asset = "MacroPause"
                    }
                    else
                        iMacroRecordingActiveImage.asset = (iMacroRecordingActiveImage.asset != "MacroBase" && pMacroTimerRunning ? "MacroBase" : "MacroRecord")
                }
            }
            Timer {
                triggeredOnStart: true
                interval: 200
                running: pMacroTimerRunning
                repeat: true
                onTriggered: {
                    if(pMacroRecordingStartTime == -1)
                        pMacroRecordingStartTime = Date.now()


                    var seconds = parseInt( (Date.now() - pMacroRecordingStartTime) / 1000 );
                    var minutes = parseInt( seconds / 60 );
                    seconds = seconds % 60;
                    var hours = parseInt( minutes / 60 );
                    minutes = minutes % 60;

                    var time = (hours < 10 ? "0" + hours : hours) + ":" + (minutes < 10 ? "0" + minutes : minutes) + ":" + (seconds < 10 ? "0" + seconds : seconds)
                    pMacroIterationText = time
                }
            }
            UiImageButton {
                id: iPlayPauseBtn
                asset: pIsMacroPause ? "PlaySmall" : "PauseSmall"
                pImageWidth: 12
                pImageHeight: 12
                visible: pIsMacroRunning
                pUseShortcutTooltip: true
                UiShortcutToolTip.text: (asset == "PauseSmall")
                                        ? (!pMacroRecordingInProcess
                                           ? qsTranslate("QObject", "Pause running macro")
                                           : qsTranslate("QObject", "Pause macro recording"))
                                        : (!pMacroRecordingInProcess
                                           ? qsTranslate("QObject", "Play macro")
                                           : qsTranslate("QObject", "Resume macro recording"))
                UiShortcutToolTip.shortcutMap: !pMacroRecordingInProcess
                                               ? [{ value: "pause_play_macros" }]
                                               : [{ value: "" }]
                onClicked: {
                    fToggleMacroPause(false)
                }
            }
            UiImageButton {
                id:iStopBtn
                asset: "StopSmall"
                pImageWidth: 12
                pImageHeight: 12
                UiToolTip.text: pMacroRecordingInProcess
                                ? qsTranslate("QObject", "Stop recording macro")
                                : qsTranslate("QObject", "Stop playing macro")
                onClicked: {
                    fStopMacro()
                }
            }
        }
    }

    Component {
        id: iSyncTopbarComponent
        UiRowLayout {
            id: iSyncTopbarLayout
            height: 26
            color: UiTheme.colors.primary90
            border.color: UiTheme.colors.primary60
            border.width: 1
            leftPadding: 8
            rightPadding: 8
            topPadding: 1
            bottomPadding: 1
            spacing: 8
            UiToolTip.text: pSyncSource ? qsTranslate("QObject", "Syncing operations to other instance(s)") : qsTranslate("QObject", "Syncing operations from another instance.")
            UiToolTip.visible: pMouseArea.containsMouse && !iSyncStopBtn.hovered && !iSyncPauseBtn.hovered
            pMouseArea {
                enabled: true
                anchors.fill: iSyncTopbarLayout
                hoverEnabled: true
            }

            property string pSyncPauseBtnAsset: "PauseSmall"

            UiImage {
                asset: pSyncSource ? "SyncPlay_hover_2" : "SyncPlay_hover"
                pImageWidth: 12
                pImageHeight: 12
            }
            UiRowLayout {
                id: iImgLayout
                spacing: 8
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignCenter
                visible: pSyncSource
                pMouseArea {
                    enabled: true
                    anchors.fill: iImgLayout
                    hoverEnabled: true
                }
                UiImageButton {
                    id: iSyncPauseBtn
                    asset: pSyncPauseBtnAsset
                    pImageWidth: 12
                    pImageHeight: 12
                    visible: pSyncSource
                    pUseShortcutTooltip: true
                    UiShortcutToolTip.text: qsTranslate("QObject", "Play/pause sync operations")
                    UiShortcutToolTip.shortcutMap: [{ value: "play_pause_sync_operations" }]
                    onClicked: {
                        plrBackend.logUIAction("Topbar", "PlayPauseSync")
                        mnsBackend.onSyncPaused()
                    }
                }
                UiImageButton {
                    id: iSyncStopBtn
                    asset: "StopSmall"
                    pImageHeight: 12
                    pImageWidth: 12
                    visible: pSyncSource
                    UiToolTip.text: qsTranslate("QObject", "Stop sync operations")
                    onClicked: {
                        plrBackend.logUIAction("Topbar", "StopSync")
                        pSyncPauseBtnAsset = "PauseSmall"
                        fStopSyncOperations()
                    }
                }
            }
        }
    }

    Component {
        id: iClosePlrPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Close %1").arg(uiBackend.confGetInstanceString("display_name"))
            pDescription: qsTranslate("QObject", "Would you like to exit %1?").arg(uiBackend.confGetInstanceString("display_name"))
            pDialogType: UiDialogWindow.SimpleNarrow
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.danger
                        pText: qsTranslate("QObject", "Close")
                        onClicked: {
                            close()
                            plrBackend.onCloseCalled()
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
        id: iFeedbackPopupComponent
        UiMessageDialog {
            property string feedbackAppLabel: ""
            property string feedbackAppPackage: ""
            title: feedbackAppLabel != ""
                   ? qsTranslate("QObject", "Enjoyed playing %1 on %2?").arg(feedbackAppLabel).arg(uiBackend.productName)
                   : qsTranslate("QObject", "Enjoyed playing on %1?").arg(uiBackend.productName)
            pDescription: qsTranslate("QObject", "Tell us about your experience. We're listening!")
            pDialogType: UiDialogWindow.Tall
            pDialogImageSource: "Feedback"
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Share feedback")
                        onClicked: {
                            close()
                            plrBackend.openAppSurvey(feedbackAppPackage)
                            plrBackend.onCloseCalled()
                        }
                    },
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.secondary
                        pText: qsTranslate("QObject", "Close %1").arg(uiBackend.productName)
                        onClicked: {
                            close()
                            plrBackend.onCloseCalled()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iHelpMenuComponent
        UiWindowedPopup {
            id: iHelpMenu
            contentWidth: iHelpMenuLayout.implicitWidth
            contentHeight: iHelpMenuLayout.implicitHeight
            visible: false

            UiColumnLayout {
                id: iHelpMenuLayout
                anchors.left: parent.left
                anchors.right: parent.right
                topPadding: 2
                bottomPadding: 2
                color: UiTheme.colors.primary80
                border.width: 1
                border.color: UiTheme.colors.primary60
                UiImageLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    pText : qsTranslate("QObject", "Help Center")
                    pImageAsset: "FAQ"
                    leftPadding: 12
                    rightPadding: 12
                    pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
                    onClicked:{
                        plrBackend.logUIAction("Topbar", "SupportFAQs")
                        iHelpMenu.close()
                        uiBackend.openHelpArticle("BS5_help")
                    }
                }
                UiImageLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    pText : qsTranslate("QObject", "Report a problem")
                    pImageAsset: "Error"
                    leftPadding: 12
                    rightPadding: 12
                    pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
                    onClicked:{
                        plrBackend.logUIAction("Topbar", "ReportAProblem")
                        iHelpMenu.close()
                        uiBackend.runLogCollector("-instance="+plrBackend.getVmName())
                    }
                }
            }
        }
    }

    Component {
        id: iMacroRecordingTooltipComponent
        UiHelpPopup {
            id: iMacroRecordingPopup
            closeOnPressOutside: false
            contentWidth: 120
            x: iAppwin.x + iMacroTopbarControl.x + (iMacroTopbarControl.width - width) / 2 + adsBackend.pExtendedBannerAdWidth
            y: iAppwin.y + iMacroTopbarControl.y + iMacroTopbarControl.height + 4
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

            onVisibleChanged: {
                if (visible)
                    iMacroRecordingPopupTimer.start()
            }

            Connections {
                target: iMacroTopbarControl
                function onVisibleChanged() {
                    if (!iMacroTopbarControl.visible)
                        iMacroRecordingPopup.destroy()
                }
            }

            arrowPosition: UiHelpPopup.ArrowPosition.TopCenter
            pInnerLayout {
                border.width: 1
                border.color: UiTheme.colors.primary60
                color: UiTheme.colors.primary80
                padding: 8
            }

            Text {
                Layout.fillWidth: true
                text: pMacroRecordingInProcess ? qsTranslate("QObject", "Recording macros, click here to pause or stop")
                                               : qsTranslate("QObject", "Executing macro, click here to stop")
                wrapMode: Text.Wrap
                color: UiTheme.colors.primary10
                font: UiTheme.fonts.bodyExtraSmall
            }

            Timer {
                id: iMacroRecordingPopupTimer
                repeat: false
                interval: 5000
                running: false
                onTriggered: {
                    iMacroRecordingPopup.destroy()
                }
            }

            onClosePopup: {
                iMacroRecordingPopupTimer.stop()
            }
        }
    }

    Component {
        id: iHamburgerMenuComponent
        UiWindowedPopup {
            id: iHamburgerMenu
            contentWidth: iHamburgerMenuLayout.implicitWidth
            contentHeight: iHamburgerMenuLayout.implicitHeight
            visible: false

            UiColumnLayout {
                id: iHamburgerMenuLayout
                anchors.left: parent.left
                anchors.right: parent.right
                topPadding: 2
                bottomPadding: 2
                color: UiTheme.colors.primary80
                border.width: 1
                border.color: UiTheme.colors.primary60
                UiRowLayout {
                    id: iHamburgerSettingsLayout
                    Layout.fillWidth: true
                    color: pMouseArea.containsMouse ? UiTheme.colors.primary60 : "transparent"
                    pMouseArea {
                        enabled: true
                        anchors.fill : iHamburgerSettingsLayout
                        hoverEnabled: true
                        onClicked: {
                            plrBackend.pSettingWindowInitializing = true
                            plrBackend.logUIAction("Topbar", "HamburgerSettings")
                            iHamburgerMenu.close()
                            hamburgerSettingClicked()
                        }
                    }
                    UiImageLabel {
                        Layout.preferredHeight: 32
                        pText : qsTranslate("QObject", "Settings")
                        pImageAsset: "MenuSettings"
                        leftPadding: 12
                        rightPadding: 12
                        pHovered: iHamburgerSettingsLayout.pMouseArea.containsMouse
                        hoverEnabled: false
                        onClicked: {
                            plrBackend.pSettingWindowInitializing = true
                            plrBackend.logUIAction("Topbar", "HamburgerSettings")
                            iHamburgerMenu.close()
                            hamburgerSettingClicked()
                        }
                    }
                    Rectangle {
                        visible: plrBackend.pDiskOptimizationOnboarding
                        Layout.preferredWidth: 6
                        Layout.preferredHeight: 6
                        radius: 3
                        color: UiTheme.colors.dangerNormal
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
                UiImageLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    pText : qsTranslate("QObject", "Mini Mode")
                    pImageAsset: "MenuMiniMode"
                    leftPadding: 12
                    rightPadding: 12
                    pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
                    onClicked:{
                        fHandleMiniModeClicked()
                        iHamburgerMenu.close()
                    }
                }
                UiImageLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    pText: qsTranslate("QObject", "Pin to top")
                    pImageAsset: "MenuPin"
                    leftPadding: 12
                    rightPadding: 12
                    pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
                    UiImage {
                        Layout.alignment: Qt.AlignRight
                        Layout.leftMargin: 8
                        pImageWidth: 24
                        pImageHeight: 12
                        asset: pCurrentTopmostState ? "SmallToggleOn" : "SmallToggleOff"
                    }
                    onClicked:{
                        pCurrentTopmostState = !pCurrentTopmostState
                        fHandlePinToTop()
                        iHamburgerMenu.flags = iHamburgerMenu.flags & ~Qt.WindowStaysOnTopHint
                        iHamburgerMenu.flags = iHamburgerMenu.flags | Qt.WindowStaysOnTopHint
                    }
                }

                UiImageLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    pText : qsTranslate("QObject", "Quick Launch")
                    pImageAsset: visible ? "MenuQuickLaunch" : ""
                    visible: plrBackend.getQuickLaunchEnabled()
                    leftPadding: 12
                    rightPadding: 12
                    pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
                    onClicked:{
                        plrBackend.launchAndArrangeAllInstances()
                        iHamburgerMenu.close()
                    }
                }

                UiImageLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    pText: qsTranslate("QObject", "Fix window size")
                    pImageAsset: "MenuWindow"
                    leftPadding: 12
                    rightPadding: 12
                    pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
                    UiImage {
                        Layout.alignment: Qt.AlignRight
                        Layout.leftMargin: 8
                        pImageWidth: 24
                        pImageHeight: 12
                        asset: pFixedWindowSizeState ? "SmallToggleOn" : "SmallToggleOff"
                    }
                    onClicked: {
                        pFixedWindowSizeState = !pFixedWindowSizeState

                        uiBackend.confSetBool("bst.fixed_window_size", pFixedWindowSizeState)
                        uiBackend.confCommit()
                        uiBackend.sendSettingStats("hamburger_menu", "fixed_window_size", pFixedWindowSizeState ? "toggle_on" : "toggle_off")
                        plrBackend.reloadConfInAllInstances()
                    }
                }
            }
        }
    }

    Component {
        id: iScreenRecordingTopbarComponent
        UiRowLayout {
            height: 26
            color: UiTheme.colors.primary100
            border.color: UiTheme.colors.primary70
            border.width: 1
            spacing: 8
            leftPadding: 8
            rightPadding: 8

            property int pCompFixedWidth: 16 + iScreenRecordingImage.width + 8 + iStopBtn.width + 8
            property int pCompVariableWidth: 16 + iScreenRecordingTimerText.width + iRecordingNameText.width
            property bool pRecordingDetailRowVisible: true
            property int pRecordingDetailWidth: iRecordingDetailRow.width

            UiImage {
                id: iScreenRecordingImage
                asset: "StopRecordingBright"
                pImageWidth: 16
                pImageHeight: 16
            }

            UiRowLayout {
                id: iRecordingDetailRow
                spacing: 8
                visible: pRecordingDetailRowVisible

                Text {
                    id: iRecordingNameText
                    text: qsTranslate("QObject", "Video recording")
                    color: UiTheme.colors.primary20
                    font: UiTheme.fonts.bodySmall
                    elide: Text.ElideRight
                }

                Text {
                    id: iScreenRecordingTimerText
                    text: pScreenRecordingTimerText
                    color: UiTheme.colors.accentNormal
                    font: UiTheme.fonts.bodyExtraSmall
                }
            }
            Timer {
                triggeredOnStart: true
                interval: 200
                running: pIsScreenRecording
                repeat: true
                onTriggered: {
                    if(pScreenRecordingStartTime == -1)
                        pScreenRecordingStartTime = Date.now()


                    var seconds = parseInt( (Date.now() - pScreenRecordingStartTime) / 1000 );
                    var minutes = parseInt( seconds / 60 );
                    seconds = seconds % 60;
                    var hours = parseInt( minutes / 60 );
                    minutes = minutes % 60;

                    var time = (hours < 10 ? "0" + hours : hours) + ":" + (minutes < 10 ? "0" + minutes : minutes) + ":" + (seconds < 10 ? "0" + seconds : seconds)
                    pScreenRecordingTimerText = time
                }
            }
            UiImageButton {
                id:iStopBtn
                asset: "StopSmall"
                pImageWidth: 12
                pImageHeight: 12
                UiToolTip.text: qsTranslate("QObject", "Stop screen recording")
                onClicked: {
                    nowggBackend.startRecording(false)
                }
            }
        }
    }

    Component {
        id: iNowbuxIapRewardSuccess
        UiHelpPopup {
            id: iNowbuxIapRewardSuccessPopup
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
            closeOnPressOutside: false
            contentWidth: 306
            x: iAppwin.x + iNowbuxBtn.x + iNowbuxBtn.width - width + adsBackend.pExtendedBannerAdWidth + 10
            y: iAppwin.y + iNowbuxBtn.y + iNowbuxBtn.height - 2

            property var pNowbuxRewardSuccessData

            onVisibleChanged: {
                if (visible)
                    iNowbuxIapRewardSuccessPopupTimer.start()
            }

            onClosePopup: {
                iNowbuxIapRewardSuccessPopupTimer.stop()
            }

            arrowPosition: UiHelpPopup.ArrowPosition.TopRight
            pInnerLayout {
                border.width: 1
                border.color: UiTheme.colors.primary50
                color: UiTheme.colors.primary70
                topPadding: 12
                bottomPadding: 16
                leftPadding: 12
                rightPadding: 12
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: iNowbuxIapRewardSuccessLayout.implicitHeight
                UiImageButton {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    asset: "CloseIcon"
                    pImageWidth: 12
                    pImageHeight: 12
                    UiToolTip.text: qsTranslate("QObject", "Close")
                    onClicked: {
                        iNowbuxIapRewardSuccessPopup.close()
                    }
                }
                UiColumnLayout {
                    id: iNowbuxIapRewardSuccessLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    topPadding: 4
                    spacing: 12
                    UiColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            Layout.fillWidth: true
                            text: pNowbuxRewardSuccessData.pTitle
                            font: UiTheme.fonts.titleLarger
                            color: UiTheme.colors.white
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: pNowbuxRewardSuccessData.pDescription
                            font: UiTheme.fonts.bodyMedium
                            color: UiTheme.colors.white
                            wrapMode: Text.Wrap
                        }
                    }
                    ListView {
                        id: iNowbuxIapRewardListView
                        Layout.fillWidth: true
                        Layout.preferredHeight: iNowbuxIapRewardListView.contentHeight
                        implicitHeight: iNowbuxIapRewardListView.contentHeight
                        model: pNowbuxRewardSuccessData.pNowbuxRewards
                        spacing: 8
                        orientation: ListView.Vertical

                        delegate: Item {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: iNowbuxIapRewardContentLayout.implicitHeight
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                height: iNowbuxIapRewardContentLayout.implicitHeight
                                color: modelData.pId === "nowbux"
                                       ? "#483921" : modelData.pId === "sweepstake"
                                         ? "#1A3BAACF" : "#1A3BCF40"
                                border.width: 1
                                border.color: modelData.pId === "nowbux"
                                              ? "#99F2CB3E" : modelData.pId === "sweepstake"
                                                ? "#3BAACF" : "#3BCF47"
                                clip: true
                                UiImage {
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: -16
                                    anchors.bottomMargin: -16
                                    pImageWidth: 64
                                    pImageHeight: 64
                                    source: modelData.pIcon
                                    opacity: 0.08
                                }
                            }
                            UiRowLayout {
                                id: iNowbuxIapRewardContentLayout
                                anchors.left: parent.left
                                anchors.right: parent.right
                                padding: 12
                                spacing: 12
                                UiImage {
                                    source: modelData.pIcon
                                    pImageWidth: 32
                                    pImageHeight: 32
                                }
                                UiColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.pDescription
                                        font: UiTheme.fonts.titleMedium
                                        color: modelData.pId === "nowbux"
                                               ? "#FBD123" : modelData.pId === "sweepstake"
                                                 ? "#3BAACF" : "#3BCF47"
                                        wrapMode: Text.Wrap
                                    }
                                    UiRowLayout {
                                        id: iLinkLayout
                                        spacing: 2
                                        pMouseArea {
                                            enabled: true
                                            anchors.fill: iLinkLayout
                                            hoverEnabled: true
                                            cursorShape: iLinkLayout.pMouseArea.containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                                            onClicked: {
                                                uiBackend.openHelpArticle(modelData.pDetailsLinkArticleKey)
                                            }
                                        }
                                        Text {
                                            Layout.preferredWidth: Math.min(implicitWidth, 160)
                                            text: modelData.pDetailsLinkText
                                            font: UiTheme.fonts.bodySmallest
                                            color: UiTheme.colors.white
                                            elide: Text.ElideRight
                                        }
                                        UiImage {
                                            pImageHeight: 10
                                            pImageWidth: 10
                                            asset: "CaretRightWhite"
                                        }
                                    }
                                }
                            }
                        }
                    }
                    UiButton {
                        visible: !uiBackend.confGetInstanceBool("nowbux_signin_completed")
                        pSize: UiTheme.buttons.sizes.medium
                        pColor: UiTheme.buttons.colors.nowbuxButton
                        text: qsTranslate("QObject", "Login to claim")
                        asset: "LinkDark"
                        pImageAfterText: true
                        onClicked: {
                            iNowbuxIapRewardSuccessPopup.close()
                            nowggBackend.setLinkAccountSource("iap_reward_notification")
                            nowggBackend.signinToNowBux()
                        }
                    }
                }
            }

            Timer {
                id: iNowbuxIapRewardSuccessPopupTimer
                repeat: false
                interval: 30000
                running: false
                onTriggered: {
                    iNowbuxIapRewardSuccessPopup.destroy()
                }
            }
        }
    }

    Component {
        id: iNowggAcountSigninComponent
        UiWindowedPopup {
            id: iNowggAccountSigninPopup
            contentWidth: 204
            contentHeight: iNowggAccountSigninLayout.implicitHeight
            visible: false
            UiColumnLayout {
                id: iNowggAccountSigninLayout
                anchors.left: parent.left
                anchors.right: parent.right
                border.width: 1
                border.color: UiTheme.colors.primary50
                color: UiTheme.colors.primary70
                topPadding: 12
                bottomPadding: 12
                spacing: 8

                UiColumnLayout {
                    Layout.fillWidth: true
                    leftPadding: 12
                    rightPadding: 12
                    topPadding: 8
                    bottomPadding: 4
                    spacing: 12
                    UiImage {
                        Layout.alignment: Qt.AlignHCenter
                        pImageWidth: 72
                        pImageHeight: 72
                        asset: "DefaultProfileAvatar"
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTranslate("QObject", "Log into your BlueStacks account")
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.white
                        wrapMode: Text.WordWrap
                    }
                }
                Rectangle {
                    Layout.preferredHeight: 1
                    Layout.fillWidth: true
                    color: UiTheme.colors.primary50
                }
                UiButton {
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.fillWidth: true
                    pSize: UiTheme.buttons.sizes.small
                    pColor: UiTheme.buttons.colors.accent
                    text: qsTranslate("QObject", "Login")
                    asset: "Link"
                    pImageAfterText: true
                    onClicked: {
                        iNowggAccountSigninPopup.close()
                        nowggBackend.setLinkAccountSource("topbar_account")
                        nowggBackend.signinToNowBux()
                    }
                }
            }
        }
    }

    Component {
        id: iNowggAcountSignoutComponent
        UiWindowedPopup {
            id: iNowggAccountSignoutPopup
            contentWidth: Math.max(204, iNowggAccountSignoutLayout.implicitWidth)
            contentHeight: iNowggAccountSignoutLayout.implicitHeight
            visible: false
            UiColumnLayout {
                id: iNowggAccountSignoutLayout
                anchors.left: parent.left
                anchors.right: parent.right
                border.width: 1
                border.color: UiTheme.colors.primary50
                color: UiTheme.colors.primary80
                topPadding: 12
                bottomPadding: 12
                spacing: 8

                UiColumnLayout {
                    Layout.fillWidth: true
                    topPadding: 8
                    bottomPadding: 8
                    spacing: 8
                    UiImage {
                        id: iAvatarImage

                        property bool pUrlLoadFailed: false

                        Layout.alignment: Qt.AlignHCenter
                        pImageWidth: 72
                        pImageHeight: 72
                        source: uiBackend.confGetInstanceString("nowgg_userAvatarUrl") !== "" && !iAvatarImage.pUrlLoadFailed
                                ? uiBackend.confGetInstanceString("nowgg_userAvatarUrl")
                                : "qrcAssets/DefaultProfileAvatar.svg"

                        onStatusChanged: {
                            if (status == Image.Error)
                                iAvatarImage.pUrlLoadFailed = true
                        }

                        UiImage {
                            visible: uiBackend.confGetInstanceString("nowgg_userAvatarUrl") !== "" && !iAvatarImage.pUrlLoadFailed
                            pImageWidth: 74
                            pImageHeight: 74
                            asset: "AvatarMask"
                            extn: ".png"
                            anchors.centerIn: parent
                        }
                    }
                    UiColumnLayout {
                        Layout.fillWidth: true
                        leftPadding: 8
                        rightPadding: 8
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: uiBackend.confGetInstanceString("nowgg_username")
                            font: UiTheme.fonts.bodyMedium
                            color: UiTheme.colors.white
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: uiBackend.confGetInstanceString("nowgg_email")
                            font: UiTheme.fonts.bodyExtraSmall
                            color: UiTheme.colors.white
                            wrapMode: Text.Wrap
                        }
                    }
                }
                Rectangle {
                    Layout.preferredHeight: 1
                    Layout.fillWidth: true
                    color: UiTheme.colors.primary50
                }

                UiImageLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    pText: qsTranslate("QObject", "Edit Profile")
                    pImageAsset: "EditIcon"
                    leftPadding: 12
                    rightPadding: 12
                    pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
                    UiImage {
                        Layout.alignment: Qt.AlignRight
                        Layout.leftMargin: 12
                        pImageWidth: 16
                        pImageHeight: 16
                        asset: "Link"
                    }
                    onClicked:{
                        iNowggAccountSignoutPopup.close()
                        nowggBackend.openNowbuxAccountPage("edit_profile_click")
                    }
                }

                UiImageLabel {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 32
                    pText: qsTranslate("QObject", "Sign out")
                    pImageAsset: "Signout"
                    leftPadding: 12
                    rightPadding: 12
                    pBackgroundColor: pHovered ? UiTheme.colors.primary60 : "transparent"
                    onClicked:{
                        iNowggAccountSignoutPopup.close()
                        nowggBackend.signOutNowggAccount("nowbux")
                    }
                }
            }
        }
    }

    Component {
        id: iNowbuxRewardTooltipComponent
        UiHelpPopup {
            id: iNowbuxRewardTooltipPopup
            closeOnPressOutside: false
            contentWidth: 172
            arrowPosition: UiHelpPopup.ArrowPosition.TopCenter
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus

            property string pText: ""

            pInnerLayout {
                border.width: 1
                border.color: UiTheme.colors.primary40
                color: UiTheme.colors.primary40
                leftPadding: 8
                rightPadding: 8
                topPadding: 4
                bottomPadding: 4
            }

            Text {
                Layout.fillWidth: true
                text: iNowbuxRewardTooltipPopup.pText
                wrapMode: Text.Wrap
                color: UiTheme.colors.primary10
                font: UiTheme.fonts.bodySmall
            }
        }
    }

    Component {
        id: iNowbuxRewardsComponent
        UiHelpPopup {
            id: iNowbuxRewardsPopup
            contentWidth: 280
            arrowPosition: UiHelpPopup.ArrowPosition.TopRight
            visible: false
            pInnerLayout {
                color: UiTheme.colors.primary70
                border.color: UiTheme.colors.primary50
                border.width: 1
                padding: 1
                spacing: 16
            }

            property string selectedTab: "balance"

            onClosePopup: {
                nowggBackend.sendNowbuxStats("nowbux_popup_closed")
            }

            onSelectedTabChanged: {
                if (iNowbuxRewardsPopup.selectedTab === "rewards" && pShowNowbuxRewardsRedDot) {
                    pShowNowbuxRewardsRedDot = false
                    uiBackend.confSetInstanceBool("show_nowbux_rewards_red_dot_onboarding", false)
                    uiBackend.confCommit()
                }
            }

            Component.onCompleted: {
                if (iNowbuxRewardsPopup.selectedTab === "rewards" && pShowNowbuxRewardsRedDot) {
                    pShowNowbuxRewardsRedDot = false
                    uiBackend.confSetInstanceBool("show_nowbux_rewards_red_dot_onboarding", false)
                    uiBackend.confCommit()
                }
            }

            UiRowLayout {
                leftPadding: 16
                rightPadding: 16
                topPadding: 8
                Layout.fillWidth: true
                UiColumnLayout {
                    Layout.preferredWidth: parent.width / 2
                    topPadding: 2
                    spacing: 2
                    visible: nowggBackend.pIsNowbuxTabsEnabled
                    UiButton {
                        Layout.fillWidth: true
                        pSize: UiTheme.buttons.sizes.small
                        pColor: UiTheme.buttons.colors.transparent
                        text: qsTranslate("QObject", "nowBux")
                        onClicked: {
                            iNowbuxRewardsPopup.selectedTab = "balance"
                            nowggBackend.sendNowbuxStats("nowbux_popup_balance_tab_clicked", nowggBackend.pNowbuxBalance)
                        }
                    }
                    Rectangle {
                        Layout.preferredHeight: 1
                        Layout.fillWidth: true
                        color: iNowbuxRewardsPopup.selectedTab === "balance" ? UiTheme.colors.accentNormal : UiTheme.colors.primary50
                    }
                }
                UiColumnLayout {
                    Layout.preferredWidth: parent.width / 2
                    topPadding: 2
                    spacing: 2
                    visible: nowggBackend.pIsNowbuxTabsEnabled
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        UiButton {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            pSize: UiTheme.buttons.sizes.small
                            pColor: UiTheme.buttons.colors.transparent
                            text: qsTranslate("QObject", "Earn")
                            onClicked: {
                                iNowbuxRewardsPopup.selectedTab = "rewards"
                                nowggBackend.sendNowbuxStats("nowbux_popup_rewards_tab_clicked")
                            }
                        }
                        Rectangle {
                            visible: pShowNowbuxRewardsRedDot && nowggBackend.pIsNowbuxTabsEnabled
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.rightMargin: 26
                            anchors.topMargin: 4
                            width: 6
                            height: 6
                            radius: 3
                            color: UiTheme.colors.dangerNormal
                        }
                    }
                    Rectangle {
                        Layout.preferredHeight: 1
                        Layout.fillWidth: true
                        color: iNowbuxRewardsPopup.selectedTab === "rewards" ? UiTheme.colors.accentNormal : UiTheme.colors.primary50
                    }
                }
            }

            UiColumnLayout {
                Layout.fillWidth: true
                visible: iNowbuxRewardsPopup.selectedTab === "balance"
                UiColumnLayout {
                    visible: !uiBackend.confGetInstanceBool("nowbux_signin_completed")
                    leftPadding: 16
                    rightPadding: 16
                    bottomPadding: 16
                    Layout.fillWidth: true
                    spacing: 12
                    UiRowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        UiImage {
                            pImageWidth: 44
                            pImageHeight: 44
                            asset: "NowBux"
                        }
                        UiColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: nowggBackend.pNowbuxIntroData.pTitle !== "" ?
                                          nowggBackend.pNowbuxIntroData.pTitle :
                                          qsTranslate("QObject", "Introducing nowBux")
                                font: UiTheme.fonts.titleLarge
                                color: "#FBD123"
                            }
                            UiTextUrl {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(16, (implicitWidth / 192) * 16)
                                textFormat: Text.RichText
                                text: (nowggBackend.pNowbuxIntroData.pDescription !== "" ? nowggBackend.pNowbuxIntroData.pDescription : qsTranslate("QObject", "The gaming currency you’ve been waiting for.")) +
                                      " <a href=\"javascript:void(0)\" style='font-size:12px; text-decoration:none; color:#1A90FF;'>" +
                                      (nowggBackend.pNowbuxIntroData.pDetailsLinkText !== "" ? nowggBackend.pNowbuxIntroData.pDetailsLinkText : qsTranslate("QObject", "Read More")) +
                                      "</a>"
                                font: UiTheme.fonts.bodyExtraSmall
                                color: UiTheme.colors.white
                                onLinkActivated: uiBackend.openHelpArticle("what_is_nowBux")
                            }
                        }
                    }
                    UiColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        UiRowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(iPrimaryOfferContentLayout.implicitHeight, iSecondaryOfferContentLayout.implicitHeight)
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: Math.max(iPrimaryOfferContentLayout.implicitHeight, iSecondaryOfferContentLayout.implicitHeight)
                                    color: "#27324E"
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: "#114006" }
                                        GradientStop { position: 1; color: "#27324E" }
                                    }
                                    border.width: 1
                                    border.color: "#6662BE1A"
                                }
                                UiColumnLayout {
                                    id: iPrimaryOfferContentLayout
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    padding: 8
                                    spacing: 8
                                    UiImage {
                                        Layout.alignment: Qt.AlignTop
                                        pImageWidth: 24
                                        pImageHeight: 24
                                        source: nowggBackend.pNowbuxIntroData.pPrimaryOfferIcon !== "" ?
                                                    nowggBackend.pNowbuxIntroData.pPrimaryOfferIcon :
                                                    "qrcAssets/Cashback.svg"
                                    }
                                    Text {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.fillHeight: true
                                        Layout.fillWidth: true
                                        text: nowggBackend.pNowbuxIntroData.pPrimaryOfferText !== "" ?
                                                  nowggBackend.pNowbuxIntroData.pPrimaryOfferText :
                                                  qsTranslate("QObject", "5% cashback")
                                        font: UiTheme.fonts.bodyExtraSmall
                                        color: UiTheme.colors.white
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.max(iPrimaryOfferContentLayout.implicitHeight, iSecondaryOfferContentLayout.implicitHeight)
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: Math.max(iPrimaryOfferContentLayout.implicitHeight, iSecondaryOfferContentLayout.implicitHeight)
                                    color: "#27324E"
                                    gradient: Gradient {
                                        orientation: Gradient.Horizontal
                                        GradientStop { position: 0; color: "#044762" }
                                        GradientStop { position: 1; color: "#27324E" }
                                    }
                                    border.width: 1
                                    border.color: "#66039EB3"
                                }
                                UiColumnLayout {
                                    id: iSecondaryOfferContentLayout
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    padding: 8
                                    spacing: 8
                                    UiImage {
                                        Layout.alignment: Qt.AlignTop
                                        pImageWidth: 24
                                        pImageHeight: 24
                                        source: nowggBackend.pNowbuxIntroData.pSecondaryOfferIcon !== "" ?
                                                    nowggBackend.pNowbuxIntroData.pSecondaryOfferIcon :
                                                    "qrcAssets/Controller.svg"
                                    }
                                    Text {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.fillHeight: true
                                        Layout.fillWidth: true
                                        text: nowggBackend.pNowbuxIntroData.pSecondaryOfferText !== "" ?
                                                  nowggBackend.pNowbuxIntroData.pSecondaryOfferText :
                                                  qsTranslate("QObject", "Earn as you play")
                                        font: UiTheme.fonts.bodyExtraSmall
                                        color: UiTheme.colors.white
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }

                        UiButton {
                            Layout.fillWidth: true
                            pSize: UiTheme.buttons.sizes.medium
                            pColor: UiTheme.buttons.colors.accent
                            text: qsTranslate("QObject", "Login/Sign-up")
                            asset: "LinkDark"
                            pImageAfterText: true
                            onClicked: {
                                iNowbuxRewardsPopup.close()
                                nowggBackend.setLinkAccountSource("topbar_nowbux")
                                nowggBackend.signinToNowBux()
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: nowggBackend.pNowbuxIntroData.pConversionText !== ""
                        text: nowggBackend.pNowbuxIntroData.pConversionText
                        font: UiTheme.fonts.bodySmallest
                        wrapMode: Text.Wrap
                        color: UiTheme.colors.primary20
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                Item {
                    visible: uiBackend.confGetInstanceBool("nowbux_signin_completed")
                    Layout.fillWidth: true
                    Layout.preferredHeight: iNowbuxContentLayout.implicitHeight
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: iNowbuxContentLayout.implicitHeight
                        color: UiTheme.colors.primary70
                        clip: true
                        UiImage {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.rightMargin: -40
                            anchors.bottomMargin: -8
                            pImageWidth: 158
                            pImageHeight: 158
                            asset: "NowBuxGrey"
                            opacity: 0.1
                        }
                    }
                    UiColumnLayout {
                        id: iNowbuxContentLayout
                        leftPadding: 16
                        rightPadding: 16
                        bottomPadding: 16
                        topPadding: 8
                        anchors.left: parent.left
                        anchors.right: parent.right
                        spacing: 24
                        UiRowLayout {
                            radius: 26
                            color: UiTheme.colors.primary90
                            spacing: 12
                            leftPadding: 4
                            topPadding: 4
                            bottomPadding: 4
                            rightPadding: 20
                            UiImage {
                                pImageWidth: 44
                                pImageHeight: 44
                                asset: "NowBux"
                            }
                            Text {
                                text: nowggBackend.pNowbuxBalance
                                font: UiTheme.fonts.titleLargest
                                color: UiTheme.colors.primary10
                            }
                        }
                        UiColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            UiButton {
                                Layout.fillWidth: true
                                pSize: UiTheme.buttons.sizes.small
                                pColor: UiTheme.buttons.colors.tertiary
                                text: qsTranslate("QObject", "My Account")
                                asset: "Link"
                                pImageAfterText: true
                                onClicked: {
                                    iNowbuxRewardsPopup.close()
                                    nowggBackend.openNowbuxAccountPage("nowbux_view_account")
                                }
                            }
                            UiButton {
                                Layout.fillWidth: true
                                pSize: UiTheme.buttons.sizes.small
                                pColor: UiTheme.buttons.colors.outline
                                text: qsTranslate("QObject", "Add Gift Card")
                                asset: "CaretRightWhite"
                                pImageAfterText: true
                                onClicked: {
                                    iNowbuxRewardsPopup.close()
                                    nowggBackend.sendNowbuxStats("nowbux_add_gift_card")
                                    fShowNowbuxAddGiftCardDialog()
                                }
                            }
                            UiRowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                UiRowLayout {
                                    id: iLink1Layout
                                    spacing: 2
                                    pMouseArea {
                                        enabled: true
                                        anchors.fill: iLink1Layout
                                        hoverEnabled: true
                                        cursorShape: iLink1Layout.pMouseArea.containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            iNowbuxRewardsPopup.close()
                                            uiBackend.openHelpArticle("what_is_nowBux")
                                            nowggBackend.sendNowbuxStats("nowbux_help_clicked")
                                        }
                                    }
                                    Text {
                                        Layout.preferredWidth: Math.min(implicitWidth, 120)
                                        text: qsTranslate("QObject", "What is nowBux?")
                                        font: UiTheme.fonts.bodySmall
                                        color: UiTheme.colors.accentNormal
                                        elide: Text.ElideRight
                                    }
                                    UiImage {
                                        pImageHeight: 12
                                        pImageWidth: 12
                                        asset: "CaretRightAccent"
                                    }
                                }
                                Rectangle {
                                    Layout.preferredHeight: 10
                                    Layout.preferredWidth: 1
                                    color: UiTheme.colors.primary40
                                }
                                UiRowLayout {
                                    id: iLink2Layout
                                    spacing: 2
                                    pMouseArea {
                                        enabled: true
                                        anchors.fill: iLink2Layout
                                        hoverEnabled: true
                                        cursorShape: iLink2Layout.pMouseArea.containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            iNowbuxRewardsPopup.close()
                                            uiBackend.openHelpArticle("how_to_use_nowBux")
                                            nowggBackend.sendNowbuxStats("nowbux_help_clicked")
                                        }
                                    }
                                    Text {
                                        id: iLink2Text
                                        Layout.preferredWidth: Math.min(implicitWidth, 80)
                                        text: qsTranslate("QObject", "How to use")
                                        font: UiTheme.fonts.bodySmall
                                        color: UiTheme.colors.accentNormal
                                        elide: Text.ElideRight
                                    }
                                    UiImage {
                                        pImageHeight: 12
                                        pImageWidth: 12
                                        asset: "CaretRightAccent"
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }

            UiColumnLayout {
                Layout.fillWidth: true
                visible: iNowbuxRewardsPopup.selectedTab === "rewards"
                leftPadding: 16
                rightPadding: 16
                bottomPadding: 16
                UiColumnLayout {
                    Layout.fillWidth: true
                    topPadding: 4
                    spacing: 12
                    visible: nowggBackend.pNowbuxUserRewardsCount === 0
                    UiImage {
                        Layout.alignment: Qt.AlignHCenter
                        pImageWidth: 48
                        pImageHeight: 48
                        asset: "Warning"
                    }
                    UiColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Text {
                            horizontalAlignment: Text.AlignHCenter
                            Layout.fillWidth: true
                            text: qsTranslate("QObject", "No offers available")
                            font: UiTheme.fonts.titleSmall
                            color: UiTheme.colors.primary10
                            wrapMode: Text.Wrap
                        }
                        Text {
                            leftPadding: 24
                            rightPadding: 24
                            horizontalAlignment: Text.AlignHCenter
                            Layout.fillWidth: true
                            text: qsTranslate("QObject", "Check back soon for more ways to earn nowBux!")
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.primary20
                            wrapMode: Text.Wrap
                        }
                    }
                }
                ListView {
                    id: iRewardsListView
                    visible: nowggBackend.pNowbuxUserRewardsCount > 0
                    Layout.topMargin: -4
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(350, iRewardsListView.contentHeight)
                    implicitHeight: iRewardsListView.contentHeight
                    model: nowggBackend.pNowbuxUserRewards
                    spacing: 12
                    orientation: ListView.Vertical
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    ScrollBar.vertical: UiScrollBar {
                        implicitWidth: 4
                        policy: iRewardsListView.contentHeight > iRewardsListView.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    }

                    delegate: UiColumnLayout {
                        anchors.left: parent != null ? parent.left : undefined
                        anchors.right: parent != null ? parent.right : undefined
                        rightPadding: 8
                        spacing: 12
                        Rectangle {
                            visible: model.index !== 0
                            Layout.preferredHeight: 1
                            Layout.fillWidth: true
                            color: UiTheme.colors.primary60
                        }
                        UiRowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            UiImage {
                                Layout.alignment: Qt.AlignTop
                                pImageWidth: 54
                                pImageHeight: 54
                                source: modelData.pRewardIcon
                            }
                            UiColumnLayout {
                                Layout.alignment: Qt.AlignTop
                                Layout.fillWidth: true
                                spacing: 4
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.pRewardDescription
                                    font: UiTheme.fonts.bodySmall
                                    color: UiTheme.colors.primary10
                                    wrapMode: Text.Wrap
                                }
                                UiRowLayout {
                                    id: iRewardTncUrlLayout
                                    visible: modelData.pRewardTncUrl !== ""
                                    Layout.fillWidth: true
                                    spacing: 2
                                    pMouseArea {
                                        enabled: modelData.pRewardTncUrl !== "" ? true : false
                                        anchors.fill: iRewardTncUrlLayout
                                        hoverEnabled: true
                                        cursorShape: iRewardTncUrlLayout.pMouseArea.containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            iNowbuxRewardsPopup.close()
                                            Qt.openUrlExternally(modelData.pRewardTncUrl)
                                            nowggBackend.sendNowbuxStats("nowbux_offer_tnc_clicked", modelData.pRewardOfferId, modelData.pRewardAppPkg)
                                        }
                                    }

                                    Text {
                                        text: qsTranslate("QObject", "Know More")
                                        font: UiTheme.fonts.bodySmall
                                        color: UiTheme.colors.accentNormal
                                    }
                                    UiImage {
                                        pImageWidth: 12
                                        pImageHeight: 12
                                        asset: "CaretRightAccent"
                                    }
                                    Item {
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }
                        UiRowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            UiRowLayout {
                                id: iRewardTncLayout

                                property UiHelpPopup tncPopup: null

                                Layout.maximumWidth: 120
                                color: UiTheme.colors.primary90
                                radius: 10
                                spacing: 4
                                leftPadding: 2
                                topPadding: 2
                                bottomPadding: 2
                                rightPadding: 6
                                pMouseArea {
                                    enabled: modelData.pRewardTncSummary !== "" ? true : false
                                    anchors.fill: iRewardTncLayout
                                    hoverEnabled: true
                                    cursorShape: iRewardTncLayout.pMouseArea.containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    onEntered: {
                                        iRewardTncLayout.tncPopup = iNowbuxRewardTooltipComponent.createObject(iNowbuxRewardsPopup, {
                                                                                                                   "pText": modelData.pRewardTncSummary,
                                                                                                                   "x": iRewardTncLayout.mapToGlobal(0,0).x + (iRewardTncLayout.width - 202) / 2,
                                                                                                                   "y": iRewardTncLayout.mapToGlobal(0,0).y + iRewardTncLayout.height - 2
                                                                                                               })
                                        iRewardTncLayout.tncPopup.show()
                                    }
                                    onExited: {
                                        if (iRewardTncLayout.tncPopup != null)
                                            iRewardTncLayout.tncPopup.close()
                                    }
                                }

                                UiImage {
                                    pImageWidth: 16
                                    pImageHeight: 16
                                    asset: "NowBux"
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.pRewardValueText
                                    font: UiTheme.fonts.titleSmallest
                                    color: UiTheme.colors.white
                                    wrapMode: Text.Wrap
                                }
                                UiImage {
                                    Layout.leftMargin: 2
                                    visible: modelData.pRewardTncSummary !== ""
                                    pImageWidth: 12
                                    pImageHeight: 12
                                    asset: "InfoSmall"
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            UiButton {
                                Layout.maximumWidth: 110
                                pSize: UiTheme.buttons.sizes.small
                                pColor: UiTheme.buttons.colors.accent
                                visible: modelData.pRewardBtnText !== ""
                                text: modelData.pRewardBtnText
                                onClicked: {
                                    iNowbuxRewardsPopup.close()
                                    nowggBackend.handleRewardActionClicked(modelData.pRewardOfferType, modelData.pRewardActionType, modelData.pRewardAppPkg)
                                    nowggBackend.sendNowbuxStats("nowbux_offer_clicked", modelData.pRewardOfferId, modelData.pRewardAppPkg)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iNowbuxAddGiftCard
        UiQuickWindow {
            id: iNowbuxAddGiftCardWindow
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            width: 336 + 2 * shadowContainer.shadowThickness
            height: iNowbuxAddGiftCardLayout.implicitHeight + 2 * shadowContainer.shadowThickness
            modality: Qt.ApplicationModal
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + plrBackend.plrWinOffsets[0] - adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.y + plrBackend.plrWinOffsets[2],
                                           iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] + adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3])

            onClosed: {
                iNowbuxAddGiftCardWindow.destroy()
            }

            onVisibleChanged: {
                if (visible) {
                    if (iNowbuxAddGiftCardWindow.transientParent != null) {
                        iNowbuxAddGiftCardWindow.x = Qt.binding(function() {
                            var messageWindowX = iNowbuxAddGiftCardWindow.transientParent.x + (iNowbuxAddGiftCardWindow.transientParent.width - iNowbuxAddGiftCardWindow.width) / 2
                            if (messageWindowX < iNowbuxAddGiftCardWindow.transientParent.screen.virtualX)
                                messageWindowX = iNowbuxAddGiftCardWindow.transientParent.screen.virtualX
                            if(messageWindowX + iNowbuxAddGiftCardWindow.width > iNowbuxAddGiftCardWindow.transientParent.screen.virtualX + iNowbuxAddGiftCardWindow.transientParent.screen.width)
                                messageWindowX = iNowbuxAddGiftCardWindow.transientParent.screen.virtualX + iNowbuxAddGiftCardWindow.transientParent.screen.width - iNowbuxAddGiftCardWindow.width
                            return messageWindowX
                        })
                        iNowbuxAddGiftCardWindow.y = Qt.binding(function() {
                            var messageWindowY = iNowbuxAddGiftCardWindow.transientParent.y + (iNowbuxAddGiftCardWindow.transientParent.height - iNowbuxAddGiftCardWindow.height) / 2
                            if (messageWindowY < iNowbuxAddGiftCardWindow.transientParent.screen.virtualY)
                                messageWindowY = iNowbuxAddGiftCardWindow.transientParent.screen.virtualY
                            if(messageWindowY + iNowbuxAddGiftCardWindow.height > iNowbuxAddGiftCardWindow.transientParent.screen.virtualY + iNowbuxAddGiftCardWindow.transientParent.screen.height)
                                messageWindowY = iNowbuxAddGiftCardWindow.transientParent.screen.virtualY + iNowbuxAddGiftCardWindow.transientParent.screen.height - iNowbuxAddGiftCardWindow.height
                            return messageWindowY
                        })
                    }
                }
            }

            signal redeemError(var errorText)

            onRedeemError: function(errorText) {
                iAddGiftCardHelperText.text = errorText
                iAddGiftCardHelperText.color = UiTheme.colors.error

                iAddGiftCardBtn.visible = true
                iLoaderLayout.visible = false
            }

            UiShadowContainer {
                id: shadowContainer
                UiColumnLayout {
                    id: iNowbuxAddGiftCardLayout
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
                    spacing: 24
                    UiRowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTranslate("QObject", "Add a gift card")
                            font: UiTheme.fonts.titleExtraLarge
                            color: "#FBD123"
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        UiImageButton {
                            Layout.alignment: Qt.AlignRight | Qt.AlignTop
                            enabled: iAddGiftCardBtn.visible
                            pDisableClickAndHoveredState: !enabled
                            asset: "CloseIcon"
                            pImageWidth: 16
                            pImageHeight: 16
                            UiToolTip.text: qsTranslate("QObject", "Close")
                            onClicked: {
                                iNowbuxAddGiftCardWindow.close()
                            }
                        }
                    }
                    Rectangle {
                        clip: true
                        Layout.fillWidth: true
                        Layout.preferredHeight: iNowbuxAddGiftCardEnterLayout.implicitHeight
                        color: "transparent"
                        UiImage {
                            asset: "GiftCard"
                            pImageHeight: 116
                            pImageWidth: 288
                        }
                        UiColumnLayout {
                            id: iNowbuxAddGiftCardEnterLayout
                            anchors.left: parent.left
                            anchors.right: parent.right
                            topPadding: 50
                            leftPadding: 12
                            rightPadding: 12
                            bottomPadding: 12
                            spacing: 8
                            Text {
                                text: qsTranslate("QObject", "Enter Gift Code")
                                font: UiTheme.fonts.overline
                                color: UiTheme.colors.primary90
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                            }
                            UiTextField {
                                id: iGiftCodeText
                                Layout.rightMargin: 12
                                Layout.fillWidth: true
                                pSize: UiTheme.textFields.sizes.medium
                                pColor: UiTheme.textFields.colors.secondary
                                horizontalAlignment: Text.AlignHCenter
                                onTextEdited: {
                                    var regex = /^\S+$/;
                                    if (!regex.test(text)) {
                                        iGiftCodeText.pInputValidationState = UiTextField.InputValidation.Error
                                    }
                                    else {
                                        iGiftCodeText.pInputValidationState = UiTextField.InputValidation.Valid
                                    }
                                }
                            }
                        }
                    }
                    UiColumnLayout {
                        Layout.fillWidth: true
                        padding: 12
                        spacing: 8
                        border.width: 1
                        border.color: UiTheme.colors.primary70
                        color: "#1C141524"
                        Text {
                            id: iAddGiftCardHelperText
                            text: qsTranslate("QObject", "Amount will be added to your nowBux balance")
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.primary20
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        UiButton {
                            id: iAddGiftCardBtn
                            Layout.fillWidth: true
                            text: qsTranslate("QObject", "Add to account")
                            pSize: UiTheme.buttons.sizes.large
                            pColor: UiTheme.buttons.colors.nowbuxButton
                            enabled: iGiftCodeText.text !== "" && iGiftCodeText.pInputValidationState !== UiTextField.InputValidation.Error
                            opacity: enabled ? 1 : 0.6
                            onClicked: {
                                iAddGiftCardBtn.visible = false
                                iLoaderLayout.visible = true
                                nowggBackend.redeemNowbuxGiftCard(iGiftCodeText.text)
                            }
                        }
                        UiRowLayout {
                            id: iLoaderLayout
                            Layout.fillWidth: true
                            Layout.preferredHeight: 40
                            leftPadding: 20
                            rightPadding: 20
                            spacing: 12
                            color: UiTheme.colors.primary60
                            visible: false
                            Item {
                                Layout.fillWidth: true
                            }
                            UiImage {
                                asset: "Loader_bright"
                                pImageWidth: 20
                                pImageHeight: 20
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
                                font: UiTheme.fonts.buttonLarge
                                color: UiTheme.colors.primary20
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: iNowbuxAddGiftCardSuccess
        UiQuickWindow {
            id: iNowbuxAddGiftCardSuccessWindow
            color: "transparent"
            flags: Qt.Window | Qt.FramelessWindowHint
            width: 360 + 2 * shadowContainer.shadowThickness
            height: iNowbuxAddGiftCardSuccessLayout.implicitHeight + 2 * shadowContainer.shadowThickness
            modality: Qt.ApplicationModal
            UiOverlay.overlayRect: Qt.rect(iAppwin.x + plrBackend.plrWinOffsets[0] - adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.y + plrBackend.plrWinOffsets[2],
                                           iAppwin.width - plrBackend.plrWinOffsets[0] - plrBackend.plrWinOffsets[1] + adsBackend.pExtendedBannerAdWidth,
                                           iAppwin.height - plrBackend.plrWinOffsets[2] - plrBackend.plrWinOffsets[3])

            onClosed: {
                iNowbuxAddGiftCardSuccessWindow.destroy()
            }

            onVisibleChanged: {
                if (visible) {
                    if (iNowbuxAddGiftCardSuccessWindow.transientParent != null) {
                        iNowbuxAddGiftCardSuccessWindow.x = Qt.binding(function() {
                            var messageWindowX = iNowbuxAddGiftCardSuccessWindow.transientParent.x + (iNowbuxAddGiftCardSuccessWindow.transientParent.width - iNowbuxAddGiftCardSuccessWindow.width) / 2
                            if (messageWindowX < iNowbuxAddGiftCardSuccessWindow.transientParent.screen.virtualX)
                                messageWindowX = iNowbuxAddGiftCardSuccessWindow.transientParent.screen.virtualX
                            if(messageWindowX + iNowbuxAddGiftCardSuccessWindow.width > iNowbuxAddGiftCardSuccessWindow.transientParent.screen.virtualX + iNowbuxAddGiftCardSuccessWindow.transientParent.screen.width)
                                messageWindowX = iNowbuxAddGiftCardSuccessWindow.transientParent.screen.virtualX + iNowbuxAddGiftCardSuccessWindow.transientParent.screen.width - iNowbuxAddGiftCardSuccessWindow.width
                            return messageWindowX
                        })
                        iNowbuxAddGiftCardSuccessWindow.y = Qt.binding(function() {
                            var messageWindowY = iNowbuxAddGiftCardSuccessWindow.transientParent.y + (iNowbuxAddGiftCardSuccessWindow.transientParent.height - iNowbuxAddGiftCardSuccessWindow.height) / 2
                            if (messageWindowY < iNowbuxAddGiftCardSuccessWindow.transientParent.screen.virtualY)
                                messageWindowY = iNowbuxAddGiftCardSuccessWindow.transientParent.screen.virtualY
                            if(messageWindowY + iNowbuxAddGiftCardSuccessWindow.height > iNowbuxAddGiftCardSuccessWindow.transientParent.screen.virtualY + iNowbuxAddGiftCardSuccessWindow.transientParent.screen.height)
                                messageWindowY = iNowbuxAddGiftCardSuccessWindow.transientParent.screen.virtualY + iNowbuxAddGiftCardSuccessWindow.transientParent.screen.height - iNowbuxAddGiftCardSuccessWindow.height
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
                    height: iNowbuxAddGiftCardSuccessLayout.implicitHeight
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
                        height: iNowbuxAddGiftCardSuccessLayout.implicitHeight * 1.5
                    }
                    UiColumnLayout {
                        id: iNowbuxAddGiftCardSuccessLayout
                        anchors.left: parent.left
                        anchors.right: parent.right
                        padding: 20
                        spacing: 24
                        UiRowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: qsTranslate("QObject", "Congratulations!")
                                font: UiTheme.fonts.titleExtraLarge
                                color: "#FBD123"
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            UiImageButton {
                                Layout.alignment: Qt.AlignRight | Qt.AlignTop
                                asset: "CloseIcon"
                                pImageWidth: 16
                                pImageHeight: 16
                                UiToolTip.text: qsTranslate("QObject", "Close")
                                onClicked: {
                                    iNowbuxAddGiftCardSuccessWindow.close()
                                }
                            }
                        }
                        UiImage {
                            asset: "NowBux"
                            pImageWidth: 96
                            pImageHeight: 96
                            Layout.alignment: Qt.AlignHCenter
                        }
                        UiColumnLayout {
                            Layout.fillWidth: true
                            padding: 12
                            spacing: 12
                            border.width: 1
                            border.color: UiTheme.colors.primary70
                            color: "#4C141524"
                            Text {
                                leftPadding: 4
                                rightPadding: 4
                                text: qsTranslate("QObject", "Your gift card amount has been added to your nowBux balance.")
                                font: UiTheme.fonts.bodyExtraLarge
                                color: UiTheme.colors.white
                                wrapMode: Text.Wrap
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                            }
                            UiButton {
                                Layout.fillWidth: true
                                text: qsTranslate("QObject", "Redeem another code")
                                pSize: UiTheme.buttons.sizes.large
                                pColor: UiTheme.buttons.colors.nowbuxButton
                                Layout.alignment: Qt.AlignHCenter
                                onClicked: {
                                    iNowbuxAddGiftCardSuccessWindow.close()
                                    fShowNowbuxAddGiftCardDialog()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function fReloadTopbarElements() {
        var availableWidth = iTopbarRow.width - pFixedElementsWidth
        var dynamicItemsAvailableWidth = availableWidth
        var showElements = true

        for(var i = 0; i < pNonFixedTopbarItems.length; i++) {
            if (pNonFixedTopbarItems[i].pVisible)
                dynamicItemsAvailableWidth -= pNonFixedTopbarItems[i].pWidth
        }

        for(var j = 0; j < pNonFixedTopbarDynamicItems.length; j++) {
            if(showElements && pNonFixedTopbarDynamicItems[j].pVisible) {
                if(dynamicItemsAvailableWidth > pNonFixedTopbarDynamicItems[j].pWidth) {
                    pNonFixedTopbarDynamicItems[j].changeVisibility(true)
                    dynamicItemsAvailableWidth = dynamicItemsAvailableWidth - pNonFixedTopbarDynamicItems[j].pWidth
                }
                else {
                    pNonFixedTopbarDynamicItems[j].changeVisibility(false)
                    showElements = false
                }
            } else {
                pNonFixedTopbarDynamicItems[j].changeVisibility(false)
            }
        }

        showElements = true

        for(var i = 0; i < pNonFixedTopbarItems.length; i++) {
            if(showElements && pNonFixedTopbarItems[i].pVisible) {
                if(availableWidth > pNonFixedTopbarItems[i].pWidth) {
                    pNonFixedTopbarItems[i].changeVisibility(true)
                    availableWidth = availableWidth - pNonFixedTopbarItems[i].pWidth
                }
                else {
                    pNonFixedTopbarItems[i].changeVisibility(false)
                    showElements = false
                }
            } else {
                pNonFixedTopbarItems[i].changeVisibility(false)
            }
        }
    }

    function fOpenHelpPopup() {
        if (helpWin != null)
        {
            if (helpWin.visible)
                helpWin.close()
            return
        }

        helpWin = iHelpMenuComponent.createObject(iAppwin)
        var point = iHelpBtn.mapToGlobal(0, 0)
        helpWin.x = point.x - helpWin.width + iHelpBtn.width + helpWin.shadowThickness
        helpWin.y = point.y + iHelpBtn.height - helpWin.shadowThickness
        helpWin.show()
    }

    function fOpenHamburgerPopup() {
        if (hamburgerWin != null)
        {
            if (hamburgerWin.visible)
                hamburgerWin.close()
            return
        }

        pFixedWindowSizeState = uiBackend.confGetBool("bst.fixed_window_size")
        hamburgerWin = iHamburgerMenuComponent.createObject(iAppwin)
        var point = iCfgBtn.mapToGlobal(0, 0)
        hamburgerWin.x = point.x - hamburgerWin.width + iCfgBtn.width + hamburgerWin.shadowThickness
        hamburgerWin.y = point.y + iCfgBtn.height - hamburgerWin.shadowThickness
        hamburgerWin.show()
    }

    function fOpenNowggAccountSigninPopup() {
        if (nowggAccountSigninWin != null) {
            if (nowggAccountSigninWin.visible)
                nowggAccountSigninWin.close()
            return
        }

        nowggAccountSigninWin = iNowggAcountSigninComponent.createObject(iAppwin)
        var point = iNowggAccountBtn.mapToGlobal(0, 0)
        nowggAccountSigninWin.x = point.x - nowggAccountSigninWin.width + nowggAccountSigninWin.shadowThickness + iNowggAccountBtn.width
        nowggAccountSigninWin.y = point.y + iNowggAccountBtn.height - nowggAccountSigninWin.shadowThickness + 2
        nowggAccountSigninWin.show()
    }

    function fOpenNowggAccountSignoutPopup() {
        if (nowggAccountSignoutWin != null) {
            if (nowggAccountSignoutWin.visible)
                nowggAccountSignoutWin.close()
            return
        }

        nowggAccountSignoutWin = iNowggAcountSignoutComponent.createObject(iAppwin)
        var point = iNowggAccountBtn.mapToGlobal(0, 0)
        nowggAccountSignoutWin.x = point.x - nowggAccountSignoutWin.width + nowggAccountSignoutWin.shadowThickness + iNowggAccountBtn.width
        nowggAccountSignoutWin.y = point.y + iNowggAccountBtn.height - nowggAccountSignoutWin.shadowThickness + 2
        nowggAccountSignoutWin.show()
    }

    function fOpenNowbuxRewardsPopup(selectedTabName = "balance") {
        if (nowbuxRewardsWin != null) {
            if (nowbuxRewardsWin.visible)
                nowbuxRewardsWin.close()
            return
        }

        if (uiBackend.confGetInstanceBool("nowbux_signin_completed"))
            nowggBackend.getNowbuxBalance()

        nowbuxRewardsWin = iNowbuxRewardsComponent.createObject(iAppwin, {"selectedTab": selectedTabName})
        var point = iNowbuxBtn.mapToGlobal(0, 0)
        nowbuxRewardsWin.x = point.x - nowbuxRewardsWin.width + nowbuxRewardsWin.shadowThickness + (iNowbuxBtn.width / 2) + 10
        nowbuxRewardsWin.y = point.y + iNowbuxBtn.height - nowbuxRewardsWin.shadowThickness + 4
        nowbuxRewardsWin.show()

        nowggBackend.sendNowbuxStats("nowbux_popup_shown", selectedTabName === "balance" ? "View balance" : "View rewards")
    }

    function fStopMacro() {
        pMacroTimerRunning = false
        pMacroRecordingInProcess = false
        pIsMacroRunning = false
        fReloadTopbarElements()
        mnsBackend.onMacroStopped()
    }

    function fToggleMacroPause(isShortcutUsed) {
        if(pIsMacroPause == false) {
            if(pEnableTimer)
            {
                pMacroTimerRunning = false
                pMacroRecordingPausedTime = Date.now()
            }
            pIsMacroPause = true
        }
        else {
            if(pEnableTimer)
            {
                pMacroTimerRunning = true
                pMacroRecordingStartTime += Date.now() - iTopbar.pMacroRecordingPausedTime
            }
            pIsMacroPause = false
        }

        if (isShortcutUsed === undefined)
            isShortcutUsed = false

        mnsBackend.onMacroPaused(isShortcutUsed)
    }

    function fPauseResumeSync() {
        if (iSyncTopbarControl.sourceComponent != undefined) {
            iSyncTopbarControl.item.pSyncPauseBtnAsset = (iSyncTopbarControl.item.pSyncPauseBtnAsset === "PauseSmall") ? "PlaySmall" : "PauseSmall"
        }
    }

    function fUpdateSyncState(isSyncing) {
        pSyncControlVisible = isSyncing
        if (iSyncTopbarControl.sourceComponent != undefined) {
            iSyncTopbarControl.item.pSyncPauseBtnAsset = isSyncing ? "PauseSmall" : "PlaySmall"
        }
    }

    function fSetImageName(imageName) {
        iImageTxt.text = imageName;
        fReloadTopbarElements();
    }

    function fShowClosePlrMsg() {

        if(plrBackend.checkMobileAppPromotion(pTopActivityAppPackage))
        {
                var component = Qt.createComponent("PlrExitMobileAppPromotions.qml");
                if (component.status === Component.Ready) {
                    var obj =  component.createObject(iAppwin, { "transientParent": iAppwin, "pTopActivityAppPackage": pTopActivityAppPackage, "pTopActivityAppLabel": pTopActivityAppLabel })
                    obj.show()
                    obj.closed.connect(function() {
                        iClosePlrPrompt.createObject(iAppwin, { "transientParent": iAppwin }).show()
                    });
                    plrBackend.sendBlueStacksStorePromotionStats("mobile_app_exit_popup_impression", pTopActivityAppPackage)
                }
                return;
        }

        if (pShowFeedbackPopup && !plrBackend.checkInFeedbackPopupIgnoredList(pTopActivityAppPackage))
        {
            plrBackend.addToFeedbackPopupIgnoredList(pTopActivityAppPackage)
            var iFeedbackPopupObject = iFeedbackPopupComponent.createObject(iAppwin, { "transientParent": iAppwin })
            iFeedbackPopupObject.feedbackAppLabel = pTopActivityAppLabel
            iFeedbackPopupObject.feedbackAppPackage = pTopActivityAppPackage
            iFeedbackPopupObject.show()
        }
        else {
            if (plrBackend.pExitPromotionViewModel.pReady) {
                var component = Qt.createComponent("PlrExitPromotionsWindow.qml");
                if (component.status === Component.Ready) {
                    component.createObject(iAppwin, { "transientParent": iAppwin }).show()
                }
            }
            else
                iClosePlrPrompt.createObject(iAppwin, { "transientParent": iAppwin }).show()
        }
    }

    function fShowFeedbackPopup(appPackage, appLabel, showFeedbackPopup) {
        pTopActivityAppPackage = appPackage
        pTopActivityAppLabel = appLabel
        pShowFeedbackPopup = showFeedbackPopup
    }

    function fToggleTopbarIconAfterBoot(isQuestVisible, isNowbuxVisible) {
        pIsQuestEnabled = isQuestVisible
        pIsNowbuxEnabled = isNowbuxVisible
        fReloadTopbarElements()
    }

    function fHandleMiniModeClicked() {
        if (iAppwin.visibility === ApplicationWindow.Maximized)
            plrBackend.onRestoreClicked()

        sidebarHideClicked()
        pCurrentTopmostState = true
        fHandlePinToTop()

        plrBackend.resizeToMiniPlayer()

        iCfgBtn.visible = false
        fReloadTopbarElements()
    }

    function fHandlePinToTop() {
        if (true === pCurrentTopmostState)
            iAppwin.flags = iAppwin.flags | Qt.WindowStaysOnTopHint
        else
            iAppwin.flags = iAppwin.flags & ~Qt.WindowStaysOnTopHint
        plrBackend.setPinToTop(pCurrentTopmostState)

        uiBackend.confSetInstanceBool("pin_to_top", pCurrentTopmostState)
        uiBackend.confCommit()
    }

    function fHandleScreenRecordingStateChanged() {
        pIsScreenRecording = (nowggBackend.pRecordMode === RecordMode.NORMAL)
        pScreenRecordingStartTime = -1
        fReloadTopbarElements()
    }

    function fShowNowbuxIapRewardDialog(data) {
        iNowbuxIapRewardSuccess.createObject(iAppwin, {
                                                 "pNowbuxRewardSuccessData": data
                                             }).show()

        if (uiBackend.confGetInstanceBool("nowbux_signin_completed"))
            nowggBackend.getNowbuxBalance()
    }

    function fShowNowbuxAddGiftCardDialog() {
        if(pNowbuxAddGiftCardWindow != null)
            return

        pNowbuxAddGiftCardWindow = iNowbuxAddGiftCard.createObject(iAppwin, { "transientParent": iAppwin })
        pNowbuxAddGiftCardWindow.show()
    }

    function fShowNowbuxRedeemGiftCardDialog(success, errorMsg) {
        if (success) {
            if (pNowbuxAddGiftCardWindow != null)
                pNowbuxAddGiftCardWindow.close()

            iNowbuxAddGiftCardSuccess.createObject(iAppwin, { "transientParent": iAppwin }).show()

            if (uiBackend.confGetInstanceBool("nowbux_signin_completed"))
                nowggBackend.getNowbuxBalance()
        }
        else {
            if (pNowbuxAddGiftCardWindow != null)
                pNowbuxAddGiftCardWindow.redeemError(errorMsg)
        }
    }

    function fClearTopBarIconsList()
    {
        pNonFixedTopbarDynamicItems = []
    }
}
