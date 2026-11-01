import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

    Dialog {
    id: editor
    parent: Overlay.overlay
    modal: true
    focus: true
    title: qsTr("Gradient theme")
    width: Math.min(660, parent ? parent.width - 32 : 660)
    height: Math.min(620, parent ? parent.height - 32 : 620)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 20

    property bool pendingSolidEdits: false
    // Hex (old) method mode: the target list is restricted to the top bar /
    // title bar chrome surfaces the hex gradient is allowed to paint, plus a
    // synthetic "all" entry. QML (new) mode lists every compatible target.
    property bool hexMode: false
    property string selectedKey: ""
    property string firstColor: "#7C5CFF"
    property string secondColor: "#00C2FF"
    property bool horizontal: true
    property bool useGradient: true
    property string message: ""
    property bool failed: false
    property string filter: ""
    // A full UI can expose dozens of gradient bindings, so the dialog filters the
    // discovered list instead of showing every path in one long drop-down.
    property var view: []
    readonly property var selectedTarget: {
        for (let i = 0; i < editor.view.length; ++i)
            if (editor.view[i].key === editor.selectedKey) return editor.view[i];
        return null;
    }
    // QtQuick before 2.12 (Qt 5.12) has no Gradient::orientation, so such a document
    // can hold a gradient but always paints it top to bottom. Older players are then
    // shown with the direction locked instead of quietly ignoring the choice.
    readonly property bool targetSupportsOrientation:
        !editor.selectedTarget || editor.selectedTarget.supportsOrientation !== false;

    function validColor(value) { return /^#(?:[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$/.test(value); }
    function refresh() {
        const all = backend.gradientTargets;
        const needle = editor.filter.trim().toLowerCase();
        const shown = [];
        let orientationEverywhere = true;
        for (let i = 0; i < all.length; ++i) {
            const target = all[i];
            // Both methods offer the complete gradient list — top bar/title
            // bar/sidebar chrome, backgrounds, buttons, palette bindings —
            // with the same controls.
            if (editor.hexMode && !target.hexGradient)
                continue;
            const haystack = (target.label + " " + target.resource + " " + target.kind).toLowerCase();
            if (needle === "" || haystack.indexOf(needle) >= 0) {
                shown.push(target);
                orientationEverywhere = orientationEverywhere && target.supportsOrientation !== false;
            }
        }
        if (editor.hexMode && shown.length > 0) {
            // Synthetic combined entry first: one apply paints every offered
            // gradient surface. The key matches GradientPatcher::allGradientKey().
            shown.unshift({
                key: "hex:all-gradient-targets",
                label: qsTr("All gradient targets (%1)").arg(shown.length),
                resource: shown.map(function (t) { return t.resource; })
                               .filter(function (v, i, a) { return a.indexOf(v) === i; }).join(", "),
                kind: "chrome",
                enabled: shown[0].enabled,
                first: shown[0].first,
                second: shown[0].second,
                horizontal: shown[0].horizontal,
                supportsOrientation: orientationEverywhere,
                hexGradient: true
            });
        }
        editor.view = shown;
    }
    function choose(index) {
        const targets = editor.view;
        if (index < 0 || index >= targets.length) { selectedKey = ""; return; }
        const target = targets[index];
        selectedKey = target.key;
        firstColor = target.first;
        secondColor = target.second;
        horizontal = target.horizontal && editor.targetSupportsOrientation;
        // Opening a solid target proposes a gradient; Solid is an explicit choice.
        useGradient = true;
    }
    onOpened: {
        const result = backend.loadGradientTargets();
        message = result.msg || "";
        failed = !result.ok;
        refresh();
        targetBox.currentIndex = editor.view.length ? 0 : -1;
        choose(targetBox.currentIndex);
    }
    Connections {
        target: backend
        function onGradientTargetsChanged() { editor.refresh(); }
    }

    background: Rectangle { color: Theme.card; border.color: Theme.border; radius: 12 }
    header: Text {
        text: editor.title
        color: Theme.text
        font.pixelSize: 20
        font.weight: Font.Bold
        padding: 20
        bottomPadding: 6
    }
    contentItem: ColumnLayout {
        spacing: 10
        Text {
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            text: editor.hexMode
                  ? qsTr("Hex gradients can paint every listed surface - top bar, title bar, sidebar, "
                         + "the settings panel, buttons and the other compatible targets - with custom "
                         + "start/end colors and a direction, "
                         + "applied to one target or to all of them at once. A solid fallback color is always "
                         + "kept underneath the gradient; the solid hex colors from the list stay editable too.")
                  : qsTr("Apply two-stop gradients to compatible backgrounds or existing Gradient properties. "
                         + "Text/icon color properties stay solid. Applying replaces the selected target's existing stops. "
                         + "\"Sidebar (player window)\" / \"Top bar (player window)\" are the chrome BlueStacks paints.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ComboBox {
                id: targetBox
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                model: editor.view
                textRole: "label"
                onActivated: (index) => editor.choose(index)
                // modelData is the target map from Backend::gradientTargets; a second
                // line explains what each entry is, because the discovered paths are
                // long and read like QML object names.
                delegate: ItemDelegate {
                    id: targetDelegate
                    width: targetBox.width
                    height: 44
                    contentItem: ColumnLayout {
                        spacing: 1
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            text: modelData.label
                            color: Theme.text
                            font.pixelSize: 13
                        }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            text: modelData.kind + " \u00b7 " + (modelData.enabled ? qsTr("gradient") : qsTr("solid"))
                                  + " \u00b7 " + modelData.resource
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }
                    background: Rectangle { color: targetDelegate.highlighted ? Theme.raised : "transparent" }
                }
            }
            TextField {
                id: filterField
                Layout.preferredWidth: 150
                Layout.preferredHeight: 40
                placeholderText: qsTr("Filter")
                selectByMouse: true
                color: Theme.text
                onTextChanged: {
                    editor.filter = text;
                    editor.refresh();
                    targetBox.currentIndex = editor.view.length ? 0 : -1;
                    editor.choose(targetBox.currentIndex);
                }
            }
        }
        Text {
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: 11
            elide: Text.ElideMiddle
            text: targetBox.currentIndex >= 0 && targetBox.currentIndex < editor.view.length
                  ? editor.view[targetBox.currentIndex].resource
                  : qsTr("No compatible target")
        }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("Gradient"), qsTr("Solid fallback")]
                currentIndex: editor.useGradient ? 0 : 1
                onActivated: (index) => editor.useGradient = index === 0
            }
            ComboBox {
                Layout.fillWidth: true
                enabled: editor.useGradient && editor.targetSupportsOrientation
                model: [qsTr("Left → right"), qsTr("Top → bottom")]
                currentIndex: editor.horizontal ? 0 : 1
                onActivated: (index) => editor.horizontal = index === 0
            }
        }
        Text {
            Layout.fillWidth: true
            visible: editor.useGradient && !editor.targetSupportsOrientation
            text: qsTr("This UI predates QtQuick 2.12: its gradient is drawn top to bottom and the direction cannot be changed there.")
            color: Theme.textDim
            font.pixelSize: 10
            font.italic: true
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            enabled: editor.useGradient
            Text { text: qsTr("Start"); color: Theme.textDim; Layout.preferredWidth: 50 }
            ColorField {
                Layout.fillWidth: true
                current: editor.firstColor
                value: editor.firstColor
                onEdited: (hex) => editor.firstColor = hex
            }
        }
        RowLayout {
            Layout.fillWidth: true
            enabled: editor.useGradient
            Text { text: qsTr("End"); color: Theme.textDim; Layout.preferredWidth: 50 }
            ColorField {
                Layout.fillWidth: true
                current: editor.secondColor
                value: editor.secondColor
                onEdited: (hex) => editor.secondColor = hex
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            radius: 8
            color: Theme.raised
            border.color: Theme.border
            gradient: editor.useGradient ? previewGradient : null
            Gradient {
                id: previewGradient
                orientation: editor.horizontal ? Gradient.Horizontal : Gradient.Vertical
                GradientStop { position: 0; color: editor.validColor(editor.firstColor) ? editor.firstColor : "#7C5CFF" }
                GradientStop { position: 1; color: editor.validColor(editor.secondColor) ? editor.secondColor : "#00C2FF" }
            }
            Text {
                anchors.centerIn: parent
                text: editor.useGradient ? qsTr("Gradient preview") : qsTr("Use the target's existing solid-color binding")
                color: "white"
                style: Text.Outline
                styleColor: "#80000000"
                font.pixelSize: 13
            }
        }
        Text {
            Layout.fillWidth: true
            visible: editor.pendingSolidEdits
            text: qsTr("Apply or clear your pending solid-color edits first. Gradients are applied separately.")
            wrapMode: Text.WordWrap
            color: Theme.logOrange
            font.pixelSize: 12
        }
        Text {
            Layout.fillWidth: true
            text: editor.message
            wrapMode: Text.WordWrap
            color: editor.failed ? Theme.logColor("red") : Theme.textDim
            font.pixelSize: 12
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Close BlueStacks before applying. The original .bak is preserved.")
                color: Theme.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            ActionButton { text: qsTr("Close"); onClicked: editor.close() }
            ActionButton {
                accent: true
                text: editor.useGradient ? qsTr("Apply Gradient") : qsTr("Use Solid")
                enabled: editor.selectedKey !== ""
                         && !editor.pendingSolidEdits
                         && (!editor.useGradient || (editor.validColor(editor.firstColor) && editor.validColor(editor.secondColor)))
                onClicked: {
                    const key = editor.selectedKey;
                    const result = editor.hexMode
                          ? backend.applyHexGradient(key, editor.firstColor, editor.secondColor,
                                                     editor.horizontal, editor.useGradient)
                          : backend.applyThemeGradient(key, editor.firstColor, editor.secondColor,
                                                       editor.horizontal, editor.useGradient);
                    editor.message = result.msg || "";
                    editor.failed = !result.ok;
                    if (result.ok) {
                        editor.refresh();
                        for (let i = 0; i < editor.view.length; ++i) {
                            if (editor.view[i].key === key) { targetBox.currentIndex = i; break; }
                        }
                    }
                }
            }
        }
    }
}
