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

import UiToolTipControl 1.0

import "KmmUtils.js" as Utils

Item {
    id: iRoot
    width: 36
    height: 36
    z: 1

    property QtObject pKmmControl: null
    property var pStartingMouse
    property bool pSettingsWindowVisible: false

    signal settingsBtnClicked(var x, var y)
    signal deleteClicked()

    UiImage {
        id: iBaseImg
        asset: "MouseLeftClick"
        pImageWidth: iRoot.width - 6
        pImageHeight: iRoot.width - 6
        anchors.centerIn: parent

        MouseArea {
            id: iBaseImgMA
            anchors.fill: iBaseImg
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: function(mouse) {
                if(mouse.button & Qt.RightButton) {
                    settingsBtnClicked(iRoot.mapToGlobal(0, 0).x, iRoot.mapToGlobal(0, 0).y + iRoot.height)
                    return
                }
                pStartingMouse = { x: mouse.x, y: mouse.y}
            }
            onPositionChanged: function(mouse) {
                if(pStartingMouse !== undefined)
                    fMoveItem(pStartingMouse, mouse)
            }
            onReleased: pStartingMouse = undefined
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
            onPressed: function(mouse) {
                mouse.accepted = false
            }
            onEntered: {
                if (!pSettingsWindowVisible) {
                    iDeleteBtn.visible = true
                    iSettingsBtn.visible = true
                }
            }
            onExited: {
                iDeleteBtn.visible = false
                iSettingsBtn.visible = false
            }

            UiImageButton {
                id: iDeleteBtn
                asset: "VerySmallClose"
                pImageWidth: 10
                pImageHeight: 10
                anchors.top: iBtnMouseArea.top
                anchors.right: iBtnMouseArea.right
                onClicked: deleteClicked()
                visible: false
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

    function fMoveItem(startingMouse, currentMouse) {
        var newX = iRoot.x + currentMouse.x - startingMouse.x
        var newY = iRoot.y + currentMouse.y - startingMouse.y
        var centerX = newX + iRoot.width / 2
        var centerY = newY + iRoot.height / 2

        if (centerX > plrBackend.glWinRect[0] && centerX < (plrBackend.glWinRect[2] + plrBackend.glWinRect[0]))
            pKmmControl.lButtonX = (((centerX - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
        else if(centerX > (plrBackend.glWinRect[2] + plrBackend.glWinRect[0]))
            pKmmControl.lButtonX = plrBackend.glWinRect[2] + plrBackend.glWinRect[0]
        if (centerY > plrBackend.glWinRect[1] && centerY < (plrBackend.glWinRect[3] + plrBackend.glWinRect[1]))
            pKmmControl.lButtonY = (((centerY - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
    }

    function fLoadControlProp(gamepadTabSelected) {
        iRoot.x = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.lButtonX
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.lButtonY
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })
    }
}
