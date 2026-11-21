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
    property string pType: "Dpad"

    property real pResizeUnit: (26 * plrBackend.glWinRect[2]) / 1280

    property string pComposedKey: ""
    property bool pGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null
    property KmmEditorTextEditBox pFocusedTextBox: null
    property bool pEditorWindowGamepadTabSelected: false
    property real pDpadType: 0

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var idx = pGamepadTabSelected ? 1 : 0
            pKmmControl.keyUp[idx] = updatedPropsMap[qsTranslate("QObject", "Up")]
            pKmmControl.keyDown[idx] = updatedPropsMap[qsTranslate("QObject", "Down")]
            pKmmControl.keyLeft[idx] = updatedPropsMap[qsTranslate("QObject", "Left")]
            pKmmControl.keyRight[idx] = updatedPropsMap[qsTranslate("QObject", "Right")]
            pKmmControl.keySpeedModifier1[idx] = updatedPropsMap[qsTranslate("QObject", "Speed Modifier 1")]
            pKmmControl.keySpeedModifier2[idx] = updatedPropsMap[qsTranslate("QObject", "Speed Modifier 2")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.radius = updatedPropsMap[qsTranslate("QObject", "Radius")]
            pKmmControl.radius1 = updatedPropsMap[qsTranslate("QObject", "Radius 1")]
            pKmmControl.radius2 = updatedPropsMap[qsTranslate("QObject", "Radius 2")]
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
            pKmmControl.activationTime = updatedPropsMap[qsTranslate("QObject", "Activation time")]
            pKmmControl.activationSpeed = updatedPropsMap[qsTranslate("QObject", "Activation speed")]
            pKmmControl.deadzoneRadius = updatedPropsMap[qsTranslate("QObject", "Dead zone radius")]
            pKmmControl.dpadExpiryTime = updatedPropsMap[qsTranslate("QObject", "Dpad expiry time")]
            pKmmControl.dpadTitle = updatedPropsMap[qsTranslate("QObject", "Dpad title")]
            pKmmControl.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
            pKmmControl.gamepadStick = updatedPropsMap[qsTranslate("QObject", "Gamepad stick")]
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
            iTextEditFieldCenter.fSetText(pKmmControl.gamepadStick === "LeftStick" ? qsTranslate("QObject", "LS") : qsTranslate("QObject", "RS"))
        }

        function onDeleteControl() {
            Utils.deleteControl(iRoot, pKmmControl)
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }
    }

    ButtonGroup {
        id: iDpadBtnGrp
    }

    KmmEditorBaseImageRect {
        id: iBaseImg
        pImgSrc: pDpadType === 0 ? "EditorD-Pad" : "LeftStick"
        onDeleteClicked: Utils.deleteControl(iRoot, pKmmControl)
        onSettingsClicked: {
            if(pEditorWindowGamepadTabSelected)
                fOpenDpadSettingsPopup()
            else
                fOpenSettingsWindow()
        }
        onItemMoved: function(startingMouse, currentMouse) {
            Utils.moveItem(pKmmControl, iRoot.x, iRoot.y, iRoot.width, iRoot.height, startingMouse, currentMouse)
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
        id: iTextEditFieldUp
        x: iRoot.width / 2 - width / 2
        y: iRoot.height / 4 - height / 2
        pEnableMouseArea: true
        visible: pDpadType === 0 ? true : false
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
        y: (iRoot.height * 3) / 4 - height / 2
        pEnableMouseArea: true
        visible: pDpadType === 0 ? true : false
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
        x: iRoot.width / 4 - width / 2
        y: iRoot.height / 2 - height / 2
        pEnableMouseArea: true
        visible: pDpadType === 0 ? true : false
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
        x: (iRoot.width * 3) / 4 - width / 2
        y: iRoot.height / 2 - height / 2
        pEnableMouseArea: true
        visible: pDpadType === 0 ? true : false
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

    KmmEditorTextEditBox {
        id: iTextEditFieldCenter
        visible: pDpadType === 0 ? false : true
        anchors.centerIn: parent
        pEnableMouseArea: true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected)
                pKmmControl.gamepadStick = key
        }
        onPEditFieldActiveFocusChanged: {
            if(pEditFieldActiveFocus) {
                pFocusedTextBox = this
                kmmBackend.setFocusedVisualItem(iRoot)
            }
        }
    }

    Component {
        id: iDpadPopupComponent
        UiHelpPopup {
            id: iDpadPopup
            contentWidth: 150
            visible: false
            x: iBaseImg.mapToGlobal(0, 0).x - iDpadPopup.width / 2 + 5
            y: iBaseImg.mapToGlobal(0, 0).y + iBaseImg.height - iDpadPopup.height - 5
            onWidthChanged: {
                iDpadPopup.x = iBaseImg.mapToGlobal(0, 0).x - iDpadPopup.width / 2 + 5
            }

            pInnerLayout {
                border.width: 1
                border.color: UiTheme.colors.primary60
                color: UiTheme.colors.primary70
                padding: 8
                spacing: 8
            }

            Text {
                text: qsTranslate("QObject", "Control using")
                font: UiTheme.fonts.titleExtraSmall
                color: UiTheme.colors.primary10
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            UiColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                UiRadioButton {
                    id: iDpadBtn
                    text: qsTranslate("QObject", "D-PAD")
                    ButtonGroup.group: iDpadBtnGrp
                    Layout.fillWidth: true
                    onCheckedChanged: {
                        if(checked) {
                            pKmmControl.gamepadStick = ""
                            pDpadType = 0
                        }
                    }
                }
                UiRadioButton {
                    id: iLeftStickBtn
                    text: qsTranslate("QObject", "Left stick")
                    Layout.fillWidth: true
                    ButtonGroup.group: iDpadBtnGrp
                    onCheckedChanged: {
                        if(checked) {
                            pKmmControl.gamepadStick = "LeftStick"
                            iTextEditFieldCenter.fSetText(qsTranslate("QObject", "LS"))
                            pDpadType = 1
                        }
                    }
                }
                UiRadioButton {
                    id: iRightStickBtn
                    text: qsTranslate("QObject", "Right stick")
                    Layout.fillWidth: true
                    ButtonGroup.group: iDpadBtnGrp
                    onCheckedChanged: {
                        if(checked) {
                            pKmmControl.gamepadStick = "RightStick"
                            iTextEditFieldCenter.fSetText(qsTranslate("QObject", "RS"))
                            pDpadType = 1
                        }
                    }
                }
            }

            Text {
                id: iMoreSettingsTxt
                text: qsTranslate("QObject", "More settings")
                font: UiTheme.fonts.bodySmall
                color: UiTheme.colors.accentNormal
                horizontalAlignment: Text.AlignLeft
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: {
                        cursorShape = Qt.PointingHandCursor
                        iMoreSettingsTxt.font.underline = true
                    }
                    onClicked:  {
                        fOpenSettingsWindow()
                    }

                    onExited: {
                        cursorShape = Qt.ArrowCursor
                        iMoreSettingsTxt.font.underline = false
                    }
                }
            }

            Component.onCompleted:  {
                if(pKmmControl.gamepadStick === "LeftStick")
                    iLeftStickBtn.checked = true
                else if(pKmmControl.gamepadStick === "RightStick")
                    iRightStickBtn.checked = true
                else
                    iDpadBtn.checked = true
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
                                             {"pTitle": qsTranslate("QObject", "Dpad settings"),
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
            "Up": {"keyName": qsTranslate("QObject", "Up"), "keyValue": pGamepadTabSelected ? pKmmControl.keyUp[1] : pKmmControl.keyUp[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "Down": {"keyName": qsTranslate("QObject", "Down"), "keyValue":  pGamepadTabSelected ? pKmmControl.keyDown[1] : pKmmControl.keyDown[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "Left": {"keyName": qsTranslate("QObject", "Left"), "keyValue":  pGamepadTabSelected ? pKmmControl.keyLeft[1] : pKmmControl.keyLeft[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "Right": {"keyName": qsTranslate("QObject", "Right"), "keyValue":  pGamepadTabSelected ? pKmmControl.keyRight[1] : pKmmControl.keyRight[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "Speed Modifier 1": {"keyName": qsTranslate("QObject", "Speed Modifier 1"), "keyValue":  pGamepadTabSelected ? pKmmControl.keySpeedModifier1[1] : pKmmControl.keySpeedModifier1[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "Speed Modifier 2": {"keyName": qsTranslate("QObject", "Speed Modifier 2"), "keyValue":  pGamepadTabSelected ? pKmmControl.keySpeedModifier2[1] : pKmmControl.keySpeedModifier2[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Radius": {"keyName": qsTranslate("QObject", "Radius"), "keyValue": pKmmControl.radius, "keyType": KmmEditorControlSettings.InputType.Double},
            "Radius 1": {"keyName": qsTranslate("QObject", "Radius 1"), "keyValue": pKmmControl.radius1, "keyType": KmmEditorControlSettings.InputType.Double},
            "Radius 2": {"keyName": qsTranslate("QObject", "Radius 2"), "keyValue": pKmmControl.radius2, "keyType": KmmEditorControlSettings.InputType.Double},
            "Speed": {"keyName": qsTranslate("QObject", "Speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Activation time": {"keyName": qsTranslate("QObject", "Activation time"), "keyValue": pKmmControl.activationTime, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Activation speed": {"keyName": qsTranslate("QObject", "Activation speed"), "keyValue": pKmmControl.activationSpeed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Dead zone radius": {"keyName": qsTranslate("QObject", "Dead zone radius"), "keyValue": pKmmControl.deadzoneRadius, "keyType": KmmEditorControlSettings.InputType.Double},
            "Gamepad stick": {"keyName": qsTranslate("QObject", "Gamepad stick"), "keyValue": pKmmControl.gamepadStick, "keyType": KmmEditorControlSettings.InputType.Key, "keyVisible": pGamepadTabSelected ? "true" : "false"}
        }
        var devPropertiesMap = {
            "Dpad expiry time": {"keyName": qsTranslate("QObject", "Dpad expiry time"), "keyValue": pKmmControl.dpadExpiryTime, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Dpad title": {"keyName": qsTranslate("QObject", "Dpad title"), "keyValue": pKmmControl.dpadTitle, "keyType": KmmEditorControlSettings.InputType.String},
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

        iRoot.width = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.radius
            return val * pResizeUnit
        })

        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
        if(pEditorWindowGamepadTabSelected) {
            if (pKmmControl.gamepadStick === "LeftStick") {
                pDpadType = 1
                iTextEditFieldCenter.fSetText(qsTranslate("QObject", "LS"))
            }
            else if (pKmmControl.gamepadStick === "RightStick") {
                pDpadType = 1
                iTextEditFieldCenter.fSetText(qsTranslate("QObject", "RS"))
            }
            else pDpadType = 0
        }
        else pDpadType = 0
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode() {
        iTextEditFieldUp.fInEditableMode()
    }

    function fOpenDpadSettingsPopup() {
        var dpadWin = iDpadPopupComponent.createObject(iRoot.parent)
        dpadWin.show()
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !pFocusedTextBox.pEditFieldActiveFocus)
            return

        //GamepadStick handling
        if(pFocusedTextBox === iTextEditFieldCenter) {
            if(key === "GamepadLStickUp" || key === "GamepadLStickDown" || key === "GamepadLStickLeft" || key === "GamepadLStickRight") {
                pFocusedTextBox.fSetText(qsTranslate("QObject", "LS"))
                pFocusedTextBox.updateKey("LeftStick")
            }
            else if(key === "GamepadRStickUp" || key === "GamepadRStickDown" || key === "GamepadRStickLeft" || key === "GamepadRStickRight") {
                pFocusedTextBox.fSetText(qsTranslate("QObject", "RS"))
                pFocusedTextBox.updateKey("RightStick")
            }
        }
        else {
            pFocusedTextBox.updateKey(key);
            pFocusedTextBox.fSetText(kmmBackend.getKeyList(key))
        }
    }
}
