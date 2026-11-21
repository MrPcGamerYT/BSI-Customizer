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
    property string pKeyUpText
    property string pKeyLeftText
    property string pKeyRightText
    property string pKeyDownText
    property real pRadius: 10
    property real pScreenWidth: 1920
    property real pScalingFactor: 1.0
    property real pUnitSize: (27 * pScreenWidth) / (pScalingFactor * 1920)

    property real controlOpacity: 1.0
    width: pUnitSize + pRadius * (plrBackend.glWinRect[2] / 100)
    height: width

    Rectangle {
        width: {
            if (dpadKeyUpImage.visible)
                dpadKeyUpImage.width;
            else {
                if (dpadKeyUpTxt.text.length === 1)
                    pUnitSize
                else
                    dpadKeyUpTxt.width + 20
            }
        }
        height: pUnitSize
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        color: UiTheme.colors.primary100
        opacity: controlOpacity
        radius: height * 0.5
        visible: pKeyUpText !== ""
        border {
            width: {
                if (dpadKeyUpImage.visible)
                    0;
                else
                    1.5;
            }
            color: UiTheme.colors.primary10
        }
        Text {
            id: dpadKeyUpTxt
            font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
            color: UiTheme.colors.primary10
            anchors.centerIn: parent
            text: Utils.keysTranslationMap[pKeyUpText] !== undefined ? Utils.keysTranslationMap[pKeyUpText] : pKeyUpText
            visible: !(Utils.keyImages.indexOf(pKeyUpText) >= 0)
        }
        Image {
            id: dpadKeyUpImage
            source: "qrcAssets/Imap_" + (visible ? pKeyUpText : "Up") + ".png"
            width: pUnitSize
            height: pUnitSize
            visible: (Utils.keyImages.indexOf(pKeyUpText) >= 0)
            anchors.centerIn: parent
        }
    }

    Rectangle {
        width: {
            if (dpadKeyLeftImage.visible)
                dpadKeyLeftImage.width;
            else {
                if (dpadKeyLeftTxt.text.length === 1)
                    pUnitSize
                else
                    dpadKeyLeftTxt.width + 20
            }
        }
        height: pUnitSize
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        color: UiTheme.colors.primary100
        opacity: controlOpacity
        radius: height * 0.5
        visible: pKeyLeftText !== ""
        border {
            width: {
                if (dpadKeyLeftImage.visible)
                    0;
                else
                    1.5;
            }
            color: UiTheme.colors.primary10
        }
        Text {
            id: dpadKeyLeftTxt
            font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
            color: UiTheme.colors.primary10
            anchors.centerIn: parent
            text: Utils.keysTranslationMap[pKeyLeftText] !== undefined ? Utils.keysTranslationMap[pKeyLeftText] : pKeyLeftText
            visible: !(Utils.keyImages.indexOf(pKeyLeftText) >= 0)
        }
        Image {
            id: dpadKeyLeftImage
            source: "qrcAssets/Imap_" + (visible ? pKeyLeftText : "Up") + ".png"
            width: pUnitSize
            height: pUnitSize
            visible: (Utils.keyImages.indexOf(pKeyLeftText) >= 0)
            anchors.centerIn: parent
        }
    }

    Rectangle {
        width: {
            if (dpadKeyRightImage.visible)
                dpadKeyRightImage.width;
            else {
                if (dpadKeyRightTxt.text.length === 1)
                    pUnitSize
                else
                    dpadKeyRightTxt.width + 20
            }
        }
        height: pUnitSize
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        color: UiTheme.colors.primary100
        opacity: controlOpacity
        radius: height * 0.5
        visible: pKeyRightText !== ""
        border {
            width: {
                if (dpadKeyRightImage.visible)
                    0;
                else
                    1.5;
            }
            color: UiTheme.colors.primary10
        }
        Text {
            id: dpadKeyRightTxt
            font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
            color: UiTheme.colors.primary10
            anchors.centerIn: parent
            text: Utils.keysTranslationMap[pKeyRightText] !== undefined ? Utils.keysTranslationMap[pKeyRightText] : pKeyRightText
            visible: !(Utils.keyImages.indexOf(pKeyRightText) >= 0)
        }
        Image {
            id: dpadKeyRightImage
            source: "qrcAssets/Imap_" + (visible ? pKeyRightText : "Up") + ".png"
            width: pUnitSize
            height: pUnitSize
            visible: (Utils.keyImages.indexOf(pKeyRightText) >= 0)
            anchors.centerIn: parent
        }
    }

    Rectangle {
        width: {
            if (dpadKeyDownImage.visible)
                dpadKeyDownImage.width;
            else {
                if (dpadKeyDownTxt.text.length === 1)
                    pUnitSize
                else
                    dpadKeyDownTxt.width + 20
            }
        }
        height: pUnitSize
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        color: UiTheme.colors.primary100
        opacity: controlOpacity
        radius: height * 0.5
        visible: pKeyDownText !== ""
        border {
            width: {
                if (dpadKeyDownImage.visible)
                    0;
                else
                    1.5;
            }
            color: UiTheme.colors.primary10
        }
        Text {
            id: dpadKeyDownTxt
            font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
            color: UiTheme.colors.primary10
            anchors.centerIn: parent
            text: Utils.keysTranslationMap[pKeyDownText] !== undefined ? Utils.keysTranslationMap[pKeyDownText] : pKeyDownText
            visible: !(Utils.keyImages.indexOf(pKeyDownText) >= 0)
        }
        Image {
            id: dpadKeyDownImage
            source: "qrcAssets/Imap_" + (visible ? pKeyDownText : "Up") + ".png"
            width: pUnitSize
            height: pUnitSize
            visible: (Utils.keyImages.indexOf(pKeyDownText) >= 0)
            anchors.centerIn: parent
        }
    }
}
