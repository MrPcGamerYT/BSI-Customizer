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

    // ---- inside the instance (the player's own adb) ----
    property var androidRecord: ({ recorded: false, removed: [], downloads: [], appData: [] })
    property var androidApps: []
    property var androidOthers: []
    property var downloadInfo: ({ entries: [], size: "" })
    property var appDataInfo: []
    property bool androidScanned: false
    property bool androidBusy: false
    property bool androidOk: true
    property string androidMsg: ""
    property bool keepCopy: true
    property bool clearAppData: false

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
        page.refreshAndroid();
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

    // ---- inside the instance ----
    function refreshAndroid() {
        page.androidRecord = backend.androidState();
    }

    function scanAndroid(silent) {
        if (page.instance.length === 0) {
            page.notify(qsTr("Choose an Android instance first."));
            return;
        }
        page.androidBusy = true;
        const result = backend.androidScan(page.instance);
        page.androidBusy = false;
        if (!result || !result.ok) {
            if (!silent) {
                page.androidOk = false;
                page.androidMsg = (result && result.msg) ? result.msg : qsTr("The instance did not answer.");
                page.androidApps = [];
                page.androidScanned = false;
                page.notify(page.androidMsg);
            }
            return;
        }
        const apps = result.apps || [];
        const rows = [];
        for (let i = 0; i < apps.length; i++) {
            const app = apps[i];
            rows.push({ package: app.package, label: app.label, note: app.note,
                        recommended: app.recommended, installed: app.installed,
                        recorded: app.recorded,
                        // The deceptive ones start ticked; nothing else does.
                        checked: app.installed && !app.recorded && app.recommended });
        }
        page.androidApps = rows;
        page.androidOthers = result.others || [];
        page.downloadInfo = result.download || ({ entries: [], size: "" });
        page.appDataInfo = result.appData || [];
        page.androidScanned = true;
        if (!silent) {
            page.androidOk = true;
            page.androidMsg = result.msg || "";
            page.notify(page.androidMsg);
        }
        page.refreshAndroid();
    }

    function tickedApps() {
        let n = 0;
        for (let i = 0; i < page.androidApps.length; i++)
            if (page.androidApps[i].checked)
                n++;
        return n;
    }

    function toggleAndroidApp(index) {
        const rows = page.androidApps.slice();
        if (index < 0 || index >= rows.length)
            return;
        const row = rows[index];
        rows[index] = { package: row.package, label: row.label, note: row.note,
                        recommended: row.recommended, installed: row.installed,
                        recorded: row.recorded, checked: !row.checked };
        page.androidApps = rows;
    }

    function removeAndroidApps() {
        const packages = [];
        for (let i = 0; i < page.androidApps.length; i++)
            if (page.androidApps[i].checked)
                packages.push(page.androidApps[i].package);
        if (packages.length === 0) {
            page.notify(qsTr("Tick at least one installed app first."));
            return;
        }
        page.androidBusy = true;
        const result = backend.androidApply(packages, false, false, false, page.instance);
        page.androidBusy = false;
        page.androidOk = !!(result && result.ok);
        page.androidMsg = (result && result.msg) ? result.msg : "";
        if (page.androidScanned)
            page.scanAndroid(true);
        page.refreshAndroid();
        if (page.androidMsg.length > 0)
            page.notify(page.androidMsg);
    }

    function cleanAndroidStorage(wipe, keep, appData) {
        page.androidBusy = true;
        const result = backend.androidApply([], wipe, keep, appData, page.instance);
        page.androidBusy = false;
        page.androidOk = !!(result && result.ok);
        page.androidMsg = (result && result.msg) ? result.msg : "";
        if (page.androidScanned)
            page.scanAndroid(true);
        page.refreshAndroid();
        if (page.androidMsg.length > 0)
            page.notify(page.androidMsg);
    }

    function restoreAndroidApps() {
        page.androidBusy = true;
        const result = backend.androidRestore(page.instance);
        page.androidBusy = false;
        page.androidOk = !!(result && result.ok);
        page.androidMsg = (result && result.msg) ? result.msg : "";
        if (page.androidScanned)
            page.scanAndroid(true);
        page.refreshAndroid();
        if (page.androidMsg.length > 0)
            page.notify(page.androidMsg);
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
                                   + "itself exposes, removes the preinstalled Android apps and wipes "
                                   + "the guest's Download folder, with one undo record for everything "
                                   + "it writes.")
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

            // ---- inside the instance ----
            // Installed apps and the guest's own storage are not in Root.vhd:
            // that image is shared and read-only. They are changed through the
            // instance's own ADB, with the same undo record as everything else.
            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: qsTr("Android apps and storage")
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("Installed apps live in the instance's own Android disk "
                                           + "— not in Root.vhd, the shared read-only system image that "
                                           + "every clone boots from and every player update replaces. "
                                           + "This section removes them through the player's own adb "
                                           + "while the instance runs, and puts them back from the "
                                           + "record.")
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                        }
                        ActionButton {
                            text: page.androidBusy ? qsTr("Working…") : qsTr("Scan apps")
                            enabled: page.ready && !page.androidBusy && page.instance.length > 0
                            onClicked: page.scanAndroid(false)
                        }
                        ActionButton {
                            text: qsTr("Undo in this instance")
                            enabled: page.ready && !page.androidBusy && page.instance.length > 0
                                     && (page.androidRecord.removed || []).length > 0
                            onClicked: page.restoreAndroidApps()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: page.androidMsg.length > 0
                        text: page.androidMsg
                        color: page.androidOk ? (page.androidScanned ? Theme.textDim : Theme.warn)
                                              : Theme.danger
                        font.pixelSize: 10
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 8
                        visible: page.androidApps.length > 0
                        Repeater {
                            model: page.androidApps
                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                implicitHeight: appRow.implicitHeight + 20
                                radius: 10
                                color: modelData.checked ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.09)
                                                         : Qt.rgba(1, 1, 1, 0.02)
                                border.width: 1
                                border.color: modelData.checked ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.45)
                                                                : Theme.border
                                RowLayout {
                                    id: appRow
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 10
                                    CheckBox {
                                        id: appTick
                                        checked: modelData.checked
                                        enabled: page.ready && !page.androidBusy
                                                 && modelData.installed && !modelData.recorded
                                        padding: 0
                                        implicitWidth: 18
                                        implicitHeight: 18
                                        indicator: Rectangle {
                                            implicitWidth: 18
                                            implicitHeight: 18
                                            radius: 5
                                            color: appTick.checked ? Theme.accent : "transparent"
                                            border.width: 1
                                            border.color: appTick.checked ? Theme.accent : Theme.border
                                            Text {
                                                anchors.centerIn: parent
                                                visible: appTick.checked
                                                text: "\u2713"
                                                color: "#04121c"
                                                font.pixelSize: 11
                                                font.weight: Font.Bold
                                            }
                                        }
                                        onToggled: page.toggleAndroidApp(index)
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            text: modelData.label
                                            color: modelData.installed && !modelData.recorded
                                                   ? Theme.text : Theme.textDim
                                            font.pixelSize: 12
                                            font.weight: Font.DemiBold
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            text: modelData.note
                                            color: Theme.textDim
                                            font.pixelSize: 10
                                        }
                                        Text {
                                            Layout.fillWidth: true
                                            wrapMode: Text.Wrap
                                            text: modelData.recorded ? qsTr("Removed by this tool — "
                                                                           + "Undo in this instance puts "
                                                                           + "it back.")
                                                  : modelData.installed ? qsTr("Installed") + " · "
                                                                           + modelData.package
                                                  : qsTr("Not installed") + " · " + modelData.package
                                            color: modelData.recorded ? Theme.warn
                                                   : modelData.installed ? Theme.green : Theme.textDim
                                            font.pixelSize: 10
                                        }
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: page.androidApps.length > 0
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("%1 app(s) ticked. Removal is per user, so the system image is "
                                       + "never written and the instance still boots.").arg(page.tickedApps())
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                        ActionButton {
                            accent: true
                            text: qsTr("Remove ticked apps")
                            enabled: page.ready && !page.androidBusy && page.tickedApps() > 0
                            onClicked: page.removeAndroidApps()
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: (page.androidOthers || []).length > 0
                        text: qsTr("Also installed in Android, left alone because they are part of "
                                   + "how BlueStacks works: ") + (page.androidOthers || []).join(", ")
                        color: Theme.textDim
                        font.pixelSize: 10
                    }

                    // ---- guest storage ----
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Theme.border
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        CheckBox {
                            id: wipeDownloads
                            text: qsTr("Empty /sdcard/Download")
                            checked: true
                            palette.text: Theme.text
                        }
                        CheckBox {
                            id: keepCopyBox
                            text: qsTr("Keep a copy on this PC first")
                            checked: page.keepCopy
                            enabled: wipeDownloads.checked
                            palette.text: Theme.textDim
                            onToggled: page.keepCopy = checked
                        }
                        CheckBox {
                            id: clearAppDataBox
                            text: qsTr("Also clear app data (Android/data, Android/obb)")
                            checked: false
                            palette.text: Theme.textDim
                            onToggled: page.clearAppData = checked
                        }
                        Item { Layout.fillWidth: true }
                        ActionButton {
                            text: page.androidBusy ? qsTr("Working…") : qsTr("Clean guest storage")
                            enabled: page.ready && !page.androidBusy && page.instance.length > 0
                                     && (wipeDownloads.checked || clearAppDataBox.checked)
                            onClicked: page.cleanAndroidStorage(wipeDownloads.checked,
                                                                keepCopyBox.checked,
                                                                clearAppDataBox.checked)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        visible: page.androidScanned
                        text: qsTr("Download folder: %1 item(s), %2.")
                                  .arg((page.downloadInfo.entries || []).length)
                                  .arg(page.downloadInfo.size || qsTr("unknown size"))
                              + qsTr(" App data: %1 in Android/data, %2 in Android/obb.")
                                    .arg((page.appDataInfo[0] || {}).count || 0)
                                    .arg((page.appDataInfo[1] || {}).count || 0)
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("The Download folder is deleted completely and recreated empty, so no "
                                   + "file, hidden file or “apps” sub-folder is left behind. Clearing app "
                                   + "data signs every app out and erases downloaded game assets. The "
                                   + "instance must be running with Android debugging on.")
                        color: Theme.textDim
                        font.pixelSize: 10
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
                                       + "from memory when it exits. Restore also puts the Android "
                                       + "apps back, so the instance has to be running for that "
                                       + "part. Selected: %1")
                                  .arg(page.countChecked())
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                        ActionButton {
                            text: page.running ? qsTr("Working…") : qsTr("Restore everything")
                            enabled: page.ready && !page.running && (page.state.recorded
                                                                      || page.state.anyCloudMoved
                                                                      || page.androidRecord.recorded)
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
