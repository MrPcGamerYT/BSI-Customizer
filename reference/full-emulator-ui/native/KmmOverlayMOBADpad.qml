/*
 * Copyright (C) 2020-2021 BlueStack Systems, Inc.
 * All Rights Reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF BLUESTACK SYSTEMS, INC.
 * The copyright notice above does not evidence any actual or intended
 * publication of such source code.
 */

import QtQuick 2.15

Image {
    property real controlOpacity: 1.0
    property real pScreenWidth: 1920
    property real pScalingFactor: 1.0
    width: (65 * pScreenWidth) / (pScalingFactor * 1920)
    height: (65 * pScreenWidth) / (pScalingFactor * 1920)
    source: "qrcAssets/Imap_MOBADpad.png"
    opacity: controlOpacity
}
