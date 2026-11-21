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
import Qt5Compat.GraphicalEffects
import QtQuick.Window 2.15

import "KmmUtils.js" as Utils

Window {
    id: iSmartScrollPopup
    width: root.implicitWidth
    height: root.implicitHeight < 70 ? 70 : root.implicitHeight
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    screen: transientParent.screen
    property string pSelectedWeaponIndex: ""
    property string pKeyText
    property real controlOpacity: 1.0
    property real pScreenWidth: 1920
    property real pScalingFactor: 1.0
    UiColumnLayout {
        id: root
        UiRowLayout {
            id: iLastWeaponUsedLayout
            color: UiTheme.colors.primary10
            padding: 3
            spacing: 8
            visible: iLastWeaponKey.text !== ""
            UiColumnLayout {
                id: iStaticIndexLayout
                color: UiTheme.colors.overlayPrimary
                radius: 3
                border.color: UiTheme.colors.overLayBright
                border.width: 1
                topPadding: 2
                bottomPadding: 2
                leftPadding: 6
                rightPadding: 6
                Layout.leftMargin: iLastWeaponKey.text.length === 1 ? 5 : 1
                implicitWidth: iLastWeaponKey.implicitWidth + leftPadding + rightPadding
                implicitHeight: iLastWeaponKey.implicitHeight + topPadding + bottomPadding
                Text {
                    id: iLastWeaponKey
                    color: UiTheme.colors.primary10
                    font: UiTheme.fonts.bodySmall
                    Layout.alignment: Text.AlignHCenter
                }
            }

            UiText {
                id: iStaticWeaponText
                Layout.fillWidth: true
                Layout.rightMargin: 5
                text: qsTranslate("QObject", "Last weapon used")
                color: UiTheme.colors.primary90
                font: UiTheme.fonts.titleSmallest
            }

            onWidthChanged:
            {
                if (iLastWeaponUsedLayout.width < 150)
                    iLastWeaponUsedLayout.width = 150
            }
        }
        UiRowLayout {
            id:iContenRowLayout
            UiColumnLayout {
                id: iContentLayout
                Layout.preferredWidth: Math.max(iLastWeaponUsedLayout.width, 150)
                color: UiTheme.colors.overLayBlack
                border.width: 1
                border.color: UiTheme.colors.overLayTint
                Layout.fillWidth: true
            gradient: Gradient{
                GradientStop{ position : 0.0; color:  UiTheme.colors.overLayBlack}
                GradientStop{ position : 0.1; color:  UiTheme.colors.overLayBlack }
                GradientStop{ position : 0.2; color:  UiTheme.colors.overLayBlack }
                GradientStop{ position : 0.4; color:  UiTheme.colors.overLayBlack}
                GradientStop{ position : 0.4; color:  UiTheme.colors.overLayBlack}
                GradientStop{ position : 0.6; color:  UiTheme.colors.overLayBlack}
                GradientStop{ position : 0.8; color:  UiTheme.colors.overLayBlack}
                //GradientStop{ position : 0.9; color:  UiTheme.colors.overlayPrimary}
                //GradientStop{ position : 1.0; color:  "#33FFFFFF"}
                GradientStop{ position : 0.9; color:  UiTheme.colors.overLayBlack}
                GradientStop{ position : 1.0; color:  UiTheme.colors.overLayBlack}

            }
                Repeater {
                    id: iList
                    Layout.fillWidth: true
                    model: kmmSmartScrollModel
                    delegate:
                        UiRowLayout {
                            Layout.fillWidth: true
                            color: pSelectedWeaponIndex != "" && pSelectedWeaponIndex === model.name ? "#4DFFFFFF" : "transparent"
                            padding: 4
                            spacing: 8
                            UiColumnLayout {
                                id: iIndexLayout
                                border.color: UiTheme.colors.overLayBright
                                border.width: 1
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                Layout.leftMargin: 4
                                color: pSelectedWeaponIndex != "" && pSelectedWeaponIndex === model.name ? UiTheme.colors.overLayBright : UiTheme.colors.overlayPrimary
                                padding: 2
                                radius: 3
                                Text {
                                    text: model.index
                                    Layout.alignment: Text.AlignHCenter
                                    color: pSelectedWeaponIndex != "" && pSelectedWeaponIndex === model.name ? UiTheme.colors.primary100 : UiTheme.colors.primary10
                                    font: UiTheme.fonts.bodySmall
                                }
                            }
                            UiText {
                                id: iWeaponText
                                Layout.fillWidth: true
                                text: model.name
                                color: pSelectedWeaponIndex != "" && pSelectedWeaponIndex === model.name ? UiTheme.colors.primary10 : UiTheme.colors.overLayBright
                                font: UiTheme.fonts.bodySmall
                            }
                        }
                }
            }
            Image {
                id: iSmartScrollImg
                source: "qrcAssets/WeaponScrollOverlay.svg"
                width: 40
                height: 48
            }
        }
    }

    Timer {
        id: iPopupTimer
        repeat: false
        interval: 3000
        running: false
        onTriggered: fClosePopup()
    }

    function fClosePopup() {
        iPopupTimer.stop()
        iSmartScrollPopup.close()
        iSmartScrollPopup.destroy()
        kmmBackend.toggleSmartScrollOverlayVisibility(true)
    }

    function fRefreshSelectedIndex(index) {
        pSelectedWeaponIndex = index
    }

    function fShowScrollOverlay(x, y, screenWidth, scalingFactor, index, count, lastWeaponKey) {
        pScreenWidth = screenWidth
        pScalingFactor = scalingFactor

        // Calculate position after popup is fully rendered
        Qt.callLater(function() {
            // Calculate x position based on current width
            var xVal = x - iSmartScrollPopup.width + iSmartScrollImg.width - 4
            iSmartScrollPopup.x = xVal

            var yVal = y - iSmartScrollPopup.height / 3
            if(count > 3)
                yVal -= 8
            iSmartScrollPopup.y = yVal
        })

        iLastWeaponKey.text = lastWeaponKey
        fRefreshSelectedIndex(index)
        iSmartScrollPopup.show()
        iPopupTimer.start()
    }

    function fRestartTimer(index) {
        iPopupTimer.restart()
        iList.model = []
        iList.model = kmmSmartScrollModel
        fRefreshSelectedIndex(index)
    }
}
