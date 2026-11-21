/*
 * Copyright (C) 2020-2021 BlueStack Systems, Inc.
 * All Rights Reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF BLUESTACK SYSTEMS, INC.
 * The copyright notice above does not evidence any actual or intended
 * publication of such source code.
 */

import QtQuick 2.15

Item {
    id: root
    anchors.fill: parent

    default property alias children: containerContentRect.data

    property alias glowRadius: shadowEffectContainer.glowRadius
    property alias cornerRadius: shadowEffectContainer.cornerRadius
    property alias shadowVisibility: shadowEffectContainer.visible
    readonly property real shadowThickness: shadowVisibility ? glowRadius + cornerRadius : 0

    UiDropShadowEffect {
        id: shadowEffectContainer
        visible: true
        anchors.fill: containerContentRect
        glowRadius: 5
        cornerRadius: 10
    }

    Rectangle {
        id: containerContentRect
        anchors.fill: parent
        anchors.margins: shadowThickness
        color: "transparent"
    }
}
