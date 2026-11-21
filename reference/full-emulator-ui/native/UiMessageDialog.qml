/*
 * Copyright (C) 2020-2021 BlueStack Systems, Inc.
 * All Rights Reserved
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF BLUESTACK SYSTEMS, INC.
 * The copyright notice above does not evidence any actual or intended
 * publication of such source code.
 */

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import UiDialogWindow 1.0

UiDialogWindow {
    id: messageWindow
    width: dialog.width + 2 * shadowContainer.shadowThickness
    height: dialog.height + 2 * shadowContainer.shadowThickness
    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.ApplicationModal
    color: "transparent"

    property string pDialogImageHeadingText: ""
    property QtObject pDialogImageSize: null

    pCheckBoxState: dialog.pCheckBoxState

    signal checkBoxClicked(var checked)

    Component.onCompleted: {
        if (typeof(uiBackend) !== "undefined")
            uiBackend.pIsShortcutsEnabled = false
    }

    onClosed: {
        if (typeof(uiBackend) !== "undefined")
            uiBackend.pIsShortcutsEnabled = true

        if(messageWindow.pOwnership === 1) //only for JavaScriptOwnership
            messageWindow.destroy()
    }

    onVisibleChanged: {
        if (visible)
        {
            if (messageWindow.transientParent != null)
            {
                messageWindow.x = Qt.binding(function() {
                    var messageWindowX = messageWindow.transientParent.x + (messageWindow.transientParent.width - messageWindow.width) / 2
                    if (messageWindowX < messageWindow.transientParent.screen.virtualX)
                        messageWindowX = messageWindow.transientParent.screen.virtualX
                    if(messageWindowX + messageWindow.width > messageWindow.transientParent.screen.virtualX + messageWindow.transientParent.screen.width)
                        messageWindowX = messageWindow.transientParent.screen.virtualX + messageWindow.transientParent.screen.width - messageWindow.width
                    return messageWindowX
                })
                messageWindow.y = Qt.binding(function() {
                    var messageWindowY = messageWindow.transientParent.y + (messageWindow.transientParent.height - messageWindow.height) / 2
                    if (messageWindowY < messageWindow.transientParent.screen.virtualY)
                        messageWindowY = messageWindow.transientParent.screen.virtualY
                    if(messageWindowY + messageWindow.height > messageWindow.transientParent.screen.virtualY + messageWindow.transientParent.screen.height)
                        messageWindowY = messageWindow.transientParent.screen.virtualY + messageWindow.transientParent.screen.height - messageWindow.height
                    return messageWindowY
                })
            }
            dialog.open()
        }
    }

    UiShadowContainer {
        id: shadowContainer
        UiDialog {
            id: dialog
            title: messageWindow.title
            pDialogType: messageWindow.pDialogType
            pButtonsList: messageWindow.pButtonsList
            pSubtitle: messageWindow.pSubtitle
            pDescription: messageWindow.pDescription
            pNotice: messageWindow.pNotice
            pDialogImageSource: messageWindow.pDialogImageSource
            pDialogImageExtn: messageWindow.pDialogImageExtn
            pDialogHeaderImageSource: messageWindow.pDialogHeaderImageSource
            pDialogImageHeadingText: messageWindow.pDialogImageHeadingText
            pDialogImageSize: messageWindow.pDialogImageSize != null ? messageWindow.pDialogImageSize : dialog.pDialogImageSize
            pCheckBoxText: messageWindow.pCheckBoxText
            pHyperlink: messageWindow.pHyperlink
            pCloseBtnVisible: messageWindow.pCloseBtnVisible
            pCheckBoxInitialState: messageWindow.pCheckBoxInitialState
            closePolicy: Popup.NoAutoClose

            onCheckBoxClicked: function(checked) {
                messageWindow.checkBoxClicked(checked)
            }
            onHyperlinkClicked: {
                messageWindow.hyperlinkClicked()
            }
            onCloseClicked: {
                messageWindow.close()
                messageWindow.closeClicked()
            }
        }
    }
}
