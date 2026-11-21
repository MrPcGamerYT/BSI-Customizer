// Black animated RIGHT emulator sidebar. Passive visuals only; native input stays native.
import QtQuick 2.15
import "Icons.js" as Icons

Item {
    id: skin
    property string mode: "button"
    property string elementId: ""
    property string assetName: ""
    property bool hovered: false
    property bool pressed: false
    property bool available: true
    property bool active: false
    property bool animating: false
    property bool motionEnabled: Theme.motionEnabled
    // The caller removes the native glyph ONLY after its replacement has loaded.
    readonly property bool known: glyph.status === Image.Ready && mode === "button"
    readonly property string iconKey: resolveIcon()
    readonly property color ink: !available ? Theme.border : pressed ? Theme.text :
                                hovered || active ? Theme.text : Theme.text
    readonly property string svg: Icons.shapes[iconKey] || ""

    function resolveIcon() {
        var id = elementId.toLowerCase().replace(/[^a-z0-9]/g, "");
        var name = assetName.toLowerCase();
        if (id === "volume") return name.indexOf("mute") >= 0 ? "mute" : "volume";
        var aliases = {
            fullscreen:"fullscreen", gamecontrol:"keymap", gamecontrols:"keymap",
            controls:"keymap", memorytrim:"memory", trimmemory:"memory",
            installapk:"install", apk:"install", install:"install",
            screenshot:"screenshot", macro:"macro", macrorecorder:"macro",
            screenrecord:"record", screenrecording:"record", record:"record",
            media:"media", mediafolder:"media", mediamanager:"media", rotate:"rotate", rotatescreen:"rotate",
            setlocation:"location", location:"location", shake:"shake", sync:"sync",
            multiinstance:"sync", mim:"sync", eco:"eco", ecomode:"eco",
            settings:"advanced", back:"back", home:"home", recent:"recent",
            recentapps:"recent", kebab:"more", more:"more", lockcursor:"lock"
        };
        if (aliases[id]) return aliases[id];
        // Only explicit native asset names: unknown tools keep their original icons.
        var assets = {
            sidebarsound:"volume", sidebarcontrols:"keymap", sidebarmediafolder:"media",
            sidebarfullscreen:"fullscreen", sidebarvolume:"volume", sidebarmute:"mute",
            sidebargamecontrols:"keymap", sidebargamecontrol:"keymap",
            sidebarmemorytrim:"memory", sidebartrimmemory:"memory",
            sidebarinstallapk:"install", sidebarscreenshot:"screenshot",
            sidebarmacro:"macro", sidebarmacrorecorder:"macro",
            sidebarscreenrecord:"record", sidebarmedia:"media", sidebarrotate:"rotate",
            sidebarsetlocation:"location", sidebarshake:"shake", sidebarsync:"sync",
            sidebareco:"eco", sidebarmim:"sync", sidebarlockcursor:"lock",
            sidebarsettings:"advanced", navbarback:"back", navbarhome:"home",
            navbarrecent:"recent", sidebarkebab:"more"
        };
        name = name.replace(/(?:[_-]?(?:active|hover|pressed|disabled|white)|\.svg)+$/g, "");
        return assets[name] || "";
    }

    Rectangle {
        anchors.fill: parent
        visible: skin.mode === "rail"
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.background }
            GradientStop { position: 0.42; color: Theme.background }
            GradientStop { position: 1; color: Theme.background }
        }
        Rectangle { width: 1; height: parent.height; color: Theme.raised; opacity: 0.55 }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.background }
    }

    Item {
        id: face
        anchors.fill: parent
        visible: skin.mode === "button"
        scale: skin.pressed ? 0.96 : 1
        Behavior on scale { enabled: skin.motionEnabled; NumberAnimation { duration: Theme.duration; easing.type: Easing.OutCubic } }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: 9
            // Unknown icons must remain visible underneath this passive overlay.
            color: Theme.raised
            Behavior on border.color { enabled: skin.motionEnabled; ColorAnimation { duration: Theme.duration } }
            opacity: !skin.available ? 0 : !skin.known ? (skin.pressed ? 0.14 : skin.hovered ? 0.08 : 0) :
                     skin.pressed ? 0.96 : skin.active ? 0.85 : skin.hovered ? 0.8 : 0
            border.width: 1
            border.color: skin.pressed ? Theme.muted : Theme.border
            Behavior on opacity { enabled: skin.motionEnabled; NumberAnimation { duration: Theme.duration } }
        }
        Rectangle {
            width: 2; height: skin.active ? 15 : 5; radius: 1
            Behavior on height { enabled: skin.motionEnabled; NumberAnimation { duration: Theme.duration; easing.type: Easing.OutBack } }
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            opacity: skin.active && skin.available ? 1 : 0
            Behavior on opacity { enabled: skin.motionEnabled; NumberAnimation { duration: Theme.duration } }
        }
        ThemeIcon {
            id: glyph
            objectName: "bsiOutlineGlyph"
            anchors.centerIn: parent
            width: 19; height: 19
            scale: skin.pressed ? 0.95 : 1
            Behavior on scale { enabled: skin.motionEnabled; NumberAnimation { duration: Theme.duration; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
            sourceSize.width: 38; sourceSize.height: 38
            fillMode: Image.PreserveAspectFit
            tint: skin.hovered || skin.active || skin.pressed ? Theme.accent : Theme.text
            opacity: !skin.available ? 0.35 : skin.hovered || skin.pressed || skin.active ? 1 : 0.75
            source: skin.svg ? "SharedIcons/"+skin.iconKey+".png" : ""
            RotationAnimation on rotation {
                from: 0; to: 360; duration: 1100; loops: Animation.Infinite
                running: skin.animating && skin.available && skin.visible && skin.mode === "button"
                onRunningChanged: if (!running) glyph.rotation = 0
            }
        }
    }
}
