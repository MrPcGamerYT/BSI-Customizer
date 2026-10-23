import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

// Every picture the player draws that is not a raw <svg> blob: PNG, JPEG, BMP,
// GIF, ICO, WEBP, TGA and SVG files that live in HD-Player.exe's Qt resource
// pools. The SVG page edits the embedded icon markup; this page replaces the
// raster art (buttons, backgrounds, logos) and previews it first.
Item {
    id: page

    property int selectedIndex: -1
    property string selectedPath: ""
    property string preview: ""
    property string status: ""
    property bool showAll: false
    property string filterText: ""

    readonly property var rows: {
        const all = backend.imageInfos;
        if (page.filterText === "")
            return all;
        const needle = page.filterText.toLowerCase();
        return all.filter(function (row) {
            return String(row.path).toLowerCase().indexOf(needle) >= 0;
        });
    }

    readonly property int pictureCount: {
        const all = backend.imageInfos;
        var n = 0;
        for (var i = 0; i < all.length; ++i)
            if (all[i].image)
                ++n;
        return n;
    }

    readonly property var selected: page.selectedIndex >= 0 && page.selectedIndex < backend.imageInfos.length
                                     ? backend.imageInfos[page.selectedIndex] : null
    readonly property bool hasSelection: page.selected !== null && page.selected !== undefined

    function humanSize(bytes) {
        if (bytes >= 1048576)
            return (bytes / 1048576).toFixed(1) + " MB";
        if (bytes >= 1024)
            return (bytes / 1024).toFixed(1) + " KB";
        return String(bytes) + " B";
    }

    function select(index) {
        page.selectedIndex = index;
        page.selectedPath = index >= 0 && index < backend.imageInfos.length
                ? backend.imageInfos[index].path : "";
        page.preview = index >= 0 ? backend.imagePreviewAt(index, 900) : "";
    }

    function rescan() {
        page.status = "";
        const count = backend.scanImages(page.showAll);
        page.status = backend.exePath === ""
                ? qsTr("Select HD-Player.exe first.")
                : qsTr("%1 embedded %2 listed.")
                      .arg(count).arg(page.showAll ? qsTr("files") : qsTr("images"));
        // Keep the row the user was looking at when it survived the rescan.
        for (var i = 0; i < backend.imageInfos.length; ++i) {
            if (page.selectedPath !== "" && backend.imageInfos[i].path === page.selectedPath) {
                page.select(i);
                return;
            }
        }
        page.select(-1);
    }

    function extensionOf(path) {
        const dot = String(path).lastIndexOf(".");
        return dot >= 0 ? String(path).substring(dot + 1).toLowerCase() : "";
    }

    Connections {
        target: backend
        function onImagesChanged() {
            if (page.selectedPath === "") {
                page.select(-1);
                return;
            }
            for (var i = 0; i < backend.imageInfos.length; ++i) {
                if (backend.imageInfos[i].path === page.selectedPath) {
                    page.select(i);
                    return;
                }
            }
            page.select(-1);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Embedded Images")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.Bold
            }
            Text {
                Layout.leftMargin: 4
                text: page.pictureCount > 0
                      ? qsTr("%1 picture(s)").arg(page.pictureCount) : ""
                color: Theme.textDim
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Extract All")
                enabled: page.pictureCount > 0
                onClicked: {
                    const r = backend.extractAllImages();
                    page.status = r.msg || "";
                }
            }
            ActionButton {
                accent: true
                text: qsTr("Scan Images")
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
            spacing: 12

            TextField {
                id: filterField
                Layout.preferredWidth: 320
                placeholderText: qsTr("Filter by name…")
                color: Theme.text
                placeholderTextColor: Theme.textDim
                font.pixelSize: 12
                onTextChanged: page.filterText = text
                background: Rectangle {
                    color: Theme.raised
                    radius: 8
                    border.width: 1
                    border.color: filterField.activeFocus ? Theme.accent : Theme.border
                }
            }

            ToggleSwitch {
                id: showAllSwitch
                Layout.preferredWidth: 300
                label: qsTr("Show every embedded file")
                description: qsTr("Lists QML, caches and the rest too — only images can be previewed or replaced.")
                checked: page.showAll
                onToggled: {
                    page.showAll = !page.showAll;
                    page.rescan();
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 460
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
            spacing: 14

            ListView {
                id: imageList
                Layout.preferredWidth: 330
                Layout.fillHeight: true
                clip: true
                model: page.rows
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: imageRow
                    required property var modelData
                    required property int index
                    // one preview request per visible row, not one per binding
                    readonly property string thumb: modelData.image
                                                      ? backend.imagePreviewAt(modelData.index, 64) : ""
                    width: imageList.width
                    height: 56
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
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                            Layout.alignment: Qt.AlignVCenter
                            radius: 6
                            color: "#80808080"     // neutral behind light AND dark art
                            border.width: 1
                            border.color: Theme.border

                            Image {
                                anchors.fill: parent
                                anchors.margins: 3
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                asynchronous: true
                                sourceSize: Qt.size(72, 72)
                                source: imageRow.thumb
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: imageRow.thumb === ""
                                text: imageRow.modelData.image
                                      ? qsTr("?") : String(imageRow.modelData.format)
                                color: Theme.textDim
                                font.pixelSize: imageRow.modelData.image ? 14 : 9
                                font.weight: Font.DemiBold
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 2

                            Text {
                                Layout.fillWidth: true
                                text: imageRow.modelData.name
                                color: Theme.text
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: imageRow.modelData.folder
                                      + (imageRow.modelData.image
                                         ? "  ·  " + imageRow.modelData.format
                                           + (imageRow.modelData.width > 0
                                              ? "  " + imageRow.modelData.width
                                                + "×" + imageRow.modelData.height : "")
                                         : "")
                                color: Theme.textDim
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: page.humanSize(imageRow.modelData.size)
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
                        onClicked: page.select(imageRow.modelData.index)
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

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: page.hasSelection
                                  ? page.selected.path
                                  : qsTr("Select an image on the left")
                            color: page.hasSelection ? Theme.text : Theme.textDim
                            font.pixelSize: 12
                            font.family: "Consolas"
                            elide: Text.ElideRight
                        }
                        ActionButton {
                            text: qsTr("Extract…")
                            implicitHeight: 28
                            enabled: page.hasSelection
                            onClicked: extractDialog.open()
                        }
                        ActionButton {
                            accent: true
                            text: qsTr("Replace…")
                            implicitHeight: 28
                            enabled: page.hasSelection && page.selected.image
                            onClicked: replaceDialog.open()

                            ToolTip.visible: hovered && page.hasSelection && !page.selected.image
                            ToolTip.text: qsTr("Only image resources can be replaced here.")
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: "#80808080"
                        border.color: Theme.border

                        Image {
                            id: bigPreview
                            visible: page.preview !== ""
                            anchors.fill: parent
                            anchors.margins: 14
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            asynchronous: true
                            source: page.preview
                            sourceSize: Qt.size(1024, 1024)
                        }

                        Text {
                            visible: page.preview === ""
                            anchors.centerIn: parent
                            width: parent.width - 60
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: page.hasSelection
                                  ? (page.selected.image
                                     ? qsTr("This image could not be decoded for a preview, but it can still be replaced.")
                                     : qsTr("This is not an image resource — %1 bytes.").arg(page.selected.size))
                                  : qsTr("Nothing to preview yet.\nPress “Scan Images” to list the pictures "
                                         + "embedded in HD-Player.exe.")
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Text {
                            text: !page.hasSelection
                                  ? ""
                                  : page.selected.image
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
                            text: qsTr("The original EXE is kept as a .bak before the first write.")
                            color: Theme.textDim
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: replaceDialog
        title: qsTr("Choose the replacement image")
        nameFilters: page.hasSelection
                     ? [String(page.selected.format) + " image (*."
                        + (page.selected.format === "JPG" ? "jpg" : page.selected.format.toLowerCase()) + ")"
                        , "All files (*)"]
                     : ["All files (*)"]
        onAccepted: {
            const r = backend.replaceImage(page.selectedIndex,
                                           selectedFile.toString().replace(/^file:\/{2,3}/, ""));
            page.status = r.msg || "";
            if (r.ok)
                page.select(page.selectedIndex);
        }
    }

    FileDialog {
        id: extractDialog
        title: qsTr("Save the embedded image as")
        fileMode: FileDialog.SaveFile
        defaultSuffix: page.hasSelection
                       ? (page.selected.format === "JPG" ? "jpg" : page.selected.format.toLowerCase())
                       : ""
        currentFile: page.hasSelection ? page.selected.name : ""
        nameFilters: ["All files (*)"]
        onAccepted: {
            const r = backend.extractImage(page.selectedIndex,
                                           selectedFile.toString().replace(/^file:\/{2,3}/, ""));
            page.status = r.msg || "";
        }
    }
}
