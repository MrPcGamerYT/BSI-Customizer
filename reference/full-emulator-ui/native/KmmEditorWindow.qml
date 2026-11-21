/*
 * Copyright (C) 2020-2021 BlueStack Systems, Inc.
 * All Rights Reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF BLUESTACK SYSTEMS, INC.
 * The copyright notice above does not evidence any actual or intended
 * publication of such source code.
 */

import Qt.labs.platform 1.1
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import UiToolTipControl 1.0
import DialogButtonModel 1.0
import DialogButtonModelList 1.0
import UiDialogWindow 1.0

import "KmmUtils.js" as Utils

Window {
    id: iKmmEditorWin
    objectName: "controlsEditorWin"
    color: "#00000000"
    flags: Qt.Window | Qt.FramelessWindowHint
    modality: Qt.ApplicationModal
    title: uiBackend.productName + " Keymap Editor"

    screen: transientParent.screen

    property int pKmmEditorPanelWidth: 260
    property bool pLoaded: false

    property alias schemeModel: iSchemeCombobox.model
    property alias schemeIdx: iSchemeCombobox.currentIndex
    property int pBuiltinIndex: 0

    property bool pEnableReset: false
    property bool pEnableSave: false
    property bool pUpdateCfgInProgress: false

    property var pSchemeExportWindow
    property var pSchemeImportWindow
    property var pDeleteSchemeIndex
    property var pDeleteSchemeName

    property var pGeneratedCanvasTap: null

    property var pChildWinList: []

    Component.onCompleted: {
        if (!pLoaded) {
            iEditableControlsList.append({ label: qsTranslate("QObject", "Tap spot"), image: "Tap", type: "Tap" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Repeated Tap"), image: "RepeatedTap", type: "TapRepeat" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "D-pad"), image: "Dpad", type: "Dpad" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Aim, pan & shoot"), image: "Aim", type: "Pan" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Free look"), image: "FreeLook", type: "FreeLook" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Script"), image: "Script", type: "Script" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Swipe"), image: "Swipe", type: "Swipe" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Zoom"), image: "Zoom", type: "Zoom" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Tilt"), image: "Tilt", type: "Tilt" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "MOBA D-Pad"), image: "MOBA-PAD", type: "MobaDpad" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "MOBA Skill pad"), image: "SkillPad", type: "MobaSkill" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Rotate"), image: "Rotate", type: "Rotate" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Edge Scroll"), image: "EdgeScroll", type: "EdgeScroll" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Scroll"), image: "Scroll", type: "Scroll" })
            iEditableControlsList.append({ label: qsTranslate("QObject", "Mouse wheel"), image: "MouseZoom", type: "MouseZoom" })

            if(kmmBackend.devModeEnabled) {
                iEditableControlsList.append({ label: qsTranslate("QObject", "State"), image: "State", type: "State" })
                iEditableControlsList.append({ label: qsTranslate("QObject", "Dont forward keys"), image: "DontForwardKeys", type: "DontForwardKeys" })
                iEditableControlsList.append({ label: qsTranslate("QObject", "Send original keys"), image: "SendOriginalKeys", type: "SendOriginalKeys" })
                iEditableControlsList.append({ label: qsTranslate("QObject", "Callback"), image: "Callback", type: "Callback" })
                iEditableControlsList.append({ label: qsTranslate("QObject", "Lock cursor"), image: "MouseLock", type: "LockCursor" })
                iEditableControlsList.append({ label: qsTranslate("QObject", "Weapon switch"), image: "SmartScroll", type: "SmartScroll" })
                iEditableControlsList.append({ label: qsTranslate("QObject", "Native cursor"), image: "NativeCursor", type: "NativeCursor" })
                iEditableControlsList.append({ label: qsTranslate("QObject", "Quick select wheel"), image: "Rotate", type: "QuickSelectWheel" })
            }

            uiBackend.pIsShortcutsEnabled = false
        }

        pLoaded = true
    }

    MouseArea {
        anchors.fill: parent
        z:0
        onClicked: forceActiveFocus()
    }

    ListModel {
        id: iEditableControlsList
    }

    Component {
        id: iSaveChangesPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "%1 game controls").arg(uiBackend.productName)
            pDescription: qsTranslate( "QObject","Would you like to save changes to the game controls and close?")
            pDialogType: UiDialogWindow.Simple
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Save changes")
                        onClicked: {
                            close()
                            fSaveAndClose()
                            plrBackend.resetSplitAdsState()
                        }
                    },
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.secondary
                        pText: qsTranslate("QObject", "Discard")
                        onClicked: {
                            close()
                            iKmmEditorWin.close()
                            plrBackend.resetSplitAdsState()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iDeleteSchemeConfirmationPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Delete scheme")
            pDescription: qsTranslate( "QObject","Are you sure you want to delete this control scheme?")
            pDialogType: UiDialogWindow.Simple
            pCheckBoxState: false
            pCheckBoxText: qsTranslate( "QObject","Do not show again")
            onCheckBoxClicked: {
                uiBackend.confSetBool("bst.show_scheme_delete_warning", !checked)
                uiBackend.confCommit()
            }
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.danger
                        pText: qsTranslate("QObject", "Delete")
                        onClicked: {
                            fDeleteScheme(pDeleteSchemeName, pDeleteSchemeIndex)
                            close()
                        }
                    },
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.secondary
                        pText: qsTranslate("QObject", "Cancel")
                        onClicked: {
                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iResetChangesPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Reset to default")
            pDescription: qsTranslate( "QObject","The selected scheme will reset to the default scheme provided by %1, all your changes will be lost.").arg(uiBackend.productName)
            pDialogType: UiDialogWindow.Simple
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.danger
                        pText: qsTranslate("QObject", "Reset")
                        onClicked: {
                            close()
                            pEnableSave = false
                            pEnableReset = false
                            kmmBackend.kmmEditorResetAndReload()
                        }
                    },
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.secondary
                        pText: qsTranslate("QObject", "Cancel")
                        onClicked: {
                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iUpdateCfgPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Update control schemes")
            pDescription: qsTranslate( "QObject","All control schemes provided by %1 will be updated. Any edits made previously will be removed.").arg(uiBackend.productName)
            pDialogType: UiDialogWindow.Simple
            pButtonsList: DialogButtonModelList {
                pButtons: [
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.accent
                        pText: qsTranslate("QObject", "Update")
                        onClicked: {
                            close()
                            pUpdateCfgInProgress = true
                            kmmBackend.onUpdateCfgClicked()
                        }
                    },
                    DialogButtonModel {
                        pColor: UiTheme.buttons.colors.secondary
                        pText: qsTranslate("QObject", "Cancel")
                        onClicked: {
                            close()
                        }
                    }
                ]
            }
        }
    }

    Component {
        id: iIncompatibleImportCfgPrompt
        UiMessageDialog {
            title: qsTranslate("QObject", "Incompatible control scheme")
            pDescription: qsTranslate( "QObject","You can only import and use Control Schemes exported from %1 version 4.180 and above.").arg(uiBackend.productName)
            pDialogType: UiDialogWindow.SimpleNarrow
            pHyperlink: qsTranslate("QObject", "Learn why")
            onHyperlinkClicked: uiBackend.openHelpArticle("invalid_scheme")
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

    FileDialog {
        id: iFileDialog
        title: "Select files"
        nameFilters:  ["Cfg files (*.cfg)|*.cfg"]
        fileMode: FileDialog.OpenFile
        visible: false

        acceptLabel: qsTranslate("QObject", "Import")
        onAccepted: {
            iFileDialog.close()

            var component = Qt.createComponent("KmmSchemesImportWindow.qml");
            if (component.status === Component.Ready) {
                if(file.toString().endsWith(".lnk"))
                    return
                var isCfgCompatible = kmmBackend.getImportSchemesModel(file)
                if(isCfgCompatible) {
                    pSchemeImportWindow = component.createObject(iAppwin);
                    pSchemeImportWindow.show();
                }
                else
                    iIncompatibleImportCfgPrompt.createObject(iKmmEditorWin, { "transientParent": iKmmEditorWin }).show()
            }
        }
    }

    UiRowLayout {
        anchors.fill: parent

        //canvas window
        Rectangle {
            id: iCanvasRect
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Qt.rgba(8.0 / 255, 10.0 / 255, 31.0 / 255, 0.6)

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton

                onReleased: function(mouse) {
                    var currMouseX = (((mouse.x - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
                    var currMouseY = (((mouse.y - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
                    var itemComponent = Qt.createComponent("KmmEditorTap.qml")

                    if(pGeneratedCanvasTap !== null && pGeneratedCanvasTap.pActiveFocus && pGeneratedCanvasTap.pDeleteOnActiveFocusLost){
                        if(pGeneratedCanvasTap.pKmmControl.key[0] === "" && pGeneratedCanvasTap.pKmmControl.key[1] === "") {
                            Utils.deleteControl(pGeneratedCanvasTap, pGeneratedCanvasTap.pKmmControl)
                            kmmBackend.setFocusedVisualItem(null)
                        }
                        pGeneratedCanvasTap.pDeleteOnActiveFocusLost = false
                        pGeneratedCanvasTap = null
                        focus = true
                    }

                    else if (itemComponent.status === Component.Ready) {
                        var draggedItem = itemComponent.createObject(iKmmEditorWin, {x: currMouseX, y: currMouseY});
                        pGeneratedCanvasTap = draggedItem
                        draggedItem.fCreateNewControl()
                        draggedItem.pKmmControl.x = currMouseX
                        draggedItem.pKmmControl.y = currMouseY
                        draggedItem.pDeleteOnActiveFocusLost = true
                        draggedItem.fInEditableMode()
                    }
                }
            }

            UiRowLayout {
                id: iKeyControlsSwitchRow
                anchors.leftMargin: 5
                anchors.topMargin: 5
                anchors.top: iCanvasRect.top
                anchors.horizontalCenter: iCanvasRect.horizontalCenter
                Rectangle {
                    id: iKeyboardRect
                    Layout.preferredWidth: 130
                    Layout.preferredHeight: 25
                    color: kmmBackend.gamepadTabSelected ? UiTheme.colors.primary60 : UiTheme.colors.primary80
                    UiColumnLayout {
                        UiText {
                            Layout.margins: 5
                            Layout.alignment: Qt.AlignCenter
                            Layout.maximumWidth: iKeyboardRect.width - 20
                            font: UiTheme.fonts.bodyExtraSmall
                            text: qsTranslate("QObject", "Keyboard and mouse")
                            color: kmmBackend.gamepadTabSelected ? UiTheme.colors.primary20 : UiTheme.colors.primary10
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            id: iKeyboardRectLine
                            Layout.alignment: Qt.AlignBottom
                            width: 130
                            height: 1
                            color: !kmmBackend.gamepadTabSelected ? UiTheme.colors.accentNormal : UiTheme.colors.primary60
                        }
                    }
                    MouseArea {
                        hoverEnabled: true
                        anchors.fill: parent
                        onClicked: kmmBackend.gamepadTabSelected = false
                    }
                }

                Rectangle {
                    id: iGamepadRect
                    color: kmmBackend.gamepadTabSelected ? UiTheme.colors.primary80 : UiTheme.colors.primary60
                    Layout.preferredWidth: 130
                    Layout.preferredHeight: 25
                    UiColumnLayout {
                        UiText {
                            Layout.margins: 5
                            Layout.maximumWidth: iGamepadRect.width - 20
                            Layout.alignment: Qt.AlignCenter
                            font: UiTheme.fonts.bodyExtraSmall
                            text: qsTranslate("QObject", "Gamepad")
                            color: kmmBackend.gamepadTabSelected ? UiTheme.colors.primary10 : UiTheme.colors.primary20
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            id: iGamepadRectLine
                            Layout.alignment: Qt.AlignBottom
                            width: 130
                            height: 1
                            color: kmmBackend.gamepadTabSelected ? UiTheme.colors.accentNormal : UiTheme.colors.primary60
                        }
                    }
                    MouseArea {
                        hoverEnabled: true
                        anchors.fill: parent
                        onClicked: {
                            if(iGamepadRect.opacity === 1) kmmBackend.gamepadTabSelected = true
                        }
                    }
                }
                Component.onCompleted: {
                    var detectionEnabled = uiBackend.confGetBool("bst.enable_gamepad_detection")
                    kmmBackend.gamepadTabSelected = kmmBackend.gamepadConnectedState && detectionEnabled
                    iGamepadRect.opacity = kmmBackend.gamepadTabSelected ? 1 : 0.5
                }
            }
        }

        //editor controls
        Rectangle {
            id: iEditorControlsRect
            Layout.preferredWidth: pKmmEditorPanelWidth
            Layout.fillHeight: true
            color: UiTheme.colors.primary80
            z: 1

            UiColumnLayout {
                anchors.fill: parent

                UiRowLayout {
                    leftPadding: 8
                    color: UiTheme.colors.primary80
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        font: UiTheme.fonts.titleExtraSmall
                        color: UiTheme.colors.primary10
                        text: qsTranslate("QObject", "Controls editor")
                        elide: Text.ElideRight
                    }
                    UiImageButton {
                        asset: "TitlebarHelp"
                        pImageWidth: 32
                        pImageHeight: 32
                        UiToolTip.text: qsTranslate("QObject", "Help")
                        pMouseCursor: hovered ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: uiBackend.openHelpArticle("advanced_game_control")
                    }
                    UiImageButton {
                        asset: "TitlebarClose"
                        pImageWidth: 32
                        pImageHeight: 32
                        UiToolTip.text: qsTranslate("QObject", "Close")
                        onClicked: {
                            if (pEnableSave)
                                iSaveChangesPrompt.createObject(iKmmEditorWin, { "transientParent": iKmmEditorWin }).show()
                            else {
                                iKmmEditorWin.close()
                                plrBackend.resetSplitAdsState()
                            }
                        }
                    }
                }

                UiColumnLayout {
                    spacing: 8
                    padding: 8
                    color: UiTheme.colors.primary70
                    Layout.fillWidth: true
                    UiRowLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        Text {
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.primary20
                            text: qsTranslate("QObject", "Control scheme")
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        UiImageButton {
                            asset: "CloudUpdate"
                            pImageWidth: 16
                            pImageHeight: 16
                            visible: !pUpdateCfgInProgress
                            UiToolTip.text: qsTranslate("QObject", "Update controls")
                            onClicked: iUpdateCfgPrompt.createObject(iKmmEditorWin, { "transientParent": iKmmEditorWin }).show()
                        }
                        UiImage {
                            id: iLoader
                            asset: "Spinner_bright"
                            pImageWidth: 16
                            pImageHeight: 16
                            visible: pUpdateCfgInProgress
                            cache: false
                            RotationAnimation on rotation {
                                loops: Animation.Infinite
                                duration: 2000
                                from: 0
                                to: 360
                                direction: RotationAnimation.Clockwise
                                running: iLoader.visible
                            }
                        }
                        UiImageButton {
                            asset: "Import"
                            pImageWidth: 16
                            pImageHeight: 16
                            UiToolTip.text: qsTranslate("QObject", "Import")
                            onClicked: iFileDialog.open()
                        }
                        UiImageButton {
                            asset: "Export"
                            pImageWidth: 16
                            pImageHeight: 16
                            UiToolTip.text: qsTranslate("QObject", "Export")
                            enabled: iSchemeCombobox.count > 0
                            onClicked: {
                                var component = Qt.createComponent("KmmSchemesExportWindow.qml");
                                if (component.status === Component.Ready) {
                                    var list = kmmBackend.exportSchemesModel
                                    pSchemeExportWindow = component.createObject(iAppwin, {"pModel": list[0], "pPackage": list[1]});
                                    pSchemeExportWindow.show();
                                }
                            }
                        }
                        UiImageButton {
                            asset: "Folder"
                            pImageWidth: 16
                            pImageHeight: 16
                            UiToolTip.text: qsTranslate("QObject", "Open folder")
                            onClicked: kmmBackend.onOpenUserfilesFolderClick()
                        }
                    }

                    UiComboBox {
                        id: iSchemeCombobox
                        pSize: UiTheme.comboBoxes.sizes.small
                        Layout.fillWidth: true
                        displayText: (schemeIdx === -1 || iSchemeCombobox.count === 0) ? qsTranslate("QObject", "Custom"): currentText
                        delegate: UiRowLayout {
                            id: iDelegate
                            width: ListView.view.width
                            height: iSchemeCombobox.height
                            color: pMouseArea.containsMouse || iMouseArea.containsMouse
                                   ? UiTheme.comboBoxes.colors.accent.dropDownHighlight
                                   : (iSchemeCombobox.currentIndex === index
                                      ? UiTheme.comboBoxes.colors.accent.dropDownSelection
                                      : UiTheme.comboBoxes.colors.accent.normal.background)
                            spacing: 8
                            leftPadding: 8
                            rightPadding: 8

                            pMouseArea {
                                enabled: true
                                anchors.fill: iDelegate
                                hoverEnabled: true
                                onClicked: {
                                    iSchemeCombobox.currentIndex = index
                                    iSchemeCombobox.popup.close()
                                    if (pLoaded)
                                        kmmBackend.schemeChanged(modelData)
                                }
                            }

                            property var pIsSavedOnFocusLost : false

                            function fRenameScheme() {
                                iEditImg.asset = "EditIcon"
                                iEditImg.UiToolTip.text = qsTranslate("QObject", "Edit")

                                var trimmedNewName = iTextField.text.trim()
                                if(trimmedNewName !== "")
                                {
                                    var ind = iSchemeCombobox.model.indexOf(trimmedNewName)
                                    if(/^[^</*?"\\>:|]+$/.test(trimmedNewName) && ind === -1) {
                                        iText.text = trimmedNewName
                                        var list = kmmBackend.onSchemeNameChanged(modelData, trimmedNewName)
                                        fSetModelAndIndex(list[1], parseInt(list[0]), list[2])
                                        iSchemeCombobox.currentIndex = parseInt(list[0])
                                    }
                                    else{
                                        iTextField.text = modelData
                                        if(/^[^</*?"\\>:|]+$/.test(trimmedNewName) == false)
                                            fShowErrorMessageToast(qsTranslate("QObject", "Error: Scheme name cannot contain") + "\n\\ / | ? * < > : \"")
                                        else if(ind !== index)
                                            fShowErrorMessageToast(qsTranslate("QObject", "Error: Invalid scheme name"))
                                    }
                                }
                                else {
                                    iTextField.text = modelData
                                    fShowErrorMessageToast(qsTranslate("QObject", "Error: Invalid scheme name"))
                                }
                            }

                            Text {
                                id: iText
                                text: modelData
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font: UiTheme.fonts.bodySmall
                                color: UiTheme.colors.primary10
                                visible: iEditImg.asset === "EditIcon" ? true : false
                                UiToolTip.text: text
                                UiToolTip.visible: iMouseArea.containsMouse && truncated
                                MouseArea {
                                    id: iMouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        iSchemeCombobox.currentIndex = index
                                        iSchemeCombobox.popup.close()
                                        kmmBackend.schemeChanged(modelData)
                                    }
                                }
                            }
                            UiTextField {
                                id: iTextField
                                text: modelData
                                Layout.fillWidth: true
                                pSize: UiTheme.textFields.sizes.extraSmall
                                font: UiTheme.fonts.bodySmall
                                color: UiTheme.colors.primary10
                                visible: iEditImg.asset === "SaveSmall" ? true : false
                                onFocusChanged:{
                                    if(!focus)
                                        pIsSavedOnFocusLost = true
                                }
                                onEditingFinished: fRenameScheme()
                                onVisibleChanged: {
                                    if(visible){
                                        selectAll()
                                    }
                                }
                            }
                            UiImageButton {
                                id: iEditImg
                                asset: "EditIcon"
                                UiToolTip.text: qsTranslate("QObject", "Edit")
                                pImageWidth: 12
                                pImageHeight: 12
                                enabled: index < pBuiltinIndex ? false : true
                                opacity: index < pBuiltinIndex ? 0 : 1
                                onPressed: {
                                    if(asset === "SaveSmall") {
                                        fRenameScheme()
                                    }
                                    else if(!pIsSavedOnFocusLost){
                                        iTextField.focus = true
                                        asset = "SaveSmall"
                                        UiToolTip.text = qsTranslate("QObject", "Save")
                                    }
                                    else
                                        pIsSavedOnFocusLost = false
                                }
                            }
                            UiImageButton {
                                asset: "DuplicateSmall"
                                pImageWidth: 12
                                pImageHeight: 12
                                UiToolTip.text: qsTranslate("QObject", "Copy")
                                onClicked: {
                                    var list = kmmBackend.cloneScheme(modelData)
                                    fSetModelAndIndex(list[1], parseInt(list[0]), list[2])
                                }
                            }

                            UiImageButton {
                                asset: "DeleteSmall"
                                pImageWidth: 12
                                pImageHeight: 12
                                UiToolTip.text: qsTranslate("QObject", "Delete")
                                enabled: index < pBuiltinIndex ? false : true
                                opacity: index < pBuiltinIndex ? 0 : 1
                                onClicked: {
                                    if(uiBackend.confGetBool("bst.show_scheme_delete_warning")){
                                        pDeleteSchemeIndex = index
                                        pDeleteSchemeName = modelData
                                        iDeleteSchemeConfirmationPrompt.createObject(iKmmEditorWin, { "transientParent": iKmmEditorWin }).show()
                                    }
                                    else
                                        fDeleteScheme(modelData, index)
                                }
                            }
                        }

                        popup: Popup {
                            y: iSchemeCombobox.height
                            width: iSchemeCombobox.width
                            padding: 0

                            onOpened: fDestroyChildWindows()
                            contentItem: UiColumnLayout {
                                padding: 1
                                border.width: 1
                                border.color: iSchemeCombobox.pColor.normal.border
                                ListView {
                                    implicitHeight: Math.min(contentHeight, iSchemeCombobox.pSize.maxDropDownHeight, iSchemeCombobox.Window.height)
                                    Layout.fillWidth: true
                                    model: iSchemeCombobox.delegateModel
                                    currentIndex: iSchemeCombobox.highlightedIndex
                                    highlightMoveDuration: 0
                                    clip: true

                                    ScrollBar.vertical: UiScrollBar {
                                        policy: parent.contentHeight > parent.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                                    }
                                }
                                UiRowLayout {
                                    id: iContentFooter
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: iSchemeCombobox.height
                                    color: pMouseArea.containsMouse ? UiTheme.colors.primary80 : UiTheme.colors.primary90
                                    leftPadding: 8
                                    rightPadding: 8
                                    spacing: 8
                                    UiImage {
                                        asset: "AddSmall"
                                        pImageWidth: 12
                                        pImageHeight: 12
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTranslate("QObject", "Create new profile")
                                        font: UiTheme.fonts.bodySmall
                                        color: UiTheme.colors.primary10
                                        elide: Text.ElideRight
                                    }

                                    pMouseArea {
                                        enabled: true
                                        anchors.fill: iContentFooter
                                        hoverEnabled: true
                                        onPressed: {
                                            var list = kmmBackend.cloneScheme("")
                                            schemeModel = list[1]
                                            schemeIdx = parseInt(list[0])
                                            pBuiltinIndex = list[2]
                                        }
                                    }
                                }
                            }

                            background: Rectangle {
                                color: iSchemeCombobox.pColor.normal.background
                            }
                        }
                    }
                }

                Rectangle {
                    id: iSchemeShortcutText
                    Layout.fillWidth: true
                    color: UiTheme.colors.primary70
                    border.width: 1
                    border.color: UiTheme.colors.primary60
                    visible: iSchemeCombobox.count > 1
                    implicitHeight: contentLayout.implicitHeight + 8
                    UiRowLayout {
                        id: contentLayout
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4
                        UiImage {
                            pImageWidth: 12
                            pImageHeight: 12
                            asset: "InfoSmall"
                            Layout.alignment: Qt.AlignCenter
                        }
                        Text {
                            wrapMode: Text.Wrap
                            textFormat: Text.RichText
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignTop
                            Layout.fillWidth: true
                            font: UiTheme.fonts.bodySmallest
                            color: UiTheme.colors.primary30
                            text: {
                                  let shortcut = uiBackend.confGetString("bst.shortcut.switch_scheme");
                                  let styledShortcut = "<font color='" + UiTheme.colors.primary20 + "'>" + shortcut + "</font>";
                                  return qsTranslate("QObject", "Quick switch scheme in-game: %1").arg(styledShortcut);
                              }
                        }
                    }
                }
                UiColumnLayout {
                    spacing: 4
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    UiColumnLayout {
                        leftPadding: 8
                        rightPadding: 8
                        topPadding: 8
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.primary10
                            text: qsTranslate("QObject", "Add controls")
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            font: UiTheme.fonts.bodyExtraSmall
                            color: UiTheme.colors.primary30
                            wrapMode: Text.Wrap
                            text: qsTranslate("QObject", "Drag and drop an action on your game screen to assign a key to it. Click on \"?\" to learn more about how to use the controls editor.")
                        }
                    }

                    UiScrollView {
                        Layout.fillHeight: true
                        Layout.fillWidth: true
                        leftPadding: 8
                        clip: true
                        contentWidth: parent.width - 8
                        contentHeight: iEditableControlsPanel.height

                        Flow {
                            id: iEditableControlsPanel
                            spacing: 8
                            anchors.left: parent.left
                            anchors.right: parent.right
                            Repeater {
                                model: iEditableControlsList
                                delegate: KmmEditorDragControls {
                                    id: iControlRect
                                    width: 76
                                    height: 90
                                }
                            }
                        }
                    }
                }

                UiColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    padding: 8
                    color: UiTheme.colors.primary70
                    Text {
                        Layout.fillWidth: true
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.primary20
                        text: qsTranslate("QObject", "Current configuration actions")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                    }
                    UiRowLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        UiButton {
                            pSize: UiTheme.buttons.sizes.small
                            pColor: UiTheme.buttons.colors.danger
                            Layout.fillWidth: true
                            text: qsTranslate("QObject", "Reset")
                            opacity: pEnableReset ? 1 : 0.4
                            enabled: pEnableReset
                            onClicked: {
                                fDestroyChildWindows()
                                iResetChangesPrompt.createObject(iKmmEditorWin, { "transientParent": iKmmEditorWin }).show()
                            }
                        }
                        UiButton {
                            pSize: UiTheme.buttons.sizes.small
                            pColor: UiTheme.buttons.colors.accent
                            Layout.fillWidth: true
                            text: qsTranslate("QObject", "Save")
                            opacity: pEnableSave ? 1 : 0.4
                            enabled: pEnableSave
                            onClicked: {
                                pEnableSave = false
                                pEnableReset = true

                                fDestroyChildWindows()
                                kmmBackend.kmmEditorSaveAndReload()
                                fShowErrorMessageBottomToast(qsTranslate("QObject", "Changes saved"))
                                pEnableSave = false
                            }
                        }
                    }
                }
            }

            UiToast {
                id: iToast
                anchors.horizontalCenter: parent.horizontalCenter
                pMaximumWidth: pKmmEditorPanelWidth - 16
                pShowCross: false
            }
        }
    }

    onClosing: {
        iKmmEditorWin.destroy()
        kmmBackend.gamepadTabSelected = false
        kmmBackend.kmmEditorClose()
        uiBackend.pIsShortcutsEnabled = true
        uiBackend.pIsEditingTextfield = false
        
        plrBackend.resetSplitAdsState()
    }

    Component {
        id: iGamepadInfoPopupComponent
        UiHelpPopup {
            id: iGampepadInfoPopup
            contentWidth: 228
            visible: false
            x: iKmmEditorWin.x + (iKmmEditorWin.width - pKmmEditorPanelWidth - iGamepadRect.width)/2
            y: iKmmEditorWin.y + 16
            arrowPosition: UiHelpPopup.ArrowPosition.TopCenter
            pInnerLayout {
                border.color: UiTheme.colors.accentNormal
                color: UiTheme.colors.accentNormal
                padding: 8
                spacing: 8
            }

            Text {
                text: qsTranslate("QObject", "Create or edit your gamepad controls here.")
                font: UiTheme.fonts.titleExtraSmall
                color: UiTheme.colors.primary10
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }

    function fOpenGamepadInfoPopup() {
        var gamepadInfoWin = iGamepadInfoPopupComponent.createObject(iKmmEditorWin)
        gamepadInfoWin.show()
    }

    function fDisableAll() {
        pEnableSave = pEnableReset = false
    }

    function fSaveAndClose() {
        kmmBackend.kmmEditorSave()
        iKmmEditorWin.close()
    }

    function fSetModelAndIndex(model, idx, builtinIndex){
        schemeModel = model
        schemeIdx = idx
        pBuiltinIndex = builtinIndex
    }

    function fShowErrorMessageToast(msg){
        if(pUpdateCfgInProgress)
            pUpdateCfgInProgress = false
        iToast.y = iEditorControlsRect.y + 20
        iToast.fShow(msg, 2000)
    }

    function fShowErrorMessageBottomToast(msg) {
        if(pUpdateCfgInProgress)
            pUpdateCfgInProgress = false
        iToast.y = iEditorControlsRect.y + iEditorControlsRect.height - 60
        iToast.fShow(msg, 2000)
    }

    function fDestroyChildWindows() {
        for (var i = pChildWinList.length - 1; i >= 0; --i) {
            if (pChildWinList[i] !== null && pChildWinList[i].contentItem !== undefined) {
                pChildWinList[i].close()
            }
            pChildWinList.pop()
        }
    }

    function fReinitWindow(){
        pEnableReset = false
        pEnableSave = false
        var schemeInfo = kmmBackend.editorSchemes
        schemeModel = schemeInfo[1]
        schemeIdx = schemeInfo[0]
        pBuiltinIndex = schemeInfo[2]
    }

    function fRelayoutWindow() {
        iKmmEditorWin.width = iAppwin.width - iAppwin.pSidebarWidth + iKmmEditorWin.pKmmEditorPanelWidth
        iKmmEditorWin.height = iAppwin.height
        iKmmEditorWin.x = iAppwin.x
        iKmmEditorWin.y = iAppwin.y
    }

    function fDeleteScheme(name, index){

        var i = iSchemeCombobox.currentIndex
        if(index < i)
            i--;
        else if (index === i)
            i = 0;
        fSetModelAndIndex(kmmBackend.deleteScheme(iSchemeCombobox.model, name, index), parseInt(i), pBuiltinIndex)
    }
}
