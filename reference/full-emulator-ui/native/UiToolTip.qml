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
import QtQuick.Layouts 1.15

Window {
    property QtObject pColor: UiTheme.tooltips.colors.accent

    property string toolTipText: ""
    property bool pDisableWrapping: false

    width: Math.min((pDisableWrapping ? Number.MAX_VALUE : 160), toolTipRect.implicitWidth + 2 * toolTipShadowContainer.shadowThickness)
    height: toolTipRect.implicitHeight + 2 * toolTipShadowContainer.shadowThickness
    color: "transparent"
    flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoFocus

    UiShadowContainer{
        id: toolTipShadowContainer
        glowRadius: 2
        cornerRadius: 4
        UiColumnLayout {
            id: toolTipRect
            anchors.fill: parent
            color: pColor.background
            border.width: 1
            border.color: pColor.border
            padding: 4
            Text {
                Layout.fillWidth: true
                text: toolTipText
                color: pColor.text
                font: UiTheme.fonts.bodyExtraSmall
                wrapMode: Text.Wrap
            }
        }
    }
}
