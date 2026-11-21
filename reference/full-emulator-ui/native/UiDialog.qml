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
import QtQuick.Layouts 1.15
import DialogButtonModelList 1.0
import UiDialogWindow 1.0

Dialog {
    id: iControl

    property QtObject pSize: pDialogType === UiDialogWindow.SimpleNarrow
                             ? UiTheme.dialogs.sizes.simpleNarrow : pDialogType === UiDialogWindow.Simple
                               ? UiTheme.dialogs.sizes.simple : pDialogType === UiDialogWindow.Descriptive
                                ? UiTheme.dialogs.sizes.descriptive: pDialogType === UiDialogWindow.DescriptiveNarrow
                                 ? UiTheme.dialogs.sizes.descriptiveNarrow : pDialogType === UiDialogWindow.Tall
                                   ? UiTheme.dialogs.sizes.tall : pDialogType === UiDialogWindow.TallAlert
                                     ? UiTheme.dialogs.sizes.tallAlert : pDialogType === UiDialogWindow.Graphic
                                       ? UiTheme.dialogs.sizes.graphic : UiTheme.dialogs.sizes.graphicSimple

    property QtObject pColor: UiTheme.dialogs.colors.accent

    property int pDialogType
    property DialogButtonModelList pButtonsList: DialogButtonModelList { }
    property string pDescription
    property string pSubtitle: ""
    property string pNotice: ""
    property string pDialogImageSource: ""
    property string pDialogImageExtn: ".svg"
    property string pDialogHeaderImageSource: ""
    property string pCheckBoxText: ""
    property string pHyperlink: ""
    property bool pCheckBoxState: iDialogCheckBox.checked
    property bool pCloseBtnVisible: true
    property bool pCheckBoxInitialState: false

    property string pDialogImageHeadingText: ""
    property QtObject pDialogImageSize: QtObject {
        property real imageWidth: iControl.pSize.iconWidth
        property real imageHeight: iControl.pSize.iconHeight
    }

    signal closeClicked()
    signal checkBoxClicked(var checked)
    signal hyperlinkClicked()

    width: pSize.width
    height: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                     implicitContentHeight + topPadding + bottomPadding
                     + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                     + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))

    padding: 1
    spacing: 0

    background: Rectangle {
        color: iControl.pColor.background
        border {
            color: iControl.pColor.border
            width: 1
        }
    }

    header: UiColumnLayout {
        leftPadding: iControl.pSize.headerLeftPadding
        rightPadding: iControl.pSize.headerRightPadding
        topPadding: iControl.pSize.headerTopPadding
        bottomPadding: iControl.pSize.headerBottomPadding
        spacing: iControl.pSize.headerVerticalSpacing
        UiRowLayout {
            Layout.fillWidth: true
            visible: iControl.pDialogType === UiDialogWindow.SimpleNarrow ||
                     iControl.pDialogType === UiDialogWindow.Simple ||
                     iControl.pDialogType === UiDialogWindow.DescriptiveNarrow ||
                     iControl.pDialogType === UiDialogWindow.Descriptive
            spacing: iControl.pSize.headerHorizontalSpacing
            UiImage {
                visible: (pDialogImageSource != "" &&
                          (iControl.pDialogType === UiDialogWindow.Simple ||
                           iControl.pDialogType === UiDialogWindow.SimpleNarrow)) ||
                         (pDialogHeaderImageSource!= "" &&
                          (iControl.pDialogType === UiDialogWindow.DescriptiveNarrow))
                asset: iControl.pDialogType === UiDialogWindow.DescriptiveNarrow
                       ? pDialogHeaderImageSource
                       : pDialogImageSource
                extn: iControl.pDialogType === UiDialogWindow.DescriptiveNarrow
                      ? ".svg"
                      : pDialogImageExtn
                pImageWidth: iControl.pSize.headerIconWidth
                pImageHeight: iControl.pSize.headerIconHeight
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Qt.AlignLeft
                text: iControl.title
                font: iControl.pSize.titleFont
                color: iControl.pColor.title
                wrapMode: Text.Wrap
            }
            UiImageButton {
                visible: iControl.pCloseBtnVisible
                asset: "CloseIcon"
                pImageWidth: 16
                pImageHeight: 16
                Layout.alignment: Qt.AlignRight
                onClicked: {
                    iControl.closeClicked()
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: pDialogImageSize.imageHeight
            visible: iControl.pDialogType === UiDialogWindow.Tall ||
                     iControl.pDialogType === UiDialogWindow.TallAlert ||
                     iControl.pDialogType === UiDialogWindow.Graphic ||
                     iControl.pDialogType === UiDialogWindow.GraphicSimple
            UiColumnLayout {
                anchors.centerIn: parent
                UiImage {
                    visible: pDialogImageSource != ""
                    asset: pDialogImageSource
                    extn: pDialogImageExtn
                    pImageWidth: pDialogImageSize.imageWidth
                    pImageHeight: pDialogImageSize.imageHeight
                }
                Text {
                    visible: text !== ""
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: iControl.pDialogImageHeadingText
                    font: iControl.pSize.imageHeadingFont
                    color: iControl.pColor.title
                    wrapMode: Text.NoWrap
                }
            }
            UiImageButton {
                visible: iControl.pCloseBtnVisible
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.rightMargin: (iControl.pDialogType === UiDialogWindow.Graphic || iControl.pDialogType === UiDialogWindow.GraphicSimple) ? 20 : 0
                anchors.topMargin: iControl.pDialogType === UiDialogWindow.Graphic ? 20 : 0
                asset: "CloseIcon"
                pImageWidth: 16
                pImageHeight: 16
                onClicked: {
                    iControl.closeClicked()
                }
            }
        }
    }

    contentItem: UiRowLayout {
        leftPadding: iControl.pSize.contentLeftPadding
        rightPadding: iControl.pSize.contentRightPadding
        topPadding: iControl.pSize.contentTopPadding
        bottomPadding: iControl.pSize.contentBottomPadding
        spacing: iControl.pSize.contentHorizontalSpacing
        UiImage {
            visible: pDialogImageSource != "" &&
                     (iControl.pDialogType === UiDialogWindow.Descriptive ||
                      iControl.pDialogType === UiDialogWindow.DescriptiveNarrow)
            asset: pDialogImageSource
            extn: pDialogImageExtn
            pImageWidth: iControl.pSize.iconWidth
            pImageHeight: iControl.pSize.iconHeight
            Layout.alignment: Qt.AlignTop
        }
        UiColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: iControl.pSize.contentVerticalSpacing
            Text {
                visible: text !== "" &&
                         (iControl.pDialogType === UiDialogWindow.Tall ||
                          iControl.pDialogType === UiDialogWindow.TallAlert ||
                          iControl.pDialogType === UiDialogWindow.Graphic ||
                          iControl.pDialogType === UiDialogWindow.GraphicSimple)
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: iControl.title
                font: iControl.pSize.titleFont
                color: iControl.pColor.title
                wrapMode: Text.Wrap
            }
            Text {
                visible: text !== ""
                Layout.fillWidth: true
                text: iControl.pSubtitle
                horizontalAlignment: (iControl.pDialogType === UiDialogWindow.Tall ||
                                      iControl.pDialogType === UiDialogWindow.TallAlert ||
                                      iControl.pDialogType === UiDialogWindow.Graphic ||
                                      iControl.pDialogType === UiDialogWindow.GraphicSimple)
                                     ? Text.AlignHCenter
                                     : undefined
                font: iControl.pSize.subtitleFont
                color: iControl.pColor.subtitle
                wrapMode: Text.Wrap
            }
            Text {
                visible: text !== ""
                Layout.fillWidth: true
                text: iControl.pDescription
                horizontalAlignment: (iControl.pDialogType === UiDialogWindow.Tall ||
                                      iControl.pDialogType === UiDialogWindow.TallAlert ||
                                      iControl.pDialogType === UiDialogWindow.Graphic ||
                                      iControl.pDialogType === UiDialogWindow.GraphicSimple)
                                     ? Text.AlignHCenter
                                     : undefined
                font: iControl.pSize.descriptionFont
                color: iControl.pColor.description
                wrapMode: Text.Wrap
                linkColor: UiTheme.colors.accentNormal
                onLinkActivated: iControl.hyperlinkClicked()
                MouseArea {
                    anchors.fill: parent
                    cursorShape:  parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    acceptedButtons: Qt.NoButton
                }
            }
            Text {
                visible: text !== ""
                Layout.fillWidth: true
                horizontalAlignment: (iControl.pDialogType === UiDialogWindow.Tall ||
                                      iControl.pDialogType === UiDialogWindow.Graphic)
                                     ? Text.AlignHCenter
                                     : undefined
                text: iControl.pNotice
                font: iControl.pSize.noticeFont
                color: iControl.pColor.notice
                wrapMode: Text.Wrap
            }
            Text {
                id: iHyperlinkText
                visible: text !== ""
                Layout.fillWidth: true
                horizontalAlignment: (iControl.pDialogType === UiDialogWindow.Tall ||
                                      iControl.pDialogType === UiDialogWindow.Graphic)
                                     ? Text.AlignHCenter
                                     : undefined
                text: iControl.pHyperlink
                font: UiTheme.fonts.bodySmall
                color: UiTheme.colors.accentNormal
                wrapMode: Text.Wrap
                Component.onCompleted: {
                      iHyperlinkText.font.underline = true
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: containsMouse ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        iControl.hyperlinkClicked()
                    }
                }
            }
            UiCheckBox {
                id: iDialogCheckBox
                visible: text !== ""
                Layout.maximumWidth: parent.width
                Layout.alignment: (iControl.pDialogType === UiDialogWindow.Tall ||
                                   iControl.pDialogType === UiDialogWindow.Graphic)
                                  ? Qt.AlignHCenter
                                  : Qt.AlignLeft
                pSize: UiTheme.checkBoxes.sizes.medium
                text: iControl.pCheckBoxText
                checked: iControl.pCheckBoxInitialState
                onClicked: {
                    iControl.checkBoxClicked(checked)
                }
            }
        }
    }

    footer: Control {
        id: iControlFooter

        implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                implicitContentWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                 topPadding + bottomPadding + listView.contentItem.childrenRect.height)

        spacing: iControl.pDialogType === UiDialogWindow.Tall
                 ? iControl.pSize.footerVerticalSpacing
                 : iControl.pSize.footerHorizontalSpacing

        leftPadding: iControl.pSize.footerLeftPadding +
                     ((pDialogImageSource != "" &&
                       (iControl.pDialogType === UiDialogWindow.Descriptive ||
                        iControl.pDialogType === UiDialogWindow.DescriptiveNarrow))
                      ? iControl.pSize.iconWidth + iControl.pSize.contentHorizontalSpacing
                      : 0)
        rightPadding: iControl.pSize.footerRightPadding
        topPadding: iControl.pSize.footerTopPadding
        bottomPadding: iControl.pSize.footerBottomPadding

        contentItem: UiRowLayout {
            ListView {
                id: listView
                Layout.preferredWidth: listView.orientation === ListView.Vertical ? parent.width : contentWidth
                Layout.preferredHeight: parent.height
                Layout.alignment: listView.orientation === ListView.Vertical
                                  ? Qt.AlignLeft : (iControl.pDialogType === UiDialogWindow.Graphic || iControl.pDialogType === UiDialogWindow.TallAlert || iControl.pDialogType === UiDialogWindow.GraphicSimple)
                                    ? Qt.AlignHCenter : Qt.AlignRight
                model: iControl.pButtonsList.pButtons
                spacing: iControlFooter.spacing
                orientation: iControl.pDialogType === UiDialogWindow.Tall ? ListView.Vertical : ListView.Horizontal
                layoutDirection: Qt.RightToLeft
                interactive: false
                onWidthChanged: {
                    // using listView.childrenRect.height to avoid initial incomplete width change computations
                    // useful to change button allignement when text goes out of bound horizontally
                    if(listView.contentWidth > iControlFooter.availableWidth && listView.childrenRect.height > 0)
                        listView.orientation = ListView.Vertical
                }

                delegate: UiButton {
                    pSize: model.modelData.pSize === null ? UiTheme.buttons.sizes.medium : model.modelData.pSize
                    pColor: model.modelData.pColor === null ? UiTheme.buttons.colors.accent : model.modelData.pColor
                    text: model.modelData.pText
                    asset: model.modelData.pImage
                    extn: model.modelData.pExtn
                    pSubText: model.modelData.pSubText
                    pImageAfterText: model.modelData.pImageAfterText
                    width: listView.orientation ===  ListView.Vertical
                           ? iControlFooter.availableWidth : iControl.pDialogType === UiDialogWindow.Graphic
                             ? Math.min((iControlFooter.availableWidth - (iControlFooter.spacing * (listView.count - 1))) / listView.count, implicitWidth)
                             : implicitWidth
                    onClicked: {
                        model.modelData.clicked()
                    }
                }
            }
        }

        background: Rectangle {
            color: "transparent"
        }
    }
}
