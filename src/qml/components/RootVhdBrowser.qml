import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

// A two-pane file manager over an engine's Root.vhd: the folder tree on the
// left, the current folder on the right, and controls to add, rename, copy,
// move, replace, extract and delete. Listings always come from the image.
Item {
    id: browser

    // The image to browse. An empty path, or one that cannot be opened, leaves
    // the panes empty and the buttons disabled.
    property string imagePath: ""
    property bool active: false
    property bool writable: false
    property string readOnlyReason: ""

    signal notify(string message)

    property string currentDir: "/"
    property var entries: []
    property var viewRows: []
    property bool busy: false
    property bool open: false
    property bool ok: true
    property string status: ""
    property string filterText: ""
    property bool searching: false
    property var searchRows: []
    property string searchText: ""
    property int sortColumn: 0          // 0 = name, 1 = size
    property bool sortAscending: true
    property bool hasUndo: false
    property int folderCount: 0
    property int fileCount: 0
    property var pendingEntry: null
    property string renameText: ""
    property string destinationText: "/"
    property string copyNameText: ""

    // The tree: children per path, what is expanded, and the flattened rows the
    // tree paints. Everything is replaced, never mutated, so bindings rerun.
    property var treeChildren: ({})
    property var treeExpanded: ({})
    property var treeRows: []

    // A compact button for the list rows: ActionButton is 36 px tall, which is
    // taller than a row.
    component RowButton: Rectangle {
        id: rowButton
        property string label
        signal clicked()

        implicitWidth: rowLabel.implicitWidth + 16
        implicitHeight: 22
        radius: 6
        color: !rowButton.enabled ? "transparent"
               : rowMouse.containsMouse ? Theme.raised : Qt.rgba(1, 1, 1, 0.05)
        border.width: 1
        border.color: rowButton.enabled ? Theme.border : Qt.rgba(1, 1, 1, 0.04)

        Text {
            id: rowLabel
            anchors.centerIn: parent
            text: rowButton.label
            color: rowButton.enabled ? Theme.soft : Theme.textDim
            font.pixelSize: 10
        }
        MouseArea {
            id: rowMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: rowButton.enabled
            cursorShape: Qt.PointingHandCursor
            onClicked: rowButton.clicked()
        }
    }

    // ---- navigation -------------------------------------------------------

    function reset() {
        browser.entries = [];
        browser.viewRows = [];
        browser.treeChildren = ({});
        browser.treeExpanded = ({});
        browser.treeRows = [];
        browser.searchRows = [];
        browser.searching = false;
        browser.searchText = "";
        browser.filterText = "";
        browser.status = "";
        browser.currentDir = "/";
        browser.open = false;
        if (browser.active && browser.imagePath.length > 0)
            browser.loadFolder("/");
    }

    function loadFolder(path) {
        if (!browser.active || browser.imagePath.length === 0)
            return;
        browser.busy = true;
        const listing = backend.rootvhdList(browser.imagePath, path);
        browser.busy = false;
        if (!listing || !listing.ok) {
            browser.ok = false;
            browser.status = (listing && listing.msg) ? listing.msg
                                                      : qsTr("That folder could not be read.");
            return;
        }
        browser.ok = true;
        browser.open = true;
        browser.currentDir = listing.path || path;
        browser.entries = listing.entries || [];
        browser.hasUndo = !!listing.undo;
        browser.searching = false;
        browser.searchRows = [];
        browser.searchText = "";
        browser.recount();
        browser.sortEntries();
        browser.expandTo(browser.currentDir);
        browser.loadAncestors(browser.currentDir);
    }

    function recount() {
        let folders = 0;
        let files = 0;
        for (let i = 0; i < browser.entries.length; ++i) {
            if (browser.entries[i].dir)
                ++folders;
            else
                ++files;
        }
        browser.folderCount = folders;
        browser.fileCount = files;
    }

    // Filters what is in this folder; the whole-image search is a separate
    // pass, because walking a system image on every keystroke is not free.
    function sortEntries() {
        const needle = browser.filterText.trim().toLowerCase();
        const rows = [];
        for (let i = 0; i < browser.entries.length; ++i) {
            const entry = browser.entries[i];
            if (needle.length > 0 && entry.name.toLowerCase().indexOf(needle) < 0)
                continue;
            rows.push(entry);
        }
        const column = browser.sortColumn;
        const direction = browser.sortAscending ? 1 : -1;
        rows.sort(function (a, b) {
            // Folders stay above files whichever way the column sorts.
            if (a.dir !== b.dir)
                return a.dir ? -1 : 1;
            if (column === 1 && a.size !== b.size)
                return (a.size - b.size) * direction;
            const left = a.name.toLowerCase();
            const right = b.name.toLowerCase();
            return left < right ? -direction : left > right ? direction : 0;
        });
        browser.viewRows = rows;
    }

    function pathRows() {
        const rows = [{ name: "/", path: "/" }];
        const parts = browser.currentDir.split("/").filter(function (part) { return part.length > 0; });
        let built = "";
        for (let i = 0; i < parts.length; ++i) {
            built += "/" + parts[i];
            rows.push({ name: parts[i], path: built });
        }
        return rows;
    }

    function goUp() {
        if (browser.currentDir === "/" || browser.currentDir.length === 0)
            return;
        const parts = browser.currentDir.split("/");
        parts.pop();
        const parent = parts.join("/");
        browser.loadFolder(parent.length === 0 ? "/" : parent);
    }

    function openEntry(entry) {
        if (entry.dir)
            browser.loadFolder(entry.path);
        else
            browser.loadFolder(entry.parent ? entry.parent : browser.currentDir);
    }

    function sizeText(bytes) {
        if (bytes < 1024)
            return bytes + " B";
        if (bytes < 1024 * 1024)
            return (bytes / 1024).toFixed(1) + " KB";
        if (bytes < 1024 * 1024 * 1024)
            return (bytes / (1024 * 1024)).toFixed(1) + " MB";
        return (bytes / (1024 * 1024 * 1024)).toFixed(2) + " GB";
    }

    // ---- the tree ---------------------------------------------------------

    function loadTree(path) {
        if (!browser.active || browser.imagePath.length === 0)
            return;
        const listing = backend.rootvhdList(browser.imagePath, path);
        if (!listing || !listing.ok) return;
        const dirs = [];
        const found = listing.entries || [];
        for (let i = 0; i < found.length; ++i)
            if (found[i].dir)
                dirs.push({ name: found[i].name, path: found[i].path });
        const previous = browser.treeChildren[path];
        if (previous !== undefined && JSON.stringify(previous) === JSON.stringify(dirs))
            return;
        const next = Object.assign({}, browser.treeChildren);
        next[path] = dirs;
        browser.treeChildren = next;
        browser.rebuildTree();
    }

    // Every level of the path, so the tree can show where the listing is.
    function loadAncestors(path) {
        browser.loadTree("/");
        const parts = path.split("/").filter(function (part) { return part.length > 0; });
        let built = "";
        for (let i = 0; i < parts.length; ++i) {
            built += "/" + parts[i];
            browser.loadTree(built);
        }
    }

    function toggleTree(path) {
        const expanded = Object.assign({}, browser.treeExpanded);
        const wasOpen = !!expanded[path];
        expanded[path] = !wasOpen;
        browser.treeExpanded = expanded;
        if (!wasOpen && browser.treeChildren[path] === undefined)
            browser.loadTree(path);
        else
            browser.rebuildTree();
    }

    function expandTree(path) {
        const expanded = Object.assign({}, browser.treeExpanded);
        expanded[path] = true;
        browser.treeExpanded = expanded;
        browser.loadTree(path);
    }

    function expandTo(path) {
        const parts = path.split("/").filter(function (part) { return part.length > 0; });
        const expanded = Object.assign({}, browser.treeExpanded);
        expanded["/"] = true;
        let built = "";
        for (let i = 0; i < parts.length; ++i) {
            built += "/" + parts[i];
            expanded[built] = true;
        }
        browser.treeExpanded = expanded;
        browser.rebuildTree();
    }

    function rebuildTree() {
        const rows = [];
        function walk(path, depth) {
            const children = browser.treeChildren[path];
            if (!children) return;
            for (let i = 0; i < children.length; ++i) {
                const child = children[i];
                rows.push({ name: child.name, path: child.path, depth: depth,
                            expanded: !!browser.treeExpanded[child.path] });
                if (browser.treeExpanded[child.path])
                    walk(child.path, depth + 1);
            }
        }
        walk("/", 0);
        browser.treeRows = rows;
    }

    // ---- search -----------------------------------------------------------

    function searchEverywhere() {
        if (!browser.open) return;
        const query = browser.filterText.trim();
        if (query.length === 0) {
            browser.clearSearch();
            return;
        }
        browser.busy = true;
        const result = backend.rootvhdSearch(browser.imagePath, query);
        browser.busy = false;
        if (!result || !result.ok) {
            browser.ok = false;
            browser.status = (result && result.msg) ? result.msg : qsTr("The search failed.");
            return;
        }
        browser.ok = true;
        browser.searching = true;
        browser.searchText = query;
        browser.searchRows = result.entries || [];
        browser.status = result.msg || "";
    }

    function clearSearch() {
        browser.searching = false;
        browser.searchRows = [];
        browser.searchText = "";
        browser.filterText = "";
        browser.status = "";
        filterField.text = "";
        browser.sortEntries();
    }

    // ---- writes -----------------------------------------------------------

    function refresh() {
        browser.loadFolder(browser.currentDir);
    }

    function report(result) {
        browser.ok = !!(result && result.ok);
        browser.status = (result && result.msg) ? result.msg : "";
        if (browser.status.length > 0)
            browser.notify(browser.status);
        browser.refresh();
    }

    function addFile(localPath) {
        if (!browser.writable) return;
        browser.busy = true;
        const result = backend.rootvhdAddPath(browser.imagePath, browser.currentDir, localPath);
        browser.busy = false;
        browser.report(result);
    }

    function replaceFile(entry, localPath) {
        if (!browser.writable || !entry || entry.dir) return;
        browser.busy = true;
        const result = backend.rootvhdReplace(browser.imagePath, entry.path, localPath);
        browser.busy = false;
        browser.report(result);
    }

    function addFolder(localDir) {
        if (!browser.writable) return;
        browser.busy = true;
        const result = backend.rootvhdAddPath(browser.imagePath, browser.currentDir, localDir);
        browser.busy = false;
        browser.report(result);
    }

    function makeFolder(name) {
        if (!browser.writable) return;
        browser.busy = true;
        const result = backend.rootvhdMkdir(browser.imagePath, browser.currentDir, name);
        browser.busy = false;
        browser.report(result);
    }

    function renameEntry(entry, newName) {
        if (!browser.writable || !entry || !newName || newName.trim().length === 0) return;
        browser.busy = true;
        const result = backend.rootvhdRename(browser.imagePath, entry.path, newName.trim());
        browser.busy = false;
        browser.report(result);
    }

    function moveEntry(entry, destinationDir) {
        if (!browser.writable || !entry || !destinationDir || destinationDir.trim().length === 0) return;
        browser.busy = true;
        const result = backend.rootvhdMove(browser.imagePath, entry.path, destinationDir.trim());
        browser.busy = false;
        browser.report(result);
    }

    function copyEntry(entry, destinationDir, newName) {
        if (!browser.writable || !entry || !destinationDir || destinationDir.trim().length === 0
                || !newName || newName.trim().length === 0) return;
        browser.busy = true;
        const result = backend.rootvhdCopy(browser.imagePath, entry.path,
                                           destinationDir.trim(), newName.trim());
        browser.busy = false;
        browser.report(result);
    }

    function removeEntry(entry) {
        if (!browser.writable) return;
        browser.busy = true;
        const result = backend.rootvhdRemove(browser.imagePath, entry.path);
        browser.busy = false;
        browser.report(result);
    }

    function extractEntry(entry, localPath) {
        browser.busy = true;
        const result = backend.rootvhdExtract(browser.imagePath, entry.path, localPath);
        browser.busy = false;
        browser.ok = !!(result && result.ok);
        browser.status = (result && result.msg) ? result.msg : "";
        if (browser.status.length > 0)
            browser.notify(browser.status);
    }

    function undo() {
        if (!browser.writable) return;
        browser.busy = true;
        const result = backend.rootvhdUndo(browser.imagePath);
        browser.busy = false;
        browser.report(result);
    }

    // Files and folders dropped from Explorer. A folder arrives as a URL with
    // a trailing slash, and the engine decides by looking at the path itself.
    function dropUrls(urls) {
        if (!browser.writable || !urls || urls.length === 0) return;
        let added = 0;
        let last = "";
        for (let i = 0; i < urls.length; ++i) {
            const path = backend.localFilePath(urls[i]);
            if (!path || path.length === 0) continue;
            const result = backend.rootvhdAddPath(browser.imagePath, browser.currentDir, path);
            if (result && result.ok)
                ++added;
            else if (result && result.msg)
                last = result.msg;
        }
        browser.refresh();
        if (added > 0)
            browser.notify(added === 1 ? qsTr("Dropped 1 item into the image.")
                                       : qsTr("Dropped %1 items into the image.").arg(added));
        else if (last.length > 0)
            browser.notify(last);
    }

    // ---- follow the page --------------------------------------------------

    onImagePathChanged: reset()
    onActiveChanged: reset()
    Component.onCompleted: if (active && imagePath.length > 0) reset()

    // ---- dialogs ----------------------------------------------------------

    FileDialog {
        id: addFileDialog
        title: qsTr("Add a file to this folder")
        nameFilters: ["All files (*)"]
        onAccepted: browser.addFile(backend.localFilePath(selectedFile))
    }

    FileDialog {
        id: replaceFileDialog
        title: qsTr("Choose replacement bytes for this image file")
        nameFilters: ["All files (*)"]
        onAccepted: browser.replaceFile(browser.pendingEntry,
                                        backend.localFilePath(selectedFile))
    }

    FolderDialog {
        id: addFolderDialog
        title: qsTr("Add a folder to this folder")
        onAccepted: browser.addFolder(backend.localFilePath(selectedFolder))
    }

    FileDialog {
        id: extractFileDialog
        title: qsTr("Save this file as")
        fileMode: FileDialog.SaveFile
        currentFile: browser.pendingEntry ? browser.pendingEntry.name : ""
        nameFilters: ["All files (*)"]
        onAccepted: browser.extractEntry(browser.pendingEntry, backend.localFilePath(selectedFile))
    }

    FolderDialog {
        id: extractFolderDialog
        title: qsTr("Save this folder into")
        onAccepted: browser.extractEntry(browser.pendingEntry, backend.localFilePath(selectedFolder))
    }

    // =====================================================================
    // the toolbar
    // =====================================================================
    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            ActionButton {
                text: "↑"
                enabled: browser.open && !browser.busy && browser.currentDir !== "/"
                onClicked: browser.goUp()
            }
            // Breadcrumbs, scrollable so a deep path stays reachable.
            Flickable {
                Layout.fillWidth: true
                implicitHeight: 36
                contentWidth: crumbRow.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                RowLayout {
                    id: crumbRow
                    spacing: 4
                    Repeater {
                        model: browser.pathRows()
                        delegate: ActionButton {
                            required property var modelData
                            text: modelData.name
                            enabled: !browser.busy && modelData.path !== browser.currentDir
                            onClicked: browser.loadFolder(modelData.path)
                        }
                    }
                }
            }
            ActionButton {
                text: qsTr("Add file")
                enabled: browser.writable && browser.open && !browser.busy
                onClicked: addFileDialog.open()
            }
            ActionButton {
                text: qsTr("Add folder")
                enabled: browser.writable && browser.open && !browser.busy
                onClicked: addFolderDialog.open()
            }
            ActionButton {
                text: qsTr("New folder")
                enabled: browser.writable && browser.open && !browser.busy
                onClicked: newFolderPopup.open()
            }
            ActionButton {
                text: qsTr("Undo")
                enabled: browser.writable && browser.open && !browser.busy && browser.hasUndo
                onClicked: browser.undo()
            }
            ActionButton {
                text: qsTr("Refresh")
                enabled: browser.open && !browser.busy
                onClicked: browser.refresh()
            }
        }

        // ---- search + status ----
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField {
                id: filterField
                Layout.preferredWidth: 300
                placeholderText: qsTr("Filter this folder — or search the whole image")
                color: Theme.text
                font.pixelSize: 11
                selectByMouse: true
                onTextChanged: {
                    if (browser.filterText !== text) {
                        browser.filterText = text;
                        if (!browser.searching)
                            browser.sortEntries();
                    }
                }
                onAccepted: browser.searchEverywhere()
                background: Rectangle {
                    color: Theme.raised
                    radius: 8
                    border.width: 1
                    border.color: filterField.activeFocus ? Theme.accent : Theme.border
                }
            }
            ActionButton {
                text: qsTr("Search everywhere")
                enabled: browser.open && !browser.busy && browser.filterText.trim().length > 0
                onClicked: browser.searchEverywhere()
            }
            ActionButton {
                text: qsTr("Clear")
                visible: browser.searching || browser.filterText.length > 0
                enabled: !browser.busy
                onClicked: browser.clearSearch()
            }
            Item { Layout.fillWidth: true }
            Text {
                text: browser.searching
                      ? qsTr("%1 result(s) for “%2”").arg(browser.searchRows.length).arg(browser.searchText)
                      : qsTr("%1 folder(s), %2 file(s)").arg(browser.folderCount).arg(browser.fileCount)
                color: Theme.textDim
                font.pixelSize: 10
            }
        }

        // ---- the panes ----
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            // ---- the folder tree ----
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                radius: 10
                color: Theme.cardLow
                border.width: 1
                border.color: Theme.border

                ListView {
                    id: treeView
                    anchors.fill: parent
                    anchors.margins: 5
                    clip: true
                    model: browser.treeRows
                    spacing: 1
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    delegate: Rectangle {
                        required property var modelData
                        width: treeView.width - 12
                        height: 24
                        x: modelData.depth * 11
                        radius: 6
                        color: modelData.path === browser.currentDir
                               ? Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.16)
                               : (treeHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent")
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 3
                            anchors.rightMargin: 4
                            spacing: 2
                            Text {
                                Layout.preferredWidth: 10
                                text: modelData.expanded ? "▾" : "▸"
                                color: Theme.textDim
                                font.pixelSize: 9
                                MouseArea {
                                    anchors.fill: parent
                                    anchors.margins: -4
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: browser.toggleTree(modelData.path)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: "📁 " + modelData.name
                                color: modelData.path === browser.currentDir ? Theme.accentText
                                                                            : Theme.soft
                                font.pixelSize: 11
                            }
                        }
                        HoverHandler { id: treeHover }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                browser.loadFolder(modelData.path);
                                if (!modelData.expanded)
                                    browser.expandTree(modelData.path);
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: browser.treeRows.length === 0
                    text: browser.open ? qsTr("No folders") : qsTr("No image")
                    color: Theme.textDim
                    font.pixelSize: 10
                }
            }

            // ---- the listing ----
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: Theme.cardLow
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 2

                    // header
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 24
                        radius: 6
                        color: Qt.rgba(1, 1, 1, 0.03)

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Name") + (browser.sortColumn === 0
                                                      ? (browser.sortAscending ? " ▲" : " ▼") : "")
                                color: Theme.text
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (browser.sortColumn === 0)
                                            browser.sortAscending = !browser.sortAscending;
                                        else {
                                            browser.sortColumn = 0;
                                            browser.sortAscending = true;
                                        }
                                        browser.sortEntries();
                                    }
                                }
                            }
                            Text {
                                Layout.preferredWidth: 84
                                horizontalAlignment: Text.AlignRight
                                text: qsTr("Size") + (browser.sortColumn === 1
                                                      ? (browser.sortAscending ? " ▲" : " ▼") : "")
                                color: Theme.text
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (browser.sortColumn === 1)
                                            browser.sortAscending = !browser.sortAscending;
                                        else {
                                            browser.sortColumn = 1;
                                            browser.sortAscending = true;
                                        }
                                        browser.sortEntries();
                                    }
                                }
                            }
                            Item { Layout.preferredWidth: 350 }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ListView {
                            id: fileView
                            anchors.fill: parent
                            clip: true
                            spacing: 1
                            model: browser.searching ? browser.searchRows : browser.viewRows
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: Rectangle {
                                required property var modelData
                                width: fileView.width - 12
                                height: 28
                                radius: 6
                                color: fileHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 6
                                    spacing: 8
                                    Text {
                                        text: modelData.dir ? "📁" : modelData.symlink ? "🔗" : "📄"
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                        text: browser.searching ? modelData.path : modelData.name
                                        color: Theme.text
                                        font.pixelSize: 11
                                    }
                                    Text {
                                        Layout.preferredWidth: 84
                                        horizontalAlignment: Text.AlignRight
                                        text: modelData.dir ? qsTr("folder")
                                                            : browser.sizeText(modelData.size)
                                        color: Theme.textDim
                                        font.pixelSize: 10
                                    }
                                    RowLayout {
                                        Layout.preferredWidth: 350
                                        spacing: 4
                                        RowButton {
                                            label: modelData.dir ? qsTr("Open") : qsTr("Show")
                                            enabled: !browser.busy
                                            onClicked: browser.openEntry(modelData)
                                        }
                                        RowButton {
                                            label: qsTr("Rename")
                                            enabled: browser.writable && !browser.busy
                                            onClicked: {
                                                browser.pendingEntry = modelData;
                                                browser.renameText = modelData.name;
                                                renamePopup.open();
                                            }
                                        }
                                        RowButton {
                                            label: qsTr("Move")
                                            enabled: browser.writable && !browser.busy
                                            onClicked: {
                                                browser.pendingEntry = modelData;
                                                browser.destinationText = browser.currentDir;
                                                movePopup.open();
                                            }
                                        }
                                        RowButton {
                                            visible: !modelData.symlink
                                            label: qsTr("Copy")
                                            enabled: browser.writable && !browser.busy
                                            onClicked: {
                                                browser.pendingEntry = modelData;
                                                browser.destinationText = browser.currentDir;
                                                browser.copyNameText = modelData.name;
                                                copyPopup.open();
                                            }
                                        }
                                        RowButton {
                                            visible: !modelData.dir && !modelData.symlink
                                            label: qsTr("Replace")
                                            enabled: browser.writable && !browser.busy
                                            onClicked: {
                                                browser.pendingEntry = modelData;
                                                replaceFileDialog.open();
                                            }
                                        }
                                        RowButton {
                                            label: modelData.symlink ? qsTr("Save target") : qsTr("Save")
                                            enabled: !browser.busy
                                            onClicked: {
                                                browser.pendingEntry = modelData;
                                                if (modelData.dir)
                                                    extractFolderDialog.open();
                                                else
                                                    extractFileDialog.open();
                                            }
                                        }
                                        RowButton {
                                            label: qsTr("Delete")
                                            enabled: browser.writable && !browser.busy
                                            onClicked: {
                                                if (modelData.dir) {
                                                    browser.pendingEntry = modelData;
                                                    deletePopup.open();
                                                } else {
                                                    browser.removeEntry(modelData);
                                                }
                                            }
                                        }
                                    }
                                }
                                HoverHandler { id: fileHover }
                                MouseArea {
                                    // Behind the row's own buttons.
                                    anchors.fill: parent
                                    z: -1
                                    acceptedButtons: Qt.LeftButton
                                    onDoubleClicked: browser.openEntry(modelData)
                                }
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            visible: (browser.searching ? browser.searchRows.length
                                                        : browser.viewRows.length) === 0
                            text: {
                                if (!browser.open)
                                    return qsTr("Point the tool at an engine's Root.vhd to browse it.");
                                if (browser.searching)
                                    return qsTr("Nothing in the image matches “%1”.").arg(browser.searchText);
                                if (browser.filterText.length > 0)
                                    return qsTr("Nothing in this folder matches “%1”.").arg(browser.filterText);
                                return qsTr("This folder is empty. Add a file or a folder, or drop one here.");
                            }
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }
                }

                // Files and folders dragged from Explorer land in this folder.
                DropArea {
                    anchors.fill: parent
                    enabled: browser.writable && browser.open
                    onDropped: (drop) => browser.dropUrls(drop.urls)
                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        visible: parent.containsDrag
                        color: Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.12)
                        border.width: 2
                        border.color: Qt.rgba(6 / 255, 182 / 255, 212 / 255, 0.6)
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Drop to add to %1").arg(browser.currentDir)
                            color: Theme.accentText
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }
        }

        // ---- status ----
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: browser.status.length > 0 || (browser.open && browser.readOnlyReason.length > 0)
            text: browser.status.length > 0
                  ? browser.status
                  : qsTr("Read-only: %1").arg(browser.readOnlyReason)
            color: browser.status.length > 0 ? (browser.ok ? Theme.textDim : Theme.danger)
                                             : Theme.warn
            font.pixelSize: 10
        }
    }

    // ---- copy a file or folder within the image ----
    Popup {
        id: copyPopup
        modal: true
        anchors.centerIn: parent
        width: 400
        padding: 16
        background: Rectangle {
            color: Theme.card
            radius: 12
            border.width: 1
            border.color: Theme.border
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: qsTr("Copy within Root.vhd")
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Choose a destination folder and name. File permission bits and folder structure are copied.")
                color: Theme.textDim
                font.pixelSize: 11
            }
            TextField {
                id: copyNameField
                Layout.fillWidth: true
                text: browser.copyNameText
                placeholderText: qsTr("Copy name")
                selectByMouse: true
                onTextChanged: browser.copyNameText = text
            }
            TextField {
                id: copyDestinationField
                Layout.fillWidth: true
                text: browser.destinationText
                placeholderText: "/system/app"
                selectByMouse: true
                onTextChanged: browser.destinationText = text
                onAccepted: {
                    const entry = browser.pendingEntry;
                    copyPopup.close();
                    if (entry) browser.copyEntry(entry, browser.destinationText, browser.copyNameText);
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancel"); onClicked: copyPopup.close() }
                ActionButton {
                    accent: true
                    text: qsTr("Copy")
                    enabled: browser.destinationText.trim().length > 0
                             && browser.copyNameText.trim().length > 0
                    onClicked: {
                        const entry = browser.pendingEntry;
                        copyPopup.close();
                        if (entry) browser.copyEntry(entry, browser.destinationText, browser.copyNameText);
                    }
                }
            }
        }
        onOpened: {
            copyNameField.forceActiveFocus();
            copyNameField.selectAll();
        }
    }

    // ---- move a file or folder within the image ----
    Popup {
        id: movePopup
        modal: true
        anchors.centerIn: parent
        width: 400
        padding: 16
        background: Rectangle {
            color: Theme.card
            radius: 12
            border.width: 1
            border.color: Theme.border
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: qsTr("Move within Root.vhd")
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Enter the destination folder's image path. The item keeps its name and metadata.")
                color: Theme.textDim
                font.pixelSize: 11
            }
            TextField {
                id: moveDestinationField
                Layout.fillWidth: true
                text: browser.destinationText
                placeholderText: "/system/app"
                selectByMouse: true
                onTextChanged: browser.destinationText = text
                onAccepted: {
                    const entry = browser.pendingEntry;
                    movePopup.close();
                    if (entry) browser.moveEntry(entry, browser.destinationText);
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancel"); onClicked: movePopup.close() }
                ActionButton {
                    accent: true
                    text: qsTr("Move")
                    enabled: browser.destinationText.trim().length > 0
                    onClicked: {
                        const entry = browser.pendingEntry;
                        movePopup.close();
                        if (entry) browser.moveEntry(entry, browser.destinationText);
                    }
                }
            }
        }
        onOpened: moveDestinationField.forceActiveFocus()
    }

    // ---- rename a file or folder ----
    Popup {
        id: renamePopup
        modal: true
        anchors.centerIn: parent
        width: 360
        padding: 16
        background: Rectangle {
            color: Theme.card
            radius: 12
            border.width: 1
            border.color: Theme.border
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: qsTr("Rename item")
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            TextField {
                id: renameNameField
                Layout.fillWidth: true
                text: browser.renameText
                selectByMouse: true
                onTextChanged: browser.renameText = text
                onAccepted: {
                    const entry = browser.pendingEntry;
                    renamePopup.close();
                    if (entry) browser.renameEntry(entry, browser.renameText);
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancel"); onClicked: renamePopup.close() }
                ActionButton {
                    accent: true
                    text: qsTr("Rename")
                    enabled: browser.renameText.trim().length > 0
                    onClicked: {
                        const entry = browser.pendingEntry;
                        renamePopup.close();
                        if (entry) browser.renameEntry(entry, browser.renameText);
                    }
                }
            }
        }
        onOpened: {
            renameNameField.forceActiveFocus();
            renameNameField.selectAll();
        }
    }

    // ---- deleting a folder ----
    Popup {
        id: deletePopup
        modal: true
        anchors.centerIn: parent
        width: 380
        padding: 16
        background: Rectangle {
            color: Theme.card
            radius: 12
            border.width: 1
            border.color: Theme.border
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: qsTr("Delete this folder?")
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("“%1” and everything inside it is removed from the image. "
                           + "Undo puts the image back.")
                      .arg(browser.pendingEntry ? browser.pendingEntry.path : "")
                color: Theme.textDim
                font.pixelSize: 11
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancel"); onClicked: deletePopup.close() }
                ActionButton {
                    accent: true
                    text: qsTr("Delete")
                    onClicked: {
                        const entry = browser.pendingEntry;
                        deletePopup.close();
                        if (entry)
                            browser.removeEntry(entry);
                    }
                }
            }
        }
    }

    // ---- new folder name ----
    Popup {
        id: newFolderPopup
        modal: true
        anchors.centerIn: parent
        width: 320
        padding: 16
        background: Rectangle {
            color: Theme.card
            radius: 12
            border.width: 1
            border.color: Theme.border
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Text {
                text: qsTr("New folder in %1").arg(browser.currentDir)
                color: Theme.text
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            TextField {
                id: folderName
                Layout.fillWidth: true
                placeholderText: qsTr("folder name")
                color: Theme.text
                onAccepted: createFolder()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Cancel"); onClicked: newFolderPopup.close() }
                ActionButton {
                    accent: true
                    text: qsTr("Create")
                    enabled: folderName.text.trim().length > 0
                    onClicked: createFolder()
                }
            }
            function createFolder() {
                const name = folderName.text.trim();
                if (name.length === 0) return;
                browser.makeFolder(name);
                folderName.text = "";
                newFolderPopup.close();
            }
        }
        onOpened: folderName.forceActiveFocus()
    }
}
