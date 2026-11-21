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

import "KmmUtils.js" as Utils

Rectangle {
    id: root
    property string pKeyText
    property real controlOpacity: 1.0
    property real pScreenWidth: 1920
    property real pScalingFactor: 1.0
    property real pUnitSize: (27 * pScreenWidth) / (pScalingFactor * 1920)

    width: {
        if (overlayKeyImage.visible)
            overlayKeyImage.width - 4;
        else {
            if (overlayKeyText.text.length === 1)
                pUnitSize;
            else
                overlayKeyText.width + 20
        }
    }
    height: {
        if (overlayKeyImage.visible)
            overlayKeyImage.height - 4;
        else
            pUnitSize;
    }
    radius: height * 0.5
    color: UiTheme.colors.primary100
    opacity: controlOpacity
    border {
        width: {
            if (overlayKeyImage.visible)
                0;
            else
                1.5;
        }
        color: UiTheme.colors.primary10
    }

    Text {
        id: overlayKeyText
        font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
        color: UiTheme.colors.primary10
        anchors.centerIn: parent
        text: Utils.keysTranslationMap[pKeyText] !== undefined ? Utils.keysTranslationMap[pKeyText] : pKeyText
        visible: !(Utils.keyImages.indexOf(pKeyText) >= 0)
    }

    Image {
        id: overlayKeyImage
        source: "qrcAssets/Imap_" + (visible ? pKeyText : "Up") + ".png"
        width: pUnitSize
        height: pUnitSize
        visible: (Utils.keyImages.indexOf(pKeyText) >= 0)
        anchors.centerIn: parent

        onSourceChanged: {
            if (pKeyText.slice(0, 5) === "Mouse") {
                overlayKeyImage.width = (21 * pScreenWidth) / (pScalingFactor * 1920)
                overlayKeyImage.height = pUnitSize
                root.width = (15 * pScreenWidth) / (pScalingFactor * 1920)
                root.height = (19 * pScreenWidth) / (pScalingFactor * 1920)
            }
        }
    }
}
