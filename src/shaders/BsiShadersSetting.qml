import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import BSIShaders 1.0

Item {
    id: page
    property string pParentWindow: "plr"
    property string pVmName: ""
    property int bsiTabIndex: -1
    property var bsiPalette: null
    property font bsiFont: Qt.font({pixelSize: 14})
    property bool pCompLoaded: false
    property var draft: ({})
    property bool pEnableSave: differs()
    // Shader drafts are separate from uiBackend's configuration transaction.
    // Participate in the native shell's existing tab/close discard prompts.
    function publishDirty() {
        var window = page.Window.window
        if (window && window.bsiShaderDirty2 !== undefined) window.bsiShaderDirty2 = pEnableSave
        else if (window && window.bsiShaderDirty !== undefined) window.bsiShaderDirty = pEnableSave
    }
    onPEnableSaveChanged: publishDirty()
    Component.onDestruction: {
        var window = page.Window.window
        if (window && window.bsiShaderDirty2 !== undefined) window.bsiShaderDirty2 = false
        else if (window && window.bsiShaderDirty !== undefined) window.bsiShaderDirty = false
    }
    function differs() {
        if (!!draft.enabled !== !!Engine.saved.enabled) return true
        for (var i = 0; i < Engine.controls.length; ++i) {
            var key = Engine.controls[i].key
            if (Number(draft[key] || 0) !== Number(Engine.saved[key] || 0)) return true
        }
        return false
    }
    property string feedback: ""
    readonly property color textColor: bsiPalette ? bsiPalette.primary10 : "#e7e9f2"
    readonly property color mutedColor: bsiPalette ? bsiPalette.primary20 : "#a9adba"
    readonly property color accent: bsiPalette ? bsiPalette.subWindowHeaderClick : "#55b8ff"
    function fInitUi() { draft = JSON.parse(JSON.stringify(Engine.saved)); feedback = "" }
    function fOnQmlLoaded() {
        fInitUi(); pCompLoaded = true; publishDirty()
        // Appending after the native tabs preserves their existing icon indexes.
        // Use the original default only when no custom/hidden override exists.
        var host = parent
        while (host && host.bIcons === undefined) host = host.parent
        if (host && bsiTabIndex >= 0 && host.bIcons[bsiTabIndex] === undefined) {
            var icons = Object.assign({}, host.bIcons)
            icons[bsiTabIndex] = "qrc:/bsi-shaders/shaders.png"
            host.bIcons = icons
        }
    }
    function fSetPreviousState() { fInitUi() }
    function fDiscardChanges() { fInitUi() }
    function fHandleConfChange() { return save() }
    function setValue(key, value) {
        var next = Object.assign({}, draft); next[key] = value; draft = next
    }
    function reset() {
        var next = {enabled: false}
        for (var i = 0; i < Engine.controls.length; ++i) next[Engine.controls[i].key] = 0
        draft = next
    }
    function preset(index) {
        reset()
        if (index === 1) {
            setValue("enabled", true); setValue("sharpen", 30); setValue("fxaa", 30)
        } else if (index === 2) {
            setValue("enabled", true); setValue("saturation", 22); setValue("contrast", 12); setValue("hdr", 18)
        } else if (index === 3) {
            setValue("enabled", true); setValue("warmth", 25); setValue("contrast", 8); setValue("bloom", 20); setValue("vignette", 25)
        }
    }
    function save() {
        var response = Engine.apply(draft)
        feedback = response.msg
        if (response.ok) draft = JSON.parse(JSON.stringify(Engine.saved))
        return response.ok
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 10
        ScrollView {
            id: scroll
            objectName: "shadersScroll"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 12
                Label { text: qsTr("Game shaders"); color: page.textColor; font: page.bsiFont }
                Label {
                    objectName: "shaderRendererStatus"
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Observed game API: ") + Engine.metrics.renderer + "\n" + Engine.metrics.status
                    color: Engine.metrics.active ? page.accent : page.mutedColor
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Uses the game's OpenGL, DirectX 11/12 or Vulkan presentation API, according to its renderer setting. Unsupported/protected surfaces are left untouched. Experimental; GPU/driver validation is incomplete. No FPS or system tweaks.")
                    color: page.mutedColor; font.pixelSize: 12
                }
                CheckBox {
                    objectName: "shaderEnabled"
                    text: qsTr("Enable visual effects")
                    checked: !!page.draft.enabled
                    palette.windowText: page.textColor; palette.highlight: page.accent
                    onToggled: page.setValue("enabled", checked)
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Preset"); color: page.textColor }
                    ComboBox {
                        objectName: "shaderPresets"
                        Layout.fillWidth: true
                        model: [qsTr("Neutral"), qsTr("Clear"), qsTr("Vivid"), qsTr("Warm cinema")]
                        onActivated: page.preset(currentIndex)
                    }
                    Button { objectName: "shaderReset"; text: qsTr("Reset all"); onClicked: page.reset() }
                }
                Repeater {
                    model: Engine.controls
                    delegate: ColumnLayout {
                        Layout.fillWidth: true; spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Label { Layout.fillWidth: true; text: modelData.label; color: page.textColor }
                            Label { text: Math.round(valueSlider.value).toString(); color: page.mutedColor }
                            ToolButton { text: qsTr("Reset"); onClicked: page.setValue(modelData.key, 0) }
                        }
                        Slider {
                            id: valueSlider
                            objectName: "shaderValue-" + modelData.key
                            Layout.fillWidth: true
                            from: modelData.minimum; to: modelData.maximum; stepSize: 1
                            value: page.draft[modelData.key] || 0
                            enabled: !!page.draft.enabled
                            palette.highlight: page.accent
                            onMoved: page.setValue(modelData.key, Math.round(value))
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Tonal pop is a contrast effect, not HDR display output. Edge smoothing is spatial antialiasing. All zero values or disabling effects completely bypasses processing.")
                    color: page.mutedColor; font.pixelSize: 12
                }
                Label {
                    objectName: "shaderError"
                    visible: !!Engine.error
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: Engine.error; color: page.mutedColor
                }
            }
        }
        Label {
            objectName: "shaderFeedback"
            visible: !!page.feedback
            Layout.fillWidth: true; wrapMode: Text.Wrap
            text: page.feedback; color: page.mutedColor; font.pixelSize: 12
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                text: qsTr("Save applies to supported game frames immediately. Unsaved edits do not change the image.")
                color: page.mutedColor; font.pixelSize: 12
            }
            Button { objectName: "shaderDiscard"; text: qsTr("Discard"); enabled: page.pEnableSave; onClicked: page.fInitUi() }
            Button {
                objectName: "shaderSave"
                text: qsTr("Save changes"); enabled: page.pEnableSave
                palette.button: page.accent
                onClicked: page.save()
            }
        }
    }
}
