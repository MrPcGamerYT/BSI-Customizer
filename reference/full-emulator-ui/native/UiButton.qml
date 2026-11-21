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
import UiToolTipControl 1.0

Button {
    id: iControl

    property QtObject pSize
    property QtObject pColor
    property QtObject pColorState: down ? pColor.click :
                                          hovered ? pColor.hover : pColor.normal
    property string asset
    property string extn: ".svg"
    property bool pImageAfterText: false
    property bool pExtraWidthAvailable: false
    property string pSubText: ""
    property Gradient btnGradient: null

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: pSize.height

    horizontalPadding: pSize.horizontalPadding
    verticalPadding: 0
    spacing: pSize.spacing

    text: "Label"
    font: pSize.textFont

    UiToolTip.visible: hovered && iContentText.truncated
    UiToolTip.text: text

    onWidthChanged: {
        if (width > implicitWidth)
            pExtraWidthAvailable = true
        else
            pExtraWidthAvailable = false
    }

    background: Loader {
        sourceComponent: iControl.pColorState.borderGradient === null ? defaultComp : gradientBorderComp
    }

    Component {
        id: gradientBorderComp
        UiRowLayout {
            padding: 1
            color: iControl.pColorState.borderColor
            radius: iControl.pSize.radius
            gradient: iControl.pColorState.borderGradient
            layer.enabled: true
            layer.smooth: true
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: iControl.pSize.radius
                color: iControl.pColorState.background
            }
        }
    }

    Component {
        id: defaultComp
        Rectangle {
            color: iControl.pColorState.background
            radius: iControl.pSize.radius
            border.color: iControl.pColorState.borderColor
            border.width: 1
            gradient: btnGradient
        }
    }

    contentItem: UiColumnLayout {
        UiRowLayout {
            Layout.fillWidth: true
            Layout.topMargin: pSubText != "" ? 8 : 0
            Item {
                visible: pExtraWidthAvailable
                Layout.fillWidth: true
            }
            UiImage {
                asset: iControl.asset
                extn: iControl.extn
                visible: asset != "" && !pImageAfterText
                pImageWidth: iControl.pSize.iconWidth
                pImageHeight: iControl.pSize.iconHeight
                Layout.rightMargin: iControl.spacing
            }
            Text {
                id: iContentText
                Layout.fillWidth: pExtraWidthAvailable ? false : true
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignTop
                text: iControl.text
                font: iControl.font
                color: iControl.pColorState.textColor
                elide: Text.ElideRight
            }
            UiImage {
                asset: iControl.asset
                extn: iControl.extn
                visible: asset != "" && pImageAfterText
                pImageWidth: iControl.pSize.iconWidth
                pImageHeight: iControl.pSize.iconHeight
                Layout.leftMargin: iControl.spacing
            }
            Item {
                visible: pExtraWidthAvailable
                Layout.fillWidth: true
            }
        }
        Text {
            Layout.fillWidth: true
            visible: pSubText != ""
            Layout.alignment: Qt.AlignHCenter
            horizontalAlignment: Text.AlignHCenter
            text: pSubText
            font: pSize.subTextFont
            Layout.bottomMargin: pSubText != "" ? 8 : 0
            color: iControl.pColorState.subTextColor
            elide: Text.ElideRight
        }
    }
}
