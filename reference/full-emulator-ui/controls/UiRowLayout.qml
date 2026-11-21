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

Rectangle {
    id: iControl

    default property alias pChildren: iRowLayout.data

    property alias pMouseArea: mouseArea

    property real leftPadding: padding
    property real topPadding: padding
    property real rightPadding: padding
    property real bottomPadding: padding
    property real padding: 0

    property real spacing: 0

    implicitWidth: iRowLayout.implicitWidth + leftPadding + rightPadding
    implicitHeight: iRowLayout.implicitHeight + topPadding + bottomPadding
    color: "transparent"

    MouseArea {
        id: mouseArea
        enabled: false
    }

    RowLayout {
        id: iRowLayout
        x: iControl.leftPadding
        y: iControl.topPadding
        width: parent.width - iControl.leftPadding - iControl.rightPadding
        height: parent.height - iControl.topPadding - iControl.bottomPadding
        anchors.leftMargin: iControl.leftPadding
        anchors.rightMargin: iControl.rightPadding
        anchors.topMargin: iControl.topPadding
        anchors.bottomMargin: iControl.bottomPadding
        spacing: iControl.spacing
    }
}
