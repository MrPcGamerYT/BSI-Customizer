// Visual-only MSI / BlueStacks sidebar skin. No mouse handlers or backend imports.
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
    // The caller removes the native glyph ONLY after its replacement has loaded.
    readonly property bool known: glyph.status === Image.Ready && mode === "button"
    readonly property string iconKey: resolveIcon()
    readonly property color ink: !available ? "#665774" : pressed ? "#ffffff" :
                                hovered || active ? "#f0e5ff" : "#c3aedc"
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
            media:"media", mediamanager:"media", rotate:"rotate", rotatescreen:"rotate",
            setlocation:"location", location:"location", shake:"shake", sync:"sync",
            multiinstance:"sync", mim:"sync", eco:"eco", ecomode:"eco",
            settings:"settings", back:"back", home:"home", recent:"recent",
            recentapps:"recent", kebab:"more", more:"more", lockcursor:"lock"
        };
        if (aliases[id]) return aliases[id];
        // Only explicit native asset names: unknown tools keep their original icons.
        var assets = {
            sidebarfullscreen:"fullscreen", sidebarvolume:"volume", sidebarmute:"mute",
            sidebargamecontrols:"keymap", sidebargamecontrol:"keymap",
            sidebarmemorytrim:"memory", sidebartrimmemory:"memory",
            sidebarinstallapk:"install", sidebarscreenshot:"screenshot",
            sidebarmacro:"macro", sidebarmacrorecorder:"macro",
            sidebarscreenrecord:"record", sidebarmedia:"media", sidebarrotate:"rotate",
            sidebarsetlocation:"location", sidebarshake:"shake", sidebarsync:"sync",
            sidebareco:"eco", sidebarmim:"sync", sidebarlockcursor:"lock",
            sidebarsettings:"settings", navbarback:"back", navbarhome:"home",
            navbarrecent:"recent", sidebarkebab:"more"
        };
        name = name.replace(/(active|hover|pressed|disabled|white|\.svg)$/g, "");
        return assets[name] || "";
    }

    Rectangle {
        anchors.fill: parent
        visible: skin.mode === "rail"
        gradient: Gradient {
            GradientStop { position: 0; color: "#291737" }
            GradientStop { position: 0.42; color: "#20122d" }
            GradientStop { position: 1; color: "#170f21" }
        }
        Rectangle { width: 1; height: parent.height; color: "#624078"; opacity: 0.55 }
        Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: "#110a19" }
    }

    Item {
        id: face
        anchors.fill: parent
        visible: skin.mode === "button"
        scale: skin.pressed ? 0.93 : 1
        Behavior on scale { NumberAnimation { duration: 85 } }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 3
            radius: 7
            // Unknown icons must remain visible underneath this passive overlay.
            color: skin.pressed ? "#75459a" : skin.active ? "#573374" : "#402653"
            opacity: !skin.available ? 0 : !skin.known ? (skin.pressed ? 0.14 : skin.hovered ? 0.08 : 0) :
                     skin.pressed ? 0.96 : skin.active ? 0.85 : skin.hovered ? 0.8 : 0
            border.width: 1
            border.color: skin.pressed ? "#b78bd9" : "#795494"
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Rectangle {
            width: 2; height: 12; radius: 1
            anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
            color: "#c18aee"
            opacity: skin.active && skin.available ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 120 } }
        }
        Image {
            id: glyph
            objectName: "bsiOutlineGlyph"
            anchors.centerIn: parent
            width: 19; height: 19
            sourceSize.width: 38; sourceSize.height: 38
            fillMode: Image.PreserveAspectFit
            smooth: true
            source: skin.svg ? "data:image/svg+xml;charset=utf-8," +
                    encodeURIComponent(skin.svg.replace(/ICON_COLOR/g, skin.ink.toString())) : ""
            RotationAnimation on rotation {
                from: 0; to: 360; duration: 1100; loops: Animation.Infinite
                running: skin.animating && skin.available && skin.visible && skin.mode === "button"
                onRunningChanged: if (!running) glyph.rotation = 0
            }
        }
    }
}
