import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtQml.Models 2.15
import BSIShaders 1.0
// Resolve the SAME resource-backed controls used by native Settings pages.
// They remain in the player; no OEM QML/assets or imitation theme are bundled.
import "qrc:/" as Native

Rectangle {
    id: page
    // Native pages are transparent Items over an opaque Settings surface.
    // Own this flat page/footer backing too, including on a module-only update
    // of an older Settings shell. Never gradient/layer/mask the content area.
    color: bsiTheme.colors.primary80
    property string pParentWindow: "plr"
    property string pVmName: ""
    // This is the NATIVE model index, never its sorted visual position.
    property int bsiTabIndex: -1
    property var bsiPalette: null
    property font bsiFont: Qt.font({family: "Segoe UI", pixelSize: 14})
    property var bsiTheme: UiTheme
    property bool pCompLoaded: false
    property var draft: ({})
    property bool pEnableSave: differs()
    property string feedback: ""
    property bool saveFailed: false
    property bool keepCurrentDisplay: false
    readonly property color textColor: bsiTheme.colors.primary10
    readonly property color mutedColor: bsiTheme.colors.primary30
    readonly property color accent: bsiTheme.colors.accentNormal
    readonly property int presetIndex: selectedPreset()

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
        // UiTheme is the player's live context object, also used inside the
        // resource-backed native controls. No palette snapshots or substitutes.
        bsiTheme = Qt.binding(function() { return UiTheme })
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
        if (keepCurrentDisplay || Number(draft.potatoMode || 0) !== Number(Engine.saved.potatoMode || 0)) return true
        if (!!draft.enabled !== !!Engine.saved.enabled) return true
        for (var i = 0; i < Engine.controls.length; ++i) {
            var key = Engine.controls[i].key
            if (Number(draft[key] || 0) !== Number(Engine.saved[key] || 0)) return true
        }
        return false
    }
    function fInitUi() {
        draft = JSON.parse(JSON.stringify(Engine.saved)); keepCurrentDisplay = false; feedback = ""; saveFailed = false
    }
    function initializeDisplay() {
        // Use exactly the context object used by native Display, not config-file
        // guessing or an unserializable QObject passed through pComponentArgs.
        // The C++ bridge verifies public method types/thread and read-back.
        if (typeof uiBackend !== "undefined") Engine.bindNativeDisplay(uiBackend)
        fInitUi()
    }
    function fOnQmlLoaded() {
        resolveStyle()
        // A page Loader can finish before the parent's Bootstrap completes.
        // Bind once after construction, never call an unattached backend or
        // repeatedly reinitialize a user's draft on the status-poll timer.
        Qt.callLater(initializeDisplay)
        fInitUi(); pCompLoaded = true; publishDirty()
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
        // Visual presets/Reset must not silently change the native render size.
        var next = {enabled: index > 0, potatoMode: Number(draft.potatoMode || 0)}
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
        var response = Engine.apply(draft, keepCurrentDisplay)
        saveFailed = !response.ok
        feedback = response.msg
        if (response.ok) {
            draft = JSON.parse(JSON.stringify(Engine.saved)); keepCurrentDisplay = false
            var window = page.Window.window
            if (window && typeof window.fShowToast === "function") {
                window.fShowToast(response.displayChanged ? qsTr("Render size saved — restart the player") : qsTr("Changes saved"))
                if (!response.displayChanged) feedback = ""
            }
        }
        return response.ok
    }

    Native.UiColumnLayout {
        anchors.fill: parent
        spacing: 24
        Native.UiScrollView {
            id: scroll
            objectName: "shadersScroll"
            Layout.fillWidth: true; Layout.fillHeight: true
            // The layout sizes the viewport, not the entire scrollable form.
            implicitWidth: 0; implicitHeight: 0
            clip: true
            contentItem: Flickable {
                id: viewport
                objectName: "shaderViewport"
                clip: true
                contentWidth: width
                contentHeight: 0
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                // Qt 5 can re-enter a ColumnLayout's implicit measurement when
                // contentHeight changes during wrapping or native theme setup.
                // Commit the extent once that layout pass has finished instead
                // of feeding its implicitHeight synchronously back into it.
                function updateExtent() { contentHeight = Math.ceil(form.implicitHeight) + 20 }
                Component.onCompleted: Qt.callLater(updateExtent)
                onVisibleChanged: if (visible) Qt.callLater(updateExtent)
                Native.UiColumnLayout {
                    id: form
                    objectName: "shaderForm"
                    onImplicitHeightChanged: Qt.callLater(viewport.updateExtent)
                    width: Math.min(520, Math.max(0, viewport.width - 24))
                    spacing: 16
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Visual effects"); color: page.bsiTheme.colors.primary10; font: page.bsiTheme.fonts.bodyLarge
                    }
                    Text {
                        objectName: "shaderFeedback"
                        visible: !!page.feedback
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: page.feedback; color: page.saveFailed ? page.bsiTheme.colors.dangerNormal : page.bsiTheme.colors.primary20; font: page.bsiTheme.fonts.bodySmall
                    }
                    Native.UiRowLayout {
                        Layout.fillWidth: true; spacing: 16
                        Native.UiCheckBox {
                            objectName: "shaderEnabled"
                            pSize: page.bsiTheme.checkBoxes.sizes.medium
                            Layout.fillWidth: true
                            text: qsTr("Enable visual effects")
                            checked: !!page.draft.enabled
                            onToggled: page.setValue("enabled", checked)
                        }
                        Native.UiButton {
                            objectName: "shaderEnhance"
                            pSize: page.bsiTheme.buttons.sizes.medium
                            pColor: page.bsiTheme.buttons.colors.secondary
                            text: qsTr("Enhance")
                            Accessible.description: qsTr("Select balanced color, tonal contrast and detail enhancement. Save changes to apply. This is not ray tracing.")
                            onClicked: page.preset(4)
                        }
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Preset"); color: page.bsiTheme.colors.primary10; font: page.bsiTheme.fonts.bodyLarge
                    }
                    Native.UiRowLayout {
                        Layout.fillWidth: true; spacing: 12
                        Native.UiComboBox {
                            objectName: "shaderPresets"
                            pSize: page.bsiTheme.comboBoxes.sizes.medium
                            // Keep native-size buttons fully legible while the
                            // selector yields to wide fallback/translated text.
                            Layout.fillWidth: true; Layout.minimumWidth: 80
                            Layout.preferredWidth: 200; Layout.maximumWidth: 200
                            model: [qsTr("Neutral"), qsTr("Clear"), qsTr("Vivid"), qsTr("Warm cinema"), qsTr("Enhanced"), qsTr("Custom")]
                            currentIndex: page.presetIndex
                            onActivated: page.preset(currentIndex)
                        }
                        Native.UiButton {
                            objectName: "shaderReset"
                            pSize: page.bsiTheme.buttons.sizes.medium
                            pColor: page.bsiTheme.buttons.colors.secondary
                            text: qsTr("Reset visuals"); onClicked: page.reset()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        visible: !!page.draft.enabled && page.presetIndex === 0
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Neutral bypass: choose Enhance or a preset to change the image.")
                        color: page.bsiTheme.colors.primary30; font: page.bsiTheme.fonts.bodySmall
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: page.bsiTheme.colors.seperator }
                    Text {
                        objectName: "shaderResolutionHeading"
                        // An unwrapped heading becomes the ColumnLayout's
                        // minimum width and can push EVERY row past the clip.
                        // Font fallback and translations must wrap, not widen it.
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Render resolution (optional)"); color: page.bsiTheme.colors.primary10; font: page.bsiTheme.fonts.bodyLarge
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Display resolution only. This is separate from the texture-detail Potato effect shown in the video; texture LOD is not implemented here.")
                        color: page.bsiTheme.colors.primary30; font: page.bsiTheme.fonts.bodySmall
                    }
                    Native.UiRowLayout {
                        Layout.fillWidth: true; spacing: 12
                        Native.UiComboBox {
                            objectName: "shaderPotatoModes"
                            pSize: page.bsiTheme.comboBoxes.sizes.medium
                            Layout.preferredWidth: 270; Layout.fillWidth: true
                            model: [qsTr("Native / restore original"), qsTr("Light · 720 px short edge"), qsTr("Strong · 540 px short edge")]
                            currentIndex: Number(page.draft.potatoMode || 0)
                            enabled: Engine.display.available && !Engine.display.pending && !Engine.display.conflict
                            Accessible.description: qsTr("Change actual native Display framebuffer size. Only Save writes it, and a full player restart is required. This does not change texture LOD or FPS caps.")
                            onActivated: { page.keepCurrentDisplay = false; page.setValue("potatoMode", currentIndex) }
                        }
                        Native.UiButton {
                            objectName: "shaderPotatoKeep"
                            pSize: page.bsiTheme.buttons.sizes.medium
                            pColor: page.bsiTheme.buttons.colors.secondary
                            text: qsTr("Keep current display")
                            visible: Engine.display.available && (Engine.display.conflict || Engine.display.pending)
                            onClicked: { page.setValue("potatoMode", 0); page.keepCurrentDisplay = true }
                        }
                    }
                    Text {
                        objectName: "shaderPotatoPreview"
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: page.keepCurrentDisplay ? qsTr("Save to keep the current native Display size and forget the previous restore snapshot. No display value will be written.")
                            : Engine.display.available ? Engine.previewPotato(Number(page.draft.potatoMode || 0)).msg : Engine.display.detail
                        color: page.bsiTheme.colors.primary20; font: page.bsiTheme.fonts.bodySmall
                    }
                    Text {
                        objectName: "shaderPotatoStatus"
                        visible: Engine.display.available
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Configured native Display: ") + Engine.display.width + " × " + Engine.display.height + "\n" + Engine.display.detail
                        color: page.bsiTheme.colors.primary30; font: page.bsiTheme.fonts.bodySmall
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: page.bsiTheme.colors.seperator }
                    Repeater {
                        model: Engine.controls
                        delegate: Native.UiColumnLayout {
                            id: group
                            property var controlData: modelData
                            Layout.fillWidth: true; spacing: 4
                            Native.UiRowLayout {
                                Layout.fillWidth: true; spacing: 12
                                Text {
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                    text: group.controlData.label; color: page.bsiTheme.colors.primary10; font: page.bsiTheme.fonts.bodyLarge
                                }
                                Text {
                                    objectName: "shaderValueLabel-" + group.controlData.key
                                    Layout.minimumWidth: 32
                                    horizontalAlignment: Text.AlignRight
                                    text: Math.round(valueSlider.value).toString(); color: page.bsiTheme.colors.primary20; font: page.bsiTheme.fonts.bodySmall
                                }
                                Native.UiButton {
                                    objectName: "shaderControlReset-" + group.controlData.key
                                    pSize: page.bsiTheme.buttons.sizes.small
                                    pColor: page.bsiTheme.buttons.colors.transparent
                                    opacity: enabled ? 1 : 0.4
                                    Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                    text: qsTr("Reset")
                                    enabled: Number(page.draft[group.controlData.key] || 0) !== 0
                                    Accessible.name: qsTr("Reset ") + group.controlData.label
                                    onClicked: {
                                        // This action disables itself. Return focus to the
                                        // viewport BEFORE changing enabled: ScrollView's
                                        // deferred focus-follow must not snap back to a
                                        // disabled old row after scrolling/resizing.
                                        viewport.forceActiveFocus(Qt.OtherFocusReason)
                                        page.setValue(group.controlData.key, 0)
                                    }
                                }
                            }
                            Native.UiSlider {
                                id: valueSlider
                                pSize: page.bsiTheme.sliders.sizes.medium
                                objectName: "shaderValue-" + group.controlData.key
                                Layout.fillWidth: true
                                from: group.controlData.minimum; to: group.controlData.maximum; stepSize: 1
                                value: page.draft[group.controlData.key] || 0
                                enabled: !!page.draft.enabled
                                onMoved: page.setValue(group.controlData.key, Math.round(value))
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: page.bsiTheme.colors.seperator }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Rendering status"); color: page.bsiTheme.colors.primary10; font: page.bsiTheme.fonts.bodyLarge
                    }
                    Text {
                        objectName: "shaderRendererStatus"
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Requested API: ") + Engine.metrics.requestedRenderer + "\n"
                            + qsTr("Observed game API: ") + Engine.metrics.renderer + "\n" + Engine.metrics.status
                        color: Engine.metrics.active ? page.bsiTheme.colors.accentNormal : page.bsiTheme.colors.primary30; font: page.bsiTheme.fonts.bodySmall
                    }
                    Text {
                        objectName: "shaderFrameCounter"
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Processed game frames: ") + Engine.metrics.processedFrames
                            + qsTr(" · Game surface observed: ") + (Engine.metrics.gameSurfaceObserved ? qsTr("yes") : qsTr("no"))
                            + qsTr(" · Processing active: ") + (Engine.metrics.active ? qsTr("yes") : qsTr("no"))
                        color: page.bsiTheme.colors.primary30; font: page.bsiTheme.fonts.bodySmall
                    }
                    Text {
                        objectName: "shaderCaptureDetails"
                        visible: Engine.metrics.requestedRenderer === "Vulkan"
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Vulkan capture (not processed game frames): ")
                            + Engine.metrics.vulkanLoaders + qsTr(" loaders · ") + Engine.metrics.vulkanInstances + qsTr(" instances · ")
                            + Engine.metrics.vulkanDevices + qsTr(" devices · ") + Engine.metrics.vulkanSurfaces + qsTr(" Win32 surfaces · ")
                            + Engine.metrics.vulkanSwapchains + qsTr(" swapchains · ") + Engine.metrics.vulkanPresents + qsTr(" presents")
                        color: page.bsiTheme.colors.primary30; font: page.bsiTheme.fonts.bodySmall
                    }
                    Native.UiButton {
                        objectName: "shaderCopyStatus"
                        pSize: page.bsiTheme.buttons.sizes.medium
                        pColor: page.bsiTheme.buttons.colors.secondary
                        text: qsTr("Copy rendering status")
                        onClicked: Engine.copyDiagnostics()
                    }
                    Text {
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: qsTr("Enhance changes color and detail, not ray tracing or game texture LOD. Unsupported or protected surfaces are left untouched. Experimental; GPU/driver validation is incomplete. Optional resolution changes need a restart. No FPS-cap or driver-profile tweaks.")
                        color: page.bsiTheme.colors.primary30; font: page.bsiTheme.fonts.bodySmall
                    }
                    Text {
                        objectName: "shaderError"
                        visible: !!Engine.error
                        Layout.fillWidth: true; wrapMode: Text.Wrap
                        text: Engine.error; color: page.bsiTheme.colors.dangerNormal; font: page.bsiTheme.fonts.bodySmall
                    }
                }
            }
        }
        // Match native Settings: one stationary, right-aligned Save button.
        // It remains visible at 40% opacity when clean. Discard belongs to the
        // existing native tab/close prompt, not a second permanent footer action.
        Native.UiRowLayout {
            objectName: "shaderFooter"
            Layout.fillWidth: true
            Layout.minimumHeight: 36; Layout.preferredHeight: 36; Layout.maximumHeight: 36
            spacing: 4
            Native.UiImage {
                objectName: "shaderFooterInfo"
                asset: "MenuInfo"
                // UiImage's relative asset string otherwise resolves against
                // THIS module's /bsi-shaders URL rather than the player's root.
                source: "qrc:/qrcAssets/MenuInfo.svg"
                pImageWidth: 12; pImageHeight: 12
            }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: Number(page.draft.potatoMode || 0) !== Number(Engine.saved.potatoMode || 0)
                    ? qsTr("Render size needs a player restart") : qsTr("Effects apply after saving")
                color: page.bsiTheme.colors.primary20; font: page.bsiTheme.fonts.bodySmall
            }
            Native.UiButton {
                objectName: "shaderSave"
                pSize: page.bsiTheme.buttons.sizes.medium
                pColor: page.bsiTheme.buttons.colors.accent
                opacity: page.pEnableSave ? 1 : 0.4
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                text: qsTranslate("QObject", "Save changes"); enabled: page.pEnableSave; onClicked: page.save()
            }
        }
    }
}
