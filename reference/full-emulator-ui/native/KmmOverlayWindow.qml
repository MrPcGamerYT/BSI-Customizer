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

Window {
    id: kmmOverlayWin
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowTransparentForInput
    title: uiBackend.productName + " Keymap Overlay"
    screen: transientParent.screen

    property KmmOverlaySmartScrollPopup pSmartScrollControl: null
    property KmmGameControlToogleHintPopup pGameControlToogleHintPopup: null

    Component {
        id: iSmartScrollComponent
        KmmOverlaySmartScrollPopup {}
    }

    Component {
        id:iGameControlToogleHintPopupComponent
        KmmGameControlToogleHintPopup {
            x: kmmOverlayWin.x + (kmmOverlayWin.width / 2) - (width / 2)
            y: kmmOverlayWin.y
        }
    }

    function fShowSmartScrollPopup(x, y, screenWidth, scalingFactor, index, count, lastWeaponKey) {
        if(pSmartScrollControl == null) {
            pSmartScrollControl = iSmartScrollComponent.createObject(kmmOverlayWin)
            pSmartScrollControl.fShowScrollOverlay(x, y, screenWidth, scalingFactor, index, count, lastWeaponKey)
        }
        else {
            pSmartScrollControl.fRestartTimer(index)
        }
    }

    function fCloseSmartScrollPopup() {
        if(pSmartScrollControl != null)
            pSmartScrollControl.fClosePopup()
    }

    function fShowGameControlToogleHintPopup() {
        if(pGameControlToogleHintPopup != null)
            return

        pGameControlToogleHintPopup = iGameControlToogleHintPopupComponent.createObject(kmmOverlayWin, { "transientParent": kmmOverlayWin })
        if (pGameControlToogleHintPopup.width > kmmOverlayWin.width) {
            fCloseGameControlToogleHintPopup()
            return
        }
        pGameControlToogleHintPopup.show()
    }

    function fCloseGameControlToogleHintPopup() {
        if(pGameControlToogleHintPopup != null)
            pGameControlToogleHintPopup.close()
    }
}
