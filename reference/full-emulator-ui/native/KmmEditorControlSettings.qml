/*
 * Copyright (C) 2021 BlueStack Systems, Inc.
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
import Qt.labs.platform 1.1

import UiToolTipControl 1.0
import UiHelper 1.0

import "KmmUtils.js" as Utils

Window {

    id: iControlSettingsWin
    color: UiTheme.colors.primary70
    flags: Qt.Window | Qt.FramelessWindowHint
    title: uiBackend.productName + " " +  pTitle

    width: 260
    height: iContentCol.implicitHeight
    screen: transientParent.screen

    property bool pCompLoaded: false

    ListModel {
        id: iKeyBindingsModel
        dynamicRoles: true
    }
    ListModel {
        id: iPropertiesModel
        dynamicRoles: true
    }
    ListModel {
        id: iExtraProperties1Model
        dynamicRoles: true
    }
    ListModel {
        id: iExtraProperties2Model
        dynamicRoles: true
    }
    ListModel {
        id: iDevModePropertiesModel
        dynamicRoles: true
    }
    ListModel {
        id: iExtraProperties3Model
        dynamicRoles: true
    }
    property list<QtObject> pCrosshairModel

    enum InputType {
        Integer,
        Double,
        String,
        Boolean,
        Key
    }

    property alias pTitle: iTitleText.text

    property string pExtraProp1Name: ""
    property string pExtraProp2Name: ""
    property string pExtraProp3Name: ""

    property alias pExtraProp1Header: iExtraProp1Header.text
    property alias pExtraProp2Header: iExtraProp2Header.text
    property alias pExtraProp3Header: iExtraProp3Header.text

    property alias pExtraProp1TogglePresent: iExtraProp1ToggleBtn.visible
    property alias pExtraProp2TogglePresent: iExtraProp2ToggleBtn.visible
    property alias pExtraProp3TogglePresent: iExtraProp3ToggleBtn.visible

    property alias pExtraProp1ToggleOn: iExtraProp1ToggleBtn.checked
    property alias pExtraProp2ToggleOn: iExtraProp2ToggleBtn.checked
    property alias pExtraProp3ToggleOn: iExtraProp3ToggleBtn.checked

    property string pExtraProp1ToggleKey: ""
    property string pExtraProp2ToggleKey: ""
    property string pExtraProp3ToggleKey: ""

    property alias pExtraProp1Image: iExtraProp1Img.asset
    property alias pExtraProp2Image: iExtraProp2Img.asset

    property alias pExtraProp1Text: iExtraProp1Txt.text
    property alias pExtraProp2Text: iExtraProp2Txt.text

    property alias pExtraProp3ImgPathText: iExtraProp3ImgPathTxt.text

    property string pExtraProp3ComboBoxIdx: ""
    property bool pShowCrosshairSection: false

    property alias pShowOnOverlay: iShowOverlayToggleBtn.checked

    property alias pShowOverlaySetting: iShowKeysRow.visible

    property var pPropertiesMap: ({})

    property var doubleValidator: DoubleValidator {bottom: 0; top: 100}
    property var intValidator: IntValidator {bottom: 0; top: 999999999}
    property var inputValidator: RegularExpressionValidator { regularExpression: RegExp("[a-zA-Z0-9]+") }
    property var stringInputValidator: RegularExpressionValidator { regularExpression: /^[a-zA-Z0-9 !@#$%^&*()_+\-=\[\]{};':"\\|,.<>\/?]+/ }
    property string pComposedKey: ""
    property string pLastComposedKey: ""
    property alias pEnableCondition: iEnableConditionTxt.text
    property alias pStartCondition: iStartConditionTxt.text
    property string pExtraProp3ColorText: ""

    signal modelUpdate(var updatedProperties)
    signal deleteControl()
    signal createExtraProp1Item(var x, var y)
    signal createExtraProp2Item(var x, var y)
    signal createExtraProp3Item(var checked)
    signal selectedTabUpdate(var gamepadTabSelected)
    signal extraProp3ImgChanged(var imgType)
    signal extraProp3CustomImgChanged(var imgPath)
    signal extraProp3ColorChanged(var selectedColor)
    property bool pGamepadTabSelected: false
    property string pFocusedProperty : ""
    property UiTextField pFocusedTextField: null
    property bool pDevModeEnabled: false

    onHeightChanged: {
        if ((iControlSettingsWin.y + iControlSettingsWin.height) > Screen.desktopAvailableHeight) {
            if(iControlSettingsWin.height > Screen.desktopAvailableHeight)
                iControlSettingsWin.height = Screen.desktopAvailableHeight
            iControlSettingsWin.y = Screen.desktopAvailableHeight - iControlSettingsWin.height
        }
    }

    Component {
        id: iFileDialogComponent
        FileDialog {
            id: iFileDialog
            title: qsTranslate("QObject", "Please choose a file")
            nameFilters: ["Image Files (*.png *.jpg *.jpeg)"]
            fileMode: FileDialog.OpenFiles
            visible: false
            onAccepted: {
                iFileDialog.close()
                iFileDialog.destroy()
                for (var i = 0; i < iFileDialog.files.length; ++i) {
                    var path = iFileDialog.files[i].toString()
                    path = path.replace(/^(file:\/{3})|(qrc:\/{2})|(http:\/{2})/, "")
                    if(kmmBackend.checkCrosshairImageSize(path)) {
                        pExtraProp3ImgPathText = "(" + path.slice(path.lastIndexOf("/")+1) + ")"
                        iExtraProp3ImgPathTxt.color = UiTheme.colors.primary30
                        extraProp3CustomImgChanged(path)
                    }
                    else {
                        pExtraProp3ImgPathText = qsTranslate("QObject", "Image size too large")
                        iExtraProp3ImgPathTxt.color = UiTheme.colors.error
                    }
                }
            }
        }
    }

    UiColumnLayout {
        id: iContentCol
        width: parent.width
        color: UiTheme.colors.primary70
        border.color: UiTheme.colors.primary60
        border.width: 1
        anchors.fill: parent

        UiRowLayout {
            id: iTitleRow
            spacing: 4
            leftPadding: 8
            Layout.fillWidth: true
            color: UiTheme.colors.primary80
            Layout.preferredHeight: 32
            pMouseArea {
                anchors.fill: iTitleRow
                enabled: true
                onPositionChanged: iControlSettingsWin.startSystemMove()
            }
            Text {
                id: iTitleText
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: "Tap settings"
                font: UiTheme.fonts.titleExtraSmall
                color: UiTheme.colors.primary10
            }
            UiImageButton {
                asset: "TitlebarClose"
                pImageWidth: 32
                pImageHeight: 32
                UiToolTip.text: qsTranslate("QObject", "Close")
                onClicked: iControlSettingsWin.close()
            }
        }

        UiRowLayout {
            id: iKeyControlSwitchRow
            Layout.margins: 8
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            color: UiTheme.colors.primary60
            spacing: 16

            Rectangle {
                id: iKeyboardRect
                Layout.leftMargin: 4
                width: 120
                Layout.preferredHeight: 25
                color: UiTheme.colors.primary60
                UiColumnLayout {
                    UiText {
                        Layout.margins: 5
                        Layout.alignment: Qt.AlignCenter
                        Layout.maximumWidth: iKeyboardRect.width - 10
                        font: UiTheme.fonts.bodyExtraSmall
                        text: qsTranslate("QObject", "Keyboard and mouse")
                        color: UiTheme.colors.primary10
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: iKeyboardRectLine
                        Layout.alignment: Qt.AlignBottom
                        width: iKeyboardRect.width
                        height: 1
                        color: !pGamepadTabSelected ? UiTheme.colors.accentNormal : UiTheme.colors.primary60
                    }
                }
                MouseArea {
                    hoverEnabled: true
                    anchors.fill: parent
                    onClicked: {
                        kmmBackend.isEditing(true)
                        pGamepadTabSelected = false
                        modelUpdate(pPropertiesMap)
                        selectedTabUpdate(pGamepadTabSelected)
                    }
                }
            }

            Rectangle {
                id: iGamepadRect
                color: UiTheme.colors.primary60
                Layout.alignment: Qt.AlignCenter
                width: 100
                Layout.preferredHeight: 25
                Layout.rightMargin: 4
                UiColumnLayout {
                    UiText {
                        Layout.margins: 5
                        Layout.maximumWidth: iGamepadRect.width - 15
                        Layout.alignment: Qt.AlignCenter
                        font: UiTheme.fonts.bodyExtraSmall
                        text: qsTranslate("QObject", "Gamepad")
                        color: UiTheme.colors.primary10
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: iGamepadRectLine
                        Layout.alignment: Qt.AlignBottom
                        width: iGamepadRect.width
                        height: 1
                        color: pGamepadTabSelected ? UiTheme.colors.accentNormal : UiTheme.colors.primary60
                    }
                }
                MouseArea {
                    hoverEnabled: true
                    anchors.fill: parent
                    onClicked: {
                        kmmBackend.isEditing(false)
                        pGamepadTabSelected = true
                        modelUpdate(pPropertiesMap)
                        selectedTabUpdate(pGamepadTabSelected)
                    }
                }
            }
            Component.onCompleted:  {
                pGamepadTabSelected = kmmBackend.gamepadTabSelected
                kmmBackend.isEditing(!pGamepadTabSelected)
            }
        }
        UiScrollView {
            id: iScrollView
            Layout.fillHeight: true
            Layout.preferredWidth: parent.width
            clip: true
            contentWidth: iContentLayout.width
            contentHeight: iContentLayout.height

            UiColumnLayout {
                id: iContentLayout
                Layout.fillWidth: true
                padding: 8
                spacing: 8

                UiColumnLayout {
                    id: iKeyCol
                    spacing: 4
                    visible: false
                    Layout.fillWidth: true
                    UiText {
                        Layout.fillWidth: true
                        text: qsTranslate("QObject", "Key Bindings")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.primary10
                    }
                    ListView {
                        id: iKeyListView
                        Layout.fillWidth: true
                        implicitHeight: contentHeight
                        model: iKeyBindingsModel
                        interactive: false
                        spacing: 4
                        delegate: UiRowLayout {
                            width: 240
                            spacing: 4
                            UiText {
                                Layout.fillWidth: true
                                text: model.itemLabel
                                font: UiTheme.fonts.bodyExtraSmall
                                color: UiTheme.colors.primary20
                            }
                            UiTextField {
                                id: iKeyBindingText
                                pSize: UiTheme.textFields.sizes.small
                                Layout.preferredWidth: 120
                                enabled: model.itemEnabled === "false" ? false : true
                                opacity: enabled ? 1 : 0.7
                                font.capitalization: Font.AllUppercase
                                UiHelper.inputMethodEditing: false
                                text: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean)
                                        model.itemValue ? "True" : "False"
                                    else {
                                        if (model.itemValue === "MouseRButton")
                                            qsTranslate("QObject", "Right Click")
                                        else if (model.itemValue === "MouseLButton")
                                            qsTranslate("QObject", "Left Click")
                                        else if (model.itemValue === "MouseMButton")
                                            qsTranslate("QObject", "Middle Click")
                                        else if (model.itemValue === "MouseXButton1")
                                            qsTranslate("QObject", "X button1")
                                        else if (model.itemValue === "MouseXButton2")
                                            qsTranslate("QObject", "X button2")
                                        else if(model.itemType === KmmEditorControlSettings.InputType.Key)
                                            pGamepadTabSelected ? kmmBackend.getKeyList(model.itemValue) : plrBackend.getDisplayComboFromNameCombo(model.itemValue)
                                        else
                                            model.itemValue
                                    }
                                }

                                inputMethodHints: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        Qt.ImhFormattedNumbersOnly
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        Qt.ImhDigitsOnly
                                    else
                                        Qt.ImhNone
                                }

                                validator: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        doubleValidator
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        intValidator
                                    else
                                        inputValidator
                                }

                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Tab) {
                                        KeyNavigation.tab = this
                                    }
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if(pGamepadTabSelected) pComposedKey = ""
                                        Utils.keyPressed(event, iControlSettingsWin)
                                        text = ""
                                        pPropertiesMap[model.itemLabel] = ""
                                    }
                                }

                                Keys.onReleased: function(event) {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if (pComposedKey !== ""){
                                            text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                                            pPropertiesMap[model.itemLabel] = pComposedKey
                                            pLastComposedKey = pComposedKey
                                        }
                                        if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                                            text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                                            pPropertiesMap[model.itemLabel] = pLastComposedKey
                                            pLastComposedKey = ""
                                        }

                                        pComposedKey = ""
                                    }
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onReleased: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onTextChanged: {
                                    if (model.itemType !== KmmEditorControlSettings.InputType.Key &&
                                            model.itemType !== KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = text
                                    }
                                }

                                onTextEdited: {
                                    fEnableSave()
                                }

                                onActiveFocusChanged: {
                                    if(activeFocus) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            kmmBackend.setFocusedWindow(iControlSettingsWin)
                                            pFocusedTextField = this
                                            pFocusedProperty = model.itemLabel
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2
                                    onWheel: function(wheel) {
                                        if(pGamepadTabSelected)
                                            return

                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(wheel.angleDelta.y > 0){
                                                iKeyBindingText.text = qsTranslate("QObject", "MouseWheelUp")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelUp"
                                            }
                                            else{
                                                iKeyBindingText.text = qsTranslate("QObject", "MouseWheelDown")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelDown"
                                            }
                                        }
                                    }

                                    onPressed: function(mouse) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(mouse.button & Qt.RightButton){
                                                iKeyBindingText.text = qsTranslate("QObject", "Right Click")
                                                pPropertiesMap[model.itemLabel]  = "MouseRButton"
                                            }
                                            else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                                                iKeyBindingText.text = qsTranslate("QObject", "Middle Click")
                                                pPropertiesMap[model.itemLabel] = "MouseMButton"
                                            }
                                            else if(mouse.button & Qt.XButton1){
                                                iKeyBindingText.text = qsTranslate("QObject", "X button1")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton1"
                                            }
                                            else if(mouse.button & Qt.XButton2){
                                                iKeyBindingText.text = qsTranslate("QObject", "X button2")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton2"
                                            }
                                            pComposedKey = ""
                                        }
                                        else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                            pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                            model.itemValue = pPropertiesMap[model.itemLabel]
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 240
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                }

                UiColumnLayout {
                    id: iPropertiesCol
                    spacing: 4
                    Layout.fillWidth: true
                    UiText {
                        Layout.fillWidth: true
                        text: qsTranslate("QObject", "Properties")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.primary10
                    }
                    ListView {
                        Layout.fillWidth: true
                        implicitHeight: contentHeight
                        model: iPropertiesModel
                        interactive: false
                        spacing: 4
                        delegate: UiRowLayout {
                            width: 240
                            spacing: 4
                            UiText {
                                Layout.fillWidth: true
                                text: model.itemLabel
                                font: UiTheme.fonts.bodyExtraSmall
                                color: UiTheme.colors.primary20
                            }
                            UiTextField {
                                id: iPropertyText
                                pSize: UiTheme.textFields.sizes.small
                                Layout.preferredWidth: 120
                                enabled: model.itemEnabled === "false" ? false : true
                                opacity: enabled ? 1 : 0.7
                                font.capitalization: model.itemType !== KmmEditorControlSettings.InputType.String ? Font.AllUppercase : Font.MixedCase
                                UiHelper.inputMethodEditing: false
                                text: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean)
                                        model.itemValue ? "True" : "False"
                                    else {
                                        if (model.itemValue === "MouseRButton")
                                            qsTranslate("QObject", "Right Click")
                                        else if (model.itemValue === "MouseLButton")
                                            qsTranslate("QObject", "Left Click")
                                        else if (model.itemValue === "MouseMButton")
                                            qsTranslate("QObject", "Middle Click")
                                        else if (model.itemValue === "MouseXButton1")
                                            qsTranslate("QObject", "X button1")
                                        else if (model.itemValue === "MouseXButton2")
                                            qsTranslate("QObject", "X button2")
                                        else if(model.itemType === KmmEditorControlSettings.InputType.Key)
                                            pGamepadTabSelected ? kmmBackend.getKeyList(model.itemValue) : plrBackend.getDisplayComboFromNameCombo(model.itemValue)
                                        else
                                            model.itemValue
                                    }
                                }

                                inputMethodHints: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        Qt.ImhFormattedNumbersOnly
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        Qt.ImhDigitsOnly
                                    else
                                        Qt.ImhNone
                                }

                                validator: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        doubleValidator
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        intValidator
                                    else
                                        inputValidator
                                }

                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Tab) {
                                        KeyNavigation.tab = this
                                    }
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if(pGamepadTabSelected) pComposedKey = ""
                                        Utils.keyPressed(event, iControlSettingsWin)
                                        text = ""
                                        pPropertiesMap[model.itemLabel] = ""
                                    }
                                }

                                Keys.onReleased: function(event) {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if (pComposedKey !== ""){
                                            text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                                            pPropertiesMap[model.itemLabel] = pComposedKey
                                            pLastComposedKey = pComposedKey
                                        }
                                        if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                                            text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                                            pPropertiesMap[model.itemLabel] = pLastComposedKey
                                            pLastComposedKey = ""
                                        }

                                        pComposedKey = ""
                                    }
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onReleased: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onTextChanged: {
                                    if (model.itemType !== KmmEditorControlSettings.InputType.Key &&
                                            model.itemType !== KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = text
                                    }
                                }

                                onTextEdited: {
                                    fEnableSave()
                                }

                                onActiveFocusChanged: {
                                    if(activeFocus) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            kmmBackend.setFocusedWindow(iControlSettingsWin)
                                            pFocusedTextField = this
                                            pFocusedProperty = model.itemLabel
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2
                                    onWheel: function(wheel) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(wheel.angleDelta.y > 0){
                                                iPropertyText.text = qsTranslate("QObject", "MouseWheelUp")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelUp"
                                            }
                                            else{
                                                iPropertyText.text = qsTranslate("QObject", "MouseWheelDown")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelDown"
                                            }
                                        }
                                    }

                                    onPressed: function(mouse) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(mouse.button & Qt.RightButton){
                                                iPropertyText.text = qsTranslate("QObject", "Right Click")
                                                pPropertiesMap[model.itemLabel]  = "MouseRButton"
                                            }
                                            else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                                                iPropertyText.text = qsTranslate("QObject", "Middle Click")
                                                pPropertiesMap[model.itemLabel] = "MouseMButton"
                                            }
                                            else if(mouse.button & Qt.XButton1){
                                                iPropertyText.text = qsTranslate("QObject", "X button1")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton1"
                                            }
                                            else if(mouse.button & Qt.XButton2){
                                                iPropertyText.text = qsTranslate("QObject", "X button2")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton2"
                                            }
                                            pComposedKey = ""
                                        }
                                        else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                            pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                            model.itemValue = pPropertiesMap[model.itemLabel]
                                        }
                                    }
                                }
                            }
                        }

                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                    visible: iShowKeysRow.visible
                }

                UiColumnLayout {
                    id: iExtraProp3Col
                    spacing: 4
                    visible: false
                    Layout.fillWidth: true
                    color: pShowCrosshairSection ? UiTheme.colors.accentClickTransparent : UiTheme.colors.primary70
                    ColorAnimation on color { to: UiTheme.colors.primary70; duration: 1000; }
                    UiRowLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        UiText {
                            id: iExtraProp3Header
                            Layout.fillWidth: true
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.primary10
                        }
                        UiToggleButton {
                            id: iExtraProp3ToggleBtn
                            visible: false
                            checked: true
                            onCheckedChanged: {
                                if (pCompLoaded) {
                                    pPropertiesMap[pExtraProp3ToggleKey] = checked
                                    fEnableSave()
                                }
                                createExtraProp3Item(checked)
                            }
                        }
                    }
                    UiColumnLayout {
                        spacing: 4
                        bottomPadding: 4
                        Layout.fillWidth: true
                        enabled: iExtraProp3ToggleBtn.checked
                        UiImageComboBox {
                            id: iPropsCombobox
                            Layout.preferredWidth: 240
                            Layout.topMargin: 4
                            pSize: UiTheme.comboBoxes.sizes.small
                            model: pCrosshairModel
                            textRole: "text"
                            valueRole: "value"
                            pImageName: "imageName"
                            opacity: enabled ? 1 : 0.5
                            onActivated: {
                                if(pCompLoaded) {
                                    extraProp3ImgChanged(iPropsCombobox.currentValue)
                                    fEnableSave()
                                }
                            }
                            Component.onCompleted:  {

                            }
                        }
                        UiRowLayout {
                            spacing: 4
                            visible: iPropsCombobox.currentIndex === 4
                            opacity: enabled ? 1 : 0.5
                            UiButton{
                                Layout.maximumWidth: 100
                                pSize: UiTheme.buttons.sizes.small
                                pColor: UiTheme.buttons.colors.accent
                                text: iExtraProp3ImgPathTxt.text === "" ? qsTranslate("QObject", "Choose file") : qsTranslate("QObject", "Change file")
                                onClicked: {
                                    iFileDialogComponent.createObject().open()
                                }
                            }
                            UiText {
                                visible: pExtraProp3ImgPathText != ""
                                id: iExtraProp3ImgPathTxt
                                font: UiTheme.fonts.bodyExtraSmall
                                color: UiTheme.colors.primary30
                                text: pExtraProp3ImgPathText
                                elide: Text.ElideRight
                                Layout.maximumWidth: 142
                                Layout.alignment: Qt.AlignLeft
                            }
                        }
                        UiText {
                            opacity: enabled ? 1 : 0.5
                            Layout.fillWidth: true
                            Layout.maximumWidth: 220
                            visible: iPropsCombobox.currentIndex === 4
                            text: qsTranslate("QObject", "Max file size (5MB)")
                            font: UiTheme.fonts.bodyTiny
                            color: UiTheme.colors.primary20
                            elide: Text.ElideRight
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        implicitHeight: contentHeight
                        model: iExtraProperties3Model
                        interactive: false
                        spacing: 4
                        enabled: iExtraProp3ToggleBtn.checked
                        opacity: enabled ? 1 : 0.5
                        delegate: UiRowLayout {
                            width: 240
                            spacing: 4
                            UiText {
                                Layout.fillWidth: true
                                text: model.itemLabel
                                font: UiTheme.fonts.bodyExtraSmall
                                color: UiTheme.colors.primary20
                                enabled: model.itemLabel === qsTranslate("QObject", "Color") && iPropsCombobox.currentIndex === 4 ? false : true
                                opacity: enabled ? 1 : 0.5
                            }
                            Rectangle {
                                id: iColorRect
                                visible: pExtraProp3ColorText != "" && model.itemLabel === qsTranslate("QObject", "Color")
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                border.color: UiTheme.colors.primary50
                                Layout.rightMargin: -4
                                color: pExtraProp3ColorText
                                opacity: enabled ? 1 : 0.5
                                enabled: visible && iPropsCombobox.currentIndex === 4 ? false : true
                                MouseArea {
                                    anchors.fill: iColorRect
                                    onClicked: {
                                        iColorDialog.open()
                                    }
                                }
                            }

                            UiTextField {
                                id: iExtraProp3ColorText
                                visible: pExtraProp3ColorText != "" && iColorRect.visible
                                pSize: UiTheme.textFields.sizes.small
                                Layout.preferredWidth: 120
                                enabled: visible && iPropsCombobox.currentIndex === 4 ? false : true
                                opacity: enabled ? 1 : 0.5
                                font.capitalization: Font.AllUppercase
                                UiHelper.inputMethodEditing: false
                                text:pExtraProp3ColorText
                                MouseArea {
                                    anchors.fill: iExtraProp3ColorText
                                    onPressed:  {
                                        iColorDialog.open()
                                    }
                                }
                            }

                            UiTextField {
                                id: iExtraProp3Text
                                pSize: UiTheme.textFields.sizes.small
                                Layout.preferredWidth: 120
                                enabled: model.itemEnabled === "false" ? false : true
                                opacity: enabled ? 1 : 0.7
                                font.capitalization: Font.AllUppercase
                                visible: !iColorRect.visible
                                UiHelper.inputMethodEditing: false
                                text: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean)
                                        model.itemValue ? "True" : "False"
                                    else {
                                        if (model.itemValue === "MouseRButton")
                                            qsTranslate("QObject", "Right Click")
                                        else if (model.itemValue === "MouseLButton")
                                            qsTranslate("QObject", "Left Click")
                                        else if (model.itemValue === "MouseMButton")
                                            qsTranslate("QObject", "Middle Click")
                                        else if (model.itemValue === "MouseXButton1")
                                            qsTranslate("QObject", "X button1")
                                        else if (model.itemValue === "MouseXButton2")
                                            qsTranslate("QObject", "X button2")
                                        else if(model.itemType === KmmEditorControlSettings.InputType.Key)
                                            pGamepadTabSelected ? kmmBackend.getKeyList(model.itemValue) : plrBackend.getDisplayComboFromNameCombo(model.itemValue)
                                        else
                                            model.itemValue
                                    }
                                }

                                inputMethodHints: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        Qt.ImhFormattedNumbersOnly
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        Qt.ImhDigitsOnly
                                    else
                                        Qt.ImhNone
                                }

                                validator: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        doubleValidator
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        intValidator
                                    else
                                        inputValidator
                                }

                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Tab) {
                                        KeyNavigation.tab = this
                                    }
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if(pGamepadTabSelected) pComposedKey = ""
                                        Utils.keyPressed(event, iControlSettingsWin)
                                        text = ""
                                        pPropertiesMap[model.itemLabel] = ""
                                    }
                                }

                                Keys.onReleased: function(event) {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if (pComposedKey !== ""){
                                            text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                                            pPropertiesMap[model.itemLabel] = pComposedKey
                                            pLastComposedKey = pComposedKey
                                        }
                                        if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                                            text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                                            pPropertiesMap[model.itemLabel] = pLastComposedKey
                                            pLastComposedKey = ""
                                        }

                                        pComposedKey = ""
                                    }
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onReleased: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onTextChanged: {
                                    if (model.itemType !== KmmEditorControlSettings.InputType.Key &&
                                            model.itemType !== KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = text
                                    }
                                }

                                onTextEdited: {
                                    fEnableSave()
                                }

                                onActiveFocusChanged: {
                                    if(activeFocus) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            kmmBackend.setFocusedWindow(iControlSettingsWin)
                                            pFocusedTextField = this
                                            pFocusedProperty = model.itemLabel
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2
                                    onWheel: function(wheel) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(wheel.angleDelta.y > 0){
                                                iExtraProp3Text.text = qsTranslate("QObject", "MouseWheelUp")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelUp"
                                            }
                                            else{
                                                iExtraProp3Text.text = qsTranslate("QObject", "MouseWheelDown")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelDown"
                                            }
                                        }
                                    }

                                    onPressed: function(mouse) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(mouse.button & Qt.RightButton){
                                                iExtraProp3Text.text = qsTranslate("QObject", "Right Click")
                                                pPropertiesMap[model.itemLabel]  = "MouseRButton"
                                            }
                                            else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                                                iExtraProp3Text.text = qsTranslate("QObject", "Middle Click")
                                                pPropertiesMap[model.itemLabel] = "MouseMButton"
                                            }
                                            else if(mouse.button & Qt.XButton1){
                                                iExtraProp3Text.text = qsTranslate("QObject", "X button1")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton1"
                                            }
                                            else if(mouse.button & Qt.XButton2){
                                                iExtraProp3Text.text = qsTranslate("QObject", "X button2")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton2"
                                            }
                                            pComposedKey = ""
                                        }
                                        else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                            pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                            model.itemValue = pPropertiesMap[model.itemLabel]
                                        }
                                        else if(model.itemType === KmmEditorControlSettings.InputType.String
                                                && model.itemLabel === qsTranslate("QObject","Color")) {
                                            iColorDialog.open()
                                        }
                                    }
                                }
                            }
                        }
                    }


                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                    visible: iExtraProp3Col.visible
                }

                UiColumnLayout {
                    id: iExtraProp1Col
                    spacing: 4
                    visible: false
                    Layout.fillWidth: true
                    UiRowLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        UiText {
                            id: iExtraProp1Header
                            Layout.fillWidth: true
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.primary10
                        }
                        UiToggleButton {
                            id: iExtraProp1ToggleBtn
                            visible: false
                            checked: true
                            onCheckedChanged: {
                                if (pCompLoaded) {
                                    pPropertiesMap[pExtraProp1ToggleKey] = checked
                                    fEnableSave()
                                }
                            }
                        }
                    }
                    UiRowLayout {
                        spacing: 4
                        padding: 4
                        Layout.fillWidth: true
                        enabled: iExtraProp1ToggleBtn.checked
                        UiImageButton {
                            id: iExtraProp1Img
                            pImageWidth: 24
                            pImageHeight: 24
                            MouseArea {
                                anchors.fill: parent
                                onPressed: {
                                    iControlSettingsWin.close()
                                    var point = iExtraProp1Img.mapToItem(null, mouseX, mouseY)
                                    createExtraProp1Item(point.x + iControlSettingsWin.x - iControlSettingsWin.transientParent.x, point.y + iControlSettingsWin.y - iControlSettingsWin.transientParent.y)
                                }
                            }
                        }
                        Text {
                            id: iExtraProp1Txt
                            Layout.preferredWidth: 200
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.warning
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        implicitHeight: contentHeight
                        model: iExtraProperties1Model
                        interactive: false
                        spacing: 4
                        delegate: UiRowLayout {
                            width: 240
                            spacing: 4
                            UiText {
                                Layout.fillWidth: true
                                text: model.itemLabel
                                font: UiTheme.fonts.bodyExtraSmall
                                color: UiTheme.colors.primary20
                            }
                            UiTextField {
                                id: iExtraProp1Text
                                pSize: UiTheme.textFields.sizes.small
                                Layout.preferredWidth: 120
                                enabled: model.itemEnabled === "false" ? false : true
                                opacity: enabled ? 1 : 0.7
                                font.capitalization: Font.AllUppercase
                                UiHelper.inputMethodEditing: false
                                text: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean)
                                        model.itemValue ? "True" : "False"
                                    else {
                                        if (model.itemValue === "MouseRButton")
                                            qsTranslate("QObject", "Right Click")
                                        else if (model.itemValue === "MouseLButton")
                                            qsTranslate("QObject", "Left Click")
                                        else if (model.itemValue === "MouseMButton")
                                            qsTranslate("QObject", "Middle Click")
                                        else if (model.itemValue === "MouseXButton1")
                                            qsTranslate("QObject", "X button1")
                                        else if (model.itemValue === "MouseXButton2")
                                            qsTranslate("QObject", "X button2")
                                        else if(model.itemType === KmmEditorControlSettings.InputType.Key)
                                            pGamepadTabSelected ? kmmBackend.getKeyList(model.itemValue) : plrBackend.getDisplayComboFromNameCombo(model.itemValue)
                                        else
                                            model.itemValue
                                    }
                                }

                                inputMethodHints: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        Qt.ImhFormattedNumbersOnly
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        Qt.ImhDigitsOnly
                                    else
                                        Qt.ImhNone
                                }

                                validator: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        doubleValidator
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        intValidator
                                    else
                                        inputValidator
                                }

                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Tab) {
                                        KeyNavigation.tab = this
                                    }
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if(pGamepadTabSelected) pComposedKey = ""
                                        Utils.keyPressed(event, iControlSettingsWin)
                                        text = ""
                                        pPropertiesMap[model.itemLabel] = ""
                                    }
                                }

                                Keys.onReleased: function(event) {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if (pComposedKey !== ""){
                                            text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                                            pPropertiesMap[model.itemLabel] = pComposedKey
                                            pLastComposedKey = pComposedKey
                                        }
                                        if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                                            text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                                            pPropertiesMap[model.itemLabel] = pLastComposedKey
                                            pLastComposedKey = ""
                                        }

                                        pComposedKey = ""
                                    }
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onReleased: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onTextChanged: {
                                    if (model.itemType !== KmmEditorControlSettings.InputType.Key &&
                                            model.itemType !== KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = text
                                    }
                                }

                                onTextEdited: {
                                    fEnableSave()
                                }

                                onActiveFocusChanged: {
                                    if(activeFocus) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            kmmBackend.setFocusedWindow(iControlSettingsWin)
                                            pFocusedTextField = this
                                            pFocusedProperty = model.itemLabel
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2
                                    onWheel: function(wheel) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(wheel.angleDelta.y > 0){
                                                iExtraProp1Text.text = qsTranslate("QObject", "MouseWheelUp")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelUp"
                                            }
                                            else{
                                                iExtraProp1Text.text = qsTranslate("QObject", "MouseWheelDown")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelDown"
                                            }
                                        }
                                    }

                                    onPressed: function(mouse) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(mouse.button & Qt.RightButton){
                                                iExtraProp1Text.text = qsTranslate("QObject", "Right Click")
                                                pPropertiesMap[model.itemLabel]  = "MouseRButton"
                                            }
                                            else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                                                iExtraProp1Text.text = qsTranslate("QObject", "Middle Click")
                                                pPropertiesMap[model.itemLabel] = "MouseMButton"
                                            }
                                            else if(mouse.button & Qt.XButton1){
                                                iExtraProp1Text.text = qsTranslate("QObject", "X button1")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton1"
                                            }
                                            else if(mouse.button & Qt.XButton2){
                                                iExtraProp1Text.text = qsTranslate("QObject", "X button2")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton2"
                                            }
                                            pComposedKey = ""
                                        }
                                        else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                            pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                            model.itemValue = pPropertiesMap[model.itemLabel]
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                    visible: iExtraProp1Col.visible
                }

                UiColumnLayout {
                    id: iExtraProp2Col
                    spacing: 4
                    visible: false
                    Layout.fillWidth: true
                    UiRowLayout {
                        spacing: 4
                        Layout.fillWidth: true
                        UiText {
                            id: iExtraProp2Header
                            Layout.fillWidth: true
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.primary10
                        }
                        UiToggleButton {
                            id: iExtraProp2ToggleBtn
                            visible: false
                            checked: true
                            onCheckedChanged: {
                                if (pCompLoaded) {
                                    pPropertiesMap[pExtraProp2ToggleKey] = checked
                                    fEnableSave()
                                }
                            }
                        }
                    }
                    UiRowLayout {
                        spacing: 4
                        padding: 4
                        Layout.fillWidth: true
                        enabled: iExtraProp2ToggleBtn.checked
                        UiImageButton {
                            id: iExtraProp2Img
                            pImageWidth: 24
                            pImageHeight: 24
                            MouseArea {
                                anchors.fill: parent
                                onPressed: {
                                    iControlSettingsWin.close()
                                    var point = iExtraProp2Img.mapToItem(null, mouseX, mouseY)
                                    createExtraProp2Item(point.x + iControlSettingsWin.x - iControlSettingsWin.transientParent.x, point.y + iControlSettingsWin.y - iControlSettingsWin.transientParent.y)
                                }
                            }
                        }
                        Text {
                            id: iExtraProp2Txt
                            Layout.preferredWidth: 200
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.warning
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        implicitHeight: contentHeight
                        model: iExtraProperties2Model
                        interactive: false
                        spacing: 4
                        enabled: iExtraProp2ToggleBtn.checked
                        delegate: UiRowLayout {
                            width: 240
                            spacing: 4
                            UiText {
                                Layout.fillWidth: true
                                text: model.itemLabel
                                font: UiTheme.fonts.bodyExtraSmall
                                color: UiTheme.colors.primary20
                            }
                            UiTextField {
                                id: iExtraProp2Text
                                pSize: UiTheme.textFields.sizes.small
                                Layout.preferredWidth: 120
                                enabled: model.itemEnabled === "false" ? false : true
                                opacity: enabled ? 1 : 0.7
                                font.capitalization: Font.AllUppercase
                                UiHelper.inputMethodEditing: false
                                text: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean)
                                        model.itemValue ? "True" : "False"
                                    else {
                                        if (model.itemValue === "MouseRButton")
                                            qsTranslate("QObject", "Right Click")
                                        else if (model.itemValue === "MouseLButton")
                                            qsTranslate("QObject", "Left Click")
                                        else if (model.itemValue === "MouseMButton")
                                            qsTranslate("QObject", "Middle Click")
                                        else if (model.itemValue === "MouseXButton1")
                                            qsTranslate("QObject", "X button1")
                                        else if (model.itemValue === "MouseXButton2")
                                            qsTranslate("QObject", "X button2")
                                        else if(model.itemType === KmmEditorControlSettings.InputType.Key)
                                            pGamepadTabSelected ? kmmBackend.getKeyList(model.itemValue) : plrBackend.getDisplayComboFromNameCombo(model.itemValue)
                                        else
                                            model.itemValue
                                    }
                                }

                                inputMethodHints: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        Qt.ImhFormattedNumbersOnly
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        Qt.ImhDigitsOnly
                                    else
                                        Qt.ImhNone
                                }

                                validator: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        doubleValidator
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        intValidator
                                    else
                                        inputValidator
                                }

                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Tab) {
                                        KeyNavigation.tab = this
                                    }
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if(pGamepadTabSelected) pComposedKey = ""
                                        Utils.keyPressed(event, iControlSettingsWin)
                                        text = ""
                                        pPropertiesMap[model.itemLabel] = ""
                                    }
                                }

                                Keys.onReleased: function(event) {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if (pComposedKey !== ""){
                                            text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                                            pPropertiesMap[model.itemLabel] = pComposedKey
                                            pLastComposedKey = pComposedKey
                                        }
                                        if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                                            text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                                            pPropertiesMap[model.itemLabel] = pLastComposedKey
                                            pLastComposedKey = ""
                                        }

                                        pComposedKey = ""
                                    }
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onReleased: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onTextChanged: {
                                    if (model.itemType !== KmmEditorControlSettings.InputType.Key &&
                                            model.itemType !== KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = text
                                    }
                                }

                                onTextEdited: {
                                    fEnableSave()
                                }

                                onActiveFocusChanged: {
                                    if(activeFocus) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            kmmBackend.setFocusedWindow(iControlSettingsWin)
                                            pFocusedTextField = this
                                            pFocusedProperty = model.itemLabel
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2
                                    onWheel: function(wheel) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(wheel.angleDelta.y > 0){
                                                iExtraProp2Text.text = qsTranslate("QObject", "MouseWheelUp")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelUp"
                                            }
                                            else{
                                                iExtraProp2Text.text = qsTranslate("QObject", "MouseWheelDown")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelDown"
                                            }
                                        }
                                    }

                                    onPressed: function(mouse) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(mouse.button & Qt.RightButton){
                                                iExtraProp2Text.text = qsTranslate("QObject", "Right Click")
                                                pPropertiesMap[model.itemLabel]  = "MouseRButton"
                                            }
                                            else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                                                iExtraProp2Text.text = qsTranslate("QObject", "Middle Click")
                                                pPropertiesMap[model.itemLabel] = "MouseMButton"
                                            }
                                            else if(mouse.button & Qt.XButton1){
                                                iExtraProp2Text.text = qsTranslate("QObject", "X button1")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton1"
                                            }
                                            else if(mouse.button & Qt.XButton2){
                                                iExtraProp2Text.text = qsTranslate("QObject", "X button2")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton2"
                                            }
                                            pComposedKey = ""
                                        }
                                        else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                            pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                            model.itemValue = pPropertiesMap[model.itemLabel]
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                    visible: iExtraProp2Col.visible && pDevModeEnabled
                }

                UiColumnLayout {
                    id: iDevPropertiesCol
                    visible: pDevModeEnabled
                    spacing: 4
                    Layout.fillWidth: true
                    UiText {
                        Layout.fillWidth: true
                        text: qsTranslate("QObject", "Development mode properties")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.primary10
                    }
                    ListView {
                        Layout.fillWidth: true
                        implicitHeight: contentHeight
                        model: iDevModePropertiesModel
                        interactive: false
                        spacing: 4
                        delegate: UiRowLayout {
                            width: 240
                            spacing: 4
                            UiText {
                                Layout.fillWidth: true
                                text: model.itemLabel
                                font: UiTheme.fonts.bodyExtraSmall
                                color: UiTheme.colors.primary20
                            }
                            UiTextField {
                                id: iDevPropertyText
                                pSize: UiTheme.textFields.sizes.small
                                Layout.preferredWidth: 120
                                enabled: model.itemEnabled === "false" ? false : true
                                opacity: enabled ? 1 : 0.7
                                font.capitalization: model.itemType !== KmmEditorControlSettings.InputType.String ? Font.AllUppercase : Font.MixedCase
                                UiHelper.inputMethodEditing: false
                                text: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean)
                                        model.itemValue ? "True" : "False"
                                    else {
                                        if (model.itemValue === "MouseRButton")
                                            qsTranslate("QObject", "Right Click")
                                        else if (model.itemValue === "MouseLButton")
                                            qsTranslate("QObject", "Left Click")
                                        else if (model.itemValue === "MouseMButton")
                                            qsTranslate("QObject", "Middle Click")
                                        else if (model.itemValue === "MouseXButton1")
                                            qsTranslate("QObject", "X button1")
                                        else if (model.itemValue === "MouseXButton2")
                                            qsTranslate("QObject", "X button2")
                                        else if(model.itemType === KmmEditorControlSettings.InputType.Key)
                                            pGamepadTabSelected ? kmmBackend.getKeyList(model.itemValue) : plrBackend.getDisplayComboFromNameCombo(model.itemValue)
                                        else
                                            model.itemValue
                                    }
                                }

                                inputMethodHints: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        Qt.ImhFormattedNumbersOnly
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        Qt.ImhDigitsOnly
                                    else
                                        Qt.ImhNone
                                }

                                validator: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Double)
                                        doubleValidator
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Integer)
                                        intValidator
                                    else if (model.itemType === KmmEditorControlSettings.InputType.String)
                                        stringInputValidator
                                    else
                                        inputValidator
                                }

                                Keys.onPressed: function(event) {
                                    if (event.key === Qt.Key_Tab) {
                                        KeyNavigation.tab = this
                                    }
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if(pGamepadTabSelected) pComposedKey = ""
                                        Utils.keyPressed(event, iControlSettingsWin)
                                        text = ""
                                        pPropertiesMap[model.itemLabel] = ""
                                    }
                                }

                                Keys.onReleased: function(event) {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                        if (pComposedKey !== ""){
                                            text = plrBackend.getDisplayComboFromNameCombo(pComposedKey)
                                            pPropertiesMap[model.itemLabel] = pComposedKey
                                            pLastComposedKey = pComposedKey
                                        }
                                        if (pComposedKey !== pLastComposedKey && event.key !== Qt.Key_Backspace){
                                            text = plrBackend.getDisplayComboFromNameCombo(pLastComposedKey)
                                            pPropertiesMap[model.itemLabel] = pLastComposedKey
                                            pLastComposedKey = ""
                                        }

                                        pComposedKey = ""
                                    }
                                    else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onReleased: {
                                    if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                        model.itemValue = pPropertiesMap[model.itemLabel]
                                    }
                                }

                                onTextChanged: {
                                    if (model.itemType !== KmmEditorControlSettings.InputType.Key &&
                                            model.itemType !== KmmEditorControlSettings.InputType.Boolean) {
                                        pPropertiesMap[model.itemLabel] = text
                                    }
                                }

                                onTextEdited: {
                                    fEnableSave()
                                }

                                onActiveFocusChanged: {
                                    if(activeFocus) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            kmmBackend.setFocusedWindow(iControlSettingsWin)
                                            pFocusedTextField = this
                                            pFocusedProperty = model.itemLabel
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.RightButton | Qt.MidButton | Qt.MiddleButton | Qt.XButton1 | Qt.XButton2
                                    onWheel: function(wheel) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(wheel.angleDelta.y > 0){
                                                iDevPropertyText.text = qsTranslate("QObject", "MouseWheelUp")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelUp"
                                            }
                                            else{
                                                iDevPropertyText.text = qsTranslate("QObject", "MouseWheelDown")
                                                pPropertiesMap[model.itemLabel]  = "MouseWheelDown"
                                            }
                                        }
                                    }

                                    onPressed: function(mouse) {
                                        if (model.itemType === KmmEditorControlSettings.InputType.Key) {
                                            if(pGamepadTabSelected)
                                                return

                                            if(mouse.button & Qt.RightButton){
                                                iDevPropertyText.text = qsTranslate("QObject", "Right Click")
                                                pPropertiesMap[model.itemLabel]  = "MouseRButton"
                                            }
                                            else if((mouse.button & Qt.MidButton) || (mouse.button & Qt.MiddleButton)){
                                                iDevPropertyText.text = qsTranslate("QObject", "Middle Click")
                                                pPropertiesMap[model.itemLabel] = "MouseMButton"
                                            }
                                            else if(mouse.button & Qt.XButton1){
                                                iDevPropertyText.text = qsTranslate("QObject", "X button1")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton1"
                                            }
                                            else if(mouse.button & Qt.XButton2){
                                                iDevPropertyText.text = qsTranslate("QObject", "X button2")
                                                pPropertiesMap[model.itemLabel] = "MouseXButton2"
                                            }
                                            pComposedKey = ""
                                        }
                                        else if (model.itemType === KmmEditorControlSettings.InputType.Boolean) {
                                            pPropertiesMap[model.itemLabel] = !pPropertiesMap[model.itemLabel]
                                            model.itemValue = pPropertiesMap[model.itemLabel]
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                UiColumnLayout {
                    id: iStartConditionRow
                    spacing: 4
                    visible: pDevModeEnabled
                    Layout.fillWidth: true
                    UiText {
                        Layout.fillWidth: true
                        text: qsTranslate("QObject", "Start condition")
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.primary20
                    }
                    UiTextField {
                        id: iStartConditionTxt
                        pSize: UiTheme.textFields.sizes.small
                        Layout.preferredWidth: 240
                        UiHelper.inputMethodEditing: false
                        text: pStartCondition
                        validator: stringInputValidator
                        onTextChanged: pPropertiesMap[qsTranslate("QObject", "Start condition")] = text
                        onTextEdited: {
                            fEnableSave()
                        }
                    }
                }

                UiColumnLayout {
                    id: iEnableConditionRow
                    spacing: 4
                    visible: pDevModeEnabled
                    Layout.fillWidth: true
                    UiText {
                        Layout.fillWidth: true
                        text: qsTranslate("QObject", "Enable condition")
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.primary20
                    }
                    UiTextField {
                        id: iEnableConditionTxt
                        pSize: UiTheme.textFields.sizes.small
                        Layout.preferredWidth: 240
                        UiHelper.inputMethodEditing: false
                        text: pEnableCondition
                        validator: stringInputValidator
                        onTextChanged: pPropertiesMap[qsTranslate("QObject", "Enable condition")] = text
                        onTextEdited: {
                            fEnableSave()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                    visible: iExtraProp2Col.visible | pDevModeEnabled
                }

                UiRowLayout {
                    id: iShowKeysRow
                    spacing: 4
                    Layout.fillWidth: true
                    UiText {
                        Layout.fillWidth: true
                        text: qsTranslate("QObject", "Show keys on-screen")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.primary10
                    }
                    UiToggleButton {
                        id: iShowOverlayToggleBtn
                        checked: true
                        onCheckedChanged: {
                            if (pCompLoaded) {
                                pPropertiesMap[qsTranslate("QObject", "Show on overlay")] = checked
                                fEnableSave()
                            }
                        }
                    }
                }
            }
        }

        UiRowLayout {
            id: iFooterRow
            spacing: 4
            leftPadding: 8
            Layout.fillWidth: true
            color: UiTheme.colors.primary80
            UiImage {
                asset: "MenuInfo"
                pImageWidth: 12
                pImageHeight: 12
            }
            UiText {
                text: qsTranslate("QObject", "Changes will be saved automatically")
                font: UiTheme.fonts.bodyExtraSmall
                color: UiTheme.colors.primary30
                Layout.fillWidth: true
            }
            UiImageButton {
                asset: "TitlebarDelete"
                pImageWidth: 32
                pImageHeight: 32
                UiToolTip.text: qsTranslate("QObject", "Delete")
                onClicked: {
                    iControlSettingsWin.close()
                    deleteControl()
                }
            }
        }
    }
    ColorDialog {
        id: iColorDialog
        title:  qsTranslate("QObject","Please choose a color")
        onAccepted: {
            console.info("You chose: " + iColorDialog.color)
            pPropertiesMap[qsTranslate("QObject", "Color")] = iColorDialog.color
            extraProp3ColorChanged(pPropertiesMap[qsTranslate("QObject", "Color")])
            pExtraProp3ColorText = pPropertiesMap[qsTranslate("QObject", "Color")]
            close()
        }
        onRejected: {
            close()
        }
    }

    function fLoadModel(keyMap, propsMap, extraProps1Map, extraProps2Map, devModePropsMap, extraProps3Map = "", crosshairModel = "") {
        iKeyBindingsModel.clear()
        iPropertiesModel.clear()
        iExtraProperties1Model.clear()
        iExtraProperties2Model.clear()
        iExtraProperties3Model.clear()
        iDevModePropertiesModel.clear()

        for (var iter in keyMap) {
            pPropertiesMap[keyMap[iter]["keyName"]] = keyMap[iter]["keyValue"]
            iKeyCol.visible = true
            iKeyBindingsModel.append({itemLabel: keyMap[iter]["keyName"], itemValue: keyMap[iter]["keyValue"], itemType: keyMap[iter]["keyType"] , itemEnabled: keyMap[iter]["keyEnabled"]})
        }
        for (iter in propsMap) {
            pPropertiesMap[propsMap[iter]["keyName"]] = propsMap[iter]["keyValue"]
            if(propsMap[iter]["keyVisible"] !== "false")
                iPropertiesModel.append({itemLabel: propsMap[iter]["keyName"], itemValue: propsMap[iter]["keyValue"], itemType: propsMap[iter]["keyType"], itemEnabled: propsMap[iter]["keyEnabled"]})
        }

        for (iter in extraProps1Map) {
            pPropertiesMap[extraProps1Map[iter]["keyName"]] = extraProps1Map[iter]["keyValue"]
            iExtraProp1Col.visible = true
            iExtraProperties1Model.append({itemLabel: extraProps1Map[iter]["keyName"], itemValue: extraProps1Map[iter]["keyValue"], itemType: extraProps1Map[iter]["keyType"], itemEnabled: extraProps1Map[iter]["keyEnabled"]})
        }
        if (extraProps1Map !== null && pExtraProp1ToggleKey !== "") {
            pPropertiesMap[pExtraProp1ToggleKey] = iExtraProp1ToggleBtn.checked
        }

        for (iter in extraProps2Map) {
            pPropertiesMap[extraProps2Map[iter]["keyName"]] = extraProps2Map[iter]["keyValue"]
            iExtraProp2Col.visible = true
            iExtraProperties2Model.append({itemLabel: extraProps2Map[iter]["keyName"], itemValue: extraProps2Map[iter]["keyValue"], itemType: extraProps2Map[iter]["keyType"], itemEnabled: extraProps2Map[iter]["keyEnabled"]})
        }
        if (extraProps2Map !== null && pExtraProp2ToggleKey !== "") {
            pPropertiesMap[pExtraProp2ToggleKey] = iExtraProp2ToggleBtn.checked
        }

        for (iter in extraProps3Map) {
            pPropertiesMap[extraProps3Map[iter]["keyName"]] = extraProps3Map[iter]["keyValue"]
            iExtraProp3Col.visible = true
            iExtraProperties3Model.append({itemLabel: extraProps3Map[iter]["keyName"], itemValue: extraProps3Map[iter]["keyValue"], itemType: extraProps3Map[iter]["keyType"], itemEnabled: extraProps3Map[iter]["keyEnabled"]})
            if(extraProps3Map[iter]["keyName"] === qsTranslate("QObject", "Color")) {
                pExtraProp3ColorText = extraProps3Map[iter]["keyValue"]
            }
        }
        if (extraProps3Map !== null && pExtraProp3ToggleKey !== "") {
            pPropertiesMap[pExtraProp3ToggleKey] = iExtraProp3ToggleBtn.checked
        }
        pPropertiesMap[qsTranslate("QObject", "Show on overlay")] = iShowOverlayToggleBtn.checked

        if(crosshairModel !== "") {
            pCrosshairModel = crosshairModel
            iPropsCombobox.currentIndex = -1
            for(var i = 0; i < pCrosshairModel.length; ++i) {
                if (pCrosshairModel[i].value === pExtraProp3ComboBoxIdx) {
                    iPropsCombobox.currentIndex = i
                }
            }
        }

        for (iter in devModePropsMap) {
            pPropertiesMap[devModePropsMap[iter]["keyName"]] = devModePropsMap[iter]["keyValue"]
            if(devModePropsMap[iter]["keyVisible"] !== "false")
                iDevModePropertiesModel.append({itemLabel: devModePropsMap[iter]["keyName"], itemValue: devModePropsMap[iter]["keyValue"], itemType: devModePropsMap[iter]["keyType"], itemEnabled: devModePropsMap[iter]["keyEnabled"]})
        }
        pPropertiesMap[qsTranslate("QObject", "Enable condition")] = pEnableCondition
        pPropertiesMap[qsTranslate("QObject", "Start condition")] = pStartCondition
        pDevModeEnabled = kmmBackend.devModeEnabled

        if (!pCompLoaded) {
            this.transientParent.pChildWinList.push(this)
        }
        pCompLoaded = true
    }

    function fEnableSave() {
        if (pCompLoaded) {
            this.transientParent.pEnableSave = true
        }
    }

    function fAssignGamepadKey(key) {
        if(!pGamepadTabSelected)
            return

        if(pFocusedProperty === qsTranslate("QObject", "Gamepad stick")) {
            if(key === "GamepadLStickUp" || key === "GamepadLStickDown" || key === "GamepadLStickLeft" || key === "GamepadLStickRight") {
                pPropertiesMap[pFocusedProperty] = "LeftStick"
                pFocusedTextField.text = kmmBackend.getKeyList("LeftStick")
            }
            else if(key === "GamepadRStickUp" || key === "GamepadRStickDown" || key === "GamepadRStickLeft" || key === "GamepadRStickRight") {
                pPropertiesMap[pFocusedProperty] = "RightStick"
                pFocusedTextField.text = kmmBackend.getKeyList("RightStick")
            }
        }
        else {
            pPropertiesMap[pFocusedProperty] = key
            pFocusedTextField.text = kmmBackend.getKeyList(key)
        }
    }

    // @disable-check M16
    onClosing: {
        pFocusedProperty = ""
        pFocusedTextField = null
        kmmBackend.isEditing(!kmmBackend.gamepadTabSelected)
        kmmBackend.setFocusedWindow(null)
        modelUpdate(pPropertiesMap)
        iControlSettingsWin.destroy()
    }
}
