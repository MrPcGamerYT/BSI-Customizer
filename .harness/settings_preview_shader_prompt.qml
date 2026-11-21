// Preview-only host dialog. The actual player keeps its own native prompt.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import BsiGuiFixture 1.0
import "qrc:/" as Native

Dialog {
    id: prompt
    objectName: "previewDiscardPrompt"
    property int pendingTab: -1
    property bool pendingClose: false
    signal discardRequested(int tabIndex, bool closeWindow)
    function request(tabIndex, closeWindow) {
        pendingTab = tabIndex; pendingClose = closeWindow
        open()
    }
    modal: true
    width: Math.min(420, parent.width - 40)
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    padding: 24
    closePolicy: Popup.CloseOnEscape
    header: null; footer: null
    background: Rectangle { color: UiTheme.colors.primary80; radius: 12; border.color: UiTheme.colors.primary60 }
    contentItem: ColumnLayout {
        spacing: 24
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: qsTr("You have unsaved changes. Discard them?")
            color: UiTheme.colors.primary10; font: UiTheme.fonts.bodyMedium
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Item { Layout.fillWidth: true }
            Native.UiButton {
                objectName: "previewKeepChanges"
                pSize: UiTheme.buttons.sizes.medium; pColor: UiTheme.buttons.colors.secondary
                text: qsTr("No"); onClicked: prompt.close()
            }
            Native.UiButton {
                objectName: "previewDiscardChanges"
                pSize: UiTheme.buttons.sizes.medium; pColor: UiTheme.buttons.colors.accent
                text: qsTr("Discard Changes")
                onClicked: { prompt.close(); prompt.discardRequested(prompt.pendingTab, prompt.pendingClose) }
            }
        }
    }
}
