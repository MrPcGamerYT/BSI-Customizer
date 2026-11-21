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
import UiToolTipControl 1.0
import UiShortcutToolTip 1.0
import QtQuick.Templates 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.impl 2.12

Switch  {
    id: iControl
    property QtObject pSize: UiTheme.toggleButtons.sizes.medium
    property var pMouseCursor
    property bool pUseShortcutTooltip: false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    Component.onCompleted: {
        if (iControl.pUseShortcutTooltip)
            UiShortcutToolTip.visible = Qt.binding(function() { return (iIndicatorMouseArea.containsMouse && !iIndicatorMouseArea.pressed) })
        else
            UiToolTip.visible = Qt.binding(function() { return (iIndicatorMouseArea.containsMouse && !iIndicatorMouseArea.pressed) })
    }

    padding: 0
    spacing: pSize.spacing
    opacity: enabled ? 1 : 0.4
    indicator: UiImage {
        x: iControl.text ? iControl.leftPadding : iControl.leftPadding + (iControl.availableWidth - width) / 2
        y: iControl.topPadding + (iControl.availableHeight - height) / 2
        pImageWidth: iControl.pSize.iconWidth
        pImageHeight: iControl.pSize.iconHeight
        asset: iControl.pSize.iconAsset + (iControl.checked ? "On" : "Off") + (iControl.hovered? "_hover" : "")
        MouseArea {
            id: iIndicatorMouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: iControl.pMouseCursor
            onPressed: function(mouse) {
                mouse.accepted = false
            }
        }
    }

    contentItem: CheckLabel {
        text: iControl.text
        leftPadding: iControl.indicator.width + iControl.spacing
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
        font: iControl.pSize.textFont
        color: UiTheme.colors.primary20
        elide: Text.ElideRight
    }
}
