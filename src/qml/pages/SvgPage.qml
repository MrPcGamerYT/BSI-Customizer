import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

Item {
    id: page

    property int selectedIndex: -1
    property string svgText: ""
    property bool showRaw: false

    // indexes (into backend.svgInfos) of the two switch SVGs, -1 = not located yet
    property int offIndex: -1
    property int onIndex: -1
    property string toggleStatus: qsTr("Press “Guess From EXE” to locate the switch SVGs.")

    readonly property bool togglesReady: offIndex >= 0 && onIndex >= 0

    function refreshPreview() {
        page.svgText = page.selectedIndex >= 0 ? backend.svgTextAt(page.selectedIndex) : "";
    }

    function guessToggles() {
        const guess = backend.guessToggleSvgs();
        page.offIndex = guess.off;
        page.onIndex = guess.on;
        if (guess.offColor !== "")
            toggleOffField.current = guess.offColor;
        if (guess.onColor !== "")
            toggleOnField.current = guess.onColor;
        page.toggleStatus = page.togglesReady
                ? qsTr("Found: OFF #%1 (%2), ON #%3 (%4). Choose colors, then recolor.")
                      .arg(guess.off).arg(guess.offColor || "?")
                      .arg(guess.on).arg(guess.onColor || "?")
                : qsTr("No switch SVGs recognised. Press “Scan SVGs” and try again.");
    }

    function useAccent() {
        const accent = Theme.hexFromColor(Theme.accent);
        toggleOnField.value = accent;
        if (toggleOffField.value === "")
            toggleOffField.value = "#111111";
        page.toggleStatus = qsTr("ON color set to this app's accent (%1).").arg(accent);
    }

    // "#RGB" and "#RRGGBBAA" both become the "#RRGGBB" the patcher wants.
    function sixDigit(c) {
        if (/^#[0-9A-Fa-f]{6}$/.test(c))
            return c.toUpperCase();
        if (/^#[0-9A-Fa-f]{3}$/.test(c))
            return ("#" + c[1] + c[1] + c[2] + c[2] + c[3] + c[3]).toUpperCase();
        if (/^#[0-9A-Fa-f]{8}$/.test(c))
            return c.substring(0, 7).toUpperCase();
        return "";
    }

    function recolorToggles() {
        const off = page.sixDigit(toggleOffField.value !== "" ? toggleOffField.value
                                                              : toggleOffField.current);
        const on = page.sixDigit(toggleOnField.value !== "" ? toggleOnField.value
                                                            : toggleOnField.current);
        if (off === "" || on === "") {
            page.toggleStatus = qsTr("Both colors must be hex, e.g. #8B5CF6.");
            return;
        }
        const r = backend.recolorToggleSvgs(page.offIndex, page.onIndex, off, on);
        page.toggleStatus = r.msg || "";
        if (r.ok) {
            toggleOffField.value = "";
            toggleOnField.value = "";
            page.guessToggles();      // re-scan: shows what is in the EXE now
            page.toggleStatus = r.msg || "";
        }
    }

    // a fresh scan clears the blobs, so any guess made earlier is stale
    Connections {
        target: backend
        function onSvgsChanged() {
            if (!page.togglesReady)
                return;
            page.offIndex = -1;
            page.onIndex = -1;
            page.toggleStatus = qsTr("SVG list changed - press “Guess From EXE” again.");
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("SVG Extract / Replace")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.Bold
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Extract All")
                onClicked: backend.extractSvgs()
            }
            ActionButton {
                accent: true
                text: qsTr("Scan SVGs")
                onClicked: backend.scanSvgs()
            }
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("HD-Player.exe")
            text: backend.exePath
            onLoad: backend.exePath = text
        }

        // ------------------------------------------------ recolor toggles
        Card {
            Layout.fillWidth: true
            Layout.preferredHeight: 108
            pad: 12

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("Toggle Switch Colors")
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: qsTr("Guess From EXE")
                        implicitHeight: 28
                        onClicked: page.guessToggles()
                    }
                    ActionButton {
                        text: qsTr("Use Theme Accent")
                        implicitHeight: 28
                        enabled: backend.themeFound
                        onClicked: page.useAccent()
                    }
                    ActionButton {
                        accent: true
                        text: qsTr("Recolor Toggles")
                        implicitHeight: 28
                        enabled: page.togglesReady
                        onClicked: page.recolorToggles()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: qsTr("Switch OFF")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    ColorField {
                        id: toggleOffField
                        current: "#111111"
                    }
                    Text {
                        Layout.leftMargin: 6
                        text: qsTr("Switch ON")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    ColorField {
                        id: toggleOnField
                        current: Theme.hexFromColor(Theme.accent)
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 6
                        text: page.toggleStatus
                        color: Theme.textDim
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            ListView {
                id: svgList
                Layout.preferredWidth: 270
                Layout.fillHeight: true
                clip: true
                model: backend.svgInfos
                spacing: 4
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: svgRow
                    required property var modelData
                    required property int index
                    // hex colors inside this blob, document order, no duplicates
                    readonly property var cols: modelData.colors
                    width: svgList.width
                    height: 52
                    radius: 8
                    color: page.selectedIndex === index ? Theme.accentSoft : Theme.card
                    border.width: 1
                    border.color: page.selectedIndex === index ? Theme.accent : Theme.border

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 10
                        spacing: 8

                        // live thumbnail of the icon itself
                        Rectangle {
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            Layout.alignment: Qt.AlignVCenter
                            radius: 6
                            color: "#80808080"     // neutral behind light AND dark icons
                            border.width: 1
                            border.color: Theme.border

                            Image {
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectFit
                                smooth: true
                                asynchronous: true
                                sourceSize: Qt.size(64, 64)
                                source: svgRow.modelData.svg !== undefined
                                        && svgRow.modelData.svg !== ""
                                        ? "data:image/svg+xml;utf8,"
                                          + encodeURIComponent(svgRow.modelData.svg)
                                        : ""
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 3

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: "SVG #" + String(svgRow.index).padStart(3, "0")
                                    color: Theme.text
                                    font.pixelSize: 12
                                    font.family: "Consolas"
                                }
                                Repeater {
                                    model: Math.min(svgRow.cols.length, 3)
                                    delegate: Rectangle {
                                        required property int index
                                        Layout.preferredWidth: 9
                                        Layout.preferredHeight: 9
                                        radius: 4
                                        color: svgRow.cols[index]
                                        border.width: 1
                                        border.color: Theme.border
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    visible: svgRow.modelData.hint !== ""
                                    text: svgRow.modelData.hint
                                    color: Theme.accentText
                                    font.pixelSize: 9
                                    font.weight: Font.DemiBold
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: svgRow.modelData.size + " bytes"
                                      + (svgRow.cols.length > 0
                                         ? "  ·  " + svgRow.cols.length + " colors" : "")
                                color: Theme.textDim
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        id: svgRowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            page.selectedIndex = index;
                            page.refreshPreview();
                        }
                    }

                    ToolTip.text: modelData.preview
                    ToolTip.visible: svgRowArea.containsMouse && modelData.preview !== ""
                    ToolTip.delay: 500
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
                            text: page.selectedIndex >= 0
                                  ? qsTr("Preview — SVG #%1").arg(page.selectedIndex)
                                  : qsTr("Select an SVG on the left")
                            color: Theme.textDim
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        ActionButton {
                            text: page.showRaw ? qsTr("Preview") : qsTr("Raw XML")
                            implicitHeight: 28
                            onClicked: page.showRaw = !page.showRaw
                        }
                        ActionButton {
                            text: qsTr("Replace…")
                            implicitHeight: 28
                            enabled: page.selectedIndex >= 0
                            onClicked: svgDialog.open()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: 8
                        color: "#80808080" // checkerboard-ish neutral
                        border.color: Theme.border

                        Image {
                            id: svgImage
                            visible: !page.showRaw && page.svgText !== ""
                            anchors.fill: parent
                            anchors.margins: 14
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            source: page.svgText !== ""
                                    ? "data:image/svg+xml;utf8," + encodeURIComponent(page.svgText)
                                    : ""
                            sourceSize: Qt.size(1024, 1024)
                        }

                        Flickable {
                            visible: page.showRaw && page.svgText !== ""
                            anchors.fill: parent
                            anchors.margins: 8
                            contentHeight: rawText.implicitHeight
                            clip: true
                            TextEdit {
                                id: rawText
                                width: parent.width
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                text: page.svgText
                                color: Theme.text
                                font.family: "Consolas"
                                font.pixelSize: 11
                            }
                        }

                        Text {
                            visible: page.svgText === ""
                            anchors.centerIn: parent
                            width: parent.width - 40
                            horizontalAlignment: Text.AlignHCenter
                            text: qsTr("Nothing to preview yet.\nPress “Scan SVGs” to list the icons inside HD-Player.exe.")
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }

    FileDialog {
        id: svgDialog
        title: qsTr("Choose replacement SVG")
        nameFilters: ["SVG files (*.svg)", "All files (*)"]
        onAccepted: backend.replaceSvg(page.selectedIndex,
                                       selectedFile.toString().replace(/^file:\/{2,3}/, ""))
    }
}
