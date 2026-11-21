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
import QtQuick.Controls 2.15

import UiToolTipControl 1.0

import "KmmUtils.js" as Utils

Item {
    id: iRoot
    width: 36
    height: 36
    z: 1

    property QtObject pKmmControl: null
    property var pStartingMouse
    property bool pLoaded: false
    property string pComposedKey: ""
    property string pType: "SendOriginalKeys"
    property bool pGamepadTabSelected: false
    property bool pEditorWindowGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            pKmmControl.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
            pKmmControl.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
            pKmmControl.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
            pKmmControl.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
            pKmmControl.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
            pKmmControl.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
            pKmmControl.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
            pKmmControl.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
            pKmmControl.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]
        }

        function onDeleteControl() {
            Utils.deleteControl(iRoot, pKmmControl)
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }
    }

    KmmEditorBaseImageRect {
        pImgSrc: "EditorSendOriginalKeys"
        height: 36
        width: 36
        onDeleteClicked: Utils.deleteControl(iRoot, pKmmControl)
        onSettingsClicked: fOpenSettingsWindow()
        onItemMoved: function(startingMouse, currentMouse) {
            var newX = iRoot.x + currentMouse.x - startingMouse.x
            var newY = iRoot.y + currentMouse.y - startingMouse.y
            var centerX = newX + 36 / 2
            var centerY = newY + 36 / 2
            if (centerX > plrBackend.glWinRect[0] && centerX < (plrBackend.glWinRect[2] + plrBackend.glWinRect[0])) {

                var val = (((centerX - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
                iRoot.x = plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
            }
            if (centerY > plrBackend.glWinRect[1] && centerY < (plrBackend.glWinRect[3] + plrBackend.glWinRect[1])) {

                var val1 = (((centerY - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
                iRoot.y = plrBackend.glWinRect[1] + (val1 * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
            }
        }
    }

    function fOpenSettingsWindow() {
        if (pSettingsWin != null) {
            pSettingsWin.destroy()
        }
        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "Send original keys settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOverlay,
                                                 "pEnableCondition": pKmmControl.enableCondition,
                                                 "pStartCondition": pKmmControl.startCondition})
            if (pSettingsWin != null) {
                pGamepadTabSelected = pEditorWindowGamepadTabSelected;
                fLoadKeyPropertiesMap()
                pSettingsWin.x = iRoot.mapToGlobal(0, 0).x
                pSettingsWin.y = iRoot.mapToGlobal(0, 0).y + iRoot.height
                pSettingsWin.show()
            }
        }
        kmmBackend.setFocusedVisualItem(null)
    }

    function fLoadKeyPropertiesMap() {
        var keyBindingsMap = {}
        var propertiesMap = {
           // "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
           // "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double}
        }
        var devPropertiesMap = {
            "Tweaks": {"keyName": qsTranslate("QObject", "Tweaks"), "keyValue": pKmmControl.tweaks, "keyType": KmmEditorControlSettings.InputType.Integer},
            "XExpr": {"keyName": qsTranslate("QObject", "XExpr"), "keyValue": pKmmControl.xExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "YExpr": {"keyName": qsTranslate("QObject", "YExpr"), "keyValue": pKmmControl.yExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "XOverlayOffset": {"keyName": qsTranslate("QObject", "XOverlayOffset"), "keyValue": pKmmControl.xOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "YOverlayOffset": {"keyName": qsTranslate("QObject", "YOverlayOffset"), "keyValue": pKmmControl.yOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "Exclusive": {"keyName": qsTranslate("QObject", "Exclusive"), "keyValue": pKmmControl.exclusive, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Exclusive delay": {"keyName": qsTranslate("QObject", "Exclusive delay"), "keyValue": pKmmControl.exclusiveDelay, "keyType": KmmEditorControlSettings.InputType.Integer}
        }
        pSettingsWin.fLoadModel(keyBindingsMap, propertiesMap, {}, {}, devPropertiesMap)
    }

    function fLoadControlProp(gamepadTabSelected) {
        iRoot.x = Qt.binding(function() {
            var val = pKmmControl == null ? 0 : 5//pKmmControl.x
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: 5 // pKmmControl.y
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })
        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode() {}

    function fAssignGamepadKey(key)
    {
        if(!pEditorWindowGamepadTabSelected || !pActiveFocus)
            return
    }
}
