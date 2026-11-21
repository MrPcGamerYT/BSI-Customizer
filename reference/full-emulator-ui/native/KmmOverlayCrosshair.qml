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

Item {
    property real pScreenWidth: 1920
    property real pScalingFactor: 1.0
    property real pCrosshairSize: 1.0
    property string pCrosshairColor: "#FFFFFF"
    property string pCrosshairImagePath: ""
    property string pCrosshairType: ""

    width: (36 * pScreenWidth * pCrosshairSize) / (pScalingFactor * 1920)
    height: (36 * pScreenWidth * pCrosshairSize) / (pScalingFactor * 1920)
    Button {
        id: iCrosshairOverlayImg
        anchors.centerIn: parent
        background: Item { }
        icon.width: parent.width
        icon.height: parent.height
        enabled: false
    }

    function fLoadCrosshairControlImgProps(imageType, imagePath, imageOpacity) {
        if(imagePath !== "") {
            if(imageType === "Custom") {
                iCrosshairOverlayImg.icon.source = "file:/" + imagePath
                iCrosshairOverlayImg.icon.color = "transparent"
            }
            else {
                iCrosshairOverlayImg.icon.source = "qrc:///qrcAssets/" + imagePath + ".svg"
                iCrosshairOverlayImg.icon.color = pCrosshairColor
            }
        }
        iCrosshairOverlayImg.opacity = imageOpacity
    }
}
