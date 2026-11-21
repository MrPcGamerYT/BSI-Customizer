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

Slider {
    id: iControl

    property QtObject pSize
    property QtObject pColor: UiTheme.sliders.colors.accent
    property string pRecommendedValueText: ""
    property string pSelectedValueText: ""
    property bool pRangeValueVisibility: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitHandleWidth + leftPadding + rightPadding,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + leftInset + rightInset,
                             implicitHandleHeight + leftPadding + rightPadding,
                             implicitContentHeight + leftPadding + rightPadding)

    padding: 0

    handle: Rectangle {
        x: iControl.leftPadding + iControl.visualPosition * (iControl.availableWidth - width)
        y: contentHeader.visible ? contentHeader.implicitHeight : 0
        implicitWidth: iControl.pSize.handleWidth
        implicitHeight: iControl.pSize.handleHeight
        radius: width / 2
        color: enabled ? iControl.pColor.handleBackground : iControl.pColor.handleBackgroundDisabled
        border.width: 2
        border.color: iControl.pColor.handleBorder
    }

    background: Rectangle {
        color: "transparent"
    }

    contentItem: UiColumnLayout {
        leftPadding: 2 //handle border width
        rightPadding: 2 //handle border width
        UiRowLayout {
            id: contentHeader
            Layout.fillWidth: true
            visible: iControl.pSelectedValueText !== "" || iControl.pRecommendedValueText !== ""
            Text {
                visible: text !== ""
                text: iControl.pSelectedValueText
                font: iControl.pSize.selectedValueFont
                color: iControl.pColor.selectedValue
            }
            Text {
                visible: text !== ""
                Layout.alignment: Qt.AlignRight
                text: iControl.pRecommendedValueText
                font: iControl.pSize.recommendedValueFont
                color: iControl.pColor.recommendedValue
            }
        }
        UiRowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: iControl.pSize.handleHeight
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: iControl.pSize.sliderHeight
                color: enabled ? iControl.pColor.sliderBackground : iControl.pColor.sliderBackgroundDisabled
                Rectangle {
                    width: iControl.visualPosition * parent.width
                    height: parent.height
                    color: enabled ? iControl.pColor.sliderFill : iControl.pColor.sliderFillDisabled
                }
            }
        }
        UiRowLayout {
            Layout.fillWidth: true
            visible: iControl.pRangeValueVisibility
            Text {
                text: iControl.from
                font: iControl.pSize.rangeValueFont
                color: iControl.pColor.rangeValue
            }
            Text {
                Layout.alignment: Qt.AlignRight
                text: iControl.to
                font: iControl.pSize.rangeValueFont
                color: iControl.pColor.rangeValue
            }
        }
    }
}
