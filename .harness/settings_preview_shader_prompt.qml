// Preview-only host dialog. The actual player keeps its own native prompt.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import BsiGuiFixture 1.0
import "qrc:/bsi-shaders" as Shaders

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
    Shaders.ShaderStyle { id: look; nativeTheme: UiTheme }
    background: Shaders.ShaderSurface { base: look.panel; radius: 12; border.color: look.border }
    contentItem: ColumnLayout {
        spacing: 24
        Text {
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: qsTr("You have unsaved changes. Discard them?")
            color: look.text; font: look.bodyFont
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Item { Layout.fillWidth: true }
            Shaders.ShaderButton {
                objectName: "previewKeepChanges"; shaderStyle: look
                text: qsTr("No"); onClicked: prompt.close()
            }
            Shaders.ShaderButton {
                objectName: "previewDiscardChanges"; shaderStyle: look; primary: true
                text: qsTr("Discard Changes")
                onClicked: { prompt.close(); prompt.discardRequested(prompt.pendingTab, prompt.pendingClose) }
            }
        }
    }
}
