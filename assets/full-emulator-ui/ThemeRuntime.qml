pragma Singleton
import QtQuick 2.15
import Qt.labs.settings 1.1
import Qt.labs.platform 1.1

QtObject {
    id: theme
    readonly property var presets: PRESET_DEFINITIONS
    readonly property var defaults: DEFAULT_DESIGN
    property string preferencesFile: StandardPaths.writableLocation(StandardPaths.GenericConfigLocation) + "/VOID-Emulator-Themes-v9.ini"
    property QtObject preferences: Settings {
        fileName: theme.preferencesFile
        category: "NativeAppearance"
        property string selectedPreset: "oled"
        property string customJson: ""
        property string chosenFont: theme.defaults.font
        property int radiusOverride: -1
        property int durationOverride: -1
        property bool motion: theme.defaults.motion
    }
    readonly property string selectedId: preferences.selectedPreset
    readonly property var activePreset: findPreset(selectedId)
    readonly property var colors: selectedId === "custom" ? customColors() : activePreset.palette
    readonly property string name: selectedId === "custom" ? "My theme" : activePreset.name
    readonly property bool motionEnabled: preferences.motion
    readonly property int duration: motionEnabled ? (preferences.durationOverride < 0 ? activePreset.durationMs : preferences.durationOverride) : 0
    readonly property int corner: preferences.radiusOverride < 0 ? activePreset.radius : preferences.radiusOverride
    readonly property string fontFamily: preferences.chosenFont
    readonly property color background: colors.background
    readonly property color surface: colors.surface
    readonly property color raised: colors.raised
    readonly property color border: colors.border
    readonly property color text: colors.text
    readonly property color muted: colors.muted
    readonly property color accent: colors.accent
    readonly property color accent_hover: Qt.lighter(accent, 1.12)
    readonly property color accent_pressed: Qt.darker(accent, 1.12)
    readonly property color danger: "#ff7888"
    readonly property color success: "#80ddb0"
    readonly property color warning: "#f4ca87"
    readonly property color overlay_black: Qt.rgba(background.r,background.g,background.b,0.6)
    readonly property color overlay_bright: Qt.rgba(text.r,text.g,text.b,0.12)
    readonly property color overlay_tint: Qt.rgba(surface.r,surface.g,surface.b,0.4)
    readonly property color overlay_primary: Qt.rgba(background.r,background.g,background.b,0.8)
    readonly property color accent_transparent: Qt.rgba(accent.r,accent.g,accent.b,0.14)
    readonly property real textContrast: contrast(colors.background,colors.text)

    function findPreset(id) {
        for(var i=0;i<presets.length;++i) if(presets[i].id===id)return presets[i]
        return presets[0]
    }
    function validPalette(value) {
        if(!value || typeof value!=="object")return false
        var keys=["background","surface","raised","border","text","muted","accent"]
        for(var i=0;i<keys.length;++i)if(!/^#[0-9a-fA-F]{6}$/.test(String(value[keys[i]])))return false
        return Object.keys(value).length===keys.length
    }
    function customColors() {
        try {var parsed=JSON.parse(preferences.customJson);if(validPalette(parsed))return parsed} catch(error) {}
        return presets[0].palette
    }
    function choose(id) {
        if(id!=="custom" && findPreset(id).id!==id)return false
        preferences.radiusOverride=-1;preferences.durationOverride=-1
        preferences.selectedPreset=id;preferences.sync();return true
    }
    function applyCustom(value) {
        if(!validPalette(value))return false
        preferences.customJson=JSON.stringify(value)
        preferences.selectedPreset="custom";preferences.sync();return true
    }
    function setCorner(value) {preferences.radiusOverride=Math.max(0,Math.min(24,Math.round(value)));preferences.sync()}
    function setDuration(value) {preferences.durationOverride=Math.max(0,Math.min(400,Math.round(value)));preferences.sync()}
    function setMotion(value) {preferences.motion=!!value;preferences.sync()}
    function setFont(value) {
        if(!/^[A-Za-z0-9 _-]{1,64}$/.test(value))return false
        preferences.chosenFont=value;preferences.sync();return true
    }
    function sync() {
        // Pull persisted values when a separately loaded native window reopens.
        preferences.sync()
        var next={id:preferences.value("selectedPreset","oled"),custom:preferences.value("customJson",""),font:preferences.value("chosenFont",defaults.font),radius:preferences.value("radiusOverride",-1),duration:preferences.value("durationOverride",-1),motion:preferences.value("motion",defaults.motion)}
        preferences.selectedPreset=next.id;preferences.customJson=next.custom
        preferences.chosenFont=next.font;preferences.radiusOverride=next.radius
        preferences.durationOverride=next.duration;preferences.motion=next.motion
    }
    function reset() {
        preferences.customJson="";preferences.chosenFont=defaults.font
        preferences.motion=defaults.motion;choose("oled")
    }
    function luminance(hex) {
        var result=0,weights=[0.2126,0.7152,0.0722]
        for(var i=0;i<3;++i){var v=parseInt(hex.substr(1+i*2,2),16)/255;result+=weights[i]*(v<=0.04045?v/12.92:Math.pow((v+0.055)/1.055,2.4))}
        return result
    }
    function contrast(a,b) {var x=luminance(a),y=luminance(b);return (Math.max(x,y)+0.05)/(Math.min(x,y)+0.05)}
}
