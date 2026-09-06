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
    property string selectedKey: ""
    property string firstColor: "#7C5CFF"
    property string secondColor: "#00C2FF"
    property bool horizontal: true
    property bool useGradient: true
    property string message: ""
    property bool failed: false

    function validColor(value) { return /^#(?:[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$/.test(value); }
    function choose(index) {
        const targets = backend.gradientTargets;
        if (index < 0 || index >= targets.length) { selectedKey = ""; return; }
        const target = targets[index];
        selectedKey = target.key;
        firstColor = target.first;
        secondColor = target.second;
        horizontal = target.horizontal;
        // Opening a solid target proposes a gradient; Solid is an explicit choice.
        useGradient = true;
    }
    onOpened: {
        const result = backend.loadGradientTargets();
        message = result.msg || "";
        failed = !result.ok;
        targetBox.currentIndex = backend.gradientTargets.length ? 0 : -1;
        choose(targetBox.currentIndex);
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
            text: qsTr("Apply two-stop gradients to compatible backgrounds or existing Gradient properties. "
                       + "Text/icon color properties stay solid. Applying replaces the selected target's existing stops.")
        }
        ComboBox {
            id: targetBox
            Layout.fillWidth: true
            model: backend.gradientTargets
            textRole: "label"
            onActivated: (index) => editor.choose(index)
        }
        Text {
            Layout.fillWidth: true
            color: Theme.textDim
            font.pixelSize: 11
            elide: Text.ElideMiddle
            text: targetBox.currentIndex >= 0 && targetBox.currentIndex < backend.gradientTargets.length
                  ? backend.gradientTargets[targetBox.currentIndex].resource : qsTr("No compatible target")
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
                enabled: editor.useGradient
                model: [qsTr("Left → right"), qsTr("Top → bottom")]
                currentIndex: editor.horizontal ? 0 : 1
                onActivated: (index) => editor.horizontal = index === 0
            }
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
                enabled: editor.selectedKey !== "" && !editor.pendingSolidEdits
                         && (!editor.useGradient || (editor.validColor(editor.firstColor) && editor.validColor(editor.secondColor)))
                onClicked: {
                    const key = editor.selectedKey;
                    const result = backend.applyThemeGradient(key, editor.firstColor, editor.secondColor,
                                                              editor.horizontal, editor.useGradient);
                    editor.message = result.msg || "";
                    editor.failed = !result.ok;
                    if (result.ok) {
                        const targets = backend.gradientTargets;
                        for (let i = 0; i < targets.length; ++i) {
                            if (targets[i].key === key) { targetBox.currentIndex = i; break; }
                        }
                    }
                }
            }
        }
    }
}
