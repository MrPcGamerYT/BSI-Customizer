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
    width: freelookBottomImage.width
    height: freelookBottomImage.height

    property string pKeyText
    property string pKeyUpText
    property string pKeyLeftText
    property string pKeyRightText
    property string pKeyDownText

    property real controlOpacity: 1.0
    property real pScreenWidth: 1920
    property real pScalingFactor: 1.0
    property real pUnitSize: (27 * pScreenWidth) / (pScalingFactor * 1920)

    Image {
        id: freelookBottomImage
        source: pKeyText === "" ? "qrcAssets/overlay_keyboard.png" : "qrcAssets/overlay_mouse.png"
        width: ((pKeyText === "" ? 100 : 32) * pScreenWidth) / (pScalingFactor * 1920)
        height: ((pKeyText === "" ? 100 : 30) * pScreenWidth) / (pScalingFactor * 1920)
        anchors.centerIn: parent
        opacity: controlOpacity

        Text {
            anchors.centerIn: parent
            text: Utils.keysTranslationMap[pKeyText] !== undefined ? Utils.keysTranslationMap[pKeyText] : pKeyText
            font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
            color: UiTheme.colors.primary10
            visible: pKeyText !== ""
        }
    }

    Rectangle {
        anchors.fill: parent
        visible: pKeyText === ""
        color: "#00000000"

        Rectangle {
            width: {
                if (freeLookKeyUpImage.visible)
                    freeLookKeyUpImage.width;
                else {
                    if (freeLookKeyUpTxt.text.length === 1)
                        pUnitSize
                    else
                        freeLookKeyUpTxt.width + 16
                }
            }
            height: {
                if (freeLookKeyUpImage.visible)
                    freeLookKeyUpImage.height;
                else
                    freeLookKeyUpTxt.height + 8;
            }
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: -(height / 2)
            color: UiTheme.colors.primary100
            opacity: controlOpacity
            radius: height * 0.5
            visible: pKeyUpText !== ""
            border {
                width: {
                    if (freeLookKeyUpImage.visible)
                        0;
                    else
                        1;
                }
                color: UiTheme.colors.primary10
            }
            Text {
                id: freeLookKeyUpTxt
                font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
                color: UiTheme.colors.primary10
                anchors.centerIn: parent
                text: Utils.keysTranslationMap[pKeyUpText] !== undefined ? Utils.keysTranslationMap[pKeyUpText] : pKeyUpText
                visible: !(Utils.keyImages.indexOf(pKeyUpText) >= 0)
            }
            Image {
                id: freeLookKeyUpImage
                source: "qrcAssets/Imap_" + (visible ? pKeyUpText : "Up") + ".png"
                width: pUnitSize
                height: pUnitSize
                visible: (Utils.keyImages.indexOf(pKeyUpText) >= 0)
                anchors.centerIn: parent
            }
        }

        Rectangle {
            width: {
                if (freeLookKeyLeftImage.visible)
                    freeLookKeyLeftImage.width;
                else {
                    if (freeLookKeyLeftTxt.text.length === 1)
                        pUnitSize
                    else
                        freeLookKeyLeftTxt.width + 16
                }
            }
            height: {
                if (freeLookKeyLeftImage.visible)
                    freeLookKeyLeftImage.height;
                else
                    freeLookKeyLeftTxt.height + 8;
            }
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: -(width / 2)
            color: UiTheme.colors.primary100
            opacity: controlOpacity
            radius: height * 0.5
            visible: pKeyLeftText !== ""
            border {
                width: {
                    if (freeLookKeyLeftImage.visible)
                        0;
                    else
                        1;
                }
                color: UiTheme.colors.primary10
            }
            Text {
                id: freeLookKeyLeftTxt
                font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
                color: UiTheme.colors.primary10
                anchors.centerIn: parent
                text: Utils.keysTranslationMap[pKeyLeftText] !== undefined ? Utils.keysTranslationMap[pKeyLeftText] : pKeyLeftText
                visible: !(Utils.keyImages.indexOf(pKeyLeftText) >= 0)
            }
            Image {
                id: freeLookKeyLeftImage
                source: "qrcAssets/Imap_" + (visible ? pKeyLeftText : "Up") + ".png"
                width: pUnitSize
                height: pUnitSize
                visible: (Utils.keyImages.indexOf(pKeyLeftText) >= 0)
                anchors.centerIn: parent
            }
        }

        Rectangle {
            width: {
                if (freeLookKeyRightImage.visible)
                    freeLookKeyRightImage.width;
                else {
                    if (freeLookKeyRightTxt.text.length === 1)
                        pUnitSize
                    else
                        freeLookKeyRightTxt.width + 16
                }
            }
            height: {
                if (freeLookKeyRightImage.visible)
                    freeLookKeyRightImage.height;
                else
                    freeLookKeyRightTxt.height + 8;
            }
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: -(width / 2)
            color: UiTheme.colors.primary100
            opacity: controlOpacity
            radius: height * 0.5
            visible: pKeyRightText !== ""
            border {
                width: {
                    if (freeLookKeyRightImage.visible)
                        0;
                    else
                        1;
                }
                color: UiTheme.colors.primary10
            }
            Text {
                id: freeLookKeyRightTxt
                font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
                color: UiTheme.colors.primary10
                anchors.centerIn: parent
                text: Utils.keysTranslationMap[pKeyRightText] !== undefined ? Utils.keysTranslationMap[pKeyRightText] : pKeyRightText
                visible: !(Utils.keyImages.indexOf(pKeyRightText) >= 0)
            }
            Image {
                id: freeLookKeyRightImage
                source: "qrcAssets/Imap_" + (visible ? pKeyRightText : "Up") + ".png"
                width: pUnitSize
                height: pUnitSize
                visible: (Utils.keyImages.indexOf(pKeyRightText) >= 0)
                anchors.centerIn: parent
            }
        }

        Rectangle {
            width: {
                if (freeLookKeyDownImage.visible)
                    freeLookKeyDownImage.width;
                else {
                    if (freeLookKeyDownTxt.text.length === 1)
                        pUnitSize
                    else
                        freeLookKeyDownTxt.width + 16
                }
            }
            height: {
                if (freeLookKeyDownImage.visible)
                    freeLookKeyDownImage.height;
                else
                    freeLookKeyDownTxt.height + 8
            }
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottomMargin: -(height / 2)
            color: UiTheme.colors.primary100
            opacity: controlOpacity
            radius: height * 0.5
            visible: pKeyDownText !== ""
            border {
                width: {
                    if (freeLookKeyDownImage.visible)
                        0;
                    else
                        1;
                }
                color: UiTheme.colors.primary10
            }
            Text {
                id: freeLookKeyDownTxt
                font: (pScreenWidth <= 1920 || pScalingFactor > 1.5 ? UiTheme.fonts.buttonSmall : UiTheme.fonts.buttonMedium)
                color: UiTheme.colors.primary10
                anchors.centerIn: parent
                text: Utils.keysTranslationMap[pKeyDownText] !== undefined ? Utils.keysTranslationMap[pKeyDownText] : pKeyDownText
                visible: !(Utils.keyImages.indexOf(pKeyDownText) >= 0)
            }
            Image {
                id: freeLookKeyDownImage
                source: "qrcAssets/Imap_" + (visible ? pKeyDownText : "Up") + ".png"
                width: pUnitSize
                height: pUnitSize
                visible: (Utils.keyImages.indexOf(pKeyDownText) >= 0)
                anchors.centerIn: parent
            }
        }
    }
}
