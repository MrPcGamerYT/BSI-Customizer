import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer
import "../ResourceSearch.js" as ResourceSearch

// Every file embedded in HD-Player.exe's Qt resource pools, in one list.
//
//   * pictures (PNG/JPEG/BMP/GIF/ICO/WEBP/TGA) - preview + replace with another
//     picture of the same kind
//   * SVG and QML/JS/JSON/XML/CSS/HTML/text        - shown as code, edited here
//   * everything else (compiled caches, fonts)     - listed, extracted and
//                                                    replaceable with a file
//
// SIZE RULES - both are hard, and the engine checks them again on save:
//   1. the embedded file itself never grows: a replacement or edit that needs
//      more bytes than the entry already has is refused outright;
//   2. HD-Player.exe never grows either: the compressed payload has to fit the
//      space the entry already owns, or a verified empty gap inside the same
//      file, which the writer re-points the resource at. Nothing is appended.
// The meter below shows both numbers before the click.
Item {
    id: page

    property int selectedIndex: -1
    property string selectedPath: ""
    property string preview: ""
    property string previewNote: ""    // why there is no picture, when there is none
    property string status: qsTr("Click Scan Resources to list the selected EXE.")
    property string filterText: ""
    property int kindFilter: 0          // 0 all, 1 pictures, 2 svg, 3 code, 4 binary
    property string code: ""            // the editor's text
    property bool codeLoaded: false
    property bool codeDirty: false
    property var room: ({ known: false, capacity: 0, used: 0, bom: false, msg: "" })
    property int needed: 0              // payload bytes the editor's text would take
    property bool measuring: false

    readonly property var kinds: [qsTr("All"), qsTr("Pictures"), qsTr("SVG"), qsTr("Code"), qsTr("Binary")]

    // One stable snapshot per scan/path change; repeatedly reading the C++
    // QVariantList property used to rebuild every map on each filter/selection.
    readonly property var allRows: backend.resourceInfos
    readonly property var searchIndex: ResourceSearch.prepare(page.allRows)
    readonly property var rows: ResourceSearch.filter(page.searchIndex, page.filterText, page.kindFilter)

    readonly property var selected: {
        if (page.selectedIndex < 0 || page.selectedIndex >= page.allRows.length)
            return null;
        return page.allRows[page.selectedIndex];
    }
    readonly property bool hasSelection: page.selected !== null
    // A row is a picture when the list says so *or* when the bytes produce a
    // preview: older players carry entries whose name does not tell what they
    // hold, and those must still show the picture.
    readonly property bool isPicture: page.hasSelection
                                      && (page.selected.kind === "image"
                                          || page.selected.kind === "svg"
                                          || page.preview !== "")
    readonly property bool isCode: page.hasSelection && page.selected.text

    // Bytes the editor's text would take inside the EXE, BOM included: rule 1.
    readonly property int fileBytes: page.isCode
                                     ? backend.resourceTextSize(page.code, page.room.bom) : 0
    readonly property bool fileGrows: page.isCode && page.selected.size > 0
                                      && page.fileBytes > page.selected.size
    // Rule 2 is not a blocker by itself: when the payload outgrows its slot the
    // engine looks for free space inside the same file and only refuses when
    // there is none. The button stays available so a fit that needs moving is
    // still savable.
    readonly property bool fits: page.hasSelection && page.room.known && !page.fileGrows
    readonly property bool needsMove: page.room.known && page.needed > page.room.capacity
    readonly property string budgetText: {
        if (!page.hasSelection)
            return "";
        if (!page.room.known)
            return page.room.msg || qsTr("Measuring the space this entry has…");
        // For a compressed entry the zlib payload is what has to fit, not the
        // size of the text on screen - say so, or the number looks wrong.
        const what = page.selected.compressed ? qsTr("compressed") : qsTr("raw");
        var text = qsTr("%1 %2 of %3 bytes").arg(what).arg(page.needed).arg(page.room.capacity);
        if (page.needsMove)
            text += qsTr(" · moved to free space");
        else
            text += qsTr(" · %1 free").arg(page.room.capacity - page.needed);
        if (page.isCode)
            text += qsTr("  |  file %1 of %2 bytes").arg(page.fileBytes).arg(page.selected.size);
        return text;
    }

    function humanSize(bytes) {
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(1) + " MB";
        if (bytes >= 1024)
            return (bytes / 1024).toFixed(1) + " KB";
        return String(bytes) + " B";
    }

    // Loads everything the right-hand panel needs for one row.
    function select(index) {
        codeSaveTimer.stop();
        page.codeDirty = false;
        page.codeLoaded = false;
        page.code = "";
        page.preview = "";
        page.previewNote = "";
        page.selectedIndex = index;
        page.selectedPath = index >= 0 && index < page.allRows.length
                ? page.allRows[index].path : "";
        if (index < 0 || index >= page.allRows.length)
            return;
        const row = page.allRows[index];
        // Asked for every row, not only the ones the list typed as pictures:
        // the engine decides from the bytes.
        page.preview = backend.resourcePreviewAt(index, 1024);
        page.previewNote = page.preview === "" ? backend.resourcePreviewNote(index) : "";
        if (row.text) {
            page.code = backend.resourceTextAt(index);
            page.codeLoaded = true;
        }
        page.measuring = true;
        const budget = backend.resourceBudgetAt(index);
        page.room = budget && budget.known !== undefined
                ? ({ known: !!budget.known, capacity: Number(budget.capacity) || 0,
                     used: Number(budget.used) || 0, bom: !!budget.bom,
                     msg: String(budget.msg || "") })
                : ({ known: false, capacity: 0, used: 0, bom: false, msg: "" });
        page.measuring = false;
        page.recomputeNeeded();
    }

    function recomputeNeeded() {
        if (!page.hasSelection || !page.isCode) {
            page.needed = page.hasSelection ? page.selected.stored : 0;
            return;
        }
        page.needed = backend.resourcePayloadSize(page.selectedIndex, page.code, page.room.bom);
    }

    function rescan() {
        page.status = "";
        const count = backend.scanResources(true);
        page.status = backend.exePath === ""
                ? qsTr("Select HD-Player.exe first.")
                : qsTr("%1 embedded file(s) listed.").arg(count);
        // resourcesChanged restores the selected path once, after the scan.
    }

    function saveCode() {
        const result = backend.saveResourceText(page.selectedIndex, page.code);
        page.status = result && result.msg ? result.msg : "";
        if (result && result.ok) {
            page.codeDirty = false;
            page.select(page.selectedIndex);
        }
    }

    // Scanning is manual. Opening this tab, starting the app or changing the
    // target only clears stale selection; Scan Resources is the only scan trigger.
    Connections {
        target: backend
        function onExePathChanged() {
            page.select(-1);
            page.status = qsTr("Click Scan Resources to list the selected EXE.");
        }
        function onResourcesChanged() {
            const wanted = page.selectedPath;
            if (wanted !== "") {
                for (let i = 0; i < page.allRows.length; ++i) {
                    if (page.allRows[i].path === wanted) {
                        page.select(i);
                        return;
                    }
                }
            }
            page.select(-1);
        }
    }

    // Avoid destroying/recreating delegates for every key of a fast keystroke burst.
    Timer {
        id: searchTimer
        interval: 150
        onTriggered: page.filterText = filterField.text
    }

    // The size meter is debounced: every keystroke would otherwise compress the
    // whole file again.
    Timer {
        id: codeSaveTimer
        interval: 250
        onTriggered: page.recomputeNeeded()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Embedded Resources")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.Bold
            }
            Text {
                Layout.leftMargin: 4
                text: page.allRows.length > 0
                      ? qsTr("%1 / %2 file(s)").arg(page.rows.length).arg(page.allRows.length) : ""
                color: Theme.textDim
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Extract All")
                enabled: page.allRows.length > 0
                onClicked: {
                    const r = backend.extractAllResources();
                    page.status = r.msg || "";
                }
            }
            ActionButton {
                accent: true
                text: qsTr("Scan Resources")
                enabled: !backend.busy && backend.exePath !== ""
                onClicked: page.rescan()
            }
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("HD-Player.exe")
            text: backend.exePath
            onLoad: backend.exePath = text
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            TextField {
                id: filterField
                objectName: "resourceSearchField"
                Layout.preferredWidth: 280
                placeholderText: qsTr("Search path, name or format…")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                font.pixelSize: 12
                onTextChanged: searchTimer.restart()
                onAccepted: { searchTimer.stop(); page.filterText = text; }
                background: Rectangle {
                    color: Theme.raised
                    radius: 8
                    border.width: 1
                    border.color: filterField.activeFocus ? Theme.accent : Theme.border
                }
            }

            ChoiceChips {
                model: page.kinds
                currentIndex: page.kindFilter
                onSelected: (index) => page.kindFilter = index
            }

            Item { Layout.fillWidth: true }

            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 420
                horizontalAlignment: Text.AlignRight
                text: page.status
                color: Theme.textDim
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            ListView {
                id: resourceList
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                clip: true
                model: page.rows
                reuseItems: true
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                Label {
                    anchors.centerIn: parent
                    width: parent.width - 24
                    visible: page.rows.length === 0
                    text: page.allRows.length === 0
                          ? qsTr("Click Scan Resources to load this EXE.")
                          : qsTr("No resources match this search and kind filter.")
                    color: Theme.textDim
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                delegate: Rectangle {
                    id: resourceRow
                    required property var modelData
                    // Prepared by the scan worker; never read/decode files here.
                    readonly property string thumb: modelData.thumbnail || ""
                    width: resourceList.width
                    height: 54
                    radius: 8
                    color: page.selectedIndex === modelData.index ? Theme.accentSoft : Theme.card
                    border.width: 1
                    border.color: page.selectedIndex === modelData.index ? Theme.accent : Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 10
                        spacing: 8

                        Rectangle {
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            Layout.alignment: Qt.AlignVCenter
                            radius: 6
                            color: "#80808080"   // neutral behind light AND dark art
                            border.width: 1
                            border.color: Theme.border

                            Image {
                                anchors.fill: parent
                                anchors.margins: 3
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                asynchronous: true
                                sourceSize: Qt.size(72, 72)
                                source: resourceRow.thumb
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: resourceRow.thumb === ""
                                text: String(resourceRow.modelData.format).substring(0, 4)
                                color: Theme.textDim
                                font.pixelSize: 8
                                font.weight: Font.DemiBold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                text: resourceRow.modelData.name
                                color: Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: (resourceRow.modelData.folder !== ""
                                       ? resourceRow.modelData.folder + "  ·  " : "")
                                      + resourceRow.modelData.format
                                      + (resourceRow.modelData.width > 0
                                         ? "  " + resourceRow.modelData.width
                                           + "×" + resourceRow.modelData.height : "")
                                color: Theme.textDim
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: page.humanSize(resourceRow.modelData.size)
                                      + (resourceRow.modelData.text ? qsTr("  ·  editable") : "")
                                color: Theme.textDim
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.select(resourceRow.modelData.index)
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                pad: 10

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    // ---- title + actions -----------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: page.hasSelection ? page.selected.path
                                                    : qsTr("Select a file on the left")
                            color: page.hasSelection ? Theme.text : Theme.textDim
                            font.pixelSize: 12
                            font.family: "Consolas"
                            elide: Text.ElideMiddle
                        }
                        ActionButton {
                            text: qsTr("Extract…")
                            implicitHeight: 28
                            enabled: page.hasSelection
                            onClicked: extractDialog.open()
                        }
                        ActionButton {
                            accent: true
                            text: qsTr("Replace with file…")
                            implicitHeight: 28
                            enabled: page.hasSelection
                            onClicked: replaceDialog.open()
                        }
                        ActionButton {
                            accent: true
                            text: qsTr("Save code")
                            implicitHeight: 28
                            enabled: page.isCode && page.codeDirty && page.fits
                            onClicked: page.saveCode()

                            ToolTip.visible: hovered && page.isCode && !page.fits
                            ToolTip.text: page.fileGrows
                                          ? qsTr("This edit makes the embedded file bigger (%1 bytes, "
                                                 + "it has %2). An embedded file is never made bigger, "
                                                 + "so nothing is saved.")
                                                .arg(page.fileBytes).arg(page.selected.size)
                                          : qsTr("The size of this entry's slot could not be measured, "
                                                 + "so saving is disabled.")
                        }
                    }

                    // ---- picture preview (SVG included) --------------------
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: page.isPicture ? Math.min(220, page.height * 0.28) : 0
                        visible: page.isPicture
                        radius: 8
                        color: "#80808080"
                        border.color: Theme.border

                        Image {
                            anchors.fill: parent
                            anchors.margins: 10
                            visible: page.preview !== ""
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: true
                            source: page.preview
                            sourceSize: Qt.size(1024, 1024)
                        }

                        Text {
                            visible: page.preview === ""
                            anchors.centerIn: parent
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: page.previewNote !== ""
                                  ? page.previewNote
                                  : qsTr("This picture could not be decoded here, but it can "
                                         + "still be replaced.")
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                    }

                    // ---- code editor ---------------------------------------
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: page.isCode
                        radius: 8
                        color: Theme.raised
                        border.width: 1
                        border.color: page.codeDirty ? (page.fits ? Theme.accent : Theme.danger)
                                                     : Theme.border

                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 4
                            clip: true

                            TextArea {
                                id: codeArea
                                text: page.code
                                wrapMode: TextArea.NoWrap
                                selectByMouse: true
                                color: Theme.text
                                font.family: "Consolas"
                                font.pixelSize: 12
                                padding: 8
                                background: Rectangle { color: "transparent" }
                                onTextChanged: {
                                    if (!page.codeLoaded || text === page.code)
                                        return;
                                    page.code = text;
                                    page.codeDirty = true;
                                    codeSaveTimer.restart();
                                }
                            }
                        }
                    }

                    // ---- binary entry --------------------------------------
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: page.hasSelection && !page.isCode && !page.isPicture
                        wrapMode: Text.Wrap
                        verticalAlignment: Text.AlignTop
                        text: page.hasSelection
                              ? qsTr("This is a compiled/opaque resource (%1, %2 bytes). It cannot be "
                                     + "shown as code, but it can be extracted and replaced with a file "
                                     + "of the same kind.")
                                    .arg(page.selected.format).arg(page.selected.size)
                              : ""
                        color: Theme.textDim
                        font.pixelSize: 12
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !page.hasSelection
                        wrapMode: Text.Wrap
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("Press “Scan Resources” to list every file embedded in "
                                   + "HD-Player.exe — QML, SVG and pictures included.")
                        color: Theme.textDim
                        font.pixelSize: 12
                    }

                    // ---- size meter ----------------------------------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        visible: page.hasSelection

                        Text {
                            text: !page.hasSelection ? ""
                                  : page.selected.width > 0
                                    ? qsTr("%1  ·  %2×%3 px  ·  %4")
                                          .arg(page.selected.format)
                                          .arg(page.selected.width)
                                          .arg(page.selected.height)
                                          .arg(page.humanSize(page.selected.size))
                                    : qsTr("%1  ·  %2").arg(page.selected.format)
                                          .arg(page.humanSize(page.selected.size))
                            color: Theme.textDim
                            font.pixelSize: 11
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: page.budgetText
                            color: page.fileGrows ? Theme.danger
                                   : (page.room.known ? (page.needsMove ? Theme.accent
                                                                        : Theme.green)
                                                      : Theme.textDim)
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Two hard limits: the embedded file is never made bigger, and "
                                   + "HD-Player.exe never changes size. A payload that outgrows its slot "
                                   + "is moved into empty space inside the same file and the resource is "
                                   + "re-pointed at it; if there is none, the edit is refused instead of "
                                   + "breaking the player. The original EXE is kept as a .bak before the "
                                   + "first write.")
                        color: Theme.textDim
                        font.pixelSize: 10
                        font.italic: true
                    }
                }
            }
        }
    }

    FileDialog {
        id: replaceDialog
        title: page.isPicture ? qsTr("Choose the replacement picture")
                              : qsTr("Choose the replacement file")
        nameFilters: page.isPicture && page.hasSelection
                     ? [String(page.selected.format).toUpperCase() + " (*."
                        + String(page.selected.format).toLowerCase() + ")", "All files (*)"]
                     : ["All files (*)"]
        onAccepted: {
            const path = selectedFile.toString().replace(/^file:\/{2,3}/, "");
            const r = backend.replaceResourceWithFile(page.selectedIndex, path);
            page.status = r && r.msg ? r.msg : "";
            if (r && r.ok)
                page.select(page.selectedIndex);
        }
    }

    FileDialog {
        id: extractDialog
        title: qsTr("Save the embedded file as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: page.hasSelection ? page.selected.format : ""
        currentFile: page.hasSelection ? page.selected.name : ""
        nameFilters: ["All files (*)"]
        onAccepted: {
            const r = backend.extractResource(page.selectedIndex,
                                              selectedFile.toString().replace(/^file:\/{2,3}/, ""));
            page.status = r && r.msg ? r.msg : "";
        }
    }
}
