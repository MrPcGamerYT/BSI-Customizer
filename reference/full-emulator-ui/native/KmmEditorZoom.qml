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
    property string pType: "Zoom"
    property real pRadius: 0
    property string pDirection: "None"
    property real pTheta: Math.PI/2

    property string pComposedKey: ""

    property real pResizeUnit: 20
    property  bool pGamepadTabSelected: false
    property KmmEditorTextEditBox pFocusedTextBox: null
    property bool pEditorWindowGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null

    onWidthChanged: {
        fAssignCoordinates()
    }

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var index = pGamepadTabSelected ? 1 : 0
            pKmmControl.keyIn[index] = updatedPropsMap[qsTranslate("QObject", "Zoom in")]
            pKmmControl.keyOut[index] = updatedPropsMap[qsTranslate("QObject", "Zoom out")]
            pKmmControl.keyModifier[index] = updatedPropsMap[qsTranslate("QObject", "Modifier key")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
            pKmmControl.mode = updatedPropsMap[qsTranslate("QObject", "Mode")]
            pKmmControl.resetDelay = updatedPropsMap[qsTranslate("QObject", "Reset delay")]
            pKmmControl.override = updatedPropsMap[qsTranslate("QObject", "Override")]
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
            iTextEditFieldUp.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyIn[1] : pKmmControl.keyIn[0]))
            iTextEditFieldDown.fSetText(kmmBackend.getKeyList(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyOut[1] : pKmmControl.keyOut[0])))
            fAssignCoordinates()
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
        pImgSrc: "EditorZoom"
        onDeleteClicked: Utils.deleteControl(iRoot, pKmmControl)
        onSettingsClicked: fOpenSettingsWindow()
        onItemMoved: function(startingMouse, currentMouse) {
            Utils.moveItem(pKmmControl, iRoot.x, iRoot.y, iRoot.width, iRoot.height, startingMouse, currentMouse)
            fAssignCoordinates()
        }
    }

    UiImageButton {
        id: iScaleBtn
        asset: "ScaleIcon"
        pImageWidth: 10
        pImageHeight: 10
        anchors.right: iRoot.right
        anchors.bottom: iRoot.bottom
        onFocusChanged: focus ? Qt.SizeFDiagCursor : Qt.ArrowCursor

        MouseArea {
            anchors.fill: iScaleBtn
            propagateComposedEvents: false
            cursorShape: Qt.SizeFDiagCursor

            drag {
                target: iScaleBtn
                axis: Drag.XAndYAxis
            }

            onMouseXChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseX) > (pResizeUnit * 2))
                        pRadius = ((iRoot.width + mouseX) / pResizeUnit).toFixed(2)
                }
            }
            onMouseYChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseY) > (pResizeUnit * 2))
                        pRadius = ((iRoot.width + mouseY) / pResizeUnit).toFixed(2)
                }
            }
        }
    }

    KmmEditorTextEditBox {
        id: iTextEditFieldUp
        x: iRoot.width / 2 - width / 2
        y: (iRoot.height * 0.015) - height / 2
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyIn[1] = key
            else pKmmControl.keyIn[0] = key
        }
        onPEditFieldActiveFocusChanged: {
            if(pEditFieldActiveFocus) {
                pFocusedTextBox = this
                kmmBackend.setFocusedVisualItem(iRoot)
            }
        }
    }

    KmmEditorTextEditBox {
        id: iTextEditFieldDown
        x: iRoot.width / 2 - width / 2
        y: (iRoot.height * 0.985) - height / 2
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyOut[1] = key
            else pKmmControl.keyOut[0] = key
        }
        onPEditFieldActiveFocusChanged: {
            if(pEditFieldActiveFocus) {
                pFocusedTextBox = this
                kmmBackend.setFocusedVisualItem(iRoot)
            }
        }
    }

    function fOpenSettingsWindow() {

        if (pSettingsWin !== null) {
            pSettingsWin.destroy()
        }

        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "Zoom settings"),
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
        var index = pGamepadTabSelected ? 1 : 0
        var keyBindingsMap = {
            "Zoom in": {"keyName": qsTranslate("QObject", "Zoom in"), "keyValue": pKmmControl.keyIn[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Zoom out": {"keyName": qsTranslate("QObject", "Zoom out"), "keyValue": pKmmControl.keyOut[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Modifier key": {"keyName": qsTranslate("QObject", "Modifier key"), "keyValue": pKmmControl.keyModifier[index], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Speed": {"keyName": qsTranslate("QObject", "Speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Mode": {"keyName": qsTranslate("QObject", "Mode"), "keyValue": pKmmControl.mode, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Reset delay": {"keyName": qsTranslate("QObject", "Reset delay"), "keyValue": pKmmControl.resetDelay, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Override": {"keyName": qsTranslate("QObject", "Override"), "keyValue": pKmmControl.override, "keyType": KmmEditorControlSettings.InputType.Boolean}
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
            var val = pKmmControl == null ? 0: pKmmControl.x
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.y
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })
        fCheckDirection()
        if(pKmmControl.x1 === -1 && pKmmControl.x2 === -1) {
            pKmmControl.x = pRadius
        }
        else
            pKmmControl.x = (pKmmControl.x1 + pKmmControl.x2) /2


        if(pKmmControl.y1 === -1 && pKmmControl.y2 === -1) {
            pKmmControl.y = pRadius
        }
        else {
            pKmmControl.y = (pKmmControl.y1 + pKmmControl.y2) / 2
        }

        fAssignCoordinates()

        iTextEditFieldUp.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyIn[1] : pKmmControl.keyIn[0]))
        iTextEditFieldDown.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyOut[1] : pKmmControl.keyOut[0]))

        iRoot.width = Qt.binding(function() {return (pRadius * pResizeUnit)})

        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCheckDirection() {
        if (pKmmControl.x1 === pKmmControl.x2)
        {
            pDirection = "Up"
        }
        else if (pKmmControl.y1 === pKmmControl.y2)
        {
            pDirection = "Right"
        }
        pRadius = (Math.sqrt((pKmmControl.x1-pKmmControl.x2)*(pKmmControl.x1-pKmmControl.x2) +
                             (pKmmControl.y1-pKmmControl.y2)*(pKmmControl.y1-pKmmControl.y2)) / 2).toFixed(2);
        if(pRadius === 0)
            pRadius = 20
    }

    function fAssignCoordinates() {
        pTheta = Math.atan((pKmmControl.y2-pKmmControl.y1) / (pKmmControl.x2-pKmmControl.x1))
        if(pKmmControl.x1 === -1 && pKmmControl.x2 === -1 && pKmmControl.y1 === -1 && pKmmControl.y2 === -1)
        {
            pTheta = Math.PI/2
        }

        if (!isNaN(pTheta)) {
            if(pDirection === "Right")
            {
                pKmmControl.x1 = (parseFloat(pKmmControl.x) - pRadius * Math.cos(pTheta)).toFixed(2)
                if(pKmmControl.x1 < 0) {
                    pKmmControl.x1 = 0
                    pKmmControl.x = (pRadius * Math.cos(pTheta)).toFixed(2)
                }
                pKmmControl.x2 = (parseFloat(pKmmControl.x) + pRadius * Math.cos(pTheta)).toFixed(2)
                pKmmControl.y1 = parseFloat(pKmmControl.y).toFixed(2)
                pKmmControl.y2 = pKmmControl.y1
            }
            else if(pDirection === "Up")
            {
                pKmmControl.y1 = (parseFloat(pKmmControl.y) - pRadius * Math.sin(pTheta)).toFixed(2)
                if(pKmmControl.y1 < 0) {
                    pKmmControl.y1 = 0
                    pKmmControl.y = (pRadius * Math.sin(pTheta)).toFixed(2)
                }
                pKmmControl.y2 = (parseFloat(pKmmControl.y) + pRadius * Math.sin(pTheta)).toFixed(2)
                pKmmControl.x1 = parseFloat(pKmmControl.x).toFixed(2)
                pKmmControl.x2 = pKmmControl.x1
            }
            else
            {
                pKmmControl.x1 = (parseFloat(pKmmControl.x) - pRadius * Math.cos(pTheta)).toFixed(2)
                if(pKmmControl.x1 < 0) {
                    pKmmControl.x1 = 0
                    pKmmControl.x = (pRadius * Math.cos(pTheta)).toFixed(2)
                }
                pKmmControl.x2 = (parseFloat(pKmmControl.x) + pRadius * Math.cos(pTheta)).toFixed(2)
                pKmmControl.y1 = (parseFloat(pKmmControl.y) - pRadius * Math.sin(pTheta)).toFixed(2)
                if(pKmmControl.y1 < 0) {
                    pKmmControl.y1 = 0
                    pKmmControl.y = (pRadius * Math.sin(pTheta)).toFixed(2)
                }
                pKmmControl.y2 = (parseFloat(pKmmControl.y) + pRadius * Math.sin(pTheta)).toFixed(2)
            }
        }

    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
        fLoadControlProp(pEditorWindowGamepadTabSelected)
        pRadius = 20
    }

    function fInEditableMode() {
        iTextEditFieldUp.fInEditableMode()
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !pFocusedTextBox.pEditFieldActiveFocus)
            return

        pFocusedTextBox.updateKey(key);
        pFocusedTextBox.fSetText(kmmBackend.getKeyList(key))
    }
}
