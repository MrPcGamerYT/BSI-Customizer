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

Image {
    property string asset
    property string extn: ".svg"
    property double pImageWidth : 0
    property double pImageHeight : 0
    property bool pUseSourceSize: true

    width: pUseSourceSize ? undefined : pImageWidth
    height: pUseSourceSize ? undefined : pImageHeight

    sourceSize.width: pUseSourceSize ? (pImageWidth * ((typeof(uiBackend) !== "undefined") ? parseFloat(uiBackend.confGetString("bst.ui_scale_factor")) : 1.0)) : 0
    sourceSize.height: pUseSourceSize ?  (pImageHeight * ((typeof(uiBackend) !== "undefined") ? parseFloat(uiBackend.confGetString("bst.ui_scale_factor")) : 1.0)) : 0
    source: asset === "" ? "" : "qrcAssets/" + asset + extn
    fillMode: Image.PreserveAspectFit
    antialiasing: true
    cache: true
    mipmap: true
    opacity: enabled ? 1 : 0.5
    onStatusChanged: {
        if (status == Image.Ready && pUseSourceSize) {
            sourceSize.width = Qt.binding(function() { return pImageWidth })
            sourceSize.height = Qt.binding(function() { return pImageHeight })
        }
    }
}
