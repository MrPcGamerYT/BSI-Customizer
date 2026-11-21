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
import QtQuick.Shapes 1.12

import UiToolTipControl 1.0

import "KmmUtils.js" as Utils

Item {
    id: iRoot
    width: (pDeviceType == 0 ? 72 : 32) * pRadius
    height: width
    z: 1

    property QtObject pKmmControl: null
    property var pStartingMouse
    property var pResizingMouse
    property bool pLoaded: false
    property string pType: "FreeLook"
    property real pDeviceType : 0

    property string pComposedKey: ""
    property KmmEditorTextEditBox pFocusedTextBox: null
    property bool pEditorWindowGamepadTabSelected: false
    property bool pGamepadTabSelected: false

    property real pRadius: 1.5

    property KmmEditorControlSettings pSettingsWin: null

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var index = pGamepadTabSelected ? 1 : 0
            pKmmControl.key[index] = updatedPropsMap[qsTranslate("QObject", "Key")]
            pKmmControl.keyUp[index] = updatedPropsMap[qsTranslate("QObject", "Key up")]
            pKmmControl.keyDown[index] = updatedPropsMap[qsTranslate("QObject", "Key down")]
            pKmmControl.keyLeft[index] = updatedPropsMap[qsTranslate("QObject", "Key left")]
            pKmmControl.keyRight[index] = updatedPropsMap[qsTranslate("QObject", "Key right")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.deviceType = updatedPropsMap[qsTranslate("QObject", "Device type")]
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Swipe speed")]
            pKmmControl.delay = updatedPropsMap[qsTranslate("QObject", "Edge reset delay")]
            pKmmControl.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
            pKmmControl.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
            pKmmControl.sensitivity = updatedPropsMap[qsTranslate("QObject", "Sensitivity")]
            pKmmControl.mouseAcceleration = updatedPropsMap[qsTranslate("QObject", "Mouse acceleration")]
            pKmmControl.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
            pKmmControl.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
            pKmmControl.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
            pKmmControl.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
            pKmmControl.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
            pKmmControl.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
            pKmmControl.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
            pKmmControl.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]

            iTextEditField.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.key[1] : pKmmControl.key[0]))
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

    ButtonGroup {
        id: iFreeLookBtnGrp
    }

    UiImage {
        id: iBaseImg
        asset: "EditorFreeLook" + (pDeviceType === 0 ? "Keyboard" : "Mouse")
        pImageWidth: iRoot.width
        pImageHeight: iRoot.width
        anchors.centerIn: parent
        property var globalPos

        MouseArea {
            id: iBaseImgMA
            anchors.fill: iBaseImg
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: function(mouse) {
                iBaseImg.globalPos = iBaseImg.mapToGlobal(0,0)
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
            onReleased: {
                pStartingMouse = undefined

                if (iBaseImg.mapToGlobal(0,0).x === iBaseImg.globalPos.x &&
                        iBaseImg.mapToGlobal(0,0).y === iBaseImg.globalPos.y &&
                        pKmmControl.deviceType === 1) {
                    iTextEditField.fInEditableMode()
                }
            }
        }
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
                             iBaseImgMA.pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor :
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
                onClicked: fOpenFreeLookSettingsPopup()
            }
        }
    }

    KmmEditorTextEditBox {
        id: iTextEditField
        anchors.centerIn: parent
        visible: pDeviceType === 0 ? false : true
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.key[1] = key
            else pKmmControl.key[0] = key
        }
        onPEditFieldActiveFocusChanged: {
            if(pEditFieldActiveFocus) {
                pFocusedTextBox = this
                kmmBackend.setFocusedVisualItem(iRoot)
            }
        }
    }

    KmmEditorTextEditBox {
        id: iTextEditFieldUp
        x: iRoot.width / 2 - width / 2
        y: (iRoot.height * 0.02) - height / 2
        visible: pDeviceType === 0 ? true : false
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
        y: (iRoot.height * 0.98) - height / 2
        visible: pDeviceType === 0 ? true : false
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
        x: (iRoot.width * 0.02) - width / 2
        y: iRoot.height / 2 - height / 2
        visible: pDeviceType === 0 ? true : false
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
        x: (iRoot.width * 0.98) - width / 2
        y: iRoot.height / 2 - height / 2
        visible: pDeviceType === 0 ? true : false
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

    Component {
        id: iFreeLookPopupComponent
        UiHelpPopup {
            id: iFreeLookPopup
            contentWidth: 150
            visible: false
            x: iSettingsBtn.mapToGlobal(0, 0).x - (width - iSettingsBtn.width) / 2
            y: iSettingsBtn.mapToGlobal(0, 0).y - height
            onWidthChanged: {
                iFreeLookPopup.x = iSettingsBtn.mapToGlobal(0, 0).x - (iFreeLookPopup.width - iSettingsBtn.width)/2
            }

            pInnerLayout {
                border.width: 1
                border.color: UiTheme.colors.primary60
                color: UiTheme.colors.primary70
                padding: 8
                spacing: 8
            }

            Text {
                text: qsTranslate("QObject", "Free look control")
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
                    id: iMouseBtn
                    text: qsTranslate("QObject", "Mouse")
                    ButtonGroup.group: iFreeLookBtnGrp
                    Layout.fillWidth: true
                    onCheckedChanged: if(checked) pKmmControl.deviceType = 1
                }
                UiRadioButton {
                    id: iKeyboardBtn
                    text: qsTranslate("QObject", "Keyboard")
                    Layout.fillWidth: true
                    ButtonGroup.group: iFreeLookBtnGrp
                    onCheckedChanged: if(checked) pKmmControl.deviceType = 0
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
                if(pKmmControl.deviceType === 0)
                    iKeyboardBtn.checked = true
                else
                    iMouseBtn.checked = true
            }
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

        iTextEditField.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.key[1] : pKmmControl.key[0]))
        iTextEditFieldUp.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyUp[1] : pKmmControl.keyUp[0]))
        iTextEditFieldDown.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyDown[1] : pKmmControl.keyDown[0]))
        iTextEditFieldLeft.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyLeft[1] : pKmmControl.keyLeft[0]))
        iTextEditFieldRight.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyRight[1] : pKmmControl.keyRight[0]))

        pDeviceType = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.deviceType
            return val
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

    function fOpenSettingsWindow() {
        if (pSettingsWin !== null) {
            pSettingsWin.destroy()
        }

        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "Freelook settings"),
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
        var index = pGamepadTabSelected ? 1 : 0
        var keyBindingsMap = {
            "Key": {"keyName": qsTranslate("QObject", "Key"), "keyValue": pKmmControl.key[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Key up": {"keyName": qsTranslate("QObject", "Key up"), "keyValue": pKmmControl.keyUp[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Key down": {"keyName": qsTranslate("QObject", "Key down"), "keyValue": pKmmControl.keyDown[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Key left": {"keyName": qsTranslate("QObject", "Key left"), "keyValue": pKmmControl.keyLeft[index], "keyType": KmmEditorControlSettings.InputType.Key},
            "Key right": {"keyName": qsTranslate("QObject", "Key right"), "keyValue": pKmmControl.keyRight[index], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Device type": {"keyName": qsTranslate("QObject", "Device type"), "keyValue": pKmmControl.deviceType, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Swipe speed": {"keyName": qsTranslate("QObject", "Swipe speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Edge reset delay": {"keyName": qsTranslate("QObject", "Edge reset delay"), "keyValue": pKmmControl.delay, "keyType": KmmEditorControlSettings.InputType.Integer}
        }
        var devPropertiesMap = {
            "Sensitivity": {"keyName": qsTranslate("QObject", "Sensitivity"), "keyValue": pKmmControl.sensitivity, "keyType": KmmEditorControlSettings.InputType.Double},
            "Mouse acceleration": {"keyName": qsTranslate("QObject", "Mouse acceleration"), "keyValue": pKmmControl.mouseAcceleration, "keyType": KmmEditorControlSettings.InputType.Boolean},
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

    function fOpenFreeLookSettingsPopup() {
        var freeLookWin = iFreeLookPopupComponent.createObject(iRoot.parent)
        freeLookWin.show()
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !pFocusedTextBox.pEditFieldActiveFocus)
            return

        pFocusedTextBox.updateKey(key);
        pFocusedTextBox.fSetText(kmmBackend.getKeyList(key))
    }
}
