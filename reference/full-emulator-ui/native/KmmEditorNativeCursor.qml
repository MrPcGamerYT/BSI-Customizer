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
    width: 36
    height: 36
    z: 1

    property QtObject pKmmControl: null
    property var pStartingMouse: undefined
    property bool pLoaded: false
    property string pComposedKey: ""
    property string pType: "NativeCursor"
    property  bool pGamepadTabSelected : false
    property bool pEditorWindowGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var idx = pGamepadTabSelected ? 1 : 0
            pKmmControl.key[idx] = updatedPropsMap[qsTranslate("QObject", "Key")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.mouseSensitivityX = updatedPropsMap[qsTranslate("QObject", "Mouse sensitivity X")]
            pKmmControl.mouseSensitivityY = updatedPropsMap[qsTranslate("QObject", "Mouse sensitivity Y")]
            pKmmControl.lockCursorOnRightMouseButtonPressed = updatedPropsMap[qsTranslate("QObject", "Lock cursor on right mouse button pressed")]
            pKmmControl.showNativeMousePointer = updatedPropsMap[qsTranslate("QObject", "Show native mouse pointer")]
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

    UiRowLayout {
        id: iRowLayout
        spacing: 0
        padding: 0

        Text {
            color: UiTheme.colors.primary20
            font: UiTheme.fonts.bodyExtraSmall
            Layout.alignment: Qt.AlignHCenter
            text: "X"
        }

        UiColumnLayout {
            spacing: 0
            padding: 0

            Layout.preferredWidth: 22
            Layout.preferredHeight: childrenRect.height
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 4

            UiImageButton {
                asset: "StepUp"
                pImageWidth: 16
                pImageHeight: 16
                onClicked: {
                    pKmmControl.mouseSensitivityX += 0.05
                }
            }

            Text {
                id: iSensiXText
                color: UiTheme.colors.primary10
                font: UiTheme.fonts.bodyExtraSmall
                Layout.alignment: Qt.AlignHCenter
            }

            UiImageButton {
                asset: "StepDown"
                pImageWidth: 16
                pImageHeight: 16
                onClicked: {
                    pKmmControl.mouseSensitivityX -= 0.05
                    if (pKmmControl.mouseSensitivityX < 0.0) {
                        pKmmControl.mouseSensitivityX = 0
                    }
                }
            }
        }

        Item {
            Layout.preferredWidth: 36
            Layout.fillHeight: true
             KmmEditorBaseImageRect {
                pImgSrc: "EditorNativeCursor"
                height: 36
                width: 36
                onDeleteClicked: Utils.deleteControl(iRoot, pKmmControl)
                onSettingsClicked: fOpenSettingsWindow()
                onItemMoved: function(startingMouse, currentMouse) {
                    Utils.moveItem(pKmmControl, iRoot.x, iRoot.y, iRoot.width, iRoot.height, startingMouse, currentMouse)
                }
            }
        }

        UiColumnLayout {
            spacing: 0
            padding: 0

            Layout.preferredWidth: 22
            Layout.preferredHeight: childrenRect.height
            Layout.alignment: Qt.AlignVCenter

            UiImageButton {
                asset: "StepUp"
                pImageWidth: 16
                pImageHeight: 16
                onClicked: {
                    pKmmControl.mouseSensitivityY += 0.05
                }
            }

            Text {
                id: iSensiYText
                color: UiTheme.colors.primary10
                font: UiTheme.fonts.bodyExtraSmall
                Layout.alignment: Qt.AlignHCenter
            }

            UiImageButton {
                asset: "StepDown"
                pImageWidth: 16
                pImageHeight: 16
                onClicked: {
                    pKmmControl.mouseSensitivityY -= 0.05
                    if (pKmmControl.mouseSensitivityY < 0.0) {
                        pKmmControl.mouseSensitivityY = 0
                    }
                }
            }
        }

        Text {
            color: UiTheme.colors.primary20
            font: UiTheme.fonts.bodyExtraSmall
            Layout.alignment: Qt.AlignHCenter
            Layout.leftMargin: 2
            text: "Y"
        }
    }

    function fOpenSettingsWindow() {
        if (pSettingsWin != null) {
            pSettingsWin.destroy()
        }

        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "Native cursor settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOverlay,
                                                 "pShowOverlaySetting": false,
                                                 "pEnableCondition": pKmmControl.enableCondition,
                                                 "pStartCondition": pKmmControl.startCondition})
            if (pSettingsWin != null) {
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
            //"Key": {"keyName": qsTranslate("QObject", "Key"), "keyValue": pGamepadTabSelected ? pKmmControl.key[1] : pKmmControl.key[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Mouse sensitivity X": {"keyName": qsTranslate("QObject", "Mouse sensitivity X"), "keyValue": parseFloat(pKmmControl.mouseSensitivityX).toFixed(2), "keyType": KmmEditorControlSettings.InputType.Double},
            "Mouse sensitivity Y": {"keyName": qsTranslate("QObject", "Mouse sensitivity Y"), "keyValue": parseFloat(pKmmControl.mouseSensitivityY).toFixed(2), "keyType": KmmEditorControlSettings.InputType.Double}
        }
        var devPropertiesMap = {
            "Tweaks": {"keyName": qsTranslate("QObject", "Tweaks"), "keyValue": pKmmControl.tweaks, "keyType": KmmEditorControlSettings.InputType.Integer},
            "XExpr": {"keyName": qsTranslate("QObject", "XExpr"), "keyValue": pKmmControl.xExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "YExpr": {"keyName": qsTranslate("QObject", "YExpr"), "keyValue": pKmmControl.yExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "XOverlayOffset": {"keyName": qsTranslate("QObject", "XOverlayOffset"), "keyValue": pKmmControl.xOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "YOverlayOffset": {"keyName": qsTranslate("QObject", "YOverlayOffset"), "keyValue": pKmmControl.yOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "Exclusive": {"keyName": qsTranslate("QObject", "Exclusive"), "keyValue": pKmmControl.exclusive, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Exclusive delay": {"keyName": qsTranslate("QObject", "Exclusive delay"), "keyValue": pKmmControl.exclusiveDelay, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Lock cursor on right mouse button pressed": {"keyName": qsTranslate("QObject", "Lock cursor on right mouse button pressed"), "keyValue": pKmmControl.lockCursorOnRightMouseButtonPressed, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Show native mouse pointer": {"keyName": qsTranslate("QObject", "Show native mouse pointer"), "keyValue": pKmmControl.showNativeMousePointer, "keyType": KmmEditorControlSettings.InputType.Boolean}
        }
        pSettingsWin.fLoadModel(keyBindingsMap, propertiesMap, {}, {}, devPropertiesMap)
    }

    function fLoadControlProp(gamepadTabSelected) {
        iRoot.x = Qt.binding(function() {
            var val = pKmmControl == null ? 0 : pKmmControl.x
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0 : pKmmControl.y
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })
        iSensiXText.text = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.mouseSensitivityX
            return parseFloat(val).toFixed(2)
        })
        iSensiYText.text = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.mouseSensitivityY
            return parseFloat(val).toFixed(2)
        })
        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode(){
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected)
            return
        pKmmControl.key[1] = key
    }
}
