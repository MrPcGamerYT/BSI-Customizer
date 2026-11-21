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
    property bool pLoaded: false
    property string pComposedKey: ""
    property string pType: "MouseZoom"
    property real pRadius: 0
    property real pTheta: Math.PI/2
    property real pResizeUnit: 18
    property bool pGamepadTabSelected: false
    property bool pEditorWindowGamepadTabSelected: false
    property bool pOpenWheelSettingsHelpPopup: false
    property bool pOpenWheelSettingsPopup: false

    property KmmEditorControlSettings pSettingsWin: null

    onWidthChanged: {
        fAssignCoordinates()
    }

    ButtonGroup {
        id: iWheelSettingsBtnGrp
    }

    Connections {
        target: pSettingsWin

        function onModelUpdate(updatedPropsMap) {
            var idx = pGamepadTabSelected ? 1 : 0
            pKmmControl.x = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "X")])
            pKmmControl.y = Utils.getPercentValue(updatedPropsMap[qsTranslate("QObject", "Y")])
            pKmmControl.keyModifier[idx] = updatedPropsMap[qsTranslate("QObject", "Modifier key")]
            pKmmControl.speed = updatedPropsMap[qsTranslate("QObject", "Zoom Speed")]
            pKmmControl.amplitude = updatedPropsMap[qsTranslate("QObject", "Zoom Amplitude")]
            pKmmControl.override = updatedPropsMap[qsTranslate("QObject", "Override")]
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
            fAssignCoordinates()
        }

        function onDeleteControl() {
            Utils.deleteControl(iRoot, pKmmControl)
        }

        function onSelectedTabUpdate(gamepadTabSelected) {
            pGamepadTabSelected = gamepadTabSelected
            fLoadKeyPropertiesMap()
        }
    }

    UiImage {
        id: iBaseImg
        asset: "EditorMouseZoom"
        pImageWidth: iRoot.width - 6
        pImageHeight: iRoot.width - 6
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
            onPressed: mouse.accepted = false
            onEntered: {
                if (pSettingsWin === null) {
                    iDeleteBtn.visible = true
                    iSettingsBtn.visible = true
                }
            }
            onExited: {
                if(!pOpenWheelSettingsPopup) {
                    iDeleteBtn.visible = false
                    iSettingsBtn.visible = false
                }
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
                onClicked: fOpenSettingsPopup()
            }
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
                        pRadius = ((iRoot.width + mouseX) / pResizeUnit).toFixed(2)
                }
            }
            onMouseYChanged: {
                if (drag.active) {
                    if ((iRoot.width + mouseY) > (pResizeUnit * 2))
                        pRadius = ((iRoot.width + mouseY) / pResizeUnit).toFixed(2)
                }
            }
        }
    }

    Component {
        id: iWheelSettingsPopupComponent

        UiHelpPopup {
            id: iWheelSettingsPopup
            contentWidth: 140
            closeOnPressOutside: !pOpenWheelSettingsHelpPopup
            visible: false
            x: iSettingsBtn.mapToGlobal(0, 0).x - (width - iSettingsBtn.width) / 2
            y: iSettingsBtn.mapToGlobal(0, 0).y - height
            onWidthChanged: {
                iWheelSettingsPopup.x = iSettingsBtn.mapToGlobal(0, 0).x - (iWheelSettingsPopup.width - iSettingsBtn.width)/2
            }

            pInnerLayout {
                border.width: 1
                border.color: UiTheme.colors.primary50
                color: UiTheme.colors.primary70
                padding: 8
                spacing: 8
            }

            UiRowLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: qsTranslate("QObject", "Wheel settings")
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
                            if(!pOpenWheelSettingsHelpPopup) {
                                pOpenWheelSettingsHelpPopup = true
                                fOpenWheelSettingsHelpPopup(iWheelSettingsPopup);
                            }
                        }
                    }
                }
            }

            UiColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                UiRadioButton {
                    id: iScrollBtn
                    text: qsTranslate("QObject", "Scroll")
                    ButtonGroup.group: iWheelSettingsBtnGrp
                    pSize: UiTheme.radioButtons.sizes.small
                    Layout.fillWidth: true
                    onCheckedChanged: if(checked) fAssignWheelSettings("Scroll")
                }
                UiRadioButton {
                    id: iZoomBtn
                    text: qsTranslate("QObject", "Zoom")
                    Layout.fillWidth: true
                    ButtonGroup.group: iWheelSettingsBtnGrp
                    pSize: UiTheme.radioButtons.sizes.small
                    onCheckedChanged: if(checked) fAssignWheelSettings("Zoom")
                }
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
                if (closeOnPressOutside && iWheelSettingsPopup.activeFocusItem === null)
                {
                    iWheelSettingsPopup.close()
                    pOpenWheelSettingsPopup = false
                }
            }

            Component.onCompleted:  {
                if(pKmmControl.wheelSettings === 0)
                    iScrollBtn.checked = true
                else if(pKmmControl.wheelSettings === 1)
                    iZoomBtn.checked = true
            }

            function fHandleActiveChanged(isActive) {
                if (activeFocusItem == null && closeOnPressOutside) {
                    close()
                    pOpenWheelSettingsPopup = false
                }
            }
        }
    }

    Component {
        id: iWheelSettingsHelpPopupComponent
        UiWindowedPopup {
            property var parentControl: null
            id: iWheelSettingsHelpPopup
            contentWidth: iWheelSettingsHelpPopupRect.width
            contentHeight: iWheelSettingsHelpPopupRect.height
            visible: false

            onWidthChanged: {
                iWheelSettingsHelpPopup.x = iSettingsBtn.mapToGlobal(0, 0).x - iWheelSettingsHelpPopup.width + iWheelSettingsHelpPopup.shadowThickness
            }

            Rectangle {
                id: iWheelSettingsHelpPopupRect
                width: iWheelSettingsHelpContentLayout.width + iWheelSettingsHelpContentLayout.anchors.leftMargin
                       + iWheelSettingsHelpContentLayout.anchors.rightMargin
                height: iWheelSettingsHelpContentLayout.height + iWheelSettingsHelpContentLayout.anchors.topMargin
                        + iWheelSettingsHelpContentLayout.anchors.bottomMargin
                color: UiTheme.colors.primary70
                border.width: 1
                border.color: UiTheme.colors.primary50
                UiColumnLayout {
                    id: iWheelSettingsHelpContentLayout
                    width: 124
                    anchors.centerIn: parent
                    anchors.margins: 8
                    spacing: 6

                    Text {
                        text: qsTranslate("QObject", "Scroll")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.primary10
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                    }

                    Text {
                        text: qsTranslate("QObject", "Scrolling the Mouse wheel will behave as scrolling on screen")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.primary20
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    Text {
                        text: qsTranslate("QObject", "Zoom")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.primary10
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                    }

                    Text {
                        text: qsTranslate("QObject", "Scrolling the Mouse wheel will behave as Zoom In or Out on screen")
                        font: UiTheme.fonts.bodySmall
                        color: UiTheme.colors.primary20
                        Layout.alignment: Qt.AlignTop
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                    }

                    UiRowLayout {
                        Item {
                            Layout.fillWidth: true
                        }

                        Text {
                            id: iReadMoreTxt
                            text: qsTranslate("QObject", "Learn more")
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
                                    iWheelSettingsHelpPopup.visible = false
                                    uiBackend.openHelpArticle("wheel_settings_help")
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
                pOpenWheelSettingsHelpPopup = false
                parentControl.fHandleActiveChanged(false)
            }
        }
    }

    function fOpenSettingsWindow() {
        if (pSettingsWin != null) {
            pSettingsWin.destroy()
        }
        var comp = Qt.createComponent("KmmEditorControlSettings.qml")
        if (comp.status === Component.Ready) {
            pSettingsWin = comp.createObject(iRoot.parent,
                                             {"pTitle": qsTranslate("QObject", "Mouse wheel settings"),
                                                 "pShowOnOverlay": pKmmControl.showOnOverlay,
                                                 "pShowOverlaySetting": false,
                                                 "pEnableCondition": pKmmControl.enableCondition,
                                                 "pStartCondition": pKmmControl.startCondition})
            if (pSettingsWin != null) {
                pGamepadTabSelected = pEditorWindowGamepadTabSelected;
                fLoadKeyPropertiesMap()
                pSettingsWin.x = iRoot.mapToGlobal(0, 0).x
                pSettingsWin.y = iRoot.mapToGlobal(0, 0).y + iRoot.height
                pSettingsWin.show()
            }
        }
        kmmBackend.setFocusedVisualItem(null)
    }

    function fLoadKeyPropertiesMap() {
        var keyBindingsMap = {}
        var propertiesMap = {
            "X": {"keyName": qsTranslate("QObject", "X"), "keyValue": pKmmControl.x, "keyType": KmmEditorControlSettings.InputType.Double},
            "Y": {"keyName": qsTranslate("QObject", "Y"), "keyValue": pKmmControl.y, "keyType": KmmEditorControlSettings.InputType.Double},
            "Zoom Amplitude": {"keyName": qsTranslate("QObject", "Zoom Amplitude"), "keyValue": pKmmControl.amplitude, "keyType": KmmEditorControlSettings.InputType.Double},
            "Zoom Speed": {"keyName": qsTranslate("QObject", "Zoom Speed"), "keyValue": pKmmControl.speed, "keyType": KmmEditorControlSettings.InputType.Double}
        }
        var devPropertiesMap = {
            "Modifier key": {"keyName": qsTranslate("QObject", "Modifier key"), "keyValue": pGamepadTabSelected ? pKmmControl.keyModifier[1] : pKmmControl.keyModifier[0], "keyType": KmmEditorControlSettings.InputType.Key},
            "Override": {"keyName": qsTranslate("QObject", "Override"), "keyValue": pKmmControl.override, "keyType": KmmEditorControlSettings.InputType.Boolean},
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
            var val = pKmmControl == null ? 0 : pKmmControl.x
            return plrBackend.glWinRect[0] + (val * plrBackend.glWinRect[2]) / 100 - (iRoot.width / 2)
        })
        iRoot.y = Qt.binding(function() {
            var val = pKmmControl == null ? 0: pKmmControl.y
            return plrBackend.glWinRect[1] + (val * plrBackend.glWinRect[3]) / 100 - (iRoot.height / 2)
        })
        pRadius = (Math.sqrt((pKmmControl.x1-pKmmControl.x2)*(pKmmControl.x1-pKmmControl.x2) +
                             (pKmmControl.y1-pKmmControl.y2)*(pKmmControl.y1-pKmmControl.y2)) / 2).toFixed(2);

        if(pRadius === 0)
            pRadius = 10

        if(pKmmControl.x1 === -1 && pKmmControl.x2 === -1) {
            pKmmControl.x = pRadius
        }
        else
            pKmmControl.x = (pKmmControl.x1 + pKmmControl.x2) /2


        if(pKmmControl.y1 === -1 && pKmmControl.y2 === -1) {
            pKmmControl.y = pRadius
        }
        else {
            pKmmControl.y = (pKmmControl.y1 + pKmmControl.y2) / 2
        }

        fAssignCoordinates()
        iRoot.width = Qt.binding(function() {return (pRadius * pResizeUnit)})

        pLoaded = true
        pEditorWindowGamepadTabSelected = gamepadTabSelected
    }

    function fAssignCoordinates() {
        pTheta = Math.atan((pKmmControl.y2-pKmmControl.y1) / (pKmmControl.x2-pKmmControl.x1))
        if(pKmmControl.x1 === -1 && pKmmControl.x2 === -1 && pKmmControl.y1 === -1 && pKmmControl.y2 === -1)
        {
            pTheta = Math.PI/2
        }

        if (!isNaN(pTheta)) {
            pKmmControl.x1 = (parseFloat(pKmmControl.x) - pRadius * Math.cos(pTheta)).toFixed(2)
            if(pKmmControl.x1 < 0) {
                pKmmControl.x1 = 0
                pKmmControl.x = (pRadius * Math.cos(pTheta)).toFixed(2)
            }
            pKmmControl.x2 = (parseFloat(pKmmControl.x) + pRadius * Math.cos(pTheta)).toFixed(2)
            pKmmControl.y1 = (parseFloat(pKmmControl.y) - pRadius * Math.sin(pTheta)).toFixed(2)
            if(pKmmControl.y1 < 0) {
                pKmmControl.y1 = 0
                pKmmControl.y = (pRadius * Math.sin(pTheta)).toFixed(2)
            }
            pKmmControl.y2 = (parseFloat(pKmmControl.y) + pRadius * Math.sin(pTheta)).toFixed(2)
        }
    }

    function fCreateNewControl() {
        kmmBackend.getNewControl(this, pType)
        fLoadControlProp(pEditorWindowGamepadTabSelected)
        pRadius = 10
    }

    function fOpenSettingsPopup() {
        var wheelSettingsWin = iWheelSettingsPopupComponent.createObject(iRoot.parent)
        wheelSettingsWin.show()
        pOpenWheelSettingsPopup = true
    }

    function fOpenWheelSettingsHelpPopup(wheelSettingsParentPopup) {
        var wheelSettingsWin = iWheelSettingsHelpPopupComponent.createObject(iRoot.parent, {"parentControl": wheelSettingsParentPopup})
        wheelSettingsWin.x = iSettingsBtn.mapToGlobal(0, 0).x  - wheelSettingsWin.width/2 + wheelSettingsParentPopup.width - 30
        wheelSettingsWin.y = iSettingsBtn.mapToGlobal(0, 0).y - wheelSettingsParentPopup.height + 12
        wheelSettingsWin.show()
    }

    function fAssignWheelSettings(checkedSetting) {
        if (checkedSetting === "Scroll") {
            pKmmControl.wheelSettings = 0
        }
        else if (checkedSetting === "Zoom") {
            pKmmControl.wheelSettings = 1
        }
    }

    function fInEditableMode() {}

    function fAssignGamepadKey(key)
    {
        if(!pEditorWindowGamepadTabSelected || !pActiveFocus)
            return

        pKmmControl.keyModifier[1] = key
    }
}
