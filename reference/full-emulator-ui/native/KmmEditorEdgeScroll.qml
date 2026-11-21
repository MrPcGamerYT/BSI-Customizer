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
    height: width
    z: 1

    property QtObject pKmmControl: null
    property var pStartingMouse
    property bool pLoaded: false
    property string pType: "EdgeScroll"
    property bool pGamepadTabSelected: false
    property bool pEditorWindowGamepadTabSelected: false

    property real pResizeUnit: 24

    property KmmEditorControlSettings pSettingsWin: null

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.xVelocity = updatedPropsMap[qsTranslate("QObject", "X velocity")]
            pKmmControl.yVelocity = updatedPropsMap[qsTranslate("QObject", "Y velocity")]
            pKmmControl.xActiveMargin = updatedPropsMap[qsTranslate("QObject", "X active margin")]
            pKmmControl.yActiveMargin = updatedPropsMap[qsTranslate("QObject", "Y active margin")]
            pKmmControl.speedUpFactor = updatedPropsMap[qsTranslate("QObject", "Speed up factor")]
            pKmmControl.resetDelay = updatedPropsMap[qsTranslate("QObject", "Reset delay")]
            pKmmControl.speedUpWaitTime = updatedPropsMap[qsTranslate("QObject", "Speed up wait time")]
            pKmmControl.edgeScrollEnabled = updatedPropsMap[qsTranslate("QObject", "Enabled")]
            pKmmControl.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
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
        pImgSrc: "EditorEdgeScroll"
        onDeleteClicked: Utils.deleteControl(iRoot, pKmmControl)
        onSettingsClicked: fOpenSettingsWindow()
        onItemMoved: function(startingMouse, currentMouse) {
            Utils.moveItem(pKmmControl, iRoot.x, iRoot.y, iRoot.width, iRoot.height, startingMouse, currentMouse)
        }
    }

    function fLoadControlProp(gamepadTabSelected) {
        iRoot.x = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.x
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.y
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })

        iRoot.width = Qt.binding(function() {return (4 * pResizeUnit)})

        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode() {
        //        iTextField.visible = false
        //        iEditField.forceActiveFocus()
    }

    function fOpenSettingsWindow() {
        if (pSettingsWin !== null) {
            pSettingsWin.destroy()
        }

        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "Edge scroll settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOverlay,
                                                 "pShowOverlaySetting": false,
                                                 "pEnableCondition": pKmmControl.enableCondition,
                                                 "pStartCondition": pKmmControl.startCondition})
            if (pSettingsWin !== null) {
                pGamepadTabSelected = pEditorWindowGamepadTabSelected
                fLoadKeyPropertiesMap()
                pSettingsWin.x = iRoot.mapToGlobal(0, 0).x
                pSettingsWin.y = iRoot.mapToGlobal(0, 0).y + iRoot.height
                pSettingsWin.show()
            }
        }
        kmmBackend.setFocusedVisualItem(null)
    }
    function fLoadKeyPropertiesMap() {
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "X velocity": {"keyName": qsTranslate("QObject", "X velocity"), "keyValue": pKmmControl.xVelocity, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y velocity": {"keyName": qsTranslate("QObject", "Y velocity"), "keyValue": pKmmControl.yVelocity, "keyType": KmmEditorControlSettings.InputType.Double},
            "X active margin": {"keyName": qsTranslate("QObject", "X active margin"), "keyValue": pKmmControl.xActiveMargin, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y active margin": {"keyName": qsTranslate("QObject", "Y active margin"), "keyValue": pKmmControl.yActiveMargin, "keyType": KmmEditorControlSettings.InputType.Double},
            "Speed up factor": {"keyName": qsTranslate("QObject", "Speed up factor"), "keyValue": pKmmControl.speedUpFactor, "keyType": KmmEditorControlSettings.InputType.Double},
            "Reset delay": {"keyName": qsTranslate("QObject", "Reset delay"), "keyValue": pKmmControl.resetDelay, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Speed up wait time": {"keyName": qsTranslate("QObject", "Speed up wait time"), "keyValue": pKmmControl.speedUpWaitTime, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Enabled": {"keyName": qsTranslate("QObject", "Enabled"), "keyValue": pKmmControl.edgeScrollEnabled, "keyType": KmmEditorControlSettings.InputType.Boolean}
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

        pSettingsWin.fLoadModel({}, propertiesMap, {}, {}, devPropertiesMap)
    }

    function fAssignGamepadKey(key)
    {
        if(!pEditorWindowGamepadTabSelected)
            return
    }
}
