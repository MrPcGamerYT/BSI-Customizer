import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

// Both shells open this one native editor, not a JS patcher or a hidden form.
ScrollView {
    id: page
    objectName: "startupDllPageScroll"
    signal notify(string message)
    contentWidth: availableWidth
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: page.availableWidth
        spacing: 16
        Label {
            objectName: "dllStartupPageTitle"
            Layout.fillWidth: true
            text: qsTr("DLL patcher")
            color: Theme.text
            font.family: Theme.headingFont
            font.pixelSize: 32
            font.weight: Font.DemiBold
        }
        DllStartupPanel { Layout.fillWidth: true; onNotify: message => page.notify(message) }
    }
}
