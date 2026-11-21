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
import QtQuick.Window 2.15
import QtQuick.Shapes 1.12

import UiToolTipControl 1.0

Text {
    property alias pMouseArea: iMouseArea

    id: iControl
    elide: Text.ElideRight
    UiToolTip.text: text
    UiToolTip.visible: iMouseArea.containsMouse && truncated

    MouseArea {
        id: iMouseArea
        anchors.fill: parent
        hoverEnabled: true
    }
}
