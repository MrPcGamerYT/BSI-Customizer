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
import UiToolTipControl 1.0
import UiShortcutToolTip 1.0

Button {
    id: iControl

    property string asset
    property string extn: ".svg"
    property var pMouseCursor
    property bool pImageAnimationRunning: false
    property int pImageWidth : 0
    property int pImageHeight : 0
    property bool pDisableClickAndHoveredState: false
    property bool pUseShortcutTooltip: false
    property bool pUseImgSourceSize: true

    horizontalPadding: 0
    verticalPadding: 0
    spacing: 0
    opacity: enabled ? 1 : 0.4

    display: AbstractButton.IconOnly

    background: Rectangle {
        color: "transparent"
    }

    onPImageAnimationRunningChanged: {
        if (!pImageAnimationRunning)
            iControl.contentItem.rotation = 0
    }

    onHoveredChanged: {
        if (iControl.pUseShortcutTooltip)
            UiShortcutToolTip.visible = iControl.hovered
        else
            UiToolTip.visible = iControl.hovered
    }

    contentItem: UiImage {
        anchors.centerIn: parent
        asset: iControl.pImageAnimationRunning ? "SidebarLoader_white"
                                               : iControl.asset + ((iControl.down && !pDisableClickAndHoveredState) ? "_click"
                                               : ((iControl.hovered && !pDisableClickAndHoveredState) ? "_hover" : ""))
        extn: iControl.extn
        pImageWidth: iControl.pImageWidth
        pImageHeight: iControl.pImageHeight
        pUseSourceSize: pUseImgSourceSize

        RotationAnimation on rotation {
            loops: Animation.Infinite
            duration: 1000
            from: 0
            to: 360
            direction: RotationAnimation.Clockwise
            running: iControl.pImageAnimationRunning
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: iControl.pMouseCursor
        onPressed: function(mouse) {
            UiToolTip.visible = false
            mouse.accepted = false
        }
    }
}
