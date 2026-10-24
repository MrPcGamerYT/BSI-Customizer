import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../Icons.js" as Icons

Item {
    id: page

    // Small outline YouTube play button, drawn as inline SVG so nothing has to be
    // shipped or downloaded. The stroke colour is passed in so the logo can follow
    // the theme (and the accent on hover) without a second asset.
    function ytLogo(color) {
        var stroke = String(color);
        return "data:image/svg+xml," + encodeURIComponent(
            "<svg xmlns='http://www.w3.org/2000/svg' width='34' height='24' viewBox='0 0 34 24'>"
            + "<rect x='1.1' y='1.1' width='31.8' height='21.8' rx='6.5' fill='none' stroke='" + stroke
            + "' stroke-width='2.2'/>"
            + "<path d='M14.6 7.6L21.2 12L14.6 16.4Z' fill='none' stroke='" + stroke
            + "' stroke-width='2.2' stroke-linejoin='round' stroke-linecap='round'/>"
            + "</svg>");
    }

    Flickable {
        anchors.fill: parent
        contentHeight: content.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: content
            x: 24
            y: 24
            width: parent.width - 48
            spacing: 16

            Text {
                text: qsTr("About")
                color: Theme.text
                font.pixelSize: 24
                font.weight: Font.Bold
            }

            // ---------------- app card ----------------
            Card {
                Layout.fillWidth: true
                pad: 18

                RowLayout {
                    anchors.fill: parent
                    spacing: 16

                    Rectangle {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 64
                        Layout.alignment: Qt.AlignTop
                        radius: 16
                        color: Theme.accent
                        Text {
                            anchors.centerIn: parent
                            text: "BSI"
                            color: "#ffffff"
                            font.pixelSize: 18
                            font.weight: Font.Black
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: qsTr("BSI Customizer")
                            color: Theme.text
                            font.pixelSize: 20
                            font.weight: Font.Bold
                        }
                        Text {
                            text: qsTr("Version %1").arg(settings.version)
                            color: Theme.textDim
                            font.pixelSize: 12
                            font.family: "Consolas"
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            wrapMode: Text.Wrap
                            text: qsTr("A native C++ / Qt 6 modding toolkit for BlueStacks 5 and "
                                       + "MSI App Player 5. It patches HD-Player.exe and "
                                       + "Qt5QuickTemplates2.dll / Qt6QuickTemplates2.dll directly — theme colors, SVG icons, "
                                       + "FPS cap, integrity check, boot video and more. Every "
                                       + "patch writes a .bak backup first and verifies before "
                                       + "writing.")
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                    }
                }
            }

            // ---------------- updates card ----------------
            //
            // The self-updater's home. The strip under the title bar only
            // appears when there is something to install; everything else
            // (why a check failed, which channel is used, the switch that
            // turns the startup check off) lives here.
            Card {
                Layout.fillWidth: true
                pad: 18

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Updates")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: qsTr("delivered through the Discord releases bot")
                            color: Theme.textDim
                            font.pixelSize: 10
                            font.italic: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Installed")
                            color: Theme.textDim
                            font.pixelSize: 12
                        }
                        Text {
                            text: updates.currentVersion
                            color: Theme.text
                            font.pixelSize: 12
                            font.family: "Consolas"
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: updates.available
                            text: qsTr("Newest: %1").arg(updates.latestVersion)
                            color: Theme.accentText
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: updates.note.length > 0
                        text: updates.note
                        color: updates.noteKind === "bad" ? Theme.danger
                             : updates.noteKind === "warn" ? Theme.warn
                             : updates.noteKind === "good" ? Theme.green
                             : Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        // Only when the status line above is not already this text.
                        visible: !updates.configured && updates.note !== updates.setupHint
                        text: updates.setupHint
                        color: Theme.textDim
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ActionButton {
                            text: updates.checking ? qsTr("Checking…") : qsTr("Check now")
                            enabled: updates.configured && !updates.busy
                            onClicked: updates.check()
                        }
                        ActionButton {
                            visible: updates.available && !updates.readyToInstall
                            text: qsTr("Download %1").arg(updates.latestVersion)
                            accent: true
                            enabled: !updates.busy && !backend.busy
                            onClicked: updates.download()
                        }
                        ActionButton {
                            visible: updates.skipped
                            text: qsTr("Offer %1 again").arg(updates.latestVersion)
                            onClicked: updates.unskip()
                        }
                        ActionButton {
                            visible: updates.readyToInstall
                            text: qsTr("Install and restart")
                            accent: true
                            enabled: !backend.busy
                            onClicked: updates.apply()
                        }
                        ActionButton {
                            visible: updates.readyToInstall
                            text: qsTr("Show the downloaded file")
                            onClicked: updates.showStagedFile()
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: updates.busy
                            text: Math.round(updates.progress * 100) + "%"
                            color: Theme.textDim
                            font.pixelSize: 12
                            font.family: "Consolas"
                        }
                    }

                    ToggleSwitch {
                        Layout.fillWidth: true
                        label: qsTr("Check for updates when the app starts")
                        description: qsTr("One request to the Discord channel a few seconds after " +
                                          "the window opens; nothing is downloaded until you say so.")
                        checked: updates.autoCheck
                        onToggled: updates.autoCheck = !updates.autoCheck
                    }
                }
            }

            // ---------------- credits card ----------------
            Card {
                Layout.fillWidth: true
                pad: 18

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 12

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Text {
                            text: qsTr("Made By")
                            color: Theme.text
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: qsTr("click a card to open the channel")
                            color: Theme.textDim
                            font.pixelSize: 10
                            font.italic: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        Repeater {
                            model: [
                                { name: "Romulos",
                                  role: qsTr("Developer"),
                                  handle: "@Romulus_FF",
                                  url: "https://www.youtube.com/@Romulus_FF?sub_confirmation=1" },
                                { name: "Mr.Pc Gamer",
                                  role: qsTr("Developer"),
                                  handle: "@MR.PC_GAMER_YT",
                                  url: "https://www.youtube.com/@MR.PC_GAMER_YT?sub_confirmation=1" }
                            ]
                            delegate: Rectangle {
                                id: authorCard
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 84
                                radius: 10
                                color: authorArea.containsMouse ? Theme.accentSoft : Theme.raised
                                border.width: 1
                                border.color: authorArea.containsMouse ? Theme.accent : Theme.border
                                Behavior on color { ColorAnimation { duration: 120 } }

                                // Logo first, the channel name underneath it.
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.topMargin: 10
                                    anchors.bottomMargin: 10
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 3

                                    Image {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.preferredWidth: 34
                                        Layout.preferredHeight: 24
                                        source: page.ytLogo(authorArea.containsMouse ? Theme.accent
                                                                                      : Theme.text)
                                        sourceSize.width: 68
                                        sourceSize.height: 48
                                        smooth: true
                                        fillMode: Image.PreserveAspectFit
                                        opacity: authorArea.containsMouse ? 1.0 : 0.8
                                        Behavior on opacity { NumberAnimation { duration: 120 } }
                                    }

                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        Layout.maximumWidth: parent.width
                                        text: authorCard.modelData.name
                                        color: Theme.text
                                        font.pixelSize: 13
                                        font.weight: Font.Bold
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: authorCard.modelData.handle
                                        color: authorArea.containsMouse ? Theme.accentText
                                                                        : Theme.textDim
                                        font.pixelSize: 10
                                        font.family: "Consolas"
                                    }
                                }

                                MouseArea {
                                    id: authorArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: Qt.openUrlExternally(authorCard.modelData.url)
                                }

                                ToolTip.text: qsTr("Open %1 on YouTube and subscribe")
                                                  .arg(authorCard.modelData.handle)
                                ToolTip.visible: authorArea.containsMouse
                                ToolTip.delay: 400
                            }
                        }
                    }
                }
            }

            // ---------------- how it works card ----------------
            Card {
                Layout.fillWidth: true
                pad: 18

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 10

                    Text {
                        text: qsTr("How It Works")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }

                    Repeater {
                        model: [
                            { t: qsTr("Theme (QML method)"),
                              d: qsTr("BlueStacks stores its whole color table in a QML file "
                                      + "(UiThemeObject.qml) compressed with zlib inside "
                                      + "HD-Player.exe. The tool locates that record through the "
                                      + "Qt resource tree, decompresses it, swaps the colors you "
                                      + "picked, recompresses and writes it back. If the new "
                                      + "theme is bigger than the old slot it is relocated into "
                                      + "empty space in the EXE and the resource pointer is "
                                      + "re-aimed — then everything is re-read and verified "
                                      + "BEFORE the file is written.") },
                            { t: qsTr("Theme (Hex method)"),
                              d: qsTr("Older builds keep plain #RRGGBB strings in the binary. "
                                      + "This mode finds each known color string (ASCII and "
                                      + "UTF-16) and overwrites it in place — same length, no "
                                      + "relocation needed.") },
                            { t: qsTr("SVG icons"),
                              d: qsTr("Every SVG icon embedded in the EXE is scanned and listed "
                                      + "with a live thumbnail. Replacing an icon null-pads the "
                                      + "new file into the old slot so the EXE size never "
                                      + "changes; the toggle-switch recolor rewrites only the "
                                      + "fill colors.") },
                            { t: qsTr("FPS unlock"),
                              d: qsTr("The FPS slider's setter inside Qt5QuickTemplates2.dll / Qt6QuickTemplates2.dll is "
                                      + "detoured to a small code cave that holds your custom "
                                      + "high-FPS maximum (60–1440). Only the 240 FPS maximum "
                                      + "is replaced; normal mode stays at 60 FPS, with no "
                                      + "normal-mode rewrite.") },
                            { t: qsTr("Integrity bypass"),
                              d: qsTr("HD-Player.exe self-checks its own bytes on start and "
                                      + "undoes patches. The check's conditional jump is NOP-ed "
                                      + "so your mods survive restarts.") },
                            { t: qsTr("Safety"),
                              d: qsTr("The first write to any file creates a .bak next to it. "
                                      + "Restore rolls back byte-identically. Patched images are "
                                      + "verified in memory before a single byte is written to "
                                      + "disk.") }
                        ]
                        delegate: ColumnLayout {
                            id: helpRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: "•  " + helpRow.modelData.t
                                color: Theme.text
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.leftMargin: 14
                                wrapMode: Text.Wrap
                                text: helpRow.modelData.d
                                color: Theme.textDim
                                font.pixelSize: 11
                                lineHeight: 1.3
                            }
                        }
                    }
                }
            }

            // ---------------- notes card ----------------
            Card {
                Layout.fillWidth: true
                pad: 18

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 8

                    Text {
                        text: qsTr("Tips")
                        color: Theme.text
                        font.pixelSize: 15
                        font.weight: Font.Bold
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: qsTr("• Close BlueStacks (HD-Player.exe) before patching, and run "
                                   + "this tool as administrator.\n"
                                   + "• Patch order that works best: Integrity bypass first, then "
                                   + "Theme / SVG / FPS, so your changes stick.\n"
                                   + "• If the Theme page cannot find the theme, run "
                                   + "\u201CBSI-Customizer.exe --theme-probe <path to "
                                   + "HD-Player.exe>\u201D from a terminal — it prints exactly "
                                   + "what detection sees.")
                        color: Theme.textDim
                        font.pixelSize: 12
                        lineHeight: 1.35
                    }
                }
            }
        }
    }
}
