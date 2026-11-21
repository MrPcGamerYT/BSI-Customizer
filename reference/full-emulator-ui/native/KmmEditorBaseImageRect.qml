/*
 * Copyright (C) 2021-2023 BlueStack Systems, Inc.
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

Rectangle {
    anchors.fill: parent
    color: "#00000000"

    property var pStartingMouse: undefined
    property alias pImgSrc: iBaseImg.asset

    signal deleteClicked()
    signal settingsClicked()
    signal itemMoved(var startingMouse, var currentMouse)

    MouseArea {
        id: iBtnMouseArea
        anchors.fill: parent
        hoverEnabled: true
        propagateComposedEvents: true
        onPressed: (mouse)=> {
                       mouse.accepted = false
                   }
        onEntered: {
            if (iRoot.pSettingsWin == null) {
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
            onClicked:  settingsClicked()
        }

        UiImage {
            id: iBaseImg
            pImageWidth: iRoot.width - 6
            pImageHeight: iRoot.width - 6
            anchors.centerIn: parent

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                propagateComposedEvents: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: (containsMouse && uiBackend.isTransparentClickAccepted(iBaseImg.source, iBaseImg.width, iBaseImg.height, mouseX, mouseY)) ?
                                 pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor : Qt.ArrowCursor
                onPressed: function(mouse) {
                    mouse.accepted = uiBackend.isTransparentClickAccepted(iBaseImg.source, iBaseImg.width, iBaseImg.height, mouse.x, mouse.y)
                    if(mouse.accepted){
                        if(mouse.button & Qt.RightButton) {
                            iRoot.fOpenSettingsWindow()
                            return
                        }
                        pStartingMouse = { x: mouse.x, y: mouse.y}
                    }
                }
                onPositionChanged: function(mouse) {
                    if (pStartingMouse !== undefined)
                        itemMoved(pStartingMouse, mouse)
                }
                onReleased: pStartingMouse = undefined
            }
        }
    }
}
