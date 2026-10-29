import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

// BlueStacks Debloat — two ways in.
//
//  * **Live ADB** talks to a running instance through the player's own adb and
//    removes preinstalled apps and the guest's Download folder there.
//  * **Deep Engine Image Debloater** opens the engine's Android system image in place,
//    like a 7-Zip window: browse, extract, add, delete — with every write
//    journalled first, so one Undo puts the image back.
//
// Below them are the settings switches, which edit bluestacks.conf, the hosts
// file and the cloud companions.
Item {
    id: page

    signal notify(string message)

    property string imageWriteError: ""

    Dialog {
        id: imageWriteErrorDialog
        parent: Overlay.overlay
        modal: true
        focus: true
        title: qsTr("Could not enable image editing")
        width: Math.min(600, parent ? parent.width - 32 : 600)
        height: Math.min(360, parent ? parent.height - 32 : 360)
        x: parent ? Math.round((parent.width - width) / 2) : 0
        y: parent ? Math.round((parent.height - height) / 2) : 0
        padding: 18
        standardButtons: Dialog.Ok
        background: Rectangle { color: Theme.card; border.color: Theme.border; radius: 12 }
        contentItem: ScrollView {
            clip: true
            contentWidth: availableWidth
            Text {
                width: imageWriteErrorDialog.width - 44
                text: page.imageWriteError
                color: Theme.text
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }
        }
    }

    // 0 = live ADB, 1 = Root.vhd, 2 = settings
    property int mode: 0

    // ---- the switches (unchanged engine) ----
    property var state: ({ supported: false, applied: false, conf: "", detail: "",
                           instances: [], instance: "", already: [], hostsInstalled: false,
                           cloud: [], anyCloudMoved: false, recorded: false,
                           pendingChanges: 0, backups: 0 })
    property var rows: []
    property bool ready: false
    property string instance: ""
    property string lastMsg: ""
    property bool lastOk: true
    property bool running: false
    // The switch set is the older half of this page: it stays, but folded away,
    // so the tab reads as the two things it is for.
    property bool showSwitches: false

    // ---- live ADB ----
    property var androidRecord: ({ recorded: false, removed: [], downloads: [], appData: [] })
    property var androidApps: []
    property var androidOthers: []
    property var downloadInfo: ({ entries: [], size: "" })
    property var appDataInfo: []
    property bool androidScanned: false
    property string androidSearch: ""
    property bool androidBusy: false
    property bool androidOk: true
    property string androidMsg: ""
    property bool keepCopy: true
    property bool clearAppData: false

    // ---- Root.vhd ----
    property string imagePath: ""
    property var imageInfo: ({ ok: false, msg: "", writable: false, description: "", kind: "" })
    property string imageMsg: ""
    property bool imageOk: true
    property bool imageBusy: false
    property bool imageOpen: false
    property var engineFolders: []

    // =====================================================================
    // switches
    // =====================================================================

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

    function groupRows(group) {
        const list = [];
        for (let i = 0; i < page.rows.length; i++)
            if (page.rows[i].group === group)
                list.push({ index: i, row: page.rows[i] });
        return list;
    }

    // =====================================================================
    // live ADB
    // =====================================================================

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
        const list = [];
        for (let i = 0; i < apps.length; i++) {
            const app = apps[i];
            list.push({ package: app.package, label: app.label, note: app.note,
                        recommended: app.recommended, installed: app.installed,
                        recorded: app.recorded,
                        checked: app.installed && !app.recorded && app.recommended,
                        debloatable: app.debloatable !== false });
        }
        page.androidApps = list;
        page.androidSearch = "";
        androidAppSearch.text = "";
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

    function filteredAndroidApps() {
        const needle = page.androidSearch.trim().toLowerCase();
        const matches = [];
        for (let i = 0; i < page.androidApps.length; ++i) {
            const app = page.androidApps[i];
            const text = (app.label + " " + app.package + " " + app.note).toLowerCase();
            if (needle.length === 0 || text.indexOf(needle) >= 0)
                matches.push(Object.assign({ sourceIndex: i }, app));
        }
        return matches;
    }

    function tickedApps() {
        let n = 0;
        for (let i = 0; i < page.androidApps.length; i++)
            if (page.androidApps[i].checked)
                n++;
        return n;
    }

    function selectInstalledAndroidApps() {
        const list = page.androidApps.slice();
        for (let i = 0; i < list.length; ++i) {
            const app = list[i];
            if (app.installed && app.debloatable && !app.recorded)
                list[i] = Object.assign({}, app, { checked: true });
        }
        page.androidApps = list;
    }

    function clearAndroidSelection() {
        const list = page.androidApps.slice();
        for (let i = 0; i < list.length; ++i)
            if (list[i].checked)
                list[i] = Object.assign({}, list[i], { checked: false });
        page.androidApps = list;
    }

    function toggleAndroidApp(index) {
        const list = page.androidApps.slice();
        if (index < 0 || index >= list.length)
            return;
        const row = list[index];
        list[index] = { package: row.package, label: row.label, note: row.note,
                        recommended: row.recommended, installed: row.installed,
                        recorded: row.recorded, checked: !row.checked,
                        debloatable: row.debloatable };
        page.androidApps = list;
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

    // =====================================================================
    // Root.vhd
    // =====================================================================

    function findImage() {
        // The engine of this installation first: <ProgramData>/BlueStacks*/Engine/*/Root.vhd
        const found = backend.rootvhdFind();
        page.engineFolders = backend.rootvhdEngineFolders();
        if (found && found.length > 0)
            page.imagePath = found;
        if (page.imagePath.length > 0)
            page.openImage();
    }

    function openImage() {
        if (page.imagePath.length === 0) {
            page.imageOk = false;
            page.imageMsg = qsTr("Choose a Root.vhd, VHDX image, or BlueStacks engine package.");
            return;
        }
        page.imageBusy = true;
        let sourceMessage = "";
        const lowerPath = page.imagePath.toLowerCase();
        if (lowerPath.endsWith(".exe") || lowerPath.endsWith(".7z")) {
            const extracted = backend.rootvhdOpenEngineArchive(page.imagePath);
            if (!extracted || !extracted.ok) {
                page.imageBusy = false;
                page.imageOk = false;
                page.imageOpen = false;
                page.imageMsg = extracted && extracted.msg
                                ? extracted.msg : qsTr("The engine archive could not be opened.");
                return;
            }
            page.imagePath = extracted.path;
            sourceMessage = extracted.msg || "";
        }
        const info = backend.rootvhdInfo(page.imagePath);
        page.imageBusy = false;
        page.imageInfo = info || ({ ok: false, msg: "" });
        page.imageOk = !!(info && info.ok);
        page.imageOpen = page.imageOk;
        page.imageMsg = (info && info.msg) ? info.msg : "";
        if (sourceMessage.length > 0)
            page.imageMsg = sourceMessage + " " + page.imageMsg;
    }

    Component.onCompleted: {
        refresh();
        findImage();
    }
    onVisibleChanged: if (visible) refresh()

    Connections {
        target: backend
        function onExePathChanged() { page.refresh(); page.findImage(); }
        function onConfPathChanged() { page.refresh(); }
    }

    readonly property string engineUrl: "https://ak-build.bluestacks.com/public/app-player/windows/msi5/5.21.151.6303/4aa785df8f4801455294736aa16b0e8c/Android/Pie64/Pie64.exe?oem=msi5"

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

            // ---- header ----
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: qsTr("Deep Engine Image Debloater")
                        color: Theme.text
                        font.pixelSize: 24
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Two ways to clean an instance. Live ADB removes apps from a "
                                   + "running instance; Deep Engine Image Debloater edits the "
                                   + "engine's Root.vhd or VHDX directly.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }

            // ---- nothing to talk to yet ----
            Rectangle {
                Layout.fillWidth: true
                visible: !page.ready
                implicitHeight: notice.implicitHeight + 20
                radius: 12
                color: Qt.rgba(255 / 255, 196 / 255, 92 / 255, 0.10)
                border.width: 1
                border.color: Qt.rgba(255 / 255, 196 / 255, 92 / 255, 0.45)
                Text {
                    id: notice
                    anchors.fill: parent
                    anchors.margins: 10
                    wrapMode: Text.Wrap
                    text: qsTr("BlueStacks is not set up yet: point the tool at HD-Player.exe on the "
                               + "Dashboard. The switch half needs that installation; the Root.vhd "
                               + "half works on any image you open yourself.")
                    color: Theme.warn
                    font.pixelSize: 11
                }
            }

            // ---- the two main choices ----
            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Repeater {
                    model: [
                        { title: qsTr("1 · Live ADB debloat"), icon: "\u26A1",
                          text: qsTr("Remove the preinstalled apps and wipe the guest's Download "
                                     + "folder inside a running instance. Nothing on disk is "
                                     + "edited; every app can be put back.") },
                        { title: qsTr("2 · Deep Engine Image Debloater"), icon: "\u26C1",
                          text: qsTr("Open Root.vhd in a file manager: add, rename, copy, move, "
                                     + "replace, extract, and delete files or folders. Changes are "
                                     + "journalled so Undo restores the original image.") }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: choice.implicitHeight + 28
                        radius: 14
                        color: page.mode === index
                               ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.10)
                               : Qt.rgba(1, 1, 1, 0.02)
                        border.width: page.mode === index ? 2 : 1
                        border.color: page.mode === index ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.6)
                                                          : Theme.border
                        ColumnLayout {
                            id: choice
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 6
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: modelData.icon
                                    color: Theme.accent
                                    font.pixelSize: 18
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: modelData.title
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                }
                                ActionButton {
                                    accent: page.mode !== index
                                    enabled: page.mode !== index
                                    text: page.mode === index ? qsTr("Opened") : qsTr("Open")
                                    onClicked: page.mode = index
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: modelData.text
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                        }
                        MouseArea {
                            // Behind the card's own buttons, so they win the click.
                            anchors.fill: parent
                            z: -1
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.mode = index
                        }
                    }
                }
            }

            // =================================================================
            // live ADB
            // =================================================================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 16
                visible: page.mode === 0

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
                                    text: qsTr("The running instance")
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: qsTr("Talks to the instance through the player's own "
                                               + "HD-Adb.exe. Scan lists every installed package; "
                                               + "the reviewed debloat suggestions are prechecked, "
                                               + "and other installed apps can be selected too. "
                                               + "Turn on ADB below and start the instance first.")
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                }
                            }
                            ComboBox {
                                id: instanceBox
                                Layout.preferredWidth: 180
                                model: page.state.instances || []
                                currentIndex: Math.max(0, model.indexOf(page.instance))
                                onActivated: page.instance = currentText
                            }
                            ActionButton {
                                text: page.androidBusy ? qsTr("Working…") : qsTr("Scan apps")
                                enabled: !page.androidBusy && page.instance.length > 0
                                onClicked: page.scanAndroid(false)
                            }
                            ActionButton {
                                text: qsTr("Undo apps")
                                enabled: !page.androidBusy && page.instance.length > 0
                                         && (page.androidRecord.removed || []).length > 0
                                onClicked: page.restoreAndroidApps()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: page.androidMsg.length > 0
                            text: page.androidMsg
                            color: page.androidOk ? Theme.textDim : Theme.danger
                            font.pixelSize: 10
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: page.androidApps.length > 0
                            TextField {
                                id: androidAppSearch
                                Layout.fillWidth: true
                                placeholderText: qsTr("Search installed apps or package names…")
                                color: Theme.text
                                placeholderTextColor: Theme.textDim
                                font.pixelSize: 12
                                selectByMouse: true
                                text: page.androidSearch
                                onTextChanged: page.androidSearch = text
                                background: Rectangle {
                                    color: Theme.raised
                                    radius: 8
                                    border.width: 1
                                    border.color: androidAppSearch.activeFocus ? Theme.accent : Theme.border
                                }
                            }
                            ActionButton {
                                text: qsTr("Select installed")
                                enabled: !page.androidBusy
                                onClicked: page.selectInstalledAndroidApps()
                            }
                            ActionButton {
                                text: qsTr("Clear")
                                enabled: !page.androidBusy && page.tickedApps() > 0
                                onClicked: page.clearAndroidSelection()
                            }
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 10
                            rowSpacing: 8
                            visible: page.androidApps.length > 0
                            Repeater {
                                model: page.filteredAndroidApps()
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    implicitHeight: appRow.implicitHeight + 20
                                    radius: 10
                                    color: modelData.checked ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.09)
                                                             : Qt.rgba(1, 1, 1, 0.02)
                                    border.width: 1
                                    border.color: modelData.checked
                                                  ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.45)
                                                  : Theme.border
                                    RowLayout {
                                        id: appRow
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 10
                                        CheckBox {
                                            id: appTick
                                            checked: modelData.checked
                                            enabled: !page.androidBusy && modelData.installed
                                                     && modelData.debloatable && !modelData.recorded
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
                                            onToggled: page.toggleAndroidApp(modelData.sourceIndex)
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
                                                text: modelData.recorded
                                                      ? qsTr("Removed by this tool — Undo puts it back.")
                                                      : modelData.installed
                                                        ? qsTr("Installed") + " · " + modelData.package
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
                                text: qsTr("%1 of %2 installed app(s) selected. Only the reviewed "
                                           + "suggestions are prechecked; removing system packages "
                                           + "can break Android. Changes are per user and undoable.")
                                      .arg(page.tickedApps()).arg(page.androidApps.filter(a => a.installed).length)
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                            ActionButton {
                                accent: true
                                text: qsTr("Remove ticked apps")
                                enabled: !page.androidBusy && page.tickedApps() > 0
                                onClicked: page.removeAndroidApps()
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: (page.androidOthers || []).length > 0
                            text: qsTr("All installed packages are shown above, including apps "
                                      + "that are not on the reviewed suggestion list.")
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    pad: 14
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 10
                        Text {
                            text: qsTr("Guest storage")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: page.androidScanned
                            text: qsTr("Download folder: %1 item(s), %2 · app data: %3 in "
                                       + "Android/data, %4 in Android/obb.")
                                      .arg((page.downloadInfo.entries || []).length)
                                      .arg(page.downloadInfo.size || qsTr("unknown size"))
                                      .arg((page.appDataInfo[0] || {}).count || 0)
                                      .arg((page.appDataInfo[1] || {}).count || 0)
                            color: Theme.textDim
                            font.pixelSize: 10
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
                                text: qsTr("Also clear Android/data and Android/obb")
                                checked: false
                                palette.text: Theme.textDim
                                onToggled: page.clearAppData = checked
                            }
                            Item { Layout.fillWidth: true }
                            ActionButton {
                                text: page.androidBusy ? qsTr("Working…") : qsTr("Clean storage")
                                enabled: !page.androidBusy && page.instance.length > 0
                                         && (wipeDownloads.checked || clearAppDataBox.checked)
                                onClicked: page.cleanAndroidStorage(wipeDownloads.checked,
                                                                    keepCopyBox.checked,
                                                                    clearAppDataBox.checked)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("The Download folder is deleted completely and recreated "
                                       + "empty, so nothing — not a hidden file or an “apps” "
                                       + "sub-folder — is left behind.")
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }
                }
            }

            // =================================================================
            // Root.vhd file manager
            // =================================================================
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 16
                visible: page.mode === 1

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
                                    text: qsTr("Engine image")
                                    color: Theme.text
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: qsTr("Root.vhd is the Android system disk inside the "
                                               + "engine folder. You can also open standalone VHDX "
                                               + "images. Close BlueStacks before editing a disk "
                                               + "that the player may be using.")
                                    color: Theme.textDim
                                    font.pixelSize: 10
                                }
                            }
                            ActionButton {
                                text: qsTr("Find")
                                enabled: !page.imageBusy
                                onClicked: page.findImage()
                            }
                        }
                        PathRow {
                            Layout.fillWidth: true
                            label: qsTr("Root.vhd / VHDX")
                            showLoad: true
                            readOnly: page.imageBusy
                            text: page.imagePath
                            onLoad: {
                                page.imagePath = text;
                                page.openImage();
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: page.imageMsg.length > 0
                            text: page.imageMsg
                            color: page.imageOk ? Theme.textDim : Theme.danger
                            font.pixelSize: 10
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: page.imageOpen
                            text: (page.imageInfo.kind || "") + " · "
                                  + (page.imageInfo.description || "")
                                  + (page.imageInfo.writable ? qsTr(" · writable")
                                                             : qsTr(" · read-only"))
                            color: page.imageInfo.writable ? Theme.green : Theme.warn
                            font.pixelSize: 10
                        }
                    }
                }

                // The file manager itself: folder tree, listing, search and
                // drag-and-drop, all against the image.
                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 560
                    pad: 12
                    RootVhdBrowser {
                        anchors.fill: parent
                        imagePath: page.imagePath
                        active: page.imageOpen
                        writable: !!page.imageInfo.writable
                        readOnlyReason: page.imageInfo.reason || ""
                        onNotify: (message) => page.notify(message)
                        onEnableWriteRequested: {
                            page.imageBusy = true;
                            const result = backend.rootvhdEnableWrite(page.imagePath);
                            page.imageBusy = false;
                            if (result && result.ok) {
                                page.openImage();
                                page.notify(result.msg || qsTr("Editing enabled."));
                            } else {
                                const detail = result && result.msg
                                               ? result.msg
                                               : qsTr("Windows could not enable write access.");
                                page.imageMsg = detail;
                                page.imageOk = false;
                                page.imageWriteError = detail;
                                imageWriteErrorDialog.open();
                                page.notify(qsTr("Editing could not be enabled. The Windows error details are open."));
                            }
                        }
                    }
                }

                // ---- how to get the engine files ----
                Card {
                    Layout.fillWidth: true
                    pad: 14
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 6
                        Text {
                            text: qsTr("No engine folder yet?")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("This tool includes its own 7-Zip archive reader. Select the "
                                       + "engine package EXE or a .7z file in the Root.vhd field; "
                                       + "BSI Customizer extracts Root.vhd into its private working "
                                       + "cache and opens it. No WinRAR, 7-Zip or other extractor "
                                       + "needs to be installed. The original package is unchanged.")
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: qsTr("Engine folders seen for this installation: ")
                                  + (page.engineFolders.length > 0
                                     ? page.engineFolders.join("  ·  ")
                                     : qsTr("none — choose the engine package EXE above, or browse "
                                            + "to a Root.vhd or VHDX image"))
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ActionButton {
                                text: qsTr("Open the engine download")
                                onClicked: Qt.openUrlExternally(page.engineUrl)
                            }
                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }

            // =================================================================
            // settings switches (folded away)
            // =================================================================
            Card {
                Layout.fillWidth: true
                pad: 14
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    // The disclosure row: the switches only take space when asked
                    // for, so the two choices above stay the page.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            text: page.showSwitches ? "▾" : "▸"
                            color: Theme.accent
                            font.pixelSize: 13
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: qsTr("Settings switches — ads, telemetry, cloud, hosts, tuning")
                                color: Theme.text
                                font.pixelSize: 13
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: page.showSwitches
                                      ? qsTr("Close BlueStacks before applying; every value is "
                                             + "recorded so Restore puts it back.")
                                      : qsTr("Separate from the two debloaters: these edit "
                                             + "bluestacks.conf, the hosts file and the companions.")
                                color: Theme.textDim
                                font.pixelSize: 10
                            }
                        }
                        ActionButton {
                            text: page.showSwitches ? qsTr("Hide") : qsTr("Show")
                            onClicked: page.showSwitches = !page.showSwitches
                        }
                    }

                    // everything below only exists once the row is opened
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        visible: page.showSwitches

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Item { Layout.fillWidth: true }
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
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            visible: page.lastMsg.length > 0
                            text: page.lastMsg
                            color: page.lastOk ? Theme.text : Theme.danger
                            font.pixelSize: 11
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
                    }
                }
            }

            Item { Layout.preferredHeight: 8 }
        }
    }

}
