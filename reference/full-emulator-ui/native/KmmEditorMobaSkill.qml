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
    width: 44
    height: width
    z: 1

    property QtObject pKmmControl: null
    property var pStartingMouse
    property bool pOpenMobaSkillHelpPopup: false
    property bool pOpenMobaMoveHelpPopup: false
    property bool pOpenMobaSkillPopup: false
    property string pType: "MOBASkill"

    property real pResizeUnit: 24
    property bool pGamepadTabSelected: false
    property bool pEditorWindowGamepadTabSelected: false

    property KmmEditorControlSettings pSettingsWin: null

    property KmmEditorMobaSkillCancel pMobaSkillCancel: null

    ButtonGroup {
        id: iMobaSkillBtnGrp
    }

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var index = pGamepadTabSelected ? 1 : 0
            pKmmControl.keyActivate[index] = updatedPropsMap[qsTranslate("QObject", "Activate")]
            pKmmControl.keyCancel[index] = updatedPropsMap[qsTranslate("QObject", "Cancel")]
            pKmmControl.keyAutocastToggle[index] = updatedPropsMap[qsTranslate("QObject", "Autocast toggle key")]
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.originX = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "OriginX")])
            pKmmControl.originY = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "OriginY")])
            pKmmControl.cancelX = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "CancelX")])
            pKmmControl.cancelY = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "CancelY")])
            pKmmControl.radius = updatedPropsMap[qsTranslate("QObject", "Radius")]
            pKmmControl.stopMOBADpad = updatedPropsMap[qsTranslate("QObject", "Stop MOBA Dpad")]
            pKmmControl.isCancelSkillEnabled = updatedPropsMap[qsTranslate("QObject", "Cancel skill enabled")]
            pKmmControl.showOnOverlay = updatedPropsMap[qsTranslate("QObject", "Show on overlay")]
            pKmmControl.gamepadStick = updatedPropsMap[qsTranslate("QObject", "Gamepad stick")]
            pKmmControl.yAxisRatio = updatedPropsMap[qsTranslate("QObject", "Y axis ratio")]
            pKmmControl.deadzoneRadius = updatedPropsMap[qsTranslate("QObject", "Deadzone radius")]
            pKmmControl.minSwipeRadius = updatedPropsMap[qsTranslate("QObject", "Min swipe radius")]
            pKmmControl.noCancelOnSwitch = updatedPropsMap[qsTranslate("QObject", "No cancel on switch")]
            pKmmControl.noCancelTime = updatedPropsMap[qsTranslate("QObject", "No cancel time")]
            pKmmControl.minSkillTime = updatedPropsMap[qsTranslate("QObject", "Min skill time")]
            pKmmControl.minSkillHoldTime = updatedPropsMap[qsTranslate("QObject", "Min skill hold time")]
            pKmmControl.autoAttack = updatedPropsMap[qsTranslate("QObject", "Auto attack")]
            pKmmControl.advancedMode = updatedPropsMap[qsTranslate("QObject", "Advanced mode")]
            pKmmControl.autocastEnabled = updatedPropsMap[qsTranslate("QObject", "Autocast enabled")]
            pKmmControl.tweaks = updatedPropsMap[qsTranslate("QObject", "Tweaks")]
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Speed")]
            pKmmControl.cancelSpeed = updatedPropsMap[qsTranslate("QObject", "Cancel speed")]
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
            pKmmControl.selectedSkillType = updatedPropsMap[qsTranslate("QObject", "Skill radius")]
            iTextEditField.fSetText(kmmBackend.getKeyList(kmmBackend.gamepadTabSelected ? pKmmControl.keyActivate[1] : pKmmControl.keyActivate[0]))

            if(!pKmmControl.isCancelSkillEnabled) {
                pKmmControl.cancelX = -1
                pKmmControl.cancelY = -1
                pKmmControl.keyCancel[0] = ""
                pKmmControl.keyCancel[1] = ""
            }

            if (pMobaSkillCancel) {
                pMobaSkillCancel.fSetCancelKey(kmmBackend.gamepadTabSelected ? pKmmControl.keyCancel[1] : pKmmControl.keyCancel[0])
                pMobaSkillCancel.pSettingsWindowVisible = false
            }
        }

        function onDeleteControl() {
            fDeleteVisualItem()
        }

        function onCreateExtraProp2Item(x, y) {
            pKmmControl.cancelX = (((x - plrBackend.glWinRect[0]) * 100) / plrBackend.glWinRect[2]).toFixed(2)
            pKmmControl.cancelY = (((y - plrBackend.glWinRect[1]) * 100) / plrBackend.glWinRect[3]).toFixed(2)
            if (pKmmControl.keyCancel[0] === "")
                pKmmControl.keyCancel[0] = "Space"
            fCreateMobaSkillCancel()
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }
    }

    Connections {
        target: pMobaSkillCancel

        function onSettingsBtnClicked(x, y) {
            fOpenSettingsWindow(x, y)
        }
        function onDeleteClicked() {
            kmmBackend.kmmEditorDeleteVisualItem(pMobaSkillCancel)
            pKmmControl.cancelX = -1
            pKmmControl.cancelY = -1
            pKmmControl.isCancelSkillEnabled = false
            pKmmControl.keyCancel[0] = ""
            pKmmControl.keyCancel[1] = ""
        }
    }

    UiImage {
        id: iBaseImg
        asset: "EditorSkillPad"
        pImageWidth: 38
        pImageHeight: 38
        anchors.centerIn: parent
        property var globalPos

        MouseArea {
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
                        iBaseImg.mapToGlobal(0,0).y === iBaseImg.globalPos.y) {
                    iTextEditField.fInEditableMode()
                }
            }
        }
    }

    UiImage {
        id: iBaseImg1
        asset: "EditorLargeCircle"
        pImageWidth: iRoot.width
        pImageHeight: iRoot.width
        visible: false
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
            onPressed: function(mouse) {
                mouse.accepted = false
            }
            onEntered: {
                if (pSettingsWin === null) {
                    iDeleteBtn.visible = true
                    iSettingsBtn.visible = true
                }
                iBaseImg1.visible = true
            }
            onExited: {
                if(!pOpenMobaSkillPopup) {
                    iDeleteBtn.visible = false
                    iSettingsBtn.visible = false
                    if(!iScaleBtn.hovered)
                        iBaseImg1.visible = false
                }
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
                onClicked: fOpenSettingsPopup()
            }
        }
    }

    UiImageButton {
        id: iScaleBtn
        asset: "ScaleIcon"
        pImageWidth: 10
        pImageHeight: 10
        anchors.right: iBaseImg1.visible ? iRoot.right : iBaseImg.right
        anchors.bottom: iBaseImg1.visible ? iRoot.bottom : iBaseImg.bottom
        onFocusChanged: focus ? Qt.SizeFDiagCursor : Qt.ArrowCursor

        MouseArea {
            id: iScaleBtnMouseArea
            anchors.fill: iScaleBtn
            hoverEnabled: true
            propagateComposedEvents: false
            cursorShape: Qt.SizeFDiagCursor

            drag {
                target: iScaleBtn
                axis: Drag.XAndYAxis
            }

            onEntered: {
                iBaseImg1.visible = true
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

    KmmEditorTextEditBox {
        id: iTextEditField
        anchors.centerIn: parent
        onUpdateKey: function(key) {
            if(pEditorWindowGamepadTabSelected) pKmmControl.keyActivate[1] = key
            else pKmmControl.keyActivate[0] = key
        }
        onPEditFieldActiveFocusChanged: if(pEditFieldActiveFocus) kmmBackend.setFocusedVisualItem(iRoot)
    }

    Component {
        id: iMobaSkillPopupComponent

        UiHelpPopup {
            id: iMobaSkillPopup
            contentWidth: 150
            closeOnPressOutside: !pOpenMobaSkillHelpPopup && !pOpenMobaMoveHelpPopup
            visible: false
            x: iSettingsBtn.mapToGlobal(0, 0).x - (width - iSettingsBtn.width) / 2
            y: iSettingsBtn.mapToGlobal(0, 0).y - height
            onWidthChanged: {
                iMobaSkillPopup.x = iSettingsBtn.mapToGlobal(0, 0).x - (iMobaSkillPopup.width - iSettingsBtn.width)/2
            }

            pInnerLayout {
                border.width: 1
                border.color: UiTheme.colors.primary60
                color: UiTheme.colors.primary70
                padding: 8
                spacing: 8
            }

            UiRowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTranslate("QObject", "Skill settings")
                    font: UiTheme.fonts.titleSmallest
                    color: UiTheme.colors.primary10
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                UiImageButton {
                    asset: "MenuHelp"
                    pImageWidth: 12
                    pImageHeight: 12
                    Layout.alignment: Qt.AlignRight
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: {
                            if(!pOpenMobaSkillHelpPopup) {
                                pOpenMobaSkillHelpPopup = true
                                fOpenMobaSkillHelpPopup(iMobaSkillPopup);
                            }
                        }
                    }
                }
            }

            UiColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                UiRadioButton {
                    id: iQuickCastBtn
                    text: qsTranslate("QObject", "Quick cast")
                    ButtonGroup.group: iMobaSkillBtnGrp
                    pSize: UiTheme.radioButtons.sizes.small
                    Layout.fillWidth: true
                    onCheckedChanged: if(checked) fAssignMobaSkill("QuickCast")
                }
                UiRadioButton {
                    id: iAutoCastBtn
                    text: qsTranslate("QObject", "Auto cast")
                    Layout.fillWidth: true
                    ButtonGroup.group: iMobaSkillBtnGrp
                    pSize: UiTheme.radioButtons.sizes.small
                    onCheckedChanged: if(checked) fAssignMobaSkill("AutoCast")
                }
                UiRadioButton {
                    id: iManualCastBtn
                    text: qsTranslate("QObject", "Manual cast")
                    Layout.fillWidth: true
                    ButtonGroup.group: iMobaSkillBtnGrp
                    pSize: UiTheme.radioButtons.sizes.small
                    onCheckedChanged: if(checked) fAssignMobaSkill("ManualCast")
                }
            }

            UiRowLayout {
                id: iSkillRadiusLayout
                Layout.fillWidth: true
                spacing: 4
                visible: fSetSkillRadiusLayoutVisibility()
                Text {
                    text: qsTranslate("QObject", "Skill radius")
                    font: UiTheme.fonts.titleSmallest
                    color: UiTheme.colors.primary10
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                UiImageButton {
                    asset: "MenuHelp"
                    pImageWidth: 12
                    pImageHeight: 12
                    Layout.alignment: Qt.AlignRight
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: {
                            cursorShape = Qt.PointingHandCursor
                        }
                        onClicked: {
                            cursorShape = Qt.ArrowCursor
                            uiBackend.openHelpArticle("moba_skill_radius_help")
                        }
                        onExited: {
                            cursorShape = Qt.ArrowCursor
                        }
                    }
                }
            }

            UiComboBox {
                id: iSkillRadiusCombobox
                Layout.fillWidth: true
                pSize: UiTheme.comboBoxes.sizes.small
                model: ListModel { }
                textRole: "displayText"
                valueRole: "value"
                pContentRightPadding: 2
                visible: iSkillRadiusLayout.visible
                onActivated: {
                    fAssignMobaSkillRadius(iSkillRadiusCombobox.currentValue)
                }
            }

            UiRowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTranslate("QObject", "Other settings")
                    font: UiTheme.fonts.titleSmallest
                    color: UiTheme.colors.primary10
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                UiImageButton {
                    asset: "MenuHelp"
                    pImageWidth: 12
                    pImageHeight: 12
                    Layout.alignment: Qt.AlignRight
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: {
                            if(!pOpenMobaMoveHelpPopup) {
                                pOpenMobaMoveHelpPopup = true
                                fOpenMobaMovementPopup(iMobaSkillPopup)
                            }
                        }
                    }
                }
            }

            UiCheckBox {
                id:iStopMovementCheckBox
                pSize: UiTheme.checkBoxes.sizes.small
                checked: pKmmControl.stopMOBADpad
                Layout.fillWidth: true
                onCheckStateChanged: pKmmControl.stopMOBADpad = checked
                text: qsTranslate("QObject", "Stop movement")
            }

            Text {
                id: iMoreSettingsTxt
                text: qsTranslate("QObject", "More settings")
                font: UiTheme.fonts.bodySmall
                color: UiTheme.colors.accentNormal
                horizontalAlignment: Text.AlignLeft
                Layout.fillWidth: true
                wrapMode: Text.Wrap
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

            onActiveChanged: {
                if (closeOnPressOutside && iMobaSkillPopup.activeFocusItem === null)
                {
                    iMobaSkillPopup.close()
                    pOpenMobaSkillPopup = false
                }
            }

            Component.onCompleted:  {
                if(pKmmControl.advancedMode && pKmmControl.autocastEnabled) {
                    iQuickCastBtn.checked = true
                }
                else if(pKmmControl.advancedMode && !pKmmControl.autocastEnabled) {
                    iAutoCastBtn.checked = true
                }
                else {
                    iManualCastBtn.checked = true
                }
                populateSkillRadiusModel()
            }

            function fHandleActiveChanged(isActive) {
                if (activeFocusItem == null && closeOnPressOutside) {
                    close()
                    pOpenMobaSkillPopup = false
                }
            }

            function populateSkillRadiusModel()
            {
                var selectedSkill = pKmmControl.selectedSkillType !== "" ? pKmmControl.selectedSkillType : "Default"
                for (var i = 0; i < kmmBackend.skillRadiusTypes.length; ++i) {
                    var item = kmmBackend.skillRadiusTypes[i];
                    iSkillRadiusCombobox.model.append({"displayText": item.displayText, "value": item.value});
                    if (item.value === selectedSkill) {
                        iSkillRadiusCombobox.currentIndex = i
                    }
                }
            }
        }
    }

    Component {
        id: iMobaSkillHelpPopupComponent
        UiWindowedPopup {
            property var parentControl: null

            id: iMobaSkillHelpPopup
            contentWidth: iMobaSkillHelpPopupRect.width
            contentHeight: iMobaSkillHelpPopupRect.height
            visible: false

            onWidthChanged: {
                iMobaSkillHelpPopup.x = iSettingsBtn.mapToGlobal(0, 0).x - iMobaSkillHelpPopup.width + iMobaSkillHelpPopup.shadowThickness
            }

            Rectangle {
                id: iMobaSkillHelpPopupRect
                width: iMobaSkillHelpContentLayout.width + iMobaSkillHelpContentLayout.anchors.leftMargin
                       + iMobaSkillHelpContentLayout.anchors.rightMargin
                height: iMobaSkillHelpContentLayout.height + iMobaSkillHelpContentLayout.anchors.topMargin
                        + iMobaSkillHelpContentLayout.anchors.bottomMargin
                color: UiTheme.colors.primary70
                border.width: 1
                border.color: UiTheme.colors.primary60
                UiColumnLayout {
                    id: iMobaSkillHelpContentLayout
                    width: 200
                    anchors.centerIn: parent
                    anchors.margins: 8
                    spacing: 6

                    Text {
                        text: qsTranslate("QObject", "Quick cast")
                        font: UiTheme.fonts.titleSmallest
                        color: UiTheme.colors.primary10
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                    }

                    Text {
                        text: qsTranslate("QObject", "Press and release the control key to cast spells")
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.primary20
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTranslate("QObject", "Auto cast")
                        font: UiTheme.fonts.titleSmallest
                        color: UiTheme.colors.primary10
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                    }

                    Text {
                        text: qsTranslate("QObject", "Press and release the control key to cast spells on the nearest enemy")
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.primary20
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTranslate("QObject", "Manual cast")
                        font: UiTheme.fonts.titleSmallest
                        color: UiTheme.colors.primary10
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                    }

                    Text {
                        text: qsTranslate("QObject", "Press and release the control key to cast spells, then left-click of the mouse to cast spells")
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.primary20
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                    }

                    UiRowLayout {
                        Item {
                            Layout.fillWidth: true
                        }

                        Text {
                            id: iReadMoreTxt
                            text: qsTranslate("QObject", "Read more")
                            font: UiTheme.fonts.bodySmall
                            color: UiTheme.colors.accentNormal
                            Layout.topMargin: 6
                            horizontalAlignment: Text.AlignRight

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onEntered: {
                                    cursorShape = Qt.PointingHandCursor
                                    iReadMoreTxt.font.underline = true
                                }
                                onClicked: {
                                    cursorShape = Qt.ArrowCursor
                                    iMobaSkillHelpPopup.visible = false
                                    uiBackend.openHelpArticle("moba_skill_help")
                                }

                                onExited: {
                                    cursorShape = Qt.ArrowCursor
                                    iReadMoreTxt.font.underline = false
                                }
                            }
                        }
                    }
                }
            }

            onClosePopup: {
                pOpenMobaSkillHelpPopup = false
                parentControl.fHandleActiveChanged(false)
            }
        }
    }

    Component {
        id: iMobaSkillMovementHelpPopupComponent

        UiWindowedPopup {
            property var parentControl: null

            id: iMobaSkillMovementHelpPopup
            contentWidth: iMobaSkillMovementHelpPopupRect.width
            contentHeight: iMobaSkillMovementHelpPopupRect.height
            visible: false

            onWidthChanged: {
                iMobaSkillMovementHelpPopup.x = iSettingsBtn.mapToGlobal(0, 0).x - iMobaSkillMovementHelpPopup.width + iMobaSkillMovementHelpPopup.shadowThickness
            }

            Rectangle {
                id: iMobaSkillMovementHelpPopupRect
                width: iMobaSkillMovementHelpContentLayout.width + iMobaSkillMovementHelpContentLayout.anchors.leftMargin
                       + iMobaSkillMovementHelpContentLayout.anchors.rightMargin
                height: iMobaSkillMovementHelpContentLayout.height + iMobaSkillMovementHelpContentLayout.anchors.topMargin
                        + iMobaSkillMovementHelpContentLayout.anchors.bottomMargin
                color: UiTheme.colors.primary70
                border.width: 1
                border.color: UiTheme.colors.primary60
                UiColumnLayout {
                    id: iMobaSkillMovementHelpContentLayout
                    width: 164
                    anchors.centerIn: parent
                    anchors.margins: 8
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: qsTranslate("QObject", "If checked, the hero will stop moving as soon as the spell is cast.")
                        font: UiTheme.fonts.bodyExtraSmall
                        color: UiTheme.colors.primary10
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                    }

                    Text {
                        id: iReadMoreTxt1
                        text: qsTranslate("QObject", "Read more")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.accentNormal
                        Layout.topMargin: 6
                        horizontalAlignment: Text.AlignRight

                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            onEntered: {
                                cursorShape = Qt.PointingHandCursor
                                iReadMoreTxt1.font.underline = true
                            }
                            onClicked: {
                                cursorShape = Qt.ArrowCursor
                                iMobaSkillMovementHelpPopup.visible = false
                                uiBackend.openHelpArticle("moba_skill_movement_help")
                            }

                            onExited: {
                                cursorShape = Qt.ArrowCursor
                                iReadMoreTxt1.font.underline = false
                            }
                        }
                    }
                }
            }

            onClosePopup: {
                pOpenMobaMoveHelpPopup = false
                parentControl.fHandleActiveChanged(false)
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
        iTextEditField.fSetText(kmmBackend.getKeyList(gamepadTabSelected ? pKmmControl.keyActivate[1] : pKmmControl.keyActivate[0]))

        iRoot.width = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.radius
            return (val * pResizeUnit)
        })

        if(pKmmControl.isCancelSkillEnabled && pKmmControl.cancelX !== -1 && pKmmControl.cancelY !== -1) {
            fCreateMobaSkillCancel()
        }
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
    }

    function fInEditableMode() {
        iTextEditField.fInEditableMode()
    }

    function fCreateMobaSkillCancel() {
        if (pMobaSkillCancel === null) {
            pMobaSkillCancel = kmmBackend.kmmEditorCreateVisualItem("MOBASkillCancel", pKmmControl)
        }
    }

    function fOpenSettingsPopup() {
        var mobaskillWin = iMobaSkillPopupComponent.createObject(iRoot.parent)
        mobaskillWin.show()
        pOpenMobaSkillPopup = true
    }

    function fOpenMobaSkillHelpPopup(mobaSkillParentPopup) {
        var mobaskillWin = iMobaSkillHelpPopupComponent.createObject(iRoot.parent, {"parentControl": mobaSkillParentPopup})
        mobaskillWin.x = iSettingsBtn.mapToGlobal(0, 0).x  - mobaskillWin.width/2 + mobaSkillParentPopup.width + 13
        mobaskillWin.y = iSettingsBtn.mapToGlobal(0, 0).y - mobaSkillParentPopup.height + 12
        mobaskillWin.show()
    }

    function fOpenMobaMovementPopup(mobaSkillParentPopup) {
        var mobaskillWin = iMobaSkillMovementHelpPopupComponent.createObject(iRoot.parent, {"parentControl": mobaSkillParentPopup})
        mobaskillWin.x = iSettingsBtn.mapToGlobal(0, 0).x - mobaskillWin.width/2  + mobaSkillParentPopup.width - 10
        mobaskillWin.y = iSettingsBtn.mapToGlobal(0, 0).y +  - mobaSkillParentPopup.height + 100
        mobaskillWin.show()
    }

    function fOpenSettingsWindow(x = undefined, y = undefined) {
        if (pSettingsWin !== null) {
            pSettingsWin.destroy()
        }

        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "MOBASkill settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOverlay,
                                                 "pExtraProp2Header": qsTranslate("QObject", "Cancel skill"),
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

                if (pMobaSkillCancel) {
                    pMobaSkillCancel.pSettingsWindowVisible = true
                }
            }
        }
        kmmBackend.setFocusedVisualItem(null)
    }

    function fLoadKeyPropertiesMap() {
        var keyBindingsMap = {
            "Activate": {"keyName": qsTranslate("QObject", "Activate"), "keyValue": pGamepadTabSelected ? pKmmControl.keyActivate[1] : pKmmControl.keyActivate[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Radius": {"keyName": qsTranslate("QObject", "Radius"), "keyValue": pKmmControl.radius, "keyType": KmmEditorControlSettings.InputType.Double},
            "Stop MOBA Dpad": {"keyName": qsTranslate("QObject", "Stop MOBA Dpad"), "keyValue": pKmmControl.stopMOBADpad, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Gamepad stick": {"keyName": qsTranslate("QObject", "Gamepad stick"), "keyValue": pKmmControl.gamepadStick, "keyType": KmmEditorControlSettings.InputType.Key , "keyVisible": pGamepadTabSelected ? "true" : "false"}
        }

        var mobaCancelpPropertiesMap = {
            "Cancel": {"keyName": qsTranslate("QObject", "Cancel"), "keyValue": pGamepadTabSelected ? pKmmControl.keyCancel[1] : pKmmControl.keyCancel[0], "keyType": KmmEditorControlSettings.InputType.Key}
        }
        var devPropertiesMap = {
            "Autocast toggle key": {"keyName": qsTranslate("QObject", "Autocast toggle key"), "keyValue": pKmmControl.keyAutocastToggle[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "OriginX": {"keyName": qsTranslate("QObject", "OriginX"), "keyValue": pKmmControl.originX, "keyType": KmmEditorControlSettings.InputType.Double},
            "OriginY": {"keyName": qsTranslate("QObject", "OriginY"), "keyValue": pKmmControl.originY, "keyType": KmmEditorControlSettings.InputType.Double},
            "CancelX": {"keyName": qsTranslate("QObject", "CancelX"), "keyValue": pKmmControl.cancelX, "keyType": KmmEditorControlSettings.InputType.Double},
            "CancelY": {"keyName": qsTranslate("QObject", "CancelY"), "keyValue": pKmmControl.cancelY, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y axis ratio": {"keyName": qsTranslate("QObject", "Y axis ratio"), "keyValue": pKmmControl.yAxisRatio, "keyType": KmmEditorControlSettings.InputType.Double},
            "Deadzone radius": {"keyName": qsTranslate("QObject", "Deadzone radius"), "keyValue": pKmmControl.deadzoneRadius, "keyType": KmmEditorControlSettings.InputType.Double},
            "Speed": {"keyName": qsTranslate("QObject", "Speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Cancel speed": {"keyName": qsTranslate("QObject", "Cancel speed"), "keyValue": pKmmControl.cancelSpeed, "keyType": KmmEditorControlSettings.InputType.Double},
            "Min swipe radius": {"keyName": qsTranslate("QObject", "Min swipe radius"), "keyValue": pKmmControl.minSwipeRadius, "keyType": KmmEditorControlSettings.InputType.Double},
            "No cancel on switch": {"keyName": qsTranslate("QObject", "No cancel on switch"), "keyValue": pKmmControl.noCancelOnSwitch, "keyType": KmmEditorControlSettings.InputType.Integer},
            "No cancel time": {"keyName": qsTranslate("QObject", "No cancel time"), "keyValue": pKmmControl.noCancelTime, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Min skill time": {"keyName": qsTranslate("QObject", "Min skill time"), "keyValue": pKmmControl.minSkillTime, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Min skill hold time": {"keyName": qsTranslate("QObject", "Min skill hold time"), "keyValue": pKmmControl.minSkillHoldTime, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Auto attack": {"keyName": qsTranslate("QObject", "Auto attack"), "keyValue": pKmmControl.autoAttack, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Advanced mode": {"keyName": qsTranslate("QObject", "Advanced mode"), "keyValue": pKmmControl.advancedMode, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Autocast enabled": {"keyName": qsTranslate("QObject", "Autocast enabled"), "keyValue": pKmmControl.autocastEnabled, "keyType": KmmEditorControlSettings.InputType.Boolean},
            "Tweaks": {"keyName": qsTranslate("QObject", "Tweaks"), "keyValue": pKmmControl.tweaks, "keyType": KmmEditorControlSettings.InputType.Integer},
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
            "Exclusive delay": {"keyName": qsTranslate("QObject", "Exclusive delay"), "keyValue": pKmmControl.exclusiveDelay, "keyType": KmmEditorControlSettings.InputType.Integer},
            "Skill radius": {"keyName": qsTranslate("QObject", "Skill radius"), "keyValue": pKmmControl.selectedSkillType, "keyType": KmmEditorControlSettings.InputType.String}
        }

        pSettingsWin.fLoadModel(keyBindingsMap, propertiesMap, {}, mobaCancelpPropertiesMap, devPropertiesMap)
    }

    function fDeleteVisualItem() {
        if (pMobaSkillCancel) {
            kmmBackend.kmmEditorDeleteVisualItem(pMobaSkillCancel)
        }
        Utils.deleteControl(iRoot, pKmmControl)
    }

    function fAssignMobaSkill(checkedMobaSkill) {
        if (checkedMobaSkill === "QuickCast") {
            pKmmControl.advancedMode = true
            pKmmControl.autocastEnabled = true
        }
        else if (checkedMobaSkill === "AutoCast") {
            pKmmControl.advancedMode = true
            pKmmControl.autocastEnabled = false
        }
        else {
            pKmmControl.advancedMode = false
            pKmmControl.autocastEnabled = false
        }
    }

    function fAssignGamepadKey(key) {
        if(!pEditorWindowGamepadTabSelected || !iTextEditField.pEditFieldActiveFocus)
            return
        pKmmControl.keyActivate[1] = key
        iTextEditField.fSetText(kmmBackend.getKeyList(key))
    }

    function fAssignMobaSkillRadius(selectedSkillRadiusType) {
        pKmmControl.selectedSkillType = selectedSkillRadiusType === "Default" ? "" : selectedSkillRadiusType
    }

    function fSetSkillRadiusLayoutVisibility() {
        return kmmBackend.skillRadiusTypes.length > 0
    }
}
