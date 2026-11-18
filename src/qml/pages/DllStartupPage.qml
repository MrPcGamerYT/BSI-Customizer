import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

// The optional Chromium shell opens the SAME native panel, not a JS patcher.
ScrollView {
    id: page
    signal notify(string message)
    contentWidth: availableWidth
    clip: true
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    DllStartupPanel { width: page.availableWidth; onNotify: message => page.notify(message) }
}
