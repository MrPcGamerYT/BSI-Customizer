import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

// BlueStacks Debloat — the switch set, the instance it applies to, and the one
// undo record every write leaves behind. Everything here edits what the player
// already owns: bluestacks.conf values, the Windows hosts file and the two
// cloud companion folders. Nothing is invented, nothing is deleted.
Item {
    id: page

    signal notify(string message)

    // What the engine reports for the selected installation.
    property var state: ({ supported: false, applied: false, conf: "", detail: "",
                           instances: [], instance: "", already: [], hostsInstalled: false,
                           cloud: [], anyCloudMoved: false, recorded: false,
                           pendingChanges: 0, backups: 0 })
    // One row per switch: id, group, label, hint, needsInstance, checked.
    property var rows: []
    property bool ready: false
    property string instance: ""
    property string lastMsg: ""
    property bool lastOk: true
    // Applying writes files: the card stays inert while the engine works.
    property bool running: false

    function refresh() {
        page.state = backend.debloatState();
        page.ready = !!page.state.supported;
        page.instance = page.state.instance || "";
        const options = backend.debloatOptions();
        const next = [];
        for (let i = 0; i < options.length; i++) {
            const option = options[i];
            next.push({
                id: option.id,
                group: option.group,
                label: option.label,
                hint: option.hint,
                needsInstance: option.needsInstance,
                checked: false,
                // Already at its target: the row still shows, greyed, so the
                // list always describes the whole tool instead of shrinking.
                done: (page.state.already || []).indexOf(option.id) >= 0
            });
        }
        page.rows = next;
    }

    function countChecked() {
        let n = 0;
        for (let i = 0; i < page.rows.length; i++)
            if (page.rows[i].checked)
                n++;
        return n;
    }

    function setChecked(index, checked) {
        const next = page.rows.slice();
        if (index < 0 || index >= next.length)
            return;
        const row = next[index];
        next[index] = { id: row.id, group: row.group, label: row.label, hint: row.hint,
                        needsInstance: row.needsInstance, checked: checked, done: row.done };
        page.rows = next;
    }

    function selectAll() {
        const next = page.rows.slice();
        for (let i = 0; i < next.length; i++) {
            const row = next[i];
            next[i] = { id: row.id, group: row.group, label: row.label, hint: row.hint,
                        needsInstance: row.needsInstance, checked: !row.done, done: row.done };
        }
        page.rows = next;
    }

    function clearAll() {
        const next = page.rows.slice();
        for (let i = 0; i < next.length; i++) {
            const row = next[i];
            next[i] = { id: row.id, group: row.group, label: row.label, hint: row.hint,
                        needsInstance: row.needsInstance, checked: false, done: row.done };
        }
        page.rows = next;
    }

    function applySelection() {
        const ids = [];
        for (let i = 0; i < page.rows.length; i++)
            if (page.rows[i].checked)
                ids.push(page.rows[i].id);
        if (ids.length === 0) {
            page.notify(qsTr("Select at least one option first."));
            return;
        }
        page.running = true;
        const result = backend.debloatApply(ids, page.instance);
        page.running = false;
        page.lastOk = !!(result && result.ok);
        page.lastMsg = (result && result.msg) ? result.msg : "";
        page.refresh();
        if (page.lastMsg.length > 0)
            page.notify(page.lastMsg);
    }

    function restoreAll() {
        page.running = true;
        const result = backend.debloatRestore();
        page.running = false;
        page.lastOk = !!(result && result.ok);
        page.lastMsg = (result && result.msg) ? result.msg : "";
        page.refresh();
        if (page.lastMsg.length > 0)
            page.notify(page.lastMsg);
    }

    function groupRows(group) {
        const list = [];
        for (let i = 0; i < page.rows.length; i++)
            if (page.rows[i].group === group)
                list.push({ index: i, row: page.rows[i] });
        return list;
    }

    Component.onCompleted: refresh()
    onVisibleChanged: if (visible) refresh()

    Connections {
        target: backend
        function onExePathChanged() { page.refresh(); }
        function onConfPathChanged() { page.refresh(); }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: content.implicitHeight
        clip: true

        ColumnLayout {
            id: content
            width: parent.width - 48
            x: 24
            y: 24
            spacing: 16

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: qsTr("BlueStacks Debloat")
                        color: Theme.text
                        font.pixelSize: 24
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Turns off the ads, telemetry, cloud and AI features the player "
                                   + "itself exposes, with one undo record for everything it writes.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
                ActionButton {
                    text: qsTr("Re-scan")
                    enabled: !page.running
                    onClicked: page.refresh()
                }
            }

            // The configuration this tab writes. It is not the patched EXE, so
            // it gets its own path even though the installer usually finds it.
            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Text {
                        text: qsTr("Installation")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    PathRow {
                        id: confPath
                        Layout.fillWidth: true
                        label: qsTr("bluestacks.conf")
                        text: backend.confPath
                        onLoad: backend.confPath = text
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: page.ready
                              ? (page.state.detail + qsTr(" Undo records: ")
                                 + (page.state.recorded ? qsTr("kept for this installation")
                                                        : qsTr("none yet"))
                                 + qsTr(" · full backups: ") + page.state.backups)
                              : page.state.detail
                        color: page.ready ? Theme.textDim : Theme.warn
                        font.pixelSize: 11
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: (page.state.instances || []).length > 0
                        Text {
                            text: qsTr("Android instance")
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                        ComboBox {
                            id: instanceBox
                            Layout.preferredWidth: 200
                            model: page.state.instances || []
                            currentIndex: Math.max(0, model.indexOf(page.instance))
                            onActivated: page.instance = currentText
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Notification and high-FPS switches apply to this instance; "
                                       + "the rest is installation-wide.")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                }
            }

            // ---- what it removes ----
            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Don't want these")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        ActionButton { text: qsTr("Select all"); enabled: page.ready && !page.running; onClicked: page.selectAll() }
                        ActionButton { text: qsTr("Clear"); enabled: page.ready && !page.running; onClicked: page.clearAll() }
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 8
                        Repeater {
                            model: page.rows
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                implicitHeight: rowLayout.implicitHeight + 20
                                radius: 10
                                color: modelData.checked ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.09)
                                                         : Qt.rgba(1, 1, 1, 0.02)
                                border.width: 1
                                border.color: modelData.checked ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.45)
                                                                : Theme.border
                                RowLayout {
                                    id: rowLayout
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10
                                    CheckBox {
                                        id: tick
                                        checked: modelData.checked
                                        enabled: page.ready && !page.running
                                        padding: 0
                                        implicitWidth: 18
                                        implicitHeight: 18
                                        indicator: Rectangle {
                                            implicitWidth: 18
                                            implicitHeight: 18
                                            radius: 5
                                            color: tick.checked ? Theme.accent : "transparent"
                                            border.width: 1
                                            border.color: tick.checked ? Theme.accent : Theme.border
                                            Text {
                                                anchors.centerIn: parent
                                                visible: tick.checked
                                                text: "\u2713"
                                                color: "#04121c"
                                                font.pixelSize: 11
                                                font.weight: Font.Bold
                                            }
                                        }
                                        onToggled: page.setChecked(index, checked)
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            text: modelData.label
                                            color: modelData.done ? Theme.textDim : Theme.text
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            text: modelData.hint
                                            color: Theme.textDim
                                            font.pixelSize: 10
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            visible: modelData.needsInstance && page.instance.length === 0
                                            text: qsTr("Needs an Android instance; choose one above.")
                                            color: Theme.warn
                                            font.pixelSize: 10
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            visible: modelData.done
                                            text: qsTr("Already in place on this installation.")
                                            color: Theme.green
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---- cloud companions ----
            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    Text {
                        text: qsTr("Cloud companions found")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: (page.state.cloud || []).length === 0
                        text: qsTr("BlueStacks X, the Store and the separate Services companion are "
                                   + "not installed here.")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    Repeater {
                        model: page.state.cloud || []
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.preferredWidth: 190
                                text: modelData.name
                                color: modelData.moved ? Theme.textDim : Theme.text
                                font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                text: modelData.path
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                            Text {
                                text: modelData.moved ? qsTr("parked") : qsTr("installed")
                                color: modelData.moved ? Theme.warn : Theme.green
                                font.pixelSize: 10
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("A removed companion is renamed to “<folder>.bsi-disabled” on the same "
                                   + "drive, never deleted: the local player, its disks and every instance "
                                   + "stay untouched. Restore renames it back.")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                }
            }

            // ---- apply / restore ----
            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Close BlueStacks before applying: it rewrites bluestacks.conf "
                                       + "from memory when it exits. Selected: %1")
                                  .arg(page.countChecked())
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                        ActionButton {
                            text: page.running ? qsTr("Working…") : qsTr("Restore everything")
                            enabled: page.ready && !page.running && (page.state.recorded
                                                                      || page.state.anyCloudMoved)
                            onClicked: page.restoreAll()
                        }
                        ActionButton {
                            accent: true
                            text: page.running ? qsTr("Working…") : qsTr("Apply")
                            enabled: page.ready && !page.running && page.countChecked() > 0
                            onClicked: page.applySelection()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: page.lastMsg.length > 0
                        text: page.lastMsg
                        color: page.lastOk ? Theme.text : Theme.danger
                        font.pixelSize: 11
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Only settings this build already declares are written; anything it "
                                   + "does not have is skipped and listed in the result. Every changed "
                                   + "value is recorded first, so Restore puts the exact previous text "
                                   + "back, and three full copies of bluestacks.conf are kept as well.")
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }
}
