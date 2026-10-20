import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import BSICustomizer

Item {
    id: page

    signal notify(string message)

    // how many removal rows are ticked (drives the Apply button state/label)
    property int checkedCount: 0
    // player layout tweaks with their live applied/supported state
    property var layoutTweaks: []

    function recountChecked() {
        let n = 0;
        for (let i = 0; i < removalList.count; i++) {
            const it = removalList.itemAtIndex(i);
            if (it !== null && it.checked)
                n++;
        }
        page.checkedCount = n;
    }

    function refreshLayout() {
        page.layoutTweaks = backend.layoutTweaks();
    }

    Component.onCompleted: refreshLayout()

    Connections {
        target: backend
        function onExePathChanged() { page.refreshLayout(); }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // ---------------- header ----------------
        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    text: qsTr("Extras")
                    color: Theme.text
                    font.pixelSize: 24
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Ad/UI string removal, version spoofing and player layout patches for HD-Player.exe. "
                               + "A backup is created automatically before any patch.")
                    color: Theme.textDim
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                Layout.alignment: Qt.AlignTop
                text: qsTr("Open Folder")
                onClicked: settings.openExeFolder()
            }
            ActionButton {
                Layout.alignment: Qt.AlignTop
                text: qsTr("Restore Backup")
                onClicked: backend.restoreBackup(backend.exePath)
            }
        }

        PathRow {
            Layout.fillWidth: true
            label: qsTr("HD-Player.exe")
            text: backend.exePath
            onLoad: backend.exePath = text
        }

        // ---------------- player layout (QML) ----------------
        Card {
            Layout.fillWidth: true
            pad: 14

            ColumnLayout {
                anchors.fill: parent
                spacing: 8

                Text {
                    text: qsTr("Player layout (QML)")
                    color: Theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: qsTr("Structural QML patches that rearrange the player UI. Works with both "
                               + "the current player and older 5.x builds. Reversible - turn an "
                               + "option off and apply again to restore the stock layout.")
                    color: Theme.textDim
                    font.pixelSize: 11
                }

                Repeater {
                    model: page.layoutTweaks
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        required property var modelData

                        // A tick means "this is the state I want". It starts
                        // from what the EXE currently has, so an applied tweak
                        // unticks to "restore" and a stock one ticks to apply.
                        CheckBox {
                            id: tweakCheck
                            Layout.alignment: Qt.AlignTop
                            checked: !!modelData.applied
                            enabled: modelData.supported
                            padding: 0
                            implicitWidth: 18
                            implicitHeight: 18
                            indicator: Rectangle {
                                implicitWidth: 18
                                implicitHeight: 18
                                radius: 5
                                color: tweakCheck.checked ? Theme.accent : "transparent"
                                border.width: 1
                                border.color: tweakCheck.checked
                                              ? Theme.accent
                                              : (tweakCheck.enabled ? Theme.borderStrong
                                                                    : Theme.border)
                                Text {
                                    anchors.centerIn: parent
                                    visible: tweakCheck.checked
                                    text: "\u2713"
                                    color: Theme.primaryText
                                    font.pixelSize: 11
                                    font.weight: Font.Black
                                }
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: tweakText.implicitHeight
                            ColumnLayout {
                                id: tweakText
                                anchors.fill: parent
                                spacing: 2
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: modelData.label
                                    color: modelData.supported ? Theme.text : Theme.textDim
                                    font.pixelSize: 12
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: modelData.detail
                                    color: Theme.textDim
                                    font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: !modelData.supported ? modelData.state
                                          : (modelData.applied
                                             ? modelData.state + qsTr(" Untick to restore the stock sidebar.")
                                             : qsTr("Ready — tick and press Apply to move the buttons to the title bar."))
                                    color: !modelData.supported ? Theme.warn
                                           : (modelData.applied ? Theme.green : Theme.textDim)
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                }
                            }
                            // The whole description toggles the tick too, so the
                            // option is not a tiny 18px target.
                            MouseArea {
                                anchors.fill: parent
                                enabled: modelData.supported
                                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                onClicked: tweakCheck.checked = !tweakCheck.checked
                            }
                        }
                        ActionButton {
                            Layout.alignment: Qt.AlignTop
                            accent: true
                            implicitHeight: 28
                            enabled: modelData.supported
                                     && tweakCheck.checked !== !!modelData.applied
                            text: !modelData.supported
                                  ? qsTr("Unavailable")
                                  : (modelData.applied
                                     ? (tweakCheck.checked ? qsTr("Applied") : qsTr("Restore"))
                                     : qsTr("Apply"))
                            onClicked: {
                                const result = backend.applyLayoutTweak(modelData.id,
                                                                        tweakCheck.checked);
                                page.refreshLayout();
                                if (result && result.msg)
                                    page.notify(result.msg);
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            // ================= LEFT: ads / UI removal =================
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                pad: 14

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: qsTr("Ads / UI Removal")
                                color: Theme.text
                                font.pixelSize: 15
                                font.weight: Font.Bold
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("Nulls out the selected ad/UI strings in the binary so "
                                           + "those elements never render.")
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                        }
                        ActionButton {
                            Layout.alignment: Qt.AlignTop
                            text: qsTr("Select All")
                            implicitHeight: 28
                            onClicked: {
                                const on = page.checkedCount < removalList.count;
                                for (let i = 0; i < removalList.count; i++) {
                                    const it = removalList.itemAtIndex(i);
                                    if (it !== null)
                                        it.checked = on;
                                }
                                page.recountChecked();
                            }
                        }
                        ActionButton {
                            Layout.alignment: Qt.AlignTop
                            accent: true
                            enabled: page.checkedCount > 0
                            implicitHeight: 28
                            text: page.checkedCount > 0
                                  ? qsTr("Apply Removals (%1)").arg(page.checkedCount)
                                  : qsTr("Apply Removals")
                            onClicked: {
                                const names = [];
                                for (let i = 0; i < removalList.count; i++) {
                                    const it = removalList.itemAtIndex(i);
                                    if (it !== null && it.checked)
                                        names.push(removalList.model.get(i).name);
                                }
                                backend.applyRemovals(names);
                            }
                        }
                    }

                    ListView {
                        id: removalList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: ListModel {}
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        Component.onCompleted: {
                            for (const n of backend.removalNames)
                                model.append({ name: n });
                        }

                        delegate: Rectangle {
                            id: rowRect
                            required property var model
                            required property int index
                            property bool checked: false
                            width: removalList.width
                            height: 34
                            radius: 8
                            color: checked ? Theme.accentSoft : Theme.raised
                            border.width: 1
                            border.color: checked ? Theme.accent : Theme.border

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8
                                Rectangle {
                                    Layout.preferredWidth: 16
                                    Layout.preferredHeight: 16
                                    radius: 4
                                    color: rowRect.checked ? Theme.accent : "transparent"
                                    border.width: 1
                                    border.color: rowRect.checked ? Theme.accent : Theme.border
                                    Text {
                                        anchors.centerIn: parent
                                        visible: rowRect.checked
                                        text: "\u2713"
                                        color: "#ffffff"
                                        font.pixelSize: 10
                                        font.weight: Font.Black
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: rowRect.model.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    rowRect.checked = !rowRect.checked;
                                    page.recountChecked();
                                }
                            }
                        }
                    }
                }
            }

            // ================= RIGHT: version spoof =================
            Card {
                Layout.fillHeight: true
                Layout.preferredWidth: 380
                Layout.minimumWidth: 340
                pad: 14

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Text {
                        text: qsTr("Version Spoof")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("Rewrite the version string inside HD-Player.exe (e.g. to fool "
                                   + "update checks). The new string must not be longer than the "
                                   + "old one.")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }

                    Text {
                        text: qsTr("1.  Find the version strings in the EXE")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ComboBox {
                            id: versionBox
                            Layout.fillWidth: true
                            model: backend.versions
                            textRole: "version"
                            popup.background: Rectangle {
                                color: Theme.card
                                border.color: Theme.border
                            }
                            delegate: ItemDelegate {
                                required property var model
                                required property int index
                                width: versionBox.width
                                contentItem: Text {
                                    text: model.version + "  @0x" + model.offset.toString(16)
                                    color: Theme.text
                                    font.family: "Consolas"
                                    font.pixelSize: 12
                                }
                                highlighted: versionBox.highlightedIndex === index
                            }
                        }
                        ActionButton {
                            text: qsTr("Find")
                            onClicked: backend.findVersions()
                        }
                    }

                    Text {
                        Layout.topMargin: 4
                        text: qsTr("2.  Enter the new version and apply")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        TextField {
                            id: newVersionField
                            Layout.fillWidth: true
                            placeholderText: qsTr("New version, e.g. 5.21.0.1000")
                            color: Theme.text
                            placeholderTextColor: Theme.textDim
                            font.pixelSize: 12
                            font.family: "Consolas"
                            background: Rectangle {
                                color: Theme.raised
                                border.color: newVersionField.activeFocus ? Theme.accent
                                                                          : Theme.border
                                radius: 8
                            }
                        }
                        ActionButton {
                            accent: true
                            text: qsTr("Apply")
                            enabled: versionBox.currentIndex >= 0
                                     && newVersionField.text.trim() !== ""
                            onClicked: backend.applyVersion(versionBox.currentText,
                                                            newVersionField.text.trim())
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: versionBox.count === 0
                        wrapMode: Text.Wrap
                        text: qsTr("No versions listed yet — press Find.")
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.italic: true
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
