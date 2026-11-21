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

Window {
    property bool closeOnPressOutside: true
    property int contentWidth: 200
    property int contentHeight: 200

    property alias shadowVisibility: shadowContainer.shadowVisibility
    readonly property alias shadowThickness: shadowContainer.shadowThickness

    default property alias children: shadowContainer.children

    signal closePopup()

    id: rootWindow
    width: contentWidth + 2 * shadowThickness
    height: contentHeight + 2 * shadowThickness
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    screen: transientParent.screen

    UiShadowContainer {
        id: shadowContainer
    }

    onClosing: {
        rootWindow.destroy()
        rootWindow.closePopup()
    }

    onActiveChanged: {
        if (closeOnPressOutside && rootWindow.activeFocusItem == null)
            rootWindow.close()
    }
}
