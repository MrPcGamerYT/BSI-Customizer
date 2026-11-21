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

Item {
    id: iRoot
    width: 36
    height: width
    z: 1

    property QtObject pKmmControl: null
    property QtObject pKmmControlUp: null
    property QtObject pKmmControlDown: null
    property QtObject pKmmControlLeft: null
    property QtObject pKmmControlRight: null
    property real pRadius: 15
    property var pStartingMouse
    property bool pLoaded: false
    property string pType: "Swipe"

    property string pComposedKey: ""
    property string pLastComposedKey: ""

    property real pResizeUnit: (26 * plrBackend.glWinRect[2]) / 1280
    property bool pGamepadTabSelected: false
    property UiTextField pFocusedTextField: null
    property string pFocusedProperty : ""
    property bool pEditorWindowGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null

    onWidthChanged: {
        if (pKmmControlUp != null)
            pKmmControlUp.y2 = (pKmmControlUp.y1 - pRadius).toFixed(2)
        if (pKmmControlDown != null)
            pKmmControlDown.y2 = (pKmmControlDown.y1 + pRadius).toFixed(2)
        if (pKmmControlLeft != null)
            pKmmControlLeft.x2 = (pKmmControlLeft.x1 - pRadius).toFixed(2)
        if (pKmmControlRight != null)
            pKmmControlRight.x2 = (pKmmControlLeft.x1 + pRadius).toFixed(2)
    }

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var index = pGamepadTabSelected ? 1 : 0
            var x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            var y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            if (pKmmControlUp != null) {
                pKmmControlUp.key[index] = updatedPropsMap[qsTranslate("QObject", "Swipe up")]
                pKmmControlUp.x1 = x
                pKmmControlUp.x2 = x
                pKmmControlUp.y1 = y
                pKmmControlUp.y2 = (y - parseFloat(updatedPropsMap[qsTranslate("QObject", "Radius")])).toFixed(2)
                pKmmControlUp.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
                pKmmControlUp.hold = updatedPropsMap[qsTranslate("QObject", "Swipe & hold")]
                pKmmControlUp.keyUpStop = updatedPropsMap[qsTranslate("QObject", "Stop on key up")]
                pKmmControlUp.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
                pKmmControlUp.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
                pKmmControlUp.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
                pKmmControlUp.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
                pKmmControlUp.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
                pKmmControlUp.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
                pKmmControlUp.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
                pKmmControlUp.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
                pKmmControlUp.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]
                pKmmControlUp.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
                iEditFieldUp.text = kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControlUp.key[1] : pKmmControlUp.key[0])
            }
            if (pKmmControlDown != null) {
                pKmmControlDown.key[index] = updatedPropsMap[qsTranslate("QObject", "Swipe down")]
                pKmmControlDown.x1 = x
                pKmmControlDown.x2 = x
                pKmmControlDown.y1 = y
                pKmmControlDown.y2 = (y + parseFloat(updatedPropsMap[qsTranslate("QObject", "Radius")])).toFixed(2)
                pKmmControlDown.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
                pKmmControlDown.hold = updatedPropsMap[qsTranslate("QObject", "Swipe & hold")]
                pKmmControlDown.keyUpStop = updatedPropsMap[qsTranslate("QObject", "Stop on key up")]
                pKmmControlDown.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
                pKmmControlDown.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
                pKmmControlDown.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
                pKmmControlDown.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
                pKmmControlDown.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
                pKmmControlDown.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
                pKmmControlDown.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
                pKmmControlDown.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
                pKmmControlDown.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]
                pKmmControlDown.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
                iEditFieldDown.text = kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControlDown.key[1] : pKmmControlDown.key[0])
            }
            if (pKmmControlLeft != null) {
                pKmmControlLeft.key[index] = updatedPropsMap[qsTranslate("QObject", "Swipe left")]
                pKmmControlLeft.x1 = x
                pKmmControlLeft.x2 = (x - parseFloat(updatedPropsMap[qsTranslate("QObject", "Radius")])).toFixed(2)
                pKmmControlLeft.y1 = y
                pKmmControlLeft.y2 = y
                pKmmControlLeft.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
                pKmmControlLeft.hold = updatedPropsMap[qsTranslate("QObject", "Swipe & hold")]
                pKmmControlLeft.keyUpStop = updatedPropsMap[qsTranslate("QObject", "Stop on key up")]
                pKmmControlLeft.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
                pKmmControlLeft.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
                pKmmControlLeft.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
                pKmmControlLeft.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
                pKmmControlLeft.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
                pKmmControlLeft.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
                pKmmControlLeft.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
                pKmmControlLeft.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
                pKmmControlLeft.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]
                pKmmControlLeft.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
                iEditFieldLeft.text = kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControlLeft.key[1] : pKmmControlLeft.key[0])
            }
            if (pKmmControlRight != null) {
                pKmmControlRight.key[index] = updatedPropsMap[qsTranslate("QObject", "Swipe right")]
                pKmmControlRight.x1 = x
                pKmmControlRight.x2 = (x + parseFloat(updatedPropsMap[qsTranslate("QObject", "Radius")])).toFixed(2)
                pKmmControlRight.y1 = y
                pKmmControlRight.y2 = y
                pKmmControlRight.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
                pKmmControlRight.hold = updatedPropsMap[qsTranslate("QObject", "Swipe & hold")]
                pKmmControlRight.keyUpStop = updatedPropsMap[qsTranslate("QObject", "Stop on key up")]
                pKmmControlRight.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
                pKmmControlRight.xExpr = updatedPropsMap[qsTranslate("QObject", "XExpr")]
                pKmmControlRight.yExpr = updatedPropsMap[qsTranslate("QObject", "YExpr")]
                pKmmControlRight.xOverlayOffset = updatedPropsMap[qsTranslate("QObject", "XOverlayOffset")]
                pKmmControlRight.yOverlayOffset = updatedPropsMap[qsTranslate("QObject", "YOverlayOffset")]
                pKmmControlRight.exclusive = updatedPropsMap[qsTranslate("QObject", "Exclusive")]
                pKmmControlRight.exclusiveDelay = updatedPropsMap[qsTranslate("QObject", "Exclusive delay")]
                pKmmControlRight.startCondition = updatedPropsMap[qsTranslate("QObject", "Start condition")]
                pKmmControlRight.enableCondition = updatedPropsMap[qsTranslate("QObject", "Enable condition")]
                pKmmControlRight.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
                iEditFieldRight.text = kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControlRight.key[1] : pKmmControlRight.key[0])
            }
            pRadius = updatedPropsMap[qsTranslate("QObject", "Radius")]
        }

        function onDeleteControl() {
            fDeleteSwipeControls()
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }
    }

    KmmEditorBaseImageRect {
        pImgSrc: "EditorSwipe"
        onDeleteClicked: fDeleteSwipeControls()
        onSettingsClicked: fOpenSettingsWindow()
        onItemMoved: function(startingMouse, currentMouse) {
            Utils.moveItem(pKmmControl, iRoot.x, iRoot.y, iRoot.width, iRoot.height, startingMouse, currentMouse, "Swipe")
            fSetControlsPosition()
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
                    if ((iRoot.width + mouseX) > (pResizeUnit * 2)) {
                        pRadius = ((iRoot.width + mouseX) / pResizeUnit).toFixed(2)
                    }
                }
            }
            onMouseYChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseY) > (pResizeUnit * 2)) {
                        pRadius = ((iRoot.width + mouseY) / pResizeUnit).toFixed(2)
                    }
                }
            }
        }
    }

    Rectangle {
        id: iEditRectUp
        width: Math.max(iEditFieldUp.width, 18)
        height: 18
        radius: 9
        color: "#FFFFFFFF"
        x: iRoot.width / 2 - width / 2
        y: (iRoot.height * 0.02) - height / 2

        Text {
            id: iTextFieldUp
            color: UiTheme.colors.primary90
            font: UiTheme.fonts.bodySmall
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: iEditFieldUp.text
        }

        UiTextField {
            id: iEditFieldUp
            anchors.centerIn: parent
            pSize: UiTheme.textFields.sizes.extraSmall
            visible: !iTextFieldUp.visible
            font.capitalization: Font.AllUppercase
            validator: RegularExpressionValidator { regularExpression: RegExp("[a-zA-Z0-9]+") }
            UiHelper.inputMethodEditing: false
            onTextChanged: {
                if (iEditFieldUp.text === "MouseRButton") {
                    iEditFieldUp.text = qsTranslate("QObject", "Right Click")
                }
                else if (iEditFieldUp.text === "MouseLButton") {
                    iEditFieldUp.text = qsTranslate("QObject", "Left Click")
                }
                else if (iEditFieldUp.text === "MouseMButton") {
                    iEditFieldUp.text = qsTranslate("QObject", "Middle Click")
                }
                else if (iEditFieldUp.text === "MouseXButton1") {
                    iEditFieldUp.text = qsTranslate("QObject", "XButton1 Click")
                }
                else if (iEditFieldUp.text === "MouseXButton2") {
                    iEditFieldUp.text = qsTranslate("QObject", "XButton2 Click")
                }
            }
            onActiveFocusChanged: {
                if (!activeFocus) {
                    iTextFieldUp.visible = true
                }
                else {
                    pFocusedTextField = this
                    pFocusedProperty = "KeyUp"
                    kmmBackend.setFocusedVisualItem(iRoot)
                }
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Tab) {
                    KeyNavigation.tab = this
                }
                if (!pKmmControlUp) {
                    kmmBackend.getNewControl(this, pType + "Up")
                    pKmmControlUp.x1 = pKmmControlUp.x2 = pKmmControl.x1
                    pKmmControlUp.y1 = pKmmControl.y1
                    pKmmControlUp.y2 = (pKmmControl.y1 - pRadius).toFixed(2)
                    pKmmControlUp.speed = pKmmControl.speed
                    pKmmControlUp.hold = pKmmControl.hold
                }
                Utils.keyPressed(event, iRoot)
                pKmmControlUp.key[0] = ""
                pKmmControlUp.key[1] = ""
                iEditFieldUp.text = ""
            }

            Keys.onReleased: function(event) {
                if (pComposedKey !== "") {
                    pKmmControlUp.key[0] = pComposedKey
                    iEditFieldUp.text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                    pLastComposedKey = pComposedKey
                }
                if(pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                    pKmmControlUp.key[0] = pLastComposedKey
                    iEditFieldUp.text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                    pLastComposedKey = ""
                }

                pComposedKey = ""
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2

                onWheel: function(wheel) {
                    if(kmmBackend.gamepadTabSelected)
                        return

                    if(wheel.angleDelta.y > 0){
                        iEditFieldUp.text = qsTranslate("QObject", "MouseWheelUp")
                        pKmmControlUp.key[0] = "MouseWheelUp"
                    }
                    else{
                        iEditFieldUp.text = qsTranslate("QObject", "MouseWheelDown")
                        pKmmControlUp.key[0] = "MouseWheelDown"
                    }
                }

                onPressed: function(mouse) {
                    if(kmmBackend.gamepadTabSelected)
                        return

                    if(mouse.button & Qt.RightButton){
                        iEditFieldUp.text = qsTranslate("QObject", "Right Click")
                        pKmmControlUp.key[0] = "MouseRButton"
                    }
                    else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                        iEditFieldUp.text = qsTranslate("QObject", "Middle Click")
                        pKmmControlUp.key[0] = "MouseMButton"
                    }
                    else if(mouse.button & Qt.XButton1){
                        iEditFieldUp.text = qsTranslate("QObject", "XButton1")
                        pKmmControlUp.key[0] = "MouseXButton1"
                    }
                    else if(mouse.button & Qt.XButton2){
                        iEditFieldUp.text = qsTranslate("QObject", "XButton2")
                        pKmmControlUp.key[0] = "MouseXButton2"
                    }
                    pComposedKey = ""
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onPressed: {
                iTextFieldUp.visible = false
                iEditFieldUp.forceActiveFocus()
            }
        }
    }

    Rectangle {
        id: iEditRectDown
        width: Math.max(iEditFieldDown.width, 18)
        height: 18
        radius: 9
        color: "#FFFFFFFF"
        x: iRoot.width / 2 - width / 2
        y: (iRoot.height * 0.98) - height / 2

        Text {
            id: iTextFieldDown
            color: UiTheme.colors.primary90
            font: UiTheme.fonts.bodySmall
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: iEditFieldDown.text
        }

        UiTextField {
            id: iEditFieldDown
            anchors.centerIn: parent
            pSize: UiTheme.textFields.sizes.extraSmall
            visible: !iTextFieldDown.visible
            font.capitalization: Font.AllUppercase
            validator: RegularExpressionValidator { regularExpression: RegExp("[a-zA-Z0-9]+") }
            UiHelper.inputMethodEditing: false
            onTextChanged: {
                if (iEditFieldDown.text === "MouseRButton") {
                    iEditFieldDown.text = qsTranslate("QObject", "Right Click")
                }
                else if (iEditFieldDown.text === "MouseLButton") {
                    iEditFieldDown.text = qsTranslate("QObject", "Left Click")
                }
                else if (iEditFieldDown.text === "MouseMButton") {
                    iEditFieldDown.text = qsTranslate("QObject", "Middle Click")
                }
                else if (iEditFieldDown.text === "MouseXButton1") {
                    iEditFieldDown.text = qsTranslate("QObject", "XButton1 Click")
                }
                else if (iEditFieldDown.text === "MouseXButton2") {
                    iEditFieldDown.text = qsTranslate("QObject", "XButton2 Click")
                }
            }
            onActiveFocusChanged: {
                if (!activeFocus) {
                    iTextFieldDown.visible = true
                }
                else {
                    pFocusedTextField = this
                    pFocusedProperty = "KeyDown"
                    kmmBackend.setFocusedVisualItem(iRoot)
                }
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Tab) {
                    KeyNavigation.tab = this
                }
                if (!pKmmControlDown) {
                    kmmBackend.getNewControl(this, pType + "Down")
                    pKmmControlDown.x1 = pKmmControlDown.x2 = pKmmControl.x1
                    pKmmControlDown.y1 = pKmmControl.y1
                    pKmmControlDown.y2 = (pKmmControl.y1 + pRadius).toFixed(2)
                    pKmmControlDown.speed = pKmmControl.speed
                    pKmmControlDown.hold = pKmmControl.hold
                }
                Utils.keyPressed(event, iRoot)
                pKmmControlDown.key[0] = ""
                pKmmControlDown.key[1] = ""
                iEditFieldDown.text = ""
            }

            Keys.onReleased: function(event) {
                if (pComposedKey !== "") {
                    pKmmControlDown.key[0] = pComposedKey
                    iEditFieldDown.text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                    pLastComposedKey = pComposedKey
                }
                if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                    pKmmControlDown.key[0] = pLastComposedKey
                    iEditFieldDown.text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                    pLastComposedKey = ""
                }

                pComposedKey = ""
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2

                onWheel: function(wheel) {
                    if(kmmBackend.gamepadTabSelected)
                        return

                    if(wheel.angleDelta.y > 0){
                        iEditFieldDown.text = qsTranslate("QObject", "MouseWheelUp")
                        pKmmControlDown.key[0] = "MouseWheelUp"
                    }
                    else{
                        iEditFieldDown.text = qsTranslate("QObject", "MouseWheelDown")
                        pKmmControlDown.key[0] = "MouseWheelDown"
                    }
                }

                onPressed: function(mouse) {
                    if(kmmBackend.gamepadTabSelected)
                        return

                    if(mouse.button & Qt.RightButton){
                        iEditFieldDown.text = qsTranslate("QObject", "Right Click")
                        pKmmControlDown.key[0] = "MouseRButton"
                    }
                    else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                        iEditFieldDown.text = qsTranslate("QObject", "Middle Click")
                        pKmmControlDown.key[0] = "MouseMButton"
                    }
                    else if(mouse.button & Qt.XButton1){
                        iEditFieldDown.text = qsTranslate("QObject", "XButton1")
                        pKmmControlDown.key[0] = "MouseXButton1"
                    }
                    else if(mouse.button & Qt.XButton2){
                        iEditFieldDown.text = qsTranslate("QObject", "XButton2")
                        pKmmControlDown.key[0] = "MouseXButton2"
                    }
                    pComposedKey = ""
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onPressed: {
                iTextFieldDown.visible = false
                iEditFieldDown.forceActiveFocus()
            }
        }
    }

    Rectangle {
        id: iEditRectLeft
        width: Math.max(iEditFieldLeft.width, 18)
        height: 18
        radius: 9
        color: "#FFFFFFFF"
        x: (iRoot.width * 0.02) - width / 2
        y: iRoot.height / 2 - height / 2

        Text {
            id: iTextFieldLeft
            color: UiTheme.colors.primary90
            font: UiTheme.fonts.bodySmall
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: iEditFieldLeft.text
        }

        UiTextField {
            id: iEditFieldLeft
            anchors.centerIn: parent
            pSize: UiTheme.textFields.sizes.extraSmall
            visible: !iTextFieldLeft.visible
            font.capitalization: Font.AllUppercase
            validator: RegularExpressionValidator { regularExpression: RegExp("[a-zA-Z0-9]+") }
            UiHelper.inputMethodEditing: false
            onTextChanged: {
                if (iEditFieldLeft.text === "MouseRButton") {
                    iEditFieldLeft.text = qsTranslate("QObject", "Right Click")
                }
                else if (iEditFieldLeft.text === "MouseLButton") {
                    iEditFieldLeft.text = qsTranslate("QObject", "Left Click")
                }
                else if (iEditFieldLeft.text === "MouseMButton") {
                    iEditFieldLeft.text = qsTranslate("QObject", "Middle Click")
                }
                else if (iEditFieldLeft.text === "MouseXButton1") {
                    iEditFieldLeft.text = qsTranslate("QObject", "XButton1 Click")
                }
                else if (iEditFieldLeft.text === "MouseXButton2") {
                    iEditFieldLeft.text = qsTranslate("QObject", "XButton2 Click")
                }
            }
            onActiveFocusChanged: {
                if (!activeFocus) {
                    iTextFieldLeft.visible = true
                }
                else {
                    pFocusedTextField = this
                    pFocusedProperty = "KeyLeft"
                    kmmBackend.setFocusedVisualItem(iRoot)
                }
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Tab) {
                    KeyNavigation.tab = this
                }
                if (!pKmmControlLeft) {
                    kmmBackend.getNewControl(this, pType + "Left")
                    pKmmControlLeft.y1 = pKmmControlLeft.y2 = pKmmControl.y1
                    pKmmControlLeft.x1 = pKmmControl.x1
                    pKmmControlLeft.x2 = (pKmmControl.x1 - pRadius).toFixed(2)
                    pKmmControlLeft.speed = pKmmControl.speed
                    pKmmControlLeft.hold = pKmmControl.hold
                }
                Utils.keyPressed(event, iRoot)
                pKmmControlLeft.key[0] = ""
                pKmmControlLeft.key[1] = ""
                iEditFieldLeft.text = ""
            }

            Keys.onReleased: function(event) {
                if (pComposedKey !== "") {
                    pKmmControlLeft.key[0] = pComposedKey
                    iEditFieldLeft.text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                    pLastComposedKey = pComposedKey
                }
                if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                    pKmmControlLeft.key[0] = pLastComposedKey
                    iEditFieldLeft.text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                    pLastComposedKey = ""
                }

                pComposedKey = ""
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2

                onWheel: function(wheel) {
                    if(kmmBackend.gamepadTabSelected)
                        return

                    if(wheel.angleDelta.y > 0){
                        iEditFieldLeft.text = qsTranslate("QObject", "MouseWheelUp")
                        pKmmControlLeft.key[0] = "MouseWheelUp"
                    }
                    else{
                        iEditFieldLeft.text = qsTranslate("QObject", "MouseWheelDown")
                        pKmmControlLeft.key[0] = "MouseWheelDown"
                    }
                }

                onPressed: function(mouse) {
                    if(kmmBackend.gamepadTabSelected)
                        return

                    if(mouse.button & Qt.RightButton){
                        iEditFieldLeft.text = qsTranslate("QObject", "Right Click")
                        pKmmControlLeft.key[0] = "MouseRButton"
                    }
                    else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                        iEditFieldLeft.text = qsTranslate("QObject", "Middle Click")
                        pKmmControlLeft.key[0] = "MouseMButton"
                    }
                    else if(mouse.button & Qt.XButton1){
                        iEditFieldLeft.text = qsTranslate("QObject", "XButton1")
                        pKmmControlLeft.key[0] = "MouseXButton1"
                    }
                    else if(mouse.button & Qt.XButton2){
                        iEditFieldLeft.text = qsTranslate("QObject", "XButton2")
                        pKmmControlLeft.key[0] = "MouseXButton2"
                    }
                    pComposedKey = ""
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onPressed: {
                iTextFieldLeft.visible = false
                iEditFieldLeft.forceActiveFocus()
            }
        }
    }

    Rectangle {
        id: iEditRectRight
        width: Math.max(iEditFieldRight.width, 18)
        height: 18
        radius: 9
        color: "#FFFFFFFF"
        x: (iRoot.width * 0.98) - width / 2
        y: iRoot.height / 2 - height / 2

        Text {
            id: iTextFieldRight
            color: UiTheme.colors.primary90
            font: UiTheme.fonts.bodySmall
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            text: iEditFieldRight.text
        }

        UiTextField {
            id: iEditFieldRight
            anchors.centerIn: parent
            pSize: UiTheme.textFields.sizes.extraSmall
            visible: !iTextFieldRight.visible
            font.capitalization: Font.AllUppercase
            validator: RegularExpressionValidator { regularExpression: RegExp("[a-zA-Z0-9]+") }
            UiHelper.inputMethodEditing: false
            onTextChanged: {
                if (iEditFieldRight.text === "MouseRButton") {
                    iEditFieldRight.text = qsTranslate("QObject", "Right Click")
                }
                else if (iEditFieldRight.text === "MouseLButton") {
                    iEditFieldRight.text = qsTranslate("QObject", "Left Click")
                }
                else if (iEditFieldRight.text === "MouseMButton") {
                    iEditFieldRight.text = qsTranslate("QObject", "Middle Click")
                }
                else if (iEditFieldRight.text === "MouseXButton1") {
                    iEditFieldRight.text = qsTranslate("QObject", "XButton1 Click")
                }
                else if (iEditFieldRight.text === "MouseXButton2") {
                    iEditFieldRight.text = qsTranslate("QObject", "XButton2 Click")
                }
            }
            onActiveFocusChanged: {
                if (!activeFocus) {
                    iTextFieldRight.visible = true
                }
                else {
                    pFocusedTextField = this
                    pFocusedProperty = "KeyRight"
                    kmmBackend.setFocusedVisualItem(iRoot)
                }
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Tab) {
                    KeyNavigation.tab = this
                }
                if (!pKmmControlRight) {
                    kmmBackend.getNewControl(this, pType + "Right")
                    pKmmControlRight.y1 = pKmmControlRight.y2 = pKmmControl.y1
                    pKmmControlRight.x1 = pKmmControl.x1
                    pKmmControlRight.x2 = (pKmmControl.x1 + pRadius).toFixed(2)
                    pKmmControlRight.speed = pKmmControl.speed
                    pKmmControlRight.hold = pKmmControl.hold
                }
                Utils.keyPressed(event, iRoot)
                pKmmControlRight.key[0] = ""
                pKmmControlRight.key[1] = ""
                iEditFieldRight.text = ""
            }

            Keys.onReleased: function(event) {
                if (pComposedKey !== "") {
                    pKmmControlRight.key[0] = pComposedKey
                    iEditFieldRight.text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                    pLastComposedKey = pComposedKey
                }
                if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                    pKmmControlRight.key[0] = pLastComposedKey
                    iEditFieldRight.text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                    pLastComposedKey = ""
                }

                pComposedKey = ""
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2

                onWheel: function(wheel) {
                    if(kmmBackend.gamepadTabSelected)
                        return

                    if(wheel.angleDelta.y > 0){
                        iEditFieldRight.text = qsTranslate("QObject", "MouseWheelUp")
                        pKmmControlRight.key[0] = "MouseWheelUp"
                    }
                    else{
                        iEditFieldRight.text = qsTranslate("QObject", "MouseWheelDown")
                        pKmmControlRight.key[0] = "MouseWheelDown"
                    }
                }

                onPressed: function(mouse) {
                    if(kmmBackend.gamepadTabSelected)
                        return

                    if(mouse.button & Qt.RightButton){
                        iEditFieldRight.text = qsTranslate("QObject", "Right Click")
                        pKmmControlRight.key[0] = "MouseRButton"
                    }
                    else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                        iEditFieldRight.text = qsTranslate("QObject", "Middle Click")
                        pKmmControlRight.key[0] = "MouseMButton"
                    }
                    else if(mouse.button & Qt.XButton1){
                        iEditFieldRight.text = qsTranslate("QObject", "XButton1")
                        pKmmControlRight.key[0] = "MouseXButton1"
                    }
                    else if(mouse.button & Qt.XButton2){
                        iEditFieldRight.text = qsTranslate("QObject", "XButton2")
                        pKmmControlRight.key[0] = "MouseXButton2"
                    }
                    pComposedKey = ""
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onPressed: {
                iTextFieldRight.visible = false
                iEditFieldRight.forceActiveFocus()
            }
        }
    }

    function fLoadControlProp(gamepadTabSelected) {
        if (pKmmControlUp)
            pKmmControl = pKmmControlUp
        else if (pKmmControlDown)
            pKmmControl = pKmmControlDown
        else if (pKmmControlLeft)
            pKmmControl = pKmmControlLeft
        else
            pKmmControl = pKmmControlRight

        iRoot.x = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.x1
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.y1
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })

        if (pKmmControlUp) {
            iEditFieldUp.text = kmmBackend.getKeyList(gamepadTabSelected ? pKmmControlUp.key[1] : pKmmControlUp.key[0])
        }

        if (pKmmControlDown) {
            iEditFieldDown.text = kmmBackend.getKeyList(gamepadTabSelected ? pKmmControlDown.key[1] : pKmmControlDown.key[0])
        }

        if (pKmmControlLeft) {
            iEditFieldLeft.text = kmmBackend.getKeyList(gamepadTabSelected ? pKmmControlLeft.key[1] : pKmmControlLeft.key[0])
        }

        if (pKmmControlRight) {
            iEditFieldRight.text = kmmBackend.getKeyList(gamepadTabSelected ? pKmmControlRight.key[1] : pKmmControlRight.key[0])
        }

        pRadius = (Math.abs(pKmmControl.x1 - pKmmControl.x2)).toFixed(2)
        if (pRadius === 0)
            pRadius = (Math.abs(pKmmControl.y1 - pKmmControl.y2)).toFixed(2)

        if (plrBackend.glWinRect[2] < plrBackend.glWinRect[3])
            pResizeUnit = pResizeUnit * 9.0 / 16

        iRoot.width = Qt.binding(function() {return (pRadius * pResizeUnit)})

        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType + "Up")
        kmmBackend.getNewControl(this, pType + "Down")
        kmmBackend.getNewControl(this, pType + "Left")
        kmmBackend.getNewControl(this, pType + "Right")
        pKmmControlUp.key[0] = "Up"
        pKmmControlDown.key[0] = "Down"
        pKmmControlLeft.key[0] = "Left"
        pKmmControlRight.key[0] = "Right"
        fLoadControlProp(kmmBackend.gamepadTabSelected)
        pRadius = 15
    }

    function fSetControlsPosition() {
        if (pKmmControlUp != null) {
            pKmmControlUp.x1 = pKmmControlUp.x2 = pKmmControl.x1
            pKmmControlUp.y1 = pKmmControl.y1
            pKmmControlUp.y2 = (pKmmControl.y1 - pRadius).toFixed(2)
        }
        if (pKmmControlDown != null) {
            pKmmControlDown.x1 = pKmmControlDown.x2 = pKmmControl.x1
            pKmmControlDown.y1 = pKmmControl.y1
            pKmmControlDown.y2 = (pKmmControl.y1 + pRadius).toFixed(2)
        }
        if (pKmmControlLeft != null) {
            pKmmControlLeft.y1 = pKmmControlLeft.y2 = pKmmControl.y1
            pKmmControlLeft.x1 = pKmmControl.x1
            pKmmControlLeft.x2 = (pKmmControl.x1 - pRadius).toFixed(2)
        }
        if (pKmmControlRight != null) {
            pKmmControlRight.y1 = pKmmControlRight.y2 = pKmmControl.y1
            pKmmControlRight.x1 = pKmmControl.x1
            pKmmControlRight.x2 = (pKmmControl.x1 + pRadius).toFixed(2)
        }
    }

    function fDeleteSwipeControls() {
        if (pKmmControlUp)
            Utils.deleteControl(iRoot, pKmmControlUp)
        if (pKmmControlDown)
            Utils.deleteControl(iRoot, pKmmControlDown)
        if (pKmmControlLeft)
            Utils.deleteControl(iRoot, pKmmControlLeft)
        if (pKmmControlRight)
            Utils.deleteControl(iRoot, pKmmControlRight)
    }

    function fInEditableMode() {
        iTextFieldUp.visible = false
        iEditFieldUp.forceActiveFocus()
    }

    function fOpenSettingsWindow() {
        if (pSettingsWin != null) {
            pSettingsWin.destroy()
        }

        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "Swipe settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOvarlay,
                                                 "pShowOverlaySetting": false,
                                                 "pEnableCondition": pKmmControl.enableCondition,
                                                 "pStartCondition": pKmmControl.startCondition})
            if (pSettingsWin != null) {
                pGamepadTabSelected = kmmBackend.gamepadTabSelected
                fLoadKeyPropertiesMap()
                pSettingsWin.x = iRoot.mapToGlobal(0, 0).x
                pSettingsWin.y = iRoot.mapToGlobal(0, 0).y + iRoot.height
                pSettingsWin.show()
            }
        }
        kmmBackend.setFocusedVisualItem(null)
        pFocusedProperty = ""
    }

    function fLoadKeyPropertiesMap() {
        var index = pGamepadTabSelected ? 1 : 0
        var keyBindingsMap = {
            "Swipe up": {"keyName": qsTranslate("QObject", "Swipe up"), "keyValue": pKmmControlUp ? pKmmControlUp.key[index] : "", "keyType": KmmEditorControlSettings.InputType.Key},
            "Swipe down": {"keyName": qsTranslate("QObject", "Swipe down"), "keyValue": pKmmControlDown ? pKmmControlDown.key[index] : "", "keyType": KmmEditorControlSettings.InputType.Key},
            "Swipe left": {"keyName": qsTranslate("QObject", "Swipe left"), "keyValue": pKmmControlLeft ? pKmmControlLeft.key[index] : "", "keyType": KmmEditorControlSettings.InputType.Key},
            "Swipe right": {"keyName": qsTranslate("QObject", "Swipe right"), "keyValue": pKmmControlRight ? pKmmControlRight.key[index] : "", "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x1, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y1, "keyType": KmmEditorControlSettings.InputType.Double},
            "Radius": {"keyName": qsTranslate("QObject", "Radius"), "keyValue": pRadius, "keyType": KmmEditorControlSettings.InputType.Double},
            "Speed": {"keyName": qsTranslate("QObject", "Speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Swipe & hold": {"keyName": qsTranslate("QObject", "Swipe & hold"), "keyValue": pKmmControl.hold, "keyType": KmmEditorControlSettings.InputType.Boolean}
        }
        var devPropertiesMap = {
            "Stop on key up": {"keyName": qsTranslate("QObject", "Stop on key up"), "keyValue": pKmmControl.keyUpStop, "keyType": KmmEditorControlSettings.InputType.Boolean},
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

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !pFocusedTextField.activeFocus)
            return

        if(pFocusedProperty === "KeyUp")
            pKmmControlUp.key[1] = key
        else if(pFocusedProperty === "KeyDown")
            pKmmControlDown.key[1] = key
        else if(pFocusedProperty === "KeyLeft")
            pKmmControlLeft.key[1] = key
        else if(pFocusedProperty === "KeyRight")
            pKmmControlRight.key[1] = key

        pFocusedTextField.text = kmmBackend.getKeyList(key)
    }
}
