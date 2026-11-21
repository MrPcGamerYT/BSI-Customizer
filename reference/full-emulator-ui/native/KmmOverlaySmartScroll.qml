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

import "KmmUtils.js" as Utils

Item {
    id: root
    width: iSmartScrollImg.width
    height: iSmartScrollImg.height

    property real controlOpacity: 1.0
    property real pScreenWidth: 1920
    property real pScalingFactor: 1.0
    property real pUnitSize: (27 * pScreenWidth) / (pScalingFactor * 1920)

    Image {
        id: iSmartScrollImg
        source: "qrcAssets/overlay_smart_scroll.svg"
        width: 32
        height: 32
        anchors.centerIn: parent
        opacity: controlOpacity
    }

    function fSetSmartScrollCoords() {
        kmmBackend.kmmSetSmartScrollPos(iSmartScrollImg.mapToGlobal(0,0).x, iSmartScrollImg.mapToGlobal(0,0).y)
    }
}
