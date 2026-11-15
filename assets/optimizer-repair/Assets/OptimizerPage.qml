// VOID Optimizer Studio 04. Native Settings navigation/Save are owned by the host.
// No shared Theme.qml/Icons.js dependency and no tuning on page entry.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: panel
    objectName: "voidEmbeddedOptimizerPage"
    property bool motionEnabled: true
    property bool previewOnly: false
    property string profile: "balanced"
    property bool highPerformancePower: false
    property bool disableCapture: false
    property int timer100ns: 0
    property bool keepAwake: false
    property bool autoRestore: true
    property string feedback: "Not applied. Your choices are staged here; the helper owns confirmation and results."
    readonly property string uiVersion: "STUDIO 04"
    readonly property int motionTime: motionEnabled ? 160 : 0
    readonly property int reviewActions: 1 + Number(highPerformancePower) + Number(disableCapture) + Number(timer100ns !== 0) + Number(keepAwake)
    readonly property string priorityLabel: profile === "performance" ? "Above normal" : profile === "eco" ? "Below normal" : "Normal"
    readonly property string timerLabel: timer100ns === 5000 ? "0.5 ms request" : timer100ns === 10000 ? "1 ms request" : "Default timing"
    signal reviewRequested(string uri)
    function reviewUri() {
        if (["balanced", "performance", "eco"].indexOf(profile) < 0 || [0, 5000, 10000].indexOf(timer100ns) < 0) return ""
        return "voidoptimizer-v3://review?profile=" + profile + "&power=" + (highPerformancePower ? "1" : "0") + "&capture=" + (disableCapture ? "1" : "0") + "&timer=" + timer100ns + "&awake=" + (keepAwake ? "1" : "0") + "&autorestore=" + (autoRestore ? "1" : "0")
    }
    function review() {
        var uri = reviewUri()
        if (!uri) { feedback = "Review stopped: unsupported profile/timer. Select a listed value; nothing was launched."; return }
        reviewRequested(uri)
        if (previewOnly) { feedback = "Preview only: no helper launched."; return }
        feedback = Qt.openUrlExternally(uri)
            ? "Helper launch requested — not an applied result. Select the intended player instance and confirm there."
            : "Helper could not launch. Run CHECK-INSTALL.cmd in the Studio repair pack; no tuning is confirmed."
    }
    component Label: Text {
        color: "#eeeeF2"; font.family: "Segoe UI"; font.pixelSize: 13
        textFormat: Text.PlainText; wrapMode: Text.WordWrap
    }
    component Glyph: Image {
        property string kind: "performance"
        source: "VoidOptimizerUI/icons/" + kind + ".png"
        sourceSize.width: 48; sourceSize.height: 48
        fillMode: Image.PreserveAspectFit; smooth: true
        width: 20; height: 20
    }
    component Action: Button {
        id: action
        property bool primary: false
        implicitHeight: 38; implicitWidth: Math.max(134, implicitContentWidth + 32)
        padding: 10; leftPadding: 16; rightPadding: 16
        hoverEnabled: true; focusPolicy: Qt.StrongFocus
        font.family: "Segoe UI"; font.pixelSize: 12; font.weight: Font.DemiBold
        contentItem: Text {
            text: action.text; textFormat: Text.PlainText; font: action.font
            color: action.primary ? "#111114" : "#dedee5"
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 10
            color: action.primary ? (action.down ? "#bdbdc8" : action.hovered ? "#ffffff" : "#e8e8ef") : action.down ? "#232329" : action.hovered ? "#1a1a20" : "#101014"
            border.color: action.activeFocus ? "#f4f4f7" : action.primary ? "#f4f4f7" : action.hovered ? "#5c5c68" : "#30303a"
            Behavior on color { ColorAnimation { duration: panel.motionTime } }
            Behavior on border.color { ColorAnimation { duration: panel.motionTime } }
        }
    }
    component Option: RowLayout {
        id: option
        property string title
        property string detail
        property bool value: false
        signal edited(bool value)
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 4
            Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: option.title; font.pixelSize: 12; font.weight: Font.Medium }
            Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: option.detail; font.pixelSize: 11; color: "#858591" }
        }
        Switch {
            id: toggle
            objectName: option.objectName + "Control"
            checked: option.value
            Accessible.name: option.title
            implicitWidth: 36; implicitHeight: 26; padding: 0
            onToggled: option.edited(checked)
            indicator: Rectangle {
                width: 34; height: 20; radius: 10; y: 3
                color: toggle.checked ? "#e5e5ee" : "#25252e"
                border.color: toggle.activeFocus ? "#ffffff" : toggle.checked ? "#f3f3f7" : "#41414d"
                Behavior on color { ColorAnimation { duration: panel.motionTime } }
                Rectangle {
                    width: 14; height: 14; radius: 7; y: 3
                    x: toggle.checked ? 17 : 3
                    color: toggle.checked ? "#111115" : "#8b8b98"
                    Behavior on x { NumberAnimation { duration: panel.motionTime; easing.type: Easing.OutCubic } }
                }
            }
            contentItem: Item {}
        }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 14
        RowLayout {
            Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 12
            Rectangle {
                Layout.preferredWidth: 44; Layout.preferredHeight: 44; radius: 13
                gradient: Gradient { GradientStop { position: 0; color: "#25252d" } GradientStop { position: 1; color: "#111116" } }
                border.color: "#43434f"
                Glyph { anchors.centerIn: parent; width: 23; height: 23; kind: "power" }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0; spacing: 3
                Label { text: "VOID  /  SESSION CONTROL"; font.pixelSize: 10; font.letterSpacing: 1.8; color: "#9898a6" }
                Label { text: "Optimizer"; font.pixelSize: 27; font.weight: Font.DemiBold }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop; spacing: 5
                Rectangle {
                    implicitWidth: 128; implicitHeight: 25; radius: 7; color: "#111115"; border.color: "#30303a"
                    Label { objectName: "voidOptimizerVersion"; anchors.centerIn: parent; text: "STUDIO 04 / HELPER V3"; font.pixelSize: 9; color: "#c4c4d0" }
                }
                Label { Layout.alignment: Qt.AlignRight; text: "REVIEW BEFORE APPLY"; font.pixelSize: 8; font.letterSpacing: 1; color: "#666674" }
            }
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true; Layout.minimumHeight: 70
            clip: true; contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; width: 5 }
            ColumnLayout {
                width: scroll.availableWidth; spacing: 12
                Rectangle {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: profilesBody.implicitHeight + 32
                    radius: 15; color: "#101014"; border.color: "#292931"
                    ColumnLayout {
                        id: profilesBody
                        anchors.fill: parent; anchors.margins: 16; spacing: 12
                        RowLayout {
                            Layout.fillWidth: true; Layout.minimumWidth: 0
                            Label { text: "01  /  PROCESS PROFILE"; font.pixelSize: 10; font.letterSpacing: 1.2; color: "#9696a3" }
                            Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                            Label { text: "One player instance"; font.pixelSize: 10; color: "#666674" }
                        }
                        GridLayout {
                            Layout.fillWidth: true; Layout.minimumWidth: 0; columns: panel.width >= 500 ? 3 : 1
                            columnSpacing: 9; rowSpacing: 9
                            Repeater {
                                model: [
                                    {key:"balanced",title:"Balanced",note:"Normal priority",icon:"balanced",tag:"DEFAULT"},
                                    {key:"performance",title:"Performance",note:"Above normal priority",icon:"performance",tag:"FOREGROUND"},
                                    {key:"eco",title:"Background",note:"Below normal · may reduce FPS",icon:"background",tag:"LOWER PRIORITY"}
                                ]
                                Button {
                                    id: profileCard
                                    objectName: "voidProfile_" + modelData.key
                                    property var info: modelData
                                    readonly property bool selected: panel.profile === info.key
                                    Layout.fillWidth: true; Layout.minimumWidth: 0; implicitHeight: 108
                                    padding: 13; hoverEnabled: true; focusPolicy: Qt.StrongFocus
                                    Accessible.name: info.title + ", " + info.note
                                    onClicked: panel.profile = info.key
                                    contentItem: ColumnLayout {
                                        spacing: 6
                                        RowLayout {
                                            Layout.fillWidth: true; Layout.minimumWidth: 0
                                            Glyph { kind: profileCard.info.icon; Layout.preferredWidth: 20; Layout.preferredHeight: 20; opacity: profileCard.selected ? 1 : 0.65 }
                                            Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
                                            Rectangle {
                                                width: 13; height: 13; radius: 6.5
                                                color: profileCard.selected ? "#efeff5" : "transparent"
                                                border.color: profileCard.selected ? "#efeff5" : "#4a4a58"
                                                Rectangle { anchors.centerIn: parent; width: 5; height: 5; radius: 2.5; color: "#111116"; visible: profileCard.selected }
                                            }
                                        }
                                        Label { text: profileCard.info.title; font.pixelSize: 14; font.weight: Font.DemiBold }
                                        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: profileCard.info.note; font.pixelSize: 10; color: "#8a8a98" }
                                    }
                                    background: Rectangle {
                                        radius: 11
                                        gradient: Gradient {
                                            GradientStop { position: 0; color: profileCard.selected ? "#27272f" : profileCard.hovered ? "#1b1b23" : "#15151b" }
                                            GradientStop { position: 1; color: profileCard.selected ? "#17171d" : "#101015" }
                                        }
                                        border.color: profileCard.activeFocus ? "#ffffff" : profileCard.selected ? "#bcbcc9" : profileCard.hovered ? "#50505f" : "#2a2a34"
                                        Behavior on border.color { ColorAnimation { duration: panel.motionTime } }
                                    }
                                }
                            }
                        }
                        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; text: "The helper will ask you to select a running HD-Player. This is not a live FPS preset or a game-memory tweak."; font.pixelSize: 10; color: "#777785" }
                    }
                }
                GridLayout {
                    Layout.fillWidth: true; Layout.minimumWidth: 0; columns: panel.width >= 720 ? 2 : 1
                    columnSpacing: 12; rowSpacing: 12
                    Rectangle {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
                        implicitHeight: sessionBody.implicitHeight + 32
                        radius: 15; color: "#101014"; border.color: "#292931"
                        ColumnLayout {
                            id: sessionBody
                            anchors.fill: parent; anchors.margins: 16; spacing: 16
                            RowLayout {
                                Glyph { kind: "session"; Layout.preferredWidth: 17; Layout.preferredHeight: 17; opacity: 0.8 }
                                Label { text: "02  /  WINDOWS SESSION"; font.pixelSize: 10; font.letterSpacing: 1; color: "#a2a2ae" }
                            }
                            Option { objectName:"voidEmbeddedPower"; Layout.fillWidth:true; Layout.minimumWidth:0; title:"High performance plan"; detail:"Use an existing Windows plan. More heat and battery use is possible."; value:panel.highPerformancePower; onEdited:panel.highPerformancePower=value }
                            Rectangle { Layout.fillWidth:true; Layout.minimumWidth:0; implicitHeight:1; color:"#25252e" }
                            Option { objectName:"voidEmbeddedCapture"; Layout.fillWidth:true; Layout.minimumWidth:0; title:"Pause Windows game capture"; detail:"Change this user's Game DVR preferences. Windows clips may stop."; value:panel.disableCapture; onEdited:panel.disableCapture=value }
                            Item { Layout.fillHeight:true; implicitHeight:0 }
                            Rectangle {
                                Layout.fillWidth:true; Layout.minimumWidth:0; implicitHeight: safetyNote.implicitHeight + 18; radius:8; color:"#17171e"
                                Label { id:safetyNote; anchors.fill:parent; anchors.margins:9; text:"No process killing, services, affinity, injection or boot edits."; font.pixelSize:10; color:"#8f8f9d" }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.fillHeight: true
                        implicitHeight: timingBody.implicitHeight + 32
                        radius: 15; color: "#101014"; border.color: "#292931"
                        ColumnLayout {
                            id: timingBody
                            anchors.fill: parent; anchors.margins: 16; spacing: 13
                            RowLayout {
                                Glyph { kind:"timing"; Layout.preferredWidth:17; Layout.preferredHeight:17; opacity:0.8 }
                                Label { text:"03  /  TIMING & RECOVERY"; font.pixelSize:10; font.letterSpacing:1; color:"#a2a2ae" }
                            }
                            Label { text:"Native timer request"; font.pixelSize:12; font.weight:Font.Medium }
                            RowLayout {
                                Layout.fillWidth:true; Layout.minimumWidth:0; spacing:5
                                Repeater {
                                    model: [{label:"Default",value:0},{label:"1.0 ms",value:10000},{label:"0.5 ms",value:5000}]
                                    Button {
                                        id: timerChip
                                        objectName:"voidTimer_"+modelData.value
                                        property var info:modelData
                                        readonly property bool selected:panel.timer100ns===info.value
                                        Layout.fillWidth:true; Layout.minimumWidth:0; implicitHeight:30; padding:4
                                        hoverEnabled:true; focusPolicy:Qt.StrongFocus
                                        Accessible.name:"Timer request "+info.label
                                        onClicked:panel.timer100ns=info.value
                                        contentItem:Label { text:timerChip.info.label; font.pixelSize:11; horizontalAlignment:Text.AlignHCenter; verticalAlignment:Text.AlignVCenter; color:timerChip.selected?"#f0f0f5":"#81818f" }
                                        background:Rectangle { radius:7; color:timerChip.selected?"#282831":timerChip.hovered?"#1a1a22":"#15151c"; border.color:timerChip.activeFocus||timerChip.selected?"#b9b9c7":"#30303a"; Behavior on border.color { ColorAnimation { duration:panel.motionTime } } }
                                    }
                                }
                            }
                            Label { Layout.fillWidth:true; Layout.minimumWidth:0; text:"Held by the helper, not injected into the player. Windows may scope or ignore it. No FPS guarantee."; color:"#777785"; font.pixelSize:10 }
                            Rectangle { Layout.fillWidth:true; Layout.minimumWidth:0; implicitHeight:1; color:"#25252e" }
                            Option { objectName:"voidEmbeddedAwake"; Layout.fillWidth:true; Layout.minimumWidth:0; title:"Keep system awake"; detail:"Prevent idle sleep for this session. Display policy stays unchanged."; value:panel.keepAwake; onEdited:panel.keepAwake=value }
                            Option { objectName:"voidEmbeddedAutoRestore"; Layout.fillWidth:true; Layout.minimumWidth:0; title:"Restore on instance exit"; detail:"Monitor PID, start time, image and user. Keep the helper open."; value:panel.autoRestore; onEdited:panel.autoRestore=value }
                        }
                    }
                }
                Label { Layout.fillWidth:true; Layout.minimumWidth:0; text:"Session changes can affect heat, battery use and Windows recordings. Review the exact process and actions in the helper before confirming."; font.pixelSize:10; color:"#71717f"; Layout.bottomMargin:4 }
            }
        }
        Rectangle {
            Layout.fillWidth:true; Layout.minimumWidth:0; implicitHeight: footerBody.implicitHeight + 24
            radius:13; color:"#131318"; border.color:"#34343f"
            ColumnLayout {
                id:footerBody
                anchors.fill:parent; anchors.margins:12; spacing:8
                RowLayout {
                    Layout.fillWidth:true; Layout.minimumWidth:0; spacing:10
                    ColumnLayout {
                        Layout.fillWidth:true; Layout.minimumWidth:0; spacing:3
                        Label { text:panel.reviewActions+" action"+(panel.reviewActions===1?"":"s")+" to review"; font.pixelSize:13; font.weight:Font.DemiBold }
                        Label { Layout.fillWidth:true; Layout.minimumWidth:0; text:panel.priorityLabel+"  /  "+panel.timerLabel+"  /  "+(panel.autoRestore?"Auto restore":"Manual restore"); font.pixelSize:10; color:"#9292a0" }
                    }
                    Action { text:"Open helper"; visible:panel.width>=570; onClicked:if(!panel.previewOnly) { var opened=Qt.openUrlExternally("voidoptimizer-v3://open"); panel.feedback=opened?"Helper open requested — use its Restore control for an active session.":"Helper unavailable. Run CHECK-INSTALL.cmd from the Studio pack." } }
                    Action { objectName:"voidEmbeddedReview"; text:"Review in helper  →"; primary:true; onClicked:panel.review() }
                }
                Label { objectName:"voidOptimizerFeedback"; Layout.fillWidth:true; Layout.minimumWidth:0; text:panel.feedback; color:"#81818f"; font.pixelSize:10 }
            }
        }
        RowLayout {
            Layout.fillWidth:true; Layout.minimumWidth:0
            Label { text:"Developed By NOVA XIS"; font.pixelSize:9; color:"#60606d" }
            Item { Layout.fillWidth:true; Layout.minimumWidth:0 }
            Label { text:"LOCAL SESSION  ·  NO CHANGES ON ENTRY"; font.pixelSize:8; color:"#60606d"; visible:panel.width>500 }
        }
    }
}
