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
    property string pType: "MOBADpad"

    property real pResizeUnit: (28 * plrBackend.glWinRect[2]) / 1280

    property KmmEditorControlSettings pSettingsWin: null
    property KmmEditorMobaHero pMobaHeroDumy: null
    property KmmEditorMobaDpadCancel pMobaDpadCancel: null
    property bool pEditorWindowGamepadTabSelected: false
    property bool pGamepadTabSelected: false

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var index = pGamepadTabSelected ? 1 : 0
            pKmmControl.keyMove = updatedPropsMap[qsTranslate("QObject", "Movement")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.radius = updatedPropsMap[qsTranslate("QObject", "Radius")]
            var newOriginX = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Dummy position X")])
            var newOriginY = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Dummy position Y")])
            if(pKmmControl.originX !== newOriginX || pKmmControl.originY !== newOriginY){
                kmmBackend.setMOBASkillOriginCoordinates(newOriginX, newOriginY)
            }
            pKmmControl.originX = newOriginX
            pKmmControl.originY = newOriginY
            pKmmControl.cancelX = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Cancel skill X")])
            pKmmControl.cancelY = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Cancel skill Y")])
            pKmmControl.charSpeed = updatedPropsMap[qsTranslate("QObject", "Hero speed")]
            pKmmControl.keyCancel[index] = updatedPropsMap[qsTranslate("QObject", "Cancel")]
            pKmmControl.isCancelSkillEnabled = updatedPropsMap[qsTranslate("QObject", "Cancel skill enabled")]
            pKmmControl.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
            pKmmControl.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Dpad speed")]
            pKmmControl.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
            pKmmControl.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
            pKmmControl.originXExpr = updatedPropsMap[qsTranslate("QObject", "OriginXExpr")]
            pKmmControl.originYExpr = updatedPropsMap[qsTranslate("QObject", "OriginYExpr")]
            pKmmControl.cancelXExpr = updatedPropsMap[qsTranslate("QObject", "CancelXExpr")]
            pKmmControl.cancelYExpr = updatedPropsMap[qsTranslate("QObject", "CancelYExpr")]
            pKmmControl.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
            pKmmControl.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
            pKmmControl.cancelXOverlayOffset = updatedPropsMap[qsTranslate("QObject", "Cancel X overlayOffset")]
            pKmmControl.cancelYOverlayOffset = updatedPropsMap[qsTranslate("QObject", "Cancel Y overlayOffset")]
            pKmmControl.cancelShowOnOverlayExpr = updatedPropsMap[qsTranslate("QObject", "Cancel show on overlay expr")]
            pKmmControl.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
            pKmmControl.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
            pKmmControl.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
            pKmmControl.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]

            if(!pKmmControl.isCancelSkillEnabled) {
                pKmmControl.cancelX = -1
                pKmmControl.cancelY = -1
                pKmmControl.keyCancel[0] = ""
                pKmmControl.keyCancel[1] = ""
            }

            if (pMobaDpadCancel) {
                pMobaDpadCancel.fSetCancelKey(kmmBackend.gamepadTabSelected ? pKmmControl.keyCancel[1] : pKmmControl.keyCancel[0])
                pMobaDpadCancel.pSettingsWindowVisible = false
            }
            if (pMobaHeroDumy) {
                pMobaHeroDumy.pSettingsWindowVisible = false
            }
        }

        function onDeleteControl() {
            fDeleteVisualItem()
        }

        function onCreateExtraProp1Item(x, y) {
            pKmmControl.originX = (((x - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
            pKmmControl.originY = (((y - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
            fCreateMobaHeroDummy()
        }

        function onCreateExtraProp2Item(x, y) {
            pKmmControl.cancelX = (((x - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
            pKmmControl.cancelY = (((y - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
            if (pKmmControl.keyCancel[0] === "")
                pKmmControl.keyCancel[0] = "Space"
            fCreateMobaDpadCancel()
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }
    }

    Connections {
        target: pMobaHeroDumy

        function onSettingsBtnClicked(x, y) {
            fOpenSettingsWindow(x, y)
        }
        function onDeleteClicked() {
            kmmBackend.kmmEditorDeleteVisualItem(pMobaHeroDumy)
            pKmmControl.originX = -1
            pKmmControl.originY = -1
        }
    }

    Connections {
        target: pMobaDpadCancel

        function onSettingsBtnClicked(x, y) {
            fOpenSettingsWindow(x, y)
        }
        function onDeleteClicked() {
            kmmBackend.kmmEditorDeleteVisualItem(pMobaDpadCancel)
            pKmmControl.cancelX = -1
            pKmmControl.cancelY = -1
            pKmmControl.isCancelSkillEnabled = false
            pKmmControl.keyCancel[0] = ""
            pKmmControl.keyCancel[1] = ""
        }
    }

    KmmEditorBaseImageRect {
        pImgSrc: "EditorMobaDpad"
        onDeleteClicked: fDeleteVisualItem()
        onSettingsClicked: fOpenSettingsWindow()
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

    function fLoadControlProp(gamepadTabSelected) {
        iRoot.x = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.x
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.y
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })

        iRoot.width = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.radius
            return val * pResizeUnit
        })

        if(pKmmControl.originX !== -1 && pKmmControl.originY !== -1) {
            fCreateMobaHeroDummy()
        }

        if(pKmmControl.isCancelSkillEnabled && pKmmControl.cancelX !== -1 && pKmmControl.cancelY !== -1) {
            fCreateMobaDpadCancel()
        }

        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode() {
        //        iTextField.visible = false
        //        iEditField.forceActiveFocus()
    }

    function fCreateMobaHeroDummy() {
        if (pMobaHeroDumy === null) {
            pMobaHeroDumy = kmmBackend.kmmEditorCreateVisualItem("MOBAHero", pKmmControl)
        }
    }

    function fCreateMobaDpadCancel() {
        if (pMobaDpadCancel === null) {
            pMobaDpadCancel = kmmBackend.kmmEditorCreateVisualItem("MOBACancel", pKmmControl)
        }
    }

    function fOpenSettingsWindow(x = undefined, y = undefined) {
        if (pSettingsWin !== null) {
            pSettingsWin.destroy()
        }

        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "MOBA Dpad settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOverlay,
                                                 "pExtraProp1Header": qsTranslate("QObject", "Hero dummy"),
                                                 "pExtraProp1Image": "EditorMobaHero",
                                                 "pExtraProp1Text": qsTranslate("QObject", "Place the hero dummy to the center of the screen for calibration"),
                                                 "pExtraProp2Header": qsTranslate("QObject", "Cancel movement"),
                                                 "pExtraProp2Image": "MobaCancelMove",
                                                 "pExtraProp2Text": qsTranslate("QObject", "Place the point on the cancel key"),
                                                 "pExtraProp2TogglePresent": true,
                                                 "pExtraProp2ToggleOn": pKmmControl.isCancelSkillEnabled,
                                                 "pExtraProp2ToggleKey": qsTranslate("QObject", "Cancel skill enabled"),
                                                 "pEnableCondition": pKmmControl.enableCondition,
                                                 "pStartCondition": pKmmControl.startCondition})
            if (pSettingsWin !== null) {
                pGamepadTabSelected = pEditorWindowGamepadTabSelected
                fLoadKeyPropertiesMap()
                if (x !== undefined && y !== undefined) {
                    pSettingsWin.x = x
                    pSettingsWin.y = y
                }
                else {
                    pSettingsWin.x = iRoot.mapToGlobal(0, 0).x
                    pSettingsWin.y = iRoot.mapToGlobal(0, 0).y + iRoot.height
                }
                pSettingsWin.show()

                if (pMobaDpadCancel) {
                    pMobaDpadCancel.pSettingsWindowVisible = true
                }
                if (pMobaHeroDumy) {
                    pMobaHeroDumy.pSettingsWindowVisible = true
                }
            }
        }
        kmmBackend.setFocusedVisualItem(null)
    }

    function fLoadKeyPropertiesMap() {
        var keyBindingsMap = {
            "Movement": {"keyName": qsTranslate("QObject", "Movement"), "keyValue": pKmmControl.keyMove, "keyType": KmmEditorControlSettings.InputType.Key, "keyEnabled": "false"}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Radius": {"keyName": qsTranslate("QObject", "Radius"), "keyValue": pKmmControl.radius, "keyType": KmmEditorControlSettings.InputType.Double}
        }

        var mobaHeroPropertiesMap = {
            "Dummy position X": {"keyName": qsTranslate("QObject", "Dummy position X"), "keyValue": pKmmControl.originX, "keyType": KmmEditorControlSettings.InputType.Double},
            "Dummy position Y": {"keyName": qsTranslate("QObject", "Dummy position Y"), "keyValue": pKmmControl.originY, "keyType": KmmEditorControlSettings.InputType.Double},
            "Hero speed": {"keyName": qsTranslate("QObject", "Hero speed"), "keyValue": pKmmControl.charSpeed, "keyType": KmmEditorControlSettings.InputType.Double}
        }

        var mobaCancelPropertiesMap = {
            "Cancel": {"keyName": qsTranslate("QObject", "Cancel"), "keyValue": pGamepadTabSelected ? pKmmControl.keyCancel[1] : pKmmControl.keyCancel[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }

        var devPropertiesMap = {
            "Cancel skill X": {"keyName": qsTranslate("QObject", "Cancel skill X"), "keyValue": pKmmControl.cancelX, "keyType": KmmEditorControlSettings.InputType.Double},
            "Cancel skill Y": {"keyName": qsTranslate("QObject", "Cancel skill Y"), "keyValue": pKmmControl.cancelY, "keyType": KmmEditorControlSettings.InputType.Double},
            "Tweaks": {"keyName": qsTranslate("QObject", "Tweaks"), "keyValue": pKmmControl.tweaks, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Dpad speed": {"keyName": qsTranslate("QObject", "Dpad speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double},
            "XExpr": {"keyName": qsTranslate("QObject", "XExpr"), "keyValue": pKmmControl.xExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "YExpr": {"keyName": qsTranslate("QObject", "YExpr"), "keyValue": pKmmControl.yExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "OriginXExpr": {"keyName": qsTranslate("QObject", "OriginXExpr"), "keyValue": pKmmControl.originXExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "OriginYExpr": {"keyName": qsTranslate("QObject", "OriginYExpr"), "keyValue": pKmmControl.originYExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "Cancel X overlayOffset": {"keyName": qsTranslate("QObject", "Cancel X overlayOffset"), "keyValue": pKmmControl.cancelXOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "Cancel Y overlayOffset": {"keyName": qsTranslate("QObject", "Cancel Y overlayOffset"), "keyValue": pKmmControl.cancelYOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "CancelXExpr": {"keyName": qsTranslate("QObject", "CancelXExpr"), "keyValue": pKmmControl.cancelXExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "CancelYExpr": {"keyName": qsTranslate("QObject", "CancelYExpr"), "keyValue": pKmmControl.cancelYExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "Cancel show on overlay expr": {"keyName": qsTranslate("QObject", "Cancel show on overlay expr"), "keyValue": pKmmControl.cancelShowOnOverlayExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "XOverlayOffset": {"keyName": qsTranslate("QObject", "XOverlayOffset"), "keyValue": pKmmControl.xOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "YOverlayOffset": {"keyName": qsTranslate("QObject", "YOverlayOffset"), "keyValue": pKmmControl.yOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "Exclusive": {"keyName": qsTranslate("QObject", "Exclusive"), "keyValue": pKmmControl.exclusive, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Exclusive delay": {"keyName": qsTranslate("QObject", "Exclusive delay"), "keyValue": pKmmControl.exclusiveDelay, "keyType": KmmEditorControlSettings.InputType.Integer}
        }

        pSettingsWin.fLoadModel(keyBindingsMap, propertiesMap, mobaHeroPropertiesMap, mobaCancelPropertiesMap, devPropertiesMap)

    }

    function fDeleteVisualItem() {
        if (pMobaHeroDumy) {
            kmmBackend.kmmEditorDeleteVisualItem(pMobaHeroDumy)
        }

        if(pMobaDpadCancel) {
            kmmBackend.kmmEditorDeleteVisualItem(pMobaDpadCancel)
        }

        Utils.deleteControl(iRoot, pKmmControl)
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected)
            return
    }
}
