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
import DialogButtonModel 1.0
import DialogButtonModelList 1.0
import UiDialogWindow 1.0

import UiToolTipControl 1.0

import "KmmUtils.js" as Utils

Item {
    id: iRoot
    width: 38
    height: 38
    z: 1

    property QtObject pKmmControl: null
    property var pStartingMouse
    property string pComposedKey: ""
    property string pType: "Script"
    property bool pGamepadTabSelected : false
      property bool pEditorWindowGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var idx = pGamepadTabSelected ? 1 : 0
            pKmmControl.key[idx] = updatedPropsMap[qsTranslate("QObject", "Script key")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Script X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Script Y")])
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
            iTextEditField.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.key[1] : pKmmControl.key[0]))
        }

        function onDeleteControl() {
            Utils.deleteControl(iRoot, pKmmControl)
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }
    }

    Component {
        id: iScriptEditorMsgBox
        UiMessageDialog {
            title: qsTranslate("QObject", "Script editor")
            pDescription: qsTranslate( "QObject", "The Script Editor is not available at the moment. It will be available soon in a future update.")
            pNotice: qsTranslate("QObject", "However, you can still change the assigned key by clicking on the control.")
            pDialogType: UiDialogWindow.Simple
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Okay")
                        onClicked: {
                            close()
                        }
                    }
                ]
            }
        }
    }

    property var globalPos

    MouseArea {
        id: iRootMA
        anchors.fill: iRoot
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(mouse) {
            globalPos = iRoot.mapToGlobal(0,0)
            if(mouse.button & Qt.RightButton) {
                fOpenSettingsWindow()

                return
            }
            pStartingMouse = { x: mouse.x, y: mouse.y}
        }
        onPositionChanged: function(mouse) {
            if (pStartingMouse !== undefined)
                Utils.moveItem(pKmmControl, iRoot.x, iRoot.y, iRoot.width, iRoot.height, pStartingMouse, mouse)
        }
        onReleased: {
            pStartingMouse = undefined

            if (iRoot.mapToGlobal(0,0).x === globalPos.x && iRoot.mapToGlobal(0,0).y === globalPos.y) {
                iTextEditField.fInEditableMode()
            }
        }
    }

    Rectangle {
        width: 28
        height: width
        radius: width / 2
        anchors.centerIn: parent
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
            cursorShape: containsMouse ? iRootMA.pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor : Qt.ArrowCursor
            onPressed: mouse.accepted = false
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
    KmmEditorTextEditBox {
        id: iTextEditField
        anchors.centerIn: parent
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.key[1] = key
            else pKmmControl.key[0] = key
        }
        onPEditFieldActiveFocusChanged: if(pEditFieldActiveFocus) kmmBackend.setFocusedVisualItem(iRoot)
    }

    Rectangle {
        id: iBaseImg
        width: 28
        height: width
        radius: width / 2
        anchors.centerIn: parent

        z:3
        color: "transparent"
        Rectangle {
            width: 12
            height: width
            radius: width / 2
            anchors.bottom: iBaseImg.bottom
            anchors.right: iBaseImg.right
            color: UiTheme.colors.accentNormal
            anchors.leftMargin: 2
            anchors.topMargin: 2
            UiImageButton {
                asset: "ScriptEdit"
                implicitWidth: parent.width
                pImageWidth: 12
                pImageHeight: 12
                anchors.centerIn: parent
                onClicked: {
                    var component = Qt.createComponent("KmmScriptEditorWindow.qml")
                    if (component.status === Component.Ready) {
                        var win = component.createObject(iRoot.parent, {"pKmmControl": pKmmControl})
                        if (win !== null) {
                            win.width = iRoot.parent.width
                            win.height = iRoot.parent.height
                            win.x = iRoot.parent.mapToGlobal(0, 0).x
                            win.y = iRoot.parent.mapToGlobal(0, 0).y
                            win.show()
                        }
                    }
                }
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
                                             {"pTitle": qsTranslate("QObject", "Script settings"),
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
            "Script key": {"keyName": qsTranslate("QObject", "Script key"), "keyValue": pGamepadTabSelected ? pKmmControl.key[1] : pKmmControl.key[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "Script X": {"keyName": qsTranslate("QObject", "Script X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Script Y": {"keyName": qsTranslate("QObject", "Script Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double}
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
        iTextEditField.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.key[1] : pKmmControl.key[0]))
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode() {
        iTextEditField.fInEditableMode()
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !iTextEditField.pEditFieldActiveFocus)
            return
        pKmmControl.key[1] = key
        iTextEditField.fSetText(kmmBackend.getKeyList(key))
    }
}
