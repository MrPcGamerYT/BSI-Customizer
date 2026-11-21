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
    property string pType: "Rotate"

    property string pComposedKey: ""
    property bool pEditorWindowGamepadTabSelected: false
    property bool pGamepadTabSelected: false
    property KmmEditorTextEditBox pFocusedTextBox: null

    property real pResizeUnit: (26 * plrBackend.glWinRect[2]) / 1280

    property KmmEditorControlSettings pSettingsWin: null

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var index = pGamepadTabSelected ? 1 : 0
            pKmmControl.keyClock[index] = updatedPropsMap[qsTranslate("QObject", "Clockwise")]
            pKmmControl.keyAntiClock[index] = updatedPropsMap[qsTranslate("QObject", "Anticlockwise")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Origin X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Origin Y")])
            pKmmControl.radius = updatedPropsMap[qsTranslate("QObject", "XRadius")]
            pKmmControl.startingAngle = updatedPropsMap[qsTranslate("QObject", "Starting angle")]
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
            pKmmControl.activationTime = updatedPropsMap[qsTranslate("QObject", "Activation time")]
            pKmmControl.tweaks = updatedPropsMap[qsTranslate("QObject", "Rotation tweaks")]
            pKmmControl.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
            pKmmControl.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
            pKmmControl.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
            pKmmControl.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
            pKmmControl.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
            pKmmControl.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
            pKmmControl.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
            pKmmControl.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
            pKmmControl.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]

            iTextEditFieldLeft.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyAntiClock[1] : pKmmControl.keyAntiClock[0]))
            iTextEditFieldRight.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyClock[1] : pKmmControl.keyClock[0]))
        }

        function onDeleteControl() {
            Utils.deleteControl(iRoot, pKmmControl)
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }
    }

    UiImageClickable {
        id: iBaseImg
        asset: "EditorRotate"
        pImageWidth: iRoot.width
        pImageHeight: iRoot.width
        anchors.centerIn: parent

        onPressed: function(mouse) {
            if(mouse.button & Qt.RightButton) {
                fOpenSettingsWindow()
                return
            }
            pStartingMouse = { x: mouse.x, y: mouse.y}
        }
        onPositionChanged: function(mouse) {
            if(pStartingMouse !== undefined)
                Utils.moveItem(pKmmControl, iRoot.x, iRoot.y, iRoot.width, iRoot.height, pStartingMouse, mouse)
        }
        onReleased: pStartingMouse = undefined
    }

    Rectangle {
        id: iBtnRect
        anchors.fill: parent
        color: "#00000000"

        MouseArea {
            id: iBtnMouseArea
            anchors.fill: iBtnRect
            hoverEnabled: true
            propagateComposedEvents: true
            onPressed: mouse.accepted = false
            cursorShape: (containsMouse && uiBackend.isTransparentClickAccepted(iBaseImg.source, iBaseImg.width, iBaseImg.height, mouseX, mouseY)) ?
                             iBaseImg.pControlPressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor :
                             Qt.ArrowCursor
            onEntered: {
                if (pSettingsWin === null) {
                    iDeleteBtn.visible = true
                    iSettingsBtn.visible = true
                }
            }
            onExited: {
                iDeleteBtn.visible = false
                iSettingsBtn.visible = false
            }

            UiImageButton {
                id: iDeleteBtn
                asset: "VerySmallClose"
                pImageWidth: 10
                pImageHeight: 10
                anchors.top: iBtnMouseArea.top
                anchors.right: iBtnMouseArea.right
                onClicked: Utils.deleteControl(iRoot, pKmmControl)
                visible: false
            }

            UiImageButton {
                id: iSettingsBtn
                asset: "VerySmallSettings"
                pImageWidth: 10
                pImageHeight: 10
                anchors.left: iBtnMouseArea.left
                anchors.bottom: iBtnMouseArea.bottom
                visible: false
                UiToolTip.text: qsTranslate("QObject", "Settings")
                onClicked: fOpenSettingsWindow()
            }
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
                        pKmmControl.radius = ((iRoot.width + mouseX) / pResizeUnit).toFixed(2)
                }
            }
            onMouseYChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseY) > (pResizeUnit * 2))
                        pKmmControl.radius = ((iRoot.width + mouseY) / pResizeUnit).toFixed(2)
                }
            }
        }
    }

    KmmEditorTextEditBox {
        id: iTextEditFieldLeft
        x: (iRoot.width * 0.13) - width / 2
        y: iRoot.height / 2 - height / 2
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyAntiClock[1] = key
            else pKmmControl.keyAntiClock[0] = key
        }
        onPEditFieldActiveFocusChanged: {
            if(pEditFieldActiveFocus) {
                pFocusedTextBox = this
                kmmBackend.setFocusedVisualItem(iRoot)
            }
        }
    }

    KmmEditorTextEditBox {
        id: iTextEditFieldRight
        x: (iRoot.width * 0.87) - width / 2
        y: iRoot.height / 2 - height / 2
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyClock[1] = key
            else pKmmControl.keyClock[0] = key
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
                                             {"pTitle": qsTranslate("QObject", "Rotate settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOverlay,
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
        var keyBindingsMap = {
            "Clockwise": {"keyName": qsTranslate("QObject", "Clockwise"), "keyValue": pGamepadTabSelected ? pKmmControl.keyClock[1] : pKmmControl.keyClock[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "Anticlockwise": {"keyName": qsTranslate("QObject", "Anticlockwise"), "keyValue": pGamepadTabSelected ? pKmmControl.keyAntiClock[1] : pKmmControl.keyAntiClock[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "Origin X": {"keyName": qsTranslate("QObject", "Origin X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Origin Y": {"keyName": qsTranslate("QObject", "Origin Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "XRadius": {"keyName": qsTranslate("QObject", "XRadius"), "keyValue": pKmmControl.radius, "keyType": KmmEditorControlSettings.InputType.Double},
            "Starting angle": {"keyName": qsTranslate("QObject", "Starting angle"), "keyValue": pKmmControl.startingAngle, "keyType": KmmEditorControlSettings.InputType.Double},
            "Speed": {"keyName": qsTranslate("QObject", "Speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Activation time": {"keyName": qsTranslate("QObject", "Activation time"), "keyValue": pKmmControl.activationTime, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Rotation tweaks": {"keyName": qsTranslate("QObject", "Rotation tweaks"), "keyValue": pKmmControl.tweaks, "keyType": KmmEditorControlSettings.InputType.Integer}
        }
        var devPropertiesMap = {
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

        iTextEditFieldLeft.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyAntiClock[1] : pKmmControl.keyAntiClock[0]))
        iTextEditFieldRight.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyClock[1] : pKmmControl.keyClock[0]))

        iRoot.width = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.radius
            return (val * pResizeUnit)
        })

        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode() {
        iTextEditFieldLeft.fInEditableMode()
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !pFocusedTextBox.pEditFieldActiveFocus)
            return

        pFocusedTextBox.updateKey(key);
        pFocusedTextBox.fSetText(kmmBackend.getKeyList(key))
    }
}
