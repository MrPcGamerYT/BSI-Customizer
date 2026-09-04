import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer
import "Icons.js" as Icons

ApplicationWindow {
    id: window
    width: 1180
    height: 760
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: qsTr("BlueStacks 5 / MSI 5 — BSI Customizer")
    color: Theme.window

    // ---- probes used by the C++ --selftest (do not remove) ----
    property color testProbe: Theme.window
    property color testProbeAccent: Theme.accent

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------- sidebar ----------------
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 216
            color: Theme.sidebar

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 3

                Item {
                    Layout.fillWidth: true
                    Layout.topMargin: 18
                    Layout.bottomMargin: 12
                    Layout.leftMargin: 18
                    Layout.rightMargin: 12
                    implicitHeight: 40

                    Row {
                        spacing: 10
                        Rectangle {
                            width: 36
                            height: 36
                            radius: 10
                            color: Theme.accent
                            Text {
                                anchors.centerIn: parent
                                text: "BSI"
                                color: "#ffffff"
                                font.pixelSize: 11
                                font.weight: Font.Black
                            }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            Text {
                                text: "BSI Customizer"
                                color: Theme.text
                                font.pixelSize: 14
                                font.weight: Font.Bold
                            }
                            Text {
                                text: "v" + settings.version
                                color: Theme.textDim
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                Repeater {
                    model: [
                        { label: qsTr("Dashboard"),   icon: Icons.dashboard },
                        { label: qsTr("Theme"),       icon: Icons.theme },
                        { label: qsTr("SVG"),         icon: Icons.svg },
                        { label: qsTr("FPS"),         icon: Icons.fps },
                        { label: qsTr("Integrity"),   icon: Icons.integrity },
                        { label: qsTr("Boot Video"),  icon: Icons.boot },
                        { label: qsTr("Extras"),      icon: Icons.extras },
                        { label: qsTr("Appearance"),  icon: Icons.appearance },
                        { label: qsTr("About"),       icon: Icons.about }
                    ]
                    delegate: SidebarButton {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        label: modelData.label
                        iconPath: modelData.icon
                        active: pages.currentIndex === index
                        onClicked: pages.currentIndex = index
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ---------------- content ----------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StackLayout {
                id: pages
                Layout.fillWidth: true
                Layout.fillHeight: true

                DashboardPage {}
                ThemePage {}
                SvgPage {}
                FpsPage {}
                IntegrityPage {}
                BootVideoPage {}
                ExtrasPage {}
                AppearancePage {
                    onNotify: (msg) => toast.show(msg)
                }
                AboutPage {}
            }

            LogPanel {
                id: logPanel
                Layout.fillWidth: true
                Layout.preferredHeight: logPanel.collapsed ? 42 : 150
                Behavior on Layout.preferredHeight { NumberAnimation { duration: 140 } }
            }
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
    }

    Connections {
        target: backend
        function onLog(message, color) { logPanel.append(message, color) }
    }
}
