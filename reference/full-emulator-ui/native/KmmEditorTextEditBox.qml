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
import UiHelper 1.0

import "KmmUtils.js" as Utils

//Size is fixed for this control
//Change color(default = white) and location

Rectangle {
    id: iRoot
    width: iEditField.width
    height: 18
    radius: 9
    z: 2

    property string pComposedKey: ""
    property string pLastComposedKey: ""

    //This property is to focus edit field when mouse is pressed
    //For single key controls this is not required
    property alias pEnableMouseArea: iFocusMouseArea.enabled
    property alias pEditFieldActiveFocus: iEditField.activeFocus

    signal updateKey(string key);

    Text {
        id: iTextField
        color: UiTheme.colors.primary90
        font: UiTheme.fonts.bodySmall
        anchors.centerIn: parent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: iEditField.text
    }

    UiTextField {
        id: iEditField
        pSize: UiTheme.textFields.sizes.extraSmall
        visible: !iTextField.visible
        anchors.centerIn: parent
        font.capitalization: Font.AllUppercase
        validator: RegularExpressionValidator { regularExpression: RegExp("[a-zA-Z0-9]+") }
        UiHelper.inputMethodEditing: false
        onTextChanged: {
            if (iEditField.text === "MouseRButton") {
                iEditField.text = qsTranslate("QObject", "Right Click")
            }
            else if (iEditField.text === "MouseLButton") {
                iEditField.text = qsTranslate("QObject", "Left Click")
            }
            else if (iEditField.text === "MouseMButton") {
                iEditField.text = qsTranslate("QObject", "Middle Click")
            }
            else if (iEditField.text === "MouseXButton1") {
                iEditField.text = qsTranslate("QObject", "X button1")
            }
            else if (iEditField.text === "MouseXButton2") {
                iEditField.text = qsTranslate("QObject", "X button2")
            }
        }
        onActiveFocusChanged: {
            if (!activeFocus) {
                iTextField.visible = true
            }
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Tab) {
                KeyNavigation.tab = this
            }

            Utils.keyPressed(event, iRoot)
            if (!kmmBackend.gamepadTabSelected || event.key === Qt.Key_Backspace) {
                updateKey("")
                iEditField.text = ""
            }
        }

        Keys.onReleased: function(event) {
            event.accepted = true

            if (event.isAutoRepeat || kmmBackend.gamepadTabSelected)
                return

            if (pComposedKey !== "") {
                updateKey(pComposedKey)
                iEditField.text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                pLastComposedKey = pComposedKey
            }
            if(pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                updateKey(pLastComposedKey)
                iEditField.text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                pLastComposedKey = ""
            }

            pComposedKey = ""
            event.accepted = true
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2
            onWheel: function(wheel) {
                if(kmmBackend.gamepadTabSelected)
                    return

                if(wheel.angleDelta.y > 0){
                    iEditField.text = qsTranslate("QObject", "MouseWheelUp")
                    updateKey("MouseWheelUp")
                }
                else{
                    iEditField.text = qsTranslate("QObject", "MouseWheelDown")
                    updateKey("MouseWheelDown")
                }
            }

            onPressed: function(mouse) {
                if(kmmBackend.gamepadTabSelected)
                    return

                if(mouse.button & Qt.RightButton){
                    iEditField.text = qsTranslate("QObject", "Right Click")
                    updateKey("MouseRButton")
                }
                else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                    iEditField.text = qsTranslate("QObject", "Middle Click")
                    updateKey("MouseMButton")
                }
                else if(mouse.button & Qt.XButton1){
                    iEditField.text = qsTranslate("QObject", "X button1")
                    updateKey("MouseXButton1")
                }
                else if(mouse.button & Qt.XButton2){
                    iEditField.text = qsTranslate("QObject", "X button2")
                    updateKey("MouseXButton2")
                }
                pComposedKey = ""
            }
        }
    }

    MouseArea {
        id: iFocusMouseArea
        anchors.fill: parent
        enabled: false
        onPressed: fInEditableMode()
    }

    function fInEditableMode() {
        iTextField.visible = false
        iEditField.forceActiveFocus()
    }

    function fSetText(text) {
        iEditField.text = text
    }
}
