// Native Settings extension. Editing these options NEVER applies OS changes.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "Icons.js" as Icons

Item {
    id: panel
    objectName: "voidEmbeddedOptimizerPage"
    property bool motionEnabled: false
    property bool previewOnly: false
    property string profile: "balanced"
    property bool highPerformancePower: false
    property bool disableCapture: false
    property int timer100ns: 0
    property bool keepAwake: false
    property bool autoRestore: true
    property string feedback: "Not applied · Windows changes require a separate review."
    signal reviewRequested(string uri)
    function reviewUri() {
        if (["balanced", "performance", "eco"].indexOf(profile) < 0 || [0, 5000, 10000].indexOf(timer100ns) < 0)
            return ""
        return "voidoptimizer-v3://review?profile="+profile+"&power="+(highPerformancePower?"1":"0")+"&capture="+(disableCapture?"1":"0")+"&timer="+timer100ns+"&awake="+(keepAwake?"1":"0")+"&autorestore="+(autoRestore?"1":"0")
    }
    function review() {
        var uri = reviewUri()
        if (!uri) { feedback="Review stopped: unsupported profile/timer. Select a listed value; nothing was launched."; return }
        reviewRequested(uri)
        if (previewOnly) { feedback="Preview only: no helper launched."; return }
        feedback=Qt.openUrlExternally(uri) ? "Helper v3 launch requested, not an applied result. Run SETUP-HELPER.cmd from the matching v3 repair pack if unavailable." : "Helper v3 unavailable. Build the helper and run SETUP-HELPER.cmd from the extracted pack."
    }
    component Action: Button {
        id: action
        property bool primary: false
        implicitHeight: 30; implicitWidth: 128
        font.family: "Segoe UI"; font.pixelSize: 11
        contentItem: Text {
            text: action.text; color: action.primary ? Theme.background : Theme.text
            font: action.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 4; color: action.primary ? (action.down ? Theme.muted : Theme.accent) : action.down ? Theme.raised : Theme.surface
            border.width: 1; border.color: action.activeFocus || action.hovered ? Theme.text : Theme.border
        }
    }
    component Section: RowLayout {
        property string title
        property string icon
        Layout.fillWidth: true; spacing: 9
        Image { source: Icons.url(parent.icon,Theme.text.toString()); sourceSize.width:32; sourceSize.height:32; Layout.preferredWidth:16; Layout.preferredHeight:16; opacity:0.8 }
        Text { text:parent.title; color:Theme.text; font.pixelSize:13; font.weight:Font.DemiBold }
        Rectangle { Layout.fillWidth:true; implicitHeight:1; color:Theme.raised; Layout.leftMargin:4 }
    }
    component Option: RowLayout {
        id: option
        property string title
        property string detail
        property alias checked: toggle.checked
        signal toggled(bool value)
        Layout.fillWidth: true; spacing: 10
        ColumnLayout {
            Layout.fillWidth:true; spacing:2
            Text { Layout.fillWidth:true; text:option.title; color:Theme.text; font.pixelSize:13; wrapMode:Text.Wrap }
            Text { Layout.fillWidth:true; text:option.detail; color:Theme.muted; font.pixelSize:13; wrapMode:Text.Wrap }
        }
        Switch {
            id: toggle
            objectName: option.objectName + "Control"
            implicitWidth:36; implicitHeight:26; padding:0
            Accessible.name: option.title
            onToggled: option.toggled(checked)
            indicator: Rectangle {
                width:32; height:18; radius:9; y:4
                color:toggle.checked ? Theme.accent : Theme.raised
                border.width:1; border.color:toggle.activeFocus ? Theme.text : Theme.border
                Rectangle { width:12; height:12; radius:6; y:3; x:toggle.checked ? 17 : 3; color:toggle.checked ? Theme.background : Theme.muted }
            }
            contentItem: Item {}
        }
    }
    ColumnLayout {
        anchors.fill:parent; spacing:0
        RowLayout {
            Layout.fillWidth:true; spacing:10
            ColumnLayout { Layout.fillWidth:true; spacing:5
                Text { text:"Session tools"; color:Theme.text; font.pixelSize:18; font.weight:Font.DemiBold }
                Text { text:"Tune one instance. Keep control of every change."; color:Theme.muted; font.pixelSize:13; wrapMode:Text.Wrap; Layout.fillWidth:true }
            }
            Rectangle { implicitWidth:92; implicitHeight:23; radius:4; color:Theme.surface; border.width:1; border.color:Theme.border
                Text { anchors.centerIn:parent; text:"HELPER V3"; color:Theme.muted; font.pixelSize:10; font.letterSpacing:0.8 }
            }
        }
        Rectangle { Layout.fillWidth:true; implicitHeight:1; color:Theme.raised; Layout.topMargin:17; Layout.bottomMargin:16 }
        ScrollView {
            id: scroll
            Layout.fillWidth:true; Layout.fillHeight:true; clip:true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width:scroll.availableWidth; spacing:10
                Section { title:"Process scheduling"; icon:"performance" }
                ComboBox {
                    id: scheduling
                    objectName:"voidEmbeddedProfile"; Layout.fillWidth:true; implicitHeight:30
                    model:["Balanced · Normal priority", "Performance · Above normal", "Background · Below normal"]
                    currentIndex:panel.profile==="performance"?1:panel.profile==="eco"?2:0
                    onActivated:function(index){panel.profile=index===1?"performance":index===2?"eco":"balanced"}
                    font.pixelSize:13; palette.text:Theme.text; palette.buttonText:Theme.text; palette.button:Theme.surface; palette.base:Theme.surface; palette.highlight:Theme.raised; palette.highlightedText:Theme.text; palette.window:Theme.surface
                    background:Rectangle { radius:4; color:Theme.surface; border.width:1; border.color:scheduling.activeFocus?Theme.text:Theme.border }
                }
                Option { objectName:"voidEmbeddedPower"; title:"High performance power plan"; detail:"Existing Windows plan only. May increase heat and battery use."; checked:panel.highPerformancePower; onToggled:function(value){panel.highPerformancePower=value} }
                Option { objectName:"voidEmbeddedCapture"; title:"Pause Windows game capture"; detail:"Current-user Game DVR preferences. Windows game clips may stop."; checked:panel.disableCapture; onToggled:function(value){panel.disableCapture=value} }
                Section { title:"Timing & session"; icon:"sliders" }
                RowLayout { Layout.fillWidth:true; spacing:10
                    ColumnLayout { Layout.fillWidth:true; spacing:2
                        Text { text:"Native timer request"; color:Theme.text; font.pixelSize:13 }
                        Text { text:"Held by the helper; released on restore or exit."; color:Theme.muted; font.pixelSize:13; wrapMode:Text.Wrap; Layout.fillWidth:true }
                    }
                    ComboBox {
                        id: timer; objectName:"voidEmbeddedTimer"; Layout.preferredWidth:138; implicitHeight:30
                        model:["System default", "1.0 ms", "0.5 ms"]
                        currentIndex:panel.timer100ns === 5000 ? 2 : panel.timer100ns === 10000 ? 1 : 0
                        onActivated:function(index){panel.timer100ns=index === 2 ? 5000 : index === 1 ? 10000 : 0}
                        font.pixelSize:13; palette.text:Theme.text; palette.buttonText:Theme.text; palette.button:Theme.surface; palette.base:Theme.surface; palette.highlight:Theme.raised; palette.highlightedText:Theme.text; palette.window:Theme.surface
                        background:Rectangle { radius:4; color:Theme.surface; border.width:1; border.color:timer.activeFocus?Theme.text:Theme.border }
                    }
                }
                Option { objectName:"voidEmbeddedAwake"; title:"Keep system awake"; detail:"Session-scoped idle-sleep prevention. Display policy stays unchanged."; checked:panel.keepAwake; onToggled:function(value){panel.keepAwake=value} }
                Option { objectName:"voidEmbeddedAutoRestore"; title:"Restore when this instance exits"; detail:"Checks the selected PID, start time, image and user every 2 seconds."; checked:panel.autoRestore; onToggled:function(value){panel.autoRestore=value} }
                Text { Layout.fillWidth:true; text:"Timer support depends on Windows and hardware. Requests may be scoped or ignored. No FPS or latency gain is guaranteed. All options are reviewed in the helper before application."; color:Theme.muted; font.pixelSize:13; wrapMode:Text.Wrap; Layout.bottomMargin:12 }
            }
        }
        Rectangle { Layout.fillWidth:true; implicitHeight:1; color:Theme.raised; Layout.topMargin:12; Layout.bottomMargin:10 }
        Text { Layout.fillWidth:true; text:panel.feedback; color:Theme.muted; font.pixelSize:13; wrapMode:Text.Wrap; Layout.bottomMargin:10 }
        RowLayout { Layout.fillWidth:true
            Action { text:"Helper / restore"; onClicked:if(!panel.previewOnly)Qt.openUrlExternally("voidoptimizer-v3://open") }
            Item { Layout.fillWidth:true }
            Action { objectName:"voidEmbeddedReview"; text:"Review changes"; primary:true; onClicked:panel.review() }
        }
        Text { Layout.topMargin:9; text:"Developed By NOVA XIS"; color:Theme.muted; font.pixelSize:11 }
    }
}
