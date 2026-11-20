import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQml.Models 2.15
import BSIShaders 1.0

Item {
    id: page
    property string pParentWindow: "plr"
    property string pVmName: ""
    // This is the NATIVE model index, never its sorted visual position.
    property int bsiTabIndex: -1
    property var bsiPalette: null
    property font bsiFont: Qt.font({family: "Segoe UI", pixelSize: 14})
    property var bsiTheme: null
    property bool pCompLoaded: false
    property var draft: ({})
    property bool pEnableSave: differs()
    property string feedback: ""
    property bool saveFailed: false
    readonly property color textColor: look.text
    readonly property color mutedColor: look.muted
    readonly property color accent: look.accent
    readonly property int presetIndex: selectedPreset()

    ShaderStyle {
        id: look
        nativeTheme: page.bsiTheme
        fallbackPalette: page.bsiPalette
        fallbackFont: page.bsiFont
    }
    // DelegateModel reorders DISPLAY groups without mutating the native list or
    // its `index` role. About/custom/hidden icon indexes and native callbacks
    // therefore stay valid across install, update, reopen and exact removal.
    DelegateModel { id: orderedTabs }
    property bool tabsArranged: false
    function settingsHost() {
        var host = parent
        while (host && host.bIcons === undefined) host = host.parent
        return host
    }
    function resolveStyle() {
        var window = page.Window.window
        var rows = window && window.pSettingsItems ? window.pSettingsItems.pSettingElements : []
        for (var i = 0; i < rows.length; ++i) {
            var about = rows[i].pComponentObj
            if (about && about.objectName === "aboutSettings" && about.bsiTheme) {
                // Do not round-trip a QObject palette through an OEM C++
                // pComponentArgs map: some hosts serialize it to an empty map.
                page.bsiTheme = Qt.binding(function() { return about.bsiTheme })
                return
            }
        }
        // Older shells still expose the palette on their own surface. Invalid
        // or missing individual roles use readable defaults, never black text.
        var host = settingsHost(), queue = host ? [host] : [], count = 0
        while (queue.length && count++ < 2048) {
            var item = queue.pop()
            if (item.c && item.c.primary10 !== undefined && item.c.primary80 !== undefined) {
                page.bsiPalette = Qt.binding(function() { return item.c })
                return
            }
            var children = item.children || []
            for (var j = 0; j < children.length && queue.length < 2048; ++j) queue.push(children[j])
        }
    }
    function arrangeTabs() {
        if (tabsArranged || bsiTabIndex < 0) return
        var window = page.Window.window
        if (!window || !window.pSettingsItems) return
        var rows = window.pSettingsItems.pSettingElements, aboutIndex = -1
        for (var i = 0; i < rows.length; ++i)
            if (/\/?PlrAboutSetting\.qml$/i.test(String(rows[i].pQmlSource))) aboutIndex = i
        if (aboutIndex < 0 || aboutIndex === bsiTabIndex) return
        var host = settingsHost(), queue = host ? [host] : [], count = 0
        while (queue.length && count++ < 2048) {
            var item = queue.pop()
            if (item.orientation === Qt.Horizontal && item.positionViewAtIndex !== undefined && item.delegate) {
                orderedTabs.model = rows
                orderedTabs.delegate = item.delegate
                item.model = orderedTabs
                orderedTabs.items.move(bsiTabIndex, aboutIndex > bsiTabIndex ? aboutIndex - 1 : aboutIndex)
                tabsArranged = true
                return
            }
            var children = item.children || []
            for (var j = 0; j < children.length && queue.length < 2048; ++j) queue.push(children[j])
        }
    }
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
    function fInitUi() {
        draft = JSON.parse(JSON.stringify(Engine.saved)); feedback = ""; saveFailed = false
    }
    function fOnQmlLoaded() {
        resolveStyle(); fInitUi(); pCompLoaded = true; publishDirty()
        var host = settingsHost()
        if (host && bsiTabIndex >= 0 && host.bIcons[bsiTabIndex] === undefined) {
            var icons = Object.assign({}, host.bIcons)
            icons[bsiTabIndex] = "qrc:/bsi-shaders/shaders.png"
            host.bIcons = icons
        }
        arrangeTabs()
    }
    function fSetPreviousState() { fInitUi() }
    function fDiscardChanges() { fInitUi() }
    function fHandleConfChange() { return save() }
    function setValue(key, value) {
        var next = Object.assign({}, draft); next[key] = value; draft = next
    }
    function presetValues(index) {
        var next = {enabled: index > 0}
        for (var i = 0; i < Engine.controls.length; ++i) next[Engine.controls[i].key] = 0
        if (index === 1) {
            next.sharpen = 30; next.fxaa = 30
        } else if (index === 2) {
            next.saturation = 22; next.contrast = 12; next.hdr = 18
        } else if (index === 3) {
            next.warmth = 25; next.contrast = 8; next.bloom = 20; next.vignette = 25
        } else if (index === 4) {
            // Real, vendor-neutral color/detail processing, not an RTX claim.
            next.saturation = 16; next.contrast = 10; next.hdr = 12
            next.sharpen = 24; next.fxaa = 20; next.bloom = 6
        }
        return next
    }
    function selectedPreset() {
        for (var index = 0; index <= 4; ++index) {
            var values = presetValues(index), same = index === 0 || !!draft.enabled
            for (var i = 0; same && i < Engine.controls.length; ++i) {
                var key = Engine.controls[i].key
                same = Number(draft[key] || 0) === values[key]
            }
            if (same) return index
        }
        return 5
    }
    function reset() { draft = presetValues(0) }
    function preset(index) { if (index >= 0 && index <= 4) draft = presetValues(index) }
    function save() {
        var response = Engine.apply(draft)
        saveFailed = !response.ok
        feedback = response.ok ? qsTr("Changes saved") : response.msg
        if (response.ok) {
            draft = JSON.parse(JSON.stringify(Engine.saved))
            var window = page.Window.window
            if (window && typeof window.fShowToast === "function") {
                window.fShowToast(qsTr("Changes saved"))
                feedback = ""
            }
        }
        return response.ok
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 24
        ScrollView {
            id: scroll
            objectName: "shadersScroll"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            contentWidth: form.width
            contentHeight: form.implicitHeight + 20
            ScrollBar.vertical: ShaderScrollBar { shaderStyle: look }
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: form
                width: Math.min(520, Math.max(0, scroll.availableWidth - 24))
                spacing: 16
                Text { text: qsTr("Visual effects"); color: look.text; font: look.bodyFont }
                Text {
                    objectName: "shaderFeedback"
                    visible: !!page.feedback
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: page.feedback; color: page.saveFailed ? look.danger : look.secondary; font: look.smallFont
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 16
                    ShaderCheckBox {
                        objectName: "shaderEnabled"
                        shaderStyle: look
                        Layout.fillWidth: true
                        text: qsTr("Enable visual effects")
                        checked: !!page.draft.enabled
                        onToggled: page.setValue("enabled", checked)
                    }
                    ShaderButton {
                        objectName: "shaderEnhance"
                        shaderStyle: look
                        primary: page.presetIndex === 4
                        text: qsTr("Enhance")
                        Accessible.description: qsTr("Select balanced color, tonal contrast and detail enhancement. Save changes to apply. This is not ray tracing.")
                        onClicked: page.preset(4)
                    }
                }
                Text { text: qsTr("Preset"); color: look.text; font: look.bodyFont }
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    ShaderComboBox {
                        objectName: "shaderPresets"
                        shaderStyle: look
                        Layout.preferredWidth: 200
                        model: [qsTr("Neutral"), qsTr("Clear"), qsTr("Vivid"), qsTr("Warm cinema"), qsTr("Enhanced"), qsTr("Custom")]
                        currentIndex: page.presetIndex
                        onActivated: page.preset(currentIndex)
                    }
                    ShaderButton {
                        objectName: "shaderReset"
                        shaderStyle: look
                        text: qsTr("Reset all"); onClicked: page.reset()
                    }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    visible: !!page.draft.enabled && page.presetIndex === 0
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Neutral bypass: choose Enhance or a preset to change the image.")
                    color: look.muted; font: look.smallFont
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: look.separator }
                Repeater {
                    model: Engine.controls
                    delegate: ColumnLayout {
                        id: group
                        property var controlData: modelData
                        Layout.fillWidth: true; spacing: 4
                        RowLayout {
                            Layout.fillWidth: true; spacing: 12
                            Text {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: group.controlData.label; color: look.text; font: look.bodyFont
                            }
                            Text {
                                objectName: "shaderValueLabel-" + group.controlData.key
                                Layout.minimumWidth: 32
                                horizontalAlignment: Text.AlignRight
                                text: Math.round(valueSlider.value).toString(); color: look.secondary; font: look.smallFont
                            }
                            ShaderButton {
                                objectName: "shaderControlReset-" + group.controlData.key
                                shaderStyle: look; quiet: true
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                text: qsTr("Reset")
                                enabled: Number(page.draft[group.controlData.key] || 0) !== 0
                                Accessible.name: qsTr("Reset ") + group.controlData.label
                                onClicked: page.setValue(group.controlData.key, 0)
                            }
                        }
                        ShaderSlider {
                            id: valueSlider
                            shaderStyle: look
                            objectName: "shaderValue-" + group.controlData.key
                            Layout.fillWidth: true
                            from: group.controlData.minimum; to: group.controlData.maximum; stepSize: 1
                            value: page.draft[group.controlData.key] || 0
                            enabled: !!page.draft.enabled
                            onMoved: page.setValue(group.controlData.key, Math.round(value))
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: look.separator }
                Text { text: qsTr("Rendering status"); color: look.text; font: look.bodyFont }
                Text {
                    objectName: "shaderRendererStatus"
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Observed game API: ") + Engine.metrics.renderer + "\n" + Engine.metrics.status
                    color: Engine.metrics.active ? look.accent : look.muted; font: look.smallFont
                }
                Text {
                    objectName: "shaderFrameCounter"
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Processed game frames: ") + Engine.metrics.processedFrames
                        + qsTr(" · Game surface observed: ") + (Engine.metrics.gameSurfaceObserved ? qsTr("yes") : qsTr("no"))
                        + qsTr(" · Processing active: ") + (Engine.metrics.active ? qsTr("yes") : qsTr("no"))
                    color: look.muted; font: look.smallFont
                }
                Text {
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: qsTr("Enhance is a balanced color and detail preset, not ray tracing. Tonal pop is not HDR display output. Unsupported or protected surfaces are left untouched. Experimental; actual GPU/driver validation is incomplete. No FPS or system tweaks.")
                    color: look.muted; font: look.smallFont
                }
                Text {
                    objectName: "shaderError"
                    visible: !!Engine.error
                    Layout.fillWidth: true; wrapMode: Text.Wrap
                    text: Engine.error; color: look.danger; font: look.smallFont
                }
            }
        }
        // Match native Settings: one stationary, right-aligned Save button.
        // It remains visible at 40% opacity when clean. Discard belongs to the
        // existing native tab/close prompt, not a second permanent footer action.
        RowLayout {
            objectName: "shaderFooter"
            Layout.fillWidth: true
            Layout.minimumHeight: 36; Layout.preferredHeight: 36; Layout.maximumHeight: 36
            spacing: 4
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: qsTr("Changes apply after saving")
                color: look.secondary; font: look.smallFont
            }
            ShaderButton {
                objectName: "shaderSave"; shaderStyle: look; primary: true
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                text: qsTr("Save changes"); enabled: page.pEnableSave; onClicked: page.save()
            }
        }
    }
}
