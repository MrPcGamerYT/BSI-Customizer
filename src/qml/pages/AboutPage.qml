import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "../Icons.js" as Icons

Item {
    id: page

    // Official YouTube play-button logo (red rounded rect + white triangle),
    // rendered as an inline SVG so no external file is needed.
    readonly property string ytLogo:
        "data:image/svg+xml," + encodeURIComponent(
            "<svg xmlns='http://www.w3.org/2000/svg' width='48' height='34' viewBox='0 0 48 34'>"
            + "<rect width='48' height='34' rx='7' fill='#FF0000'/>"
            + "<path d='M19 8.5v17L33 17z' fill='#ffffff'/>"
            + "</svg>")

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

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 14
                                    spacing: 12

                                    Rectangle {
                                        Layout.preferredWidth: 42
                                        Layout.preferredHeight: 42
                                        radius: 21
                                        color: Theme.accentSoft
                                        border.width: 1
                                        border.color: Theme.accent
                                        Text {
                                            anchors.centerIn: parent
                                            text: authorCard.modelData.name.charAt(0)
                                            color: Theme.accentText
                                            font.pixelSize: 18
                                            font.weight: Font.Black
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: authorCard.modelData.name
                                            color: Theme.text
                                            font.pixelSize: 14
                                            font.weight: Font.Bold
                                        }
                                        Text {
                                            text: authorCard.modelData.role
                                            color: Theme.textDim
                                            font.pixelSize: 11
                                        }
                                        Text {
                                            text: authorCard.modelData.handle
                                            color: authorArea.containsMouse ? Theme.accentText
                                                                            : Theme.textDim
                                            font.pixelSize: 10
                                            font.family: "Consolas"
                                        }
                                    }

                                    // YouTube logo — click target hint
                                    Image {
                                        Layout.preferredWidth: 40
                                        Layout.preferredHeight: 28
                                        source: page.ytLogo
                                        smooth: true
                                        opacity: authorArea.containsMouse ? 1.0 : 0.85
                                        Behavior on opacity { NumberAnimation { duration: 120 } }
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
                                      + "cap (60–1440), and the built-in 60/240/360 constants "
                                      + "are rewritten.") },
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
