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
    property var pStartingMouse: undefined
    property bool pLoaded: false
    property string pComposedKey: ""
    property string pType: "TapRepeat"
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
            pKmmControl.count = updatedPropsMap[qsTranslate("QObject", "Count")]
            pKmmControl.repeatUntilKeyUp = updatedPropsMap[qsTranslate("QObject", "Repeat until key up")]
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
            fSetTapRepeatDelay()
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
        id: iTapRepeatBtnGrp
    }

    UiRowLayout {
        id: iRowLayout
        spacing: 0
        padding: 0
        anchors.topMargin: 6

        UiColumnLayout {
            id: iStepperCol

            Layout.preferredHeight: childrenRect.height
            Layout.alignment: Qt.AlignTop

            UiImageButton {
                asset: "StepUp"
                pImageWidth: 16
                pImageHeight: 16
                Layout.alignment: Qt.AlignHCenter
                onClicked: {
                    pKmmControl.count += 1
                    fSetTapRepeatDelay()
                }
            }

            Text {
                id: iRepeatText
                color: UiTheme.colors.primary10
                font: UiTheme.fonts.bodyExtraSmall
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: -5
            }

            UiImageButton {
                asset: "StepDown"
                pImageWidth: 16
                pImageHeight: 16
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: -5
                onClicked: {
                    pKmmControl.count -= 1
                    fSetTapRepeatDelay()
                }
            }
            Item {
                Layout.fillHeight: true
            }
        }


        Item {
            Layout.preferredWidth: Math.max(iTextEditField.width, 48)
            Layout.fillHeight: true

            Rectangle {
                id: iBaseRect
                width: 38
                height: width
                anchors.centerIn: parent
                color: "#00000000"
                border.color: UiTheme.colors.accentNormal
                border.width: 1
                radius: width * 0.5
                property var globalPos
                Rectangle {
                    width: 26
                    height: width
                    radius: width / 2
                    anchors.centerIn: parent
                }
                MouseArea {
                    id: iBaseRectMA
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onPressed: function(mouse) {
                        iBaseRect.globalPos = iBaseRect.mapToGlobal(0,0)
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

                        if (iBaseRect.mapToGlobal(0,0).x === iBaseRect.globalPos.x &&
                                iBaseRect.mapToGlobal(0,0).y === iBaseRect.globalPos.y) {
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
                    cursorShape: containsMouse ? iBaseRectMA.pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor : Qt.ArrowCursor
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
                        onClicked: fOpenTapRepeatSettingsPopup()
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
        }
    }

    Component {
        id: iTapRepeatPopupComponent

        UiHelpPopup {
            id: iTapRepeatPopup
            contentWidth: 150
            visible: false
            x: iSettingsBtn.mapToGlobal(0, 0).x - (width - iSettingsBtn.width) / 2
            y: iSettingsBtn.mapToGlobal(0, 0).y - height
            onWidthChanged: {
                iTapRepeatPopup.x = iSettingsBtn.mapToGlobal(0, 0).x - (iTapRepeatPopup.width - iSettingsBtn.width)/2
            }

            pInnerLayout {
                border.width: 1
                border.color: UiTheme.colors.primary60
                color: UiTheme.colors.primary70
                padding: 8
                spacing: 8
            }

            Text {
                text: qsTranslate("QObject", "Trigger action on")
                font: UiTheme.fonts.titleExtraSmall
                color: UiTheme.colors.primary10
                Layout.alignment: Qt.AlignTop
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }

            UiColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                UiRadioButton {
                    id: iTapBtn
                    text: qsTranslate("QObject", "Tap")
                    ButtonGroup.group: iTapRepeatBtnGrp
                    Layout.fillWidth: true
                    onCheckedChanged: if(checked) pKmmControl.repeatUntilKeyUp = false
                }
                UiRadioButton {
                    id: iLongPressBtn
                    text: qsTranslate("QObject", "Long press")
                    Layout.fillWidth: true
                    ButtonGroup.group: iTapRepeatBtnGrp
                    onCheckedChanged: if(checked) pKmmControl.repeatUntilKeyUp = true
                }
            }

            Text {
                id: iMoreSettingsTxt
                text: qsTranslate("QObject", "More settings")
                font: UiTheme.fonts.bodySmall
                color: UiTheme.colors.accentNormal
                Layout.topMargin: 6
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
                if(pKmmControl.repeatUntilKeyUp)
                    iLongPressBtn.checked = true
                else
                    iTapBtn.checked = true
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
                                             {"pTitle": qsTranslate("QObject", "Repeated Tap settings"),
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
            "Key": {"keyName": qsTranslate("QObject", "Key"), "keyValue": pGamepadTabSelected ? pKmmControl.key[1] : pKmmControl.key[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Count": {"keyName": qsTranslate("QObject", "Count"), "keyValue": pKmmControl.count, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Repeat until key up": {"keyName": qsTranslate("QObject", "Repeat until key up"), "keyValue": pKmmControl.repeatUntilKeyUp, "keyType": KmmEditorControlSettings.InputType.Boolean}
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

    function fOpenTapRepeatSettingsPopup() {
        var tapRepeatWin = iTapRepeatPopupComponent.createObject(iRoot.parent)
        tapRepeatWin.show()
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
        iRepeatText.text = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.count
            return val
        })
        iTextEditField.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.key[1] : pKmmControl.key[0]))
        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode(){
        iTextEditField.fInEditableMode()
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !iTextEditField.pEditFieldActiveFocus)
            return
        pKmmControl.key[1] = key
        iTextEditField.fSetText(kmmBackend.getKeyList(key))
    }

    function fSetTapRepeatDelay() {
        if(pKmmControl.count < 1) pKmmControl.count = 1
        pKmmControl.delay =  1000 / (2 * pKmmControl.count)
    }
}
