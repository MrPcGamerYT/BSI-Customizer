/*
 * Copyright (C) 2020-2021 BlueStack Systems, Inc.
 * All Rights Reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF BLUESTACK SYSTEMS, INC.
 * The copyright notice above does not evidence any actual or intended
 * publication of such source code.
 */

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "KmmUtils.js" as Utils

Item {
    id: root
    property string pKeyClockwiseText
    property string pKeyCounterClockwiseText
    property real pRadius: 6
    property real pScreenWidth: 1920
    property real pScalingFactor: 1.0
    property real pUnitSize: (27 * pScreenWidth) / (pScalingFactor * 1920)

    property real controlOpacity: 1.0

    width: pUnitSize + pRadius * (plrBackend.glWinRect[2] / 100)
    height: width / 2

    Image {
        id: rotateCounterClockwiseImg
        source: "qrcAssets/overlay_left_arrow.png"
        fillMode: Image.PreserveAspectFit
        opacity: controlOpacity
        height: parent.height
        anchors.left: parent.left
        anchors.top: parent.top
        visible: pKeyCounterClockwiseText !== ""
    }
    Rectangle {
        width: {
            if (rotateCounterClockImage.visible)
                rotateCounterClockImage.width;
            else {
                if (rotateCounterClockwiseKeyText.text.length === 1)
                    pUnitSize
                else
                    rotateCounterClockwiseKeyText.width + 16
            }
        }
        height: {
            if (rotateCounterClockImage.visible)
                rotateCounterClockImage.height;
            else
                rotateCounterClockwiseKeyText.height + 8
        }
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: -(width / 2) + 2
        radius: height * 0.5
        color: UiTheme.colors.primary100
        opacity: controlOpacity
        visible: pKeyCounterClockwiseText !== ""
        border {
            width: {
                if (rotateCounterClockImage.visible)
                    0;
                else
                    1;
            }
            color: UiTheme.colors.primary10
        }
        Text {
            id: rotateCounterClockwiseKeyText
            font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
            color: UiTheme.colors.primary10
            anchors.centerIn: parent
            text: Utils.keysTranslationMap[pKeyCounterClockwiseText] !== undefined ? Utils.keysTranslationMap[pKeyCounterClockwiseText] : pKeyCounterClockwiseText
            visible: !(Utils.keyImages.indexOf(pKeyCounterClockwiseText) >= 0)
        }
        Image {
            id: rotateCounterClockImage
            source: "qrcAssets/Imap_" + (visible ? pKeyCounterClockwiseText : "Up") + ".png"
            width: pUnitSize
            height: pUnitSize
            visible: (Utils.keyImages.indexOf(pKeyCounterClockwiseText) >= 0)
            anchors.centerIn: parent
        }
    }

    Image {
        id: rotateClockwiseImg
        source: "qrcAssets/overlay_right_arrow.png"
        fillMode: Image.PreserveAspectFit
        opacity: controlOpacity
        height: parent.height
        anchors.right: parent.right
        anchors.top: parent.top
        visible: pKeyClockwiseText !== ""
    }
    Rectangle {
        width: {
            if (rotateClockImage.visible)
                rotateClockImage.width;
            else {
                if (rotateClockwiseKeyText.text.length === 1)
                    pUnitSize
                else
                    rotateClockwiseKeyText.width + 16
            }
        }
        height: {
            if (rotateClockImage.visible)
                rotateClockImage.height;
            else
                rotateClockwiseKeyText.height + 8
        }
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: -(width / 2) + 2
        radius: height * 0.5
        color: UiTheme.colors.primary100
        opacity: controlOpacity
        visible: pKeyClockwiseText !== ""
        border {
            width: {
                if (rotateClockImage.visible)
                    0;
                else
                    1;
            }
            color: UiTheme.colors.primary10
        }
        Text {
            id: rotateClockwiseKeyText
            font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
            color: UiTheme.colors.primary10
            anchors.centerIn: parent
            text: Utils.keysTranslationMap[pKeyClockwiseText] !== undefined ? Utils.keysTranslationMap[pKeyClockwiseText] : pKeyClockwiseText
            visible: !(Utils.keyImages.indexOf(pKeyClockwiseText) >= 0)
        }
        Image {
            id: rotateClockImage
            source: "qrcAssets/Imap_" + (visible ? pKeyClockwiseText : "Up") + ".png"
            width: pUnitSize
            height: pUnitSize
            visible: (Utils.keyImages.indexOf(pKeyClockwiseText) >= 0)
            anchors.centerIn: parent
        }
    }
}
