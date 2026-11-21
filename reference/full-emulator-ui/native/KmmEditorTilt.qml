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
    property var pResizingMouse
    property bool pLoaded: false
    property string pType: "Tilt"

    property string pComposedKey: ""

    property real pResizeUnit: 20
    property  bool pGamepadTabSelected: false
    property KmmEditorTextEditBox pFocusedTextBox: null
    property bool pEditorWindowGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var index = pGamepadTabSelected ? 1 : 0
            pKmmControl.keyUp[index] = updatedPropsMap[qsTranslate("QObject", "Up")]
            pKmmControl.keyDown[index] = updatedPropsMap[qsTranslate("QObject", "Down")]
            pKmmControl.keyLeft[index] = updatedPropsMap[qsTranslate("QObject", "Left")]
            pKmmControl.keyRight[index] = updatedPropsMap[qsTranslate("QObject", "Right")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Tilt X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Tilt Y")])
            pKmmControl.radius = updatedPropsMap[qsTranslate("QObject", "Tilt radius")]
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
            pKmmControl.maxAngle = updatedPropsMap[qsTranslate("QObject", "Max tilt angle")]
            pKmmControl.autoReset = updatedPropsMap[qsTranslate("QObject", "Auto reset")]
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

            iTextEditFieldUp.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyUp[1] : pKmmControl.keyUp[0]))
            iTextEditFieldDown.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyDown[1] : pKmmControl.keyDown[0]))
            iTextEditFieldLeft.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyLeft[1] : pKmmControl.keyLeft[0]))
            iTextEditFieldRight.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyRight[1] : pKmmControl.keyRight[0]))
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
        asset: "EditorTilt"
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

            drag {
                target: iScaleBtn
                axis: Drag.XAndYAxis
            }
            cursorShape: Qt.SizeFDiagCursor

            onMouseXChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseX) > (pResizeUnit * 4))
                        pKmmControl.radius = ((iRoot.width + mouseX) / pResizeUnit).toFixed(2)
                }
            }
            onMouseYChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseY) > (pResizeUnit * 4))
                        pKmmControl.radius = ((iRoot.width + mouseY) / pResizeUnit).toFixed(2)
                }
            }
        }
    }

    KmmEditorTextEditBox {
        id: iTextEditFieldUp
        x: iRoot.width / 2 - width / 2
        y: (iRoot.height * 0.01) - height / 2
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyUp[1] = key
            else pKmmControl.keyUp[0] = key
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
        y: (iRoot.height * 0.99) - height / 2
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyDown[1] = key
            else pKmmControl.keyDown[0] = key
        }
        onPEditFieldActiveFocusChanged: {
            if(pEditFieldActiveFocus) {
                pFocusedTextBox = this
                kmmBackend.setFocusedVisualItem(iRoot)
            }
        }
    }

    KmmEditorTextEditBox {
        id: iTextEditFieldLeft
        x: (iRoot.width * 0.01) - width / 2
        y: iRoot.height / 2 - height / 2
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyLeft[1] = key
            else pKmmControl.keyLeft[0] = key
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
        x: (iRoot.width * 0.99) - width / 2
        y: iRoot.height / 2 - height / 2
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyRight[1] = key
            else pKmmControl.keyRight[0] = key
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
                                             {"pTitle": qsTranslate("QObject", "Tilt settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOvarlay,
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
            "Up": {"keyName": qsTranslate("QObject", "Up"), "keyValue": pKmmControl.keyUp[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Down": {"keyName": qsTranslate("QObject", "Down"), "keyValue": pKmmControl.keyDown[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Left": {"keyName": qsTranslate("QObject", "Left"), "keyValue": pKmmControl.keyLeft[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Right": {"keyName": qsTranslate("QObject", "Right"), "keyValue": pKmmControl.keyRight[index], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "Tilt X": {"keyName": qsTranslate("QObject", "Tilt X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Tilt Y": {"keyName": qsTranslate("QObject", "Tilt Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Tilt radius": {"keyName": qsTranslate("QObject", "Tilt radius"), "keyValue": pKmmControl.radius, "keyType": KmmEditorControlSettings.InputType.Double},
            "Speed": {"keyName": qsTranslate("QObject", "Speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Max tilt angle": {"keyName": qsTranslate("QObject", "Max tilt angle"), "keyValue": pKmmControl.maxAngle, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Auto reset": {"keyName": qsTranslate("QObject", "Auto reset"), "keyValue": pKmmControl.autoReset, "keyType": KmmEditorControlSettings.InputType.Boolean}
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

        iTextEditFieldUp.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyUp[1] : pKmmControl.keyUp[0]))
        iTextEditFieldDown.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyDown[1] : pKmmControl.keyDown[0]))
        iTextEditFieldLeft.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyLeft[1] : pKmmControl.keyLeft[0]))
        iTextEditFieldRight.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyRight[1] : pKmmControl.keyRight[0]))

        if (plrBackend.glWinRect[2] < plrBackend.glWinRect[3])
            pResizeUnit = pResizeUnit * 9.0 / 16

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
        iTextEditFieldUp.fInEditableMode()
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !pFocusedTextBox.pEditFieldActiveFocus)
            return

        pFocusedTextBox.updateKey(key);
        pFocusedTextBox.fSetText(kmmBackend.getKeyList(key))
    }
}
