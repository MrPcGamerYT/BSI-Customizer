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
    width: iRowLayout.width
    height: 48
    z: 1

    property QtObject pKmmControl: null
    property var pStartingMouse
    property string pType: "Pan"
    property var globalPos
    property bool pGamepadTabSelected: false
    property bool pEditorWindowGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null
    property KmmEditorPanShoot pPanShootItem: null
    property KmmEditorPanLookAround pPanLookAroundItem: null
    property KmmEditorCrosshair pCrosshairItem: null

    property list<QtObject> pCrosshairTypeModel: [
        QtObject {
            property string text: qsTranslate("QObject", "Cross")
            property string value: "Cross"
            property string imageName: "CrosshairCross"
        },
        QtObject {
            property string text: qsTranslate("QObject", "Dot")
            property string value: "Dot"
            property string imageName: "CrosshairDot"
        },
        QtObject {
            property string text: qsTranslate("QObject", "Cross dot")
            property string value: "CrossDot"
            property string imageName: "CrosshairCrossDot"
        },
        QtObject {
            property string text: qsTranslate("QObject", "Circle")
            property string value: "Circle"
            property string imageName: "CrosshairCircle"
        },
        QtObject {
            property string text: qsTranslate("QObject", "Custom")
            property string value: "Custom"
            property string imageName: ""
        }
    ]

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var index = pGamepadTabSelected ? 1 : 0
            pKmmControl.keyStartStop[index] = updatedPropsMap[qsTranslate("QObject", "Start/Stop")]
            pKmmControl.keySuspend[index] = updatedPropsMap[qsTranslate("QObject", "Suspend")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.sensitivity = updatedPropsMap[qsTranslate("QObject", "Mouse sensitivity X")]
            pKmmControl.sensitivityRatioY = updatedPropsMap[qsTranslate("QObject", "Mouse sensitivity Y")] / pKmmControl.sensitivity
            pKmmControl.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
            pKmmControl.mouseAcceleration = updatedPropsMap[qsTranslate("QObject", "Mouse acceleration")]
            pKmmControl.lookAroundX = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Free look position X")])
            pKmmControl.lookAroundY = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Free look position Y")])
            pKmmControl.keyLookAround[index] = updatedPropsMap[qsTranslate("QObject", "Free look")]
            pKmmControl.isLookAroundEnabled = updatedPropsMap[qsTranslate("QObject", "Free look enabled")]
            pKmmControl.lButtonX = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Action position X")])
            pKmmControl.lButtonY = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Action position Y")])
            pKmmControl.keyAction = updatedPropsMap[qsTranslate("QObject", "Action")]
            pKmmControl.isShootOnClickEnabled = updatedPropsMap[qsTranslate("QObject", "Shoot on click enabled")]
            pKmmControl.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
            iSensiYText.text = parseFloat(updatedPropsMap[qsTranslate("QObject", "Mouse sensitivity Y")]).toFixed(2)
            pKmmControl.gamepadStick = updatedPropsMap[qsTranslate("QObject", "Gamepad stick")]
            pKmmControl.deadzoneRadius = updatedPropsMap[qsTranslate("QObject", "Deadzone radius")]
            pKmmControl.gamepadSensitivity = updatedPropsMap[qsTranslate("QObject", "Gamepad sensitivity")]
            pKmmControl.tapX = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Tap X")])
            pKmmControl.tapY = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Tap Y")])
            pKmmControl.crosshairX = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Crosshair X")])
            pKmmControl.crosshairY = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Crosshair Y")])
            pKmmControl.panLeft = updatedPropsMap[qsTranslate("QObject", "Left")]
            pKmmControl.panRight = updatedPropsMap[qsTranslate("QObject", "Right")]
            pKmmControl.panTop = updatedPropsMap[qsTranslate("QObject", "Top")]
            pKmmControl.panBottom = updatedPropsMap[qsTranslate("QObject", "Bottom")]
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
            pKmmControl.activationTimeMs = updatedPropsMap[qsTranslate("QObject", "Activation time in ms")]
            pKmmControl.waitAfterShootTime = updatedPropsMap[qsTranslate("QObject", "Wait after shoot time")]
            pKmmControl.isCrosshairEnabled = updatedPropsMap[qsTranslate("QObject", "Crosshair enabled")]
            pKmmControl.crosshairOpacity = updatedPropsMap[qsTranslate("QObject", "Opacity (Changes appear in the app. Is unaffected by the opacity of on-screen controls.)")]
            pKmmControl.crosshairSize = updatedPropsMap[qsTranslate("QObject", "Size")]
            pKmmControl.crosshairColor = updatedPropsMap[qsTranslate("QObject", "Color")]
            pKmmControl.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
            pKmmControl.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
            pKmmControl.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
            pKmmControl.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
            pKmmControl.lButtonXExpr = updatedPropsMap[qsTranslate("QObject", "LButton XExpr")]
            pKmmControl.lButtonYExpr = updatedPropsMap[qsTranslate("QObject", "LButton YExpr")]
            pKmmControl.lButtonXOverlayOffset = updatedPropsMap[qsTranslate("QObject", "LButton XOverlayOffset")]
            pKmmControl.lButtonYOverlayOffset = updatedPropsMap[qsTranslate("QObject", "LButton YOverlayOffset")]
            pKmmControl.lookAroundXExpr = updatedPropsMap[qsTranslate("QObject", "LookAround XExpr")]
            pKmmControl.lookAroundYExpr = updatedPropsMap[qsTranslate("QObject", "LookAround YExpr")]
            pKmmControl.lookAroundXOverlayOffset = updatedPropsMap[qsTranslate("QObject", "LookAround XOverlayOffset")]
            pKmmControl.lookAroundYOverlayOffset = updatedPropsMap[qsTranslate("QObject", "LookAround YOverlayOffset")]
            pKmmControl.lButtonShowOnOverlayExpr = updatedPropsMap[qsTranslate("QObject", "LButton show on overlay expr")]
            pKmmControl.lookAroundShowOnOverlayExpr = updatedPropsMap[qsTranslate("QObject", "LookAround show on overlay expr")]
            pKmmControl.crosshairShowOnOverlayExpr = updatedPropsMap[qsTranslate("QObject", "Crosshair show on overlay expr")]
            pKmmControl.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
            pKmmControl.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
            pKmmControl.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
            pKmmControl.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]
            iTextEditField.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyStartStop[1] : pKmmControl.keyStartStop[0]))
            if (!pKmmControl.isLookAroundEnabled) {
                pKmmControl.lookAroundX = -1
                pKmmControl.lookAroundY = -1
                pKmmControl.keyLookAround[0] = ""
                pKmmControl.keyLookAround[1] = ""
            }
            if (!pKmmControl.isShootOnClickEnabled) {
                pKmmControl.lButtonX = -1
                pKmmControl.lButtonY = -1
            }

            if(pKmmControl.crosshairType !== "Custom") {
                pKmmControl.crosshairImage = ""
            }

            if(!pKmmControl.isCrosshairEnabled) {
                pKmmControl.crosshairSize = 1
                pKmmControl.crosshairOpacity = 100
                if (pCrosshairItem) {
                    pCrosshairItem.fChangeCrosshairImage(pKmmControl.isCrosshairEnabled)
                }
            }

            if(pKmmControl.crosshairSize < 1)
                pKmmControl.crosshairSize = 1.0

            if (pPanLookAroundItem) {
                pPanLookAroundItem.fSetLookAroundKey(kmmBackend.gamepadTabSelected ? pKmmControl.keyLookAround[1] : pKmmControl.keyLookAround[0])
                pPanLookAroundItem.pSettingsWindowVisible = false
            }
            if (pPanShootItem) {
                pPanShootItem.pSettingsWindowVisible = false
            }
            if(pCrosshairItem) {
                pCrosshairItem.pSettingsWindowVisible = false
            }
        }

        function onDeleteControl() {
            fDeleteVisualItem()
        }

        function onCreateExtraProp1Item(x, y) {
            pKmmControl.lButtonX = (((x - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
            pKmmControl.lButtonY = (((y - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
            fCreatePanShootItem()
        }

        function onCreateExtraProp2Item(x, y) {
            pKmmControl.lookAroundX = (((x - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
            pKmmControl.lookAroundY = (((y - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
            if (pKmmControl.keyLookAround[0] === "")
                pKmmControl.keyLookAround[0] = "V"
            fCreatePanLookAroundItem()
            pPanLookAroundItem.fInEditableMode()
        }

        function onCreateExtraProp3Item(enabled) {
            if(pKmmControl != null && pCrosshairItem != null && enabled !== undefined) {
                if(pKmmControl.isCrosshairEnabled !== enabled)
                    kmmBackend.crosshairEnabled = enabled

                pKmmControl.isCrosshairEnabled = enabled
                pCrosshairItem.fChangeCrosshairImage(enabled)

            }
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }

        function onExtraProp3CustomImgChanged(imagePath) {
            pKmmControl.crosshairType = "Custom"
            if(imagePath !== "") {
                kmmBackend.saveCrosshairImageInTempPath(imagePath)
            }
            pKmmControl.crosshairImage = imagePath.slice(imagePath.lastIndexOf("/")+1)
            pKmmControl.isCrosshairEnabled = true
            pCrosshairItem.fLoadControlProp(pGamepadTabSelected)
        }

        function onExtraProp3ImgChanged(imageType) {
            var isTypeChanged = imageType !== pKmmControl.crosshairType ? true : false
            kmmBackend.setCrosshairTypeChanged(imageType, isTypeChanged)
            pKmmControl.crosshairType = imageType
            pKmmControl.isCrosshairEnabled = true
        }

        function onExtraProp3ColorChanged(selectedColor) {
            pKmmControl.crosshairColor = selectedColor
        }
    }

    Connections {
        target: pPanLookAroundItem

        function onSettingsBtnClicked(x, y) {
            fOpenSettingsWindow(x, y)
        }
        function onDeleteClicked() {
            kmmBackend.kmmEditorDeleteVisualItem(pPanLookAroundItem)
            pKmmControl.lookAroundX = -1
            pKmmControl.lookAroundY = -1
            pKmmControl.isLookAroundEnabled = false
            pKmmControl.keyLookAround[0] = ""
            pKmmControl.keyLookAround[1] = ""
        }
    }

    Connections {
        target: pPanShootItem

        function onSettingsBtnClicked(x, y) {
            fOpenSettingsWindow(x, y)
        }
        function onDeleteClicked() {
            kmmBackend.kmmEditorDeleteVisualItem(pPanShootItem)
            pKmmControl.lButtonX = -1
            pKmmControl.lButtonY = -1
            pKmmControl.isShootOnClickEnabled = false
        }
    }

    Connections {
        target: pCrosshairItem

        function onSettingsBtnClicked(x, y) {
            fOpenSettingsWindow(x, y, true)
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
                    pKmmControl.sensitivity = pKmmControl.sensitivity + 0.05
                    pKmmControl.sensitivityRatioY = parseFloat(iSensiYText.text) / pKmmControl.sensitivity
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
                    pKmmControl.sensitivity = pKmmControl.sensitivity - 0.05
                    if (pKmmControl.sensitivity < 0.0 || pKmmControl.sensitivity === Number.Nan)
                        pKmmControl.sensitivity = 0

                    if (pKmmControl.sensitivity === 0)
                        pKmmControl.sensitivityRatioY = 0
                    else
                        pKmmControl.sensitivityRatioY = parseFloat(iSensiYText.text) / pKmmControl.sensitivity
                }
            }
        }

        Item {
            Layout.preferredWidth: (iBaseImg.width + iTextEditField.width) > 48 ? (iBaseImg.width - 30 + iTextEditField.width): 48
            Layout.fillHeight: true

            UiImage {
                id: iBaseImg
                asset: "EditorPan"
                pImageWidth: iRoot.height - 6
                pImageHeight: iRoot.height - 6
                anchors.centerIn: parent

                MouseArea {
                    id: iBaseImgMA
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: function(mouse) {
                        globalPos = iRoot.mapToGlobal(0, 0)
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

                        if (iRoot.mapToGlobal(0,0).x === globalPos.x &&
                                iRoot.mapToGlobal(0,0).y === globalPos.y) {
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
                    onPressed: function(mouse) {
                        mouse.accepted = false
                    }
                    cursorShape: containsMouse ? iBaseImgMA.pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor : Qt.ArrowCursor
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
                        onClicked: fDeleteVisualItem()
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
                    if(pEditorWindowGamepadTabSelected) pKmmControl.keyStartStop[1] = key
                    else pKmmControl.keyStartStop[0] = key
                }
                onPEditFieldActiveFocusChanged: if(pEditFieldActiveFocus) kmmBackend.setFocusedVisualItem(iRoot)
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
                    var y = (parseFloat(iSensiYText.text) + 0.05).toFixed(2)
                    pKmmControl.sensitivityRatioY = y / pKmmControl.sensitivity
                    iSensiYText.text = y
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
                    var y = (parseFloat(iSensiYText.text) - 0.05).toFixed(2)
                    if (y < 0.0 || y === Number.NaN)
                        y = 0
                    pKmmControl.sensitivityRatioY = y / pKmmControl.sensitivity
                    iSensiYText.text = y
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

    function fLoadControlProp(gamepadTabSelected) {
        iRoot.x = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.x
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.y
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })
        iSensiXText.text = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.sensitivity
            return parseFloat(val).toFixed(2)
        })
        iSensiYText.text = (pKmmControl.sensitivity * pKmmControl.sensitivityRatioY).toFixed(2)
        iTextEditField.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyStartStop[1] : pKmmControl.keyStartStop[0]))

        if(pKmmControl.lookAroundX !== -1 && pKmmControl.lookAroundY !== -1) {
            fCreatePanLookAroundItem()
        }

        if(pKmmControl.lButtonX !== -1 && pKmmControl.lButtonY !== -1) {
            fCreatePanShootItem()
        }

        fCreateCrosshairItem()

        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreatePanShootItem() {
        if (pPanShootItem === null) {
            pPanShootItem = kmmBackend.kmmEditorCreateVisualItem("PanShoot", pKmmControl)
        }
    }

    function fCreatePanLookAroundItem() {
        if (pPanLookAroundItem === null) {
            pPanLookAroundItem = kmmBackend.kmmEditorCreateVisualItem("PanLookAround", pKmmControl)
        }
    }

    function fCreateCrosshairItem() {
        if (pCrosshairItem === null) {
            pCrosshairItem = kmmBackend.kmmEditorCreateVisualItem("Crosshair", pKmmControl)
        }
    }

    function fOpenSettingsWindow(x = undefined, y = undefined, isCrosshair = false) {
        if (pSettingsWin !== null) {
            pSettingsWin.destroy()
        }

        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "Aim, pan & shoot settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOverlay,
                                                 "pExtraProp1Header": qsTranslate("QObject", "Fire with left click"),
                                                 "pExtraProp1Image": "MouseLeftClick",
                                                 "pExtraProp1Text": qsTranslate("QObject", "Place the fire icon in the fire weapon control on the screen"),
                                                 "pExtraProp1TogglePresent": true,
                                                 "pExtraProp1ToggleOn": pKmmControl.isShootOnClickEnabled,
                                                 "pExtraProp1ToggleKey": qsTranslate("QObject", "Shoot on click enabled"),
                                                 "pExtraProp2Header": qsTranslate("QObject", "Look around mode"),
                                                 "pExtraProp2Image": "FreeLook",
                                                 "pExtraProp2TogglePresent": true,
                                                 "pExtraProp2ToggleOn": pKmmControl.isLookAroundEnabled,
                                                 "pExtraProp2ToggleKey": qsTranslate("QObject", "Free look enabled"),
                                                 "pExtraProp2Text": qsTranslate("QObject", "Place the eye icon on the look around control if available"),
                                                 "pExtraProp3Header": qsTranslate("QObject", "Crosshair"),
                                                 "pExtraProp3TogglePresent": true,
                                                 "pExtraProp3ToggleOn": pKmmControl.isCrosshairEnabled,
                                                 "pExtraProp3ToggleKey": qsTranslate("QObject", "Crosshair enabled"),
                                                 "pExtraProp3ImgPathText": pKmmControl.crosshairImage === "" ? "" : "(" + pKmmControl.crosshairImage + ")",
                                                 "pExtraProp3ComboBoxIdx": pKmmControl.crosshairType,
                                                 "pEnableCondition": pKmmControl.enableCondition,
                                                 "pStartCondition": pKmmControl.startCondition,
                                                 "pShowCrosshairSection": isCrosshair})

            if (pSettingsWin !== null) {
                pGamepadTabSelected = pEditorWindowGamepadTabSelected
                fLoadKeyPropertiesMap()
                if (x !== undefined && y !== undefined) {
                    pSettingsWin.x = x
                    pSettingsWin.y = y
                }
                else {
                    pSettingsWin.x = iSettingsBtn.mapToGlobal(0, 0).x
                    pSettingsWin.y = iRoot.mapToGlobal(0, 0).y + iRoot.height
                }

                pSettingsWin.show()

                if (pPanLookAroundItem) {
                    pPanLookAroundItem.pSettingsWindowVisible = true
                }
                if (pPanShootItem) {
                    pPanShootItem.pSettingsWindowVisible = true
                }
                if(pCrosshairItem) {
                    pCrosshairItem.pSettingsWindowVisible = true
                }
            }
        }
        kmmBackend.setFocusedVisualItem(null)
    }

    function fLoadKeyPropertiesMap() {
        var keyBindingsMap = {
            "Start/Stop": {"keyName": qsTranslate("QObject", "Start/Stop"), "keyValue": pGamepadTabSelected ? pKmmControl.keyStartStop[1] : pKmmControl.keyStartStop[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "Suspend": {"keyName": qsTranslate("QObject", "Suspend"), "keyValue": pGamepadTabSelected ? pKmmControl.keySuspend[1] : pKmmControl.keySuspend[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Mouse sensitivity X": {"keyName": qsTranslate("QObject", "Mouse sensitivity X"), "keyValue": parseFloat(pKmmControl.sensitivity).toFixed(2), "keyType": KmmEditorControlSettings.InputType.Double},
            "Mouse sensitivity Y": {"keyName": qsTranslate("QObject", "Mouse sensitivity Y"), "keyValue": (pKmmControl.sensitivity * pKmmControl.sensitivityRatioY).toFixed(2), "keyType": KmmEditorControlSettings.InputType.Double},
            "Tweaks": {"keyName": qsTranslate("QObject", "Tweaks"), "keyValue": pKmmControl.tweaks, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Mouse acceleration": {"keyName": qsTranslate("QObject", "Mouse acceleration"), "keyValue": pKmmControl.mouseAcceleration, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Gamepad stick": {"keyName": qsTranslate("QObject", "Gamepad stick"), "keyValue": pKmmControl.gamepadStick, "keyType": KmmEditorControlSettings.InputType.Key , "keyVisible": pGamepadTabSelected ? "true" : "false"}
        }

        var freeLookMap = {
            "Free look position X": {"keyName": qsTranslate("QObject", "Free look position X"), "keyValue": pKmmControl.lookAroundX, "keyType": KmmEditorControlSettings.InputType.Double},
            "Free look position Y": {"keyName": qsTranslate("QObject", "Free look position Y"), "keyValue": pKmmControl.lookAroundY, "keyType": KmmEditorControlSettings.InputType.Double},
            "Free look": {"keyName": qsTranslate("QObject", "Free look"), "keyValue": pGamepadTabSelected ? pKmmControl.keyLookAround[1] : pKmmControl.keyLookAround[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }

        var shootOnClickMap = {
            "Action position X": {"keyName": qsTranslate("QObject", "Action position X"), "keyValue": pKmmControl.lButtonX, "keyType": KmmEditorControlSettings.InputType.Double},
            "Action position Y": {"keyName": qsTranslate("QObject", "Action position Y"), "keyValue": pKmmControl.lButtonY, "keyType": KmmEditorControlSettings.InputType.Double},
            "Action": {"keyName": qsTranslate("QObject", "Action"), "keyValue": pKmmControl.keyAction, "keyType": KmmEditorControlSettings.InputType.Key, "keyEnabled": "false"}
        }

        var devPropertiesMap = {
            "Gamepad sensitivity": {"keyName": qsTranslate("QObject", "Gamepad sensitivity"), "keyValue": pKmmControl.gamepadSensitivity, "keyType": KmmEditorControlSettings.InputType.Double},
            "Tap X": {"keyName": qsTranslate("QObject", "Tap X"), "keyValue": pKmmControl.tapX, "keyType": KmmEditorControlSettings.InputType.Double},
            "Tap Y": {"keyName": qsTranslate("QObject", "Tap Y"), "keyValue": pKmmControl.tapY, "keyType": KmmEditorControlSettings.InputType.Double},
            "Deadzone radius": {"keyName": qsTranslate("QObject", "Deadzone radius"), "keyValue": pKmmControl.deadzoneRadius, "keyType": KmmEditorControlSettings.InputType.Double},
            "Speed": {"keyName": qsTranslate("QObject", "Speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Left": {"keyName": qsTranslate("QObject", "Left"), "keyValue": pKmmControl.panLeft, "keyType": KmmEditorControlSettings.InputType.Double},
            "Right": {"keyName": qsTranslate("QObject", "Right"), "keyValue": pKmmControl.panRight, "keyType": KmmEditorControlSettings.InputType.Double},
            "Top": {"keyName": qsTranslate("QObject", "Top"), "keyValue": pKmmControl.panTop, "keyType": KmmEditorControlSettings.InputType.Double},
            "Bottom": {"keyName": qsTranslate("QObject", "Bottom"), "keyValue": pKmmControl.panBottom, "keyType": KmmEditorControlSettings.InputType.Double},
            "Activation time in ms": {"keyName": qsTranslate("QObject", "Activation time in ms"), "keyValue": pKmmControl.activationTimeMs, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Wait after shoot time": {"keyName": qsTranslate("QObject", "Wait after shoot time"), "keyValue": pKmmControl.waitAfterShootTime, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Crosshair X": {"keyName": qsTranslate("QObject", "Crosshair X"), "keyValue": pKmmControl.crosshairX, "keyType": KmmEditorControlSettings.InputType.Double},
            "Crosshair Y": {"keyName": qsTranslate("QObject", "Crosshair Y"), "keyValue": pKmmControl.crosshairY, "keyType": KmmEditorControlSettings.InputType.Double},
            "XExpr": {"keyName": qsTranslate("QObject", "XExpr"), "keyValue": pKmmControl.xExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "YExpr": {"keyName": qsTranslate("QObject", "YExpr"), "keyValue": pKmmControl.yExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "XOverlayOffset": {"keyName": qsTranslate("QObject", "XOverlayOffset"), "keyValue": pKmmControl.xOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "YOverlayOffset": {"keyName": qsTranslate("QObject", "YOverlayOffset"), "keyValue": pKmmControl.yOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "LButton XExpr": {"keyName": qsTranslate("QObject", "LButton XExpr"), "keyValue": pKmmControl.lButtonXExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "LButton YExpr": {"keyName": qsTranslate("QObject", "LButton YExpr"), "keyValue": pKmmControl.lButtonYExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "LButton XOverlayOffset": {"keyName": qsTranslate("QObject", "LButton XOverlayOffset"), "keyValue": pKmmControl.lButtonXOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "LButton YOverlayOffset": {"keyName": qsTranslate("QObject", "LButton YOverlayOffset"), "keyValue": pKmmControl.lButtonYOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "LookAround XExpr": {"keyName": qsTranslate("QObject", "LookAround XExpr"), "keyValue": pKmmControl.lookAroundXExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "LookAround YExpr": {"keyName": qsTranslate("QObject", "LookAround YExpr"), "keyValue": pKmmControl.lookAroundYExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "LookAround XOverlayOffset": {"keyName": qsTranslate("QObject", "LookAround XOverlayOffset"), "keyValue": pKmmControl.lookAroundXOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "LookAround YOverlayOffset": {"keyName": qsTranslate("QObject", "LookAround YOverlayOffset"), "keyValue": pKmmControl.lookAroundYOverlayOffset, "keyType": KmmEditorControlSettings.InputType.String},
            "LButton show on overlay expr": {"keyName": qsTranslate("QObject", "LButton show on overlay expr"), "keyValue": pKmmControl.lButtonShowOnOverlayExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "LookAround show on overlay expr": {"keyName": qsTranslate("QObject", "LookAround show on overlay expr"), "keyValue": pKmmControl.lookAroundShowOnOverlayExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "Crosshair show on overlay expr": {"keyName": qsTranslate("QObject", "Crosshair show on overlay expr"), "keyValue": pKmmControl.crosshairShowOnOverlayExpr, "keyType": KmmEditorControlSettings.InputType.String},
            "Exclusive": {"keyName": qsTranslate("QObject", "Exclusive"), "keyValue": pKmmControl.exclusive, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Exclusive delay": {"keyName": qsTranslate("QObject", "Exclusive delay"), "keyValue": pKmmControl.exclusiveDelay, "keyType": KmmEditorControlSettings.InputType.Integer}
        }

        var crosshairMap = {
            "Size": {"keyName": qsTranslate("QObject", "Size"), "keyValue": pKmmControl.crosshairSize, "keyType": KmmEditorControlSettings.InputType.Double},
            "Opacity (Changes appear in the app. Is unaffected by the opacity of on-screen controls.)": {"keyName": qsTranslate("QObject", "Opacity (Changes appear in the app. Is unaffected by the opacity of on-screen controls.)"), "keyValue": pKmmControl.crosshairOpacity, "keyType": KmmEditorControlSettings.InputType.Double},
            "Color": {"keyName": qsTranslate("QObject", "Color"), "keyValue": pKmmControl.crosshairColor, "keyType": KmmEditorControlSettings.InputType.String}
        }

        pSettingsWin.fLoadModel(keyBindingsMap, propertiesMap, shootOnClickMap, freeLookMap, devPropertiesMap, crosshairMap, pCrosshairTypeModel)
    }

    function fDeleteVisualItem() {
        if (pPanShootItem) {
            kmmBackend.kmmEditorDeleteVisualItem(pPanShootItem)
        }
        if (pPanLookAroundItem) {
            kmmBackend.kmmEditorDeleteVisualItem(pPanLookAroundItem)
        }
        if (pCrosshairItem) {
            kmmBackend.kmmEditorDeleteVisualItem(pCrosshairItem)
            kmmBackend.crosshairEnabled = false
            kmmBackend.setCrosshairTypeChanged("", false)
        }

        Utils.deleteControl(iRoot, pKmmControl)
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

        pKmmControl.keyStartStop[1] = key
        iTextEditField.fSetText(kmmBackend.getKeyList(key))
    }

    function fShowCrosshairPopup() {
        if(pKmmControl != null && !pKmmControl.isCrosshairEnabled && !uiBackend.confGetBool("bst.crosshair_onboarding_popup_shown")) {
            pCrosshairItem.fOpenCrosshairInfoPopup()
            uiBackend.confSetBool("bst.crosshair_onboarding_popup_shown", true)
            uiBackend.confCommit()
        }
    }
}
