/*
 * Copyright (C) 2020-2021 BlueStack Systems, Inc.
 * All Rights Reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF BLUESTACK SYSTEMS, INC.
 * The copyright notice above does not evidence any actual or intended
 * publication of such source code.
 */

import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import QtQuick.Shapes 1.12

import UiToolTipControl 1.0

import "KmmUtils.js" as Utils

Item {
    id: iRoot
    width: 36
    height: 36
    z: 1

    property QtObject pKmmControl: null
    property real pResizeUnit: 36
    property bool pSettingsWindowVisible: false
    signal settingsBtnClicked(var x, var y)

    Button {
        id: iBaseBtnId
        anchors.centerIn: parent
        background: Item { }
        icon.width: parent.width
        icon.height: parent.height
        enabled: true
        MouseArea {
            id: iBaseImgMA
            anchors.fill: iBaseBtnId
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: function(mouse) {
                if(mouse.button & Qt.RightButton) {
                    settingsBtnClicked(iRoot.mapToGlobal(0, 0).x, iRoot.mapToGlobal(0, 0).y + iRoot.height)
                    return
                }
            }
        }
    }

    Rectangle {
        id: iBtnRect
        anchors.fill: parent
        color: "#00000000"

        MouseArea {
            id: iBtnMouseArea
            anchors.fill: iBtnRect
            hoverEnabled: true
            propagateComposedEvents: true
            cursorShape: containsMouse ? iBaseImgMA.pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor : Qt.ArrowCursor
            onPressed: mouse.accepted = false
            onEntered: {
                if (!pSettingsWindowVisible) {
                    iSettingsBtn.visible = true
                }
            }
            onExited: {
                iSettingsBtn.visible = false
            }

            UiImageButton {
                id: iSettingsBtn
                asset: "VerySmallSettings"
                pImageWidth: 10
                pImageHeight: 10
                anchors.left: iBtnMouseArea.left
                anchors.bottom: iBtnMouseArea.bottom
                visible: false
                UiToolTip.text: qsTranslate("QObject", "Settings")
                onClicked: settingsBtnClicked(iRoot.mapToGlobal(0, 0).x, iRoot.mapToGlobal(0, 0).y + iRoot.height)
            }
        }
    }
    UiImageButton {
        id: iScaleBtn
        asset: "ScaleIcon"
        visible: !(pKmmControl != null && pKmmControl.crosshairType === "Custom" && pKmmControl.crosshairImage === "")
        pImageWidth: 10
        pImageHeight: 10
        anchors.right: iBaseBtnId.right
        anchors.bottom: iBaseBtnId.bottom
        onFocusChanged: focus ? Qt.SizeFDiagCursor : Qt.ArrowCursor
        enabled: pKmmControl != null && pKmmControl.isCrosshairEnabled ? true : false
        opacity: enabled ? 1 : 0.4

        MouseArea {
            id: iScaleBtnMouseArea
            anchors.fill: iScaleBtn
            hoverEnabled: true
            propagateComposedEvents: false
            cursorShape: Qt.SizeFDiagCursor

            drag {
                target: iScaleBtn
                axis: Drag.XAndYAxis
            }

            onMouseXChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseX) > (pResizeUnit))
                        pKmmControl.crosshairSize = ((iRoot.width + mouseX) / pResizeUnit).toFixed(2)
                }
            }
            onMouseYChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseY) > (pResizeUnit))
                        pKmmControl.crosshairSize = ((iRoot.width + mouseY) / pResizeUnit).toFixed(2)
                }
            }
        }
    }

    Component {
        id: iCrosshairPopupComponent
        UiWindowedPopup {
            id:  iCrosshairPopup
            contentWidth: iCrosshairPopupRect.width
            contentHeight: iCrosshairPopupRect.height
            visible: false
            closeOnPressOutside: true
            x: iSettingsBtn.mapToGlobal(0, 0).x - (width - iSettingsBtn.width) / 2
            y: iSettingsBtn.mapToGlobal(0, 0).y - (height - iSettingsBtn.height) / 2

            onWidthChanged: {
                iCrosshairPopup.x = iSettingsBtn.mapToGlobal(0, 0).x - iCrosshairPopup.width - iSettingsBtn.width + iCrosshairPopup.shadowThickness
            }

            Rectangle {
                id: iCrosshairPopupRect
                width: iCrosshairPopupContentLayout.width + iCrosshairPopupContentLayout.anchors.leftMargin
                       + iCrosshairPopupContentLayout.anchors.rightMargin
                height: iCrosshairPopupContentLayout.height + iCrosshairPopupContentLayout.anchors.topMargin
                        + iCrosshairPopupContentLayout.anchors.bottomMargin
                color: UiTheme.colors.accentNormal
                UiColumnLayout {
                    id: iCrosshairPopupContentLayout
                    width: 180
                    anchors.centerIn: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 10
                    anchors.bottomMargin: 10

                    UiText {
                        text: qsTranslate("QObject", "Enable crosshair to use in game")
                        font: UiTheme.fonts.bodyMedium
                        color: UiTheme.colors.primary10
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }
                }
            }
            Shape {
                anchors.horizontalCenter: iCrosshairPopupRect.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: iCrosshairPopupRect.height / 2
                anchors.horizontalCenterOffset: iCrosshairPopupRect.width / 2
                width: 12
                height: 6
                ShapePath {
                    strokeColor: UiTheme.colors.accentNormal
                    fillColor: UiTheme.colors.accentNormal
                    startX: 0
                    startY: 6
                    PathLine { x: 12; y: 0 }
                    PathLine { x: 0; y: -6 }
                }
            }
        }
    }

    function fOpenCrosshairInfoPopup() {
        var crosshairWin = iCrosshairPopupComponent.createObject(iRoot.parent)
        iSettingsBtn.visible = true
        crosshairWin.show()
    }


    function fLoadControlProp(gamepadTabSelected) {
        iRoot.x = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.crosshairX
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.crosshairY
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })
        iRoot.width = Qt.binding(function() {
            var val = pKmmControl == null ? 1 : pKmmControl.crosshairSize
            return (val * pResizeUnit)
        })
        iRoot.height = Qt.binding(function() {
            var val = pKmmControl == null ? 1 : pKmmControl.crosshairSize
            return (val * pResizeUnit)
        })

        if(pKmmControl != null)
            fChangeCrosshairImage(pKmmControl.isCrosshairEnabled)
    }

    function fChangeCrosshairImage(isEnabled) {
        iBaseBtnId.icon.source = Qt.binding(function() {
            if(pKmmControl != null) {
                var val = ""
                if(!isEnabled) {
                    val = "qrcAssets/Crosshair_disabled.svg"
                    return val
                }
                else {
                    if(pKmmControl.crosshairType !== "Custom")
                        val = "qrcAssets/Crosshair" + pKmmControl.crosshairType + ".svg"
                    else if(pKmmControl.crosshairImage !== "")
                        val = "file:/" + kmmBackend.getCrosshairImagePath(pKmmControl.crosshairImage)
                    return val
                }
            }
            else return ""
        })
        iBaseBtnId.icon.color = Qt.binding(function() {
            var val = (pKmmControl == null || pKmmControl.crosshairType === "Custom" || !pKmmControl.isCrosshairEnabled) ? "transparent" : pKmmControl.crosshairColor
            return val
        })
    }
}
