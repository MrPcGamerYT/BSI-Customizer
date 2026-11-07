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
    property bool motionEnabled: true
    // The caller removes the native glyph ONLY after its replacement has loaded.
    readonly property bool known: glyph.status === Image.Ready && mode === "button"
    readonly property string iconKey: resolveIcon()
    readonly property color ink: !available ? "#63636d" : pressed ? "#ffffff" :
                                hovered || active ? "#ffffff" : "#d5d5de"
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
        name = name.replace(/(active|hover|pressed|disabled|white|\.svg)$/g, "");
        return assets[name] || "";
    }

    Rectangle {
        anchors.fill: parent
        visible: skin.mode === "rail"
        gradient: Gradient {
            GradientStop { position: 0; color: "#080809" }
            GradientStop { position: 0.42; color: "#070708" }
            GradientStop { position: 1; color: "#080809" }
        }
        Rectangle { width: 1; height: parent.height; color: "#34343b"; opacity: 0.55 }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#030304" }
    }

    Item {
        id: face
        anchors.fill: parent
        visible: skin.mode === "button"
        scale: skin.pressed ? 0.91 : skin.hovered && skin.available ? 1.025 : 1
        Behavior on scale { enabled: skin.motionEnabled; NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: 7
            // Unknown icons must remain visible underneath this passive overlay.
            color: skin.pressed ? "#34343c" : skin.active ? "#26262d" : "#25252c"
            Behavior on border.color { enabled: skin.motionEnabled; ColorAnimation { duration: 140 } }
            opacity: !skin.available ? 0 : !skin.known ? (skin.pressed ? 0.14 : skin.hovered ? 0.08 : 0) :
                     skin.pressed ? 0.96 : skin.active ? 0.85 : skin.hovered ? 0.8 : 0
            border.width: 1
            border.color: skin.pressed ? "#858590" : "#565660"
            Behavior on opacity { enabled: skin.motionEnabled; NumberAnimation { duration: 160 } }
        }
        Rectangle {
            width: 2; height: skin.active ? 15 : 5; radius: 1
            Behavior on height { enabled: skin.motionEnabled; NumberAnimation { duration: 200; easing.type: Easing.OutBack } }
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            color: "#e2e2e9"
            opacity: skin.active && skin.available ? 1 : 0
            Behavior on opacity { enabled: skin.motionEnabled; NumberAnimation { duration: 160 } }
        }
        Image {
            id: glyph
            objectName: "bsiOutlineGlyph"
            anchors.centerIn: parent
            width: 19; height: 19
            scale: skin.pressed ? 0.88 : skin.hovered && skin.available ? 1.12 : 1
            Behavior on scale { enabled: skin.motionEnabled; NumberAnimation { duration: 170; easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
            transform: [
                Translate {
                    y: skin.hovered && skin.available && !skin.pressed ? -1 : 0
                    Behavior on y { enabled: skin.motionEnabled; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                },
                Rotation {
                    origin.x: glyph.width/2; origin.y: glyph.height/2
                    angle: skin.hovered && skin.available && skin.iconKey === "advanced" ? 16 : 0
                    Behavior on angle { enabled: skin.motionEnabled; NumberAnimation { duration: 190; easing.type: Easing.OutCubic } }
                }
            ]
            sourceSize.width: 38; sourceSize.height: 38
            fillMode: Image.PreserveAspectFit
            smooth: true
            source: skin.svg ? "data:image/svg+xml;charset=utf-8," +
                    encodeURIComponent(skin.svg.replace(/ICON_COLOR/g, skin.ink.toString())) : ""
            RotationAnimation on rotation {
                from: 0; to: 360; duration: 1100; loops: Animation.Infinite
                running: skin.motionEnabled && skin.animating && skin.available && skin.visible && skin.mode === "button"
                onRunningChanged: if (!running) glyph.rotation = 0
            }
        }
    }
}
