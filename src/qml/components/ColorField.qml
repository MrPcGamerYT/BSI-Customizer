import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

// Swatch + hex field with a popup color picker — deterministic, drawn by the
// app itself instead of an OS dialog.
RowLayout {
    id: control

    property string current: ""   // current value from the EXE
    property string value: ""     // new value being set ("")
    signal edited(string hex)

    spacing: 6

    readonly property string shown: value !== "" ? value : current

    Rectangle {
        width: 34
        height: 22
        radius: 5
        color: {
            const c = control.shown;
            if (/^#[0-9A-Fa-f]{6}$/.test(c))
                return c;
            if (/^#[0-9A-Fa-f]{8}$/.test(c))
                return "#" + c.substring(3);
            return Theme.raised;
        }
        border.width: 1
        border.color: Theme.border

        Text {
            anchors.centerIn: parent
            visible: !/^#[0-9A-Fa-f]{6,8}$/.test(control.shown)
            text: "?"
            color: Theme.textDim
            font.pixelSize: 11
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                picker.hexText = /^#[0-9A-Fa-f]{6,8}$/.test(control.shown)
                                 ? (control.shown.length === 9 ? "#" + control.shown.substring(3)
                                                              : control.shown) : "#7c5cff";
                picker.open();
            }
        }
    }

    TextField {
        id: hexInput
        Layout.preferredWidth: 92
        Layout.fillWidth: true
        Layout.minimumWidth: 62
        text: control.value
        placeholderText: "#RRGGBB"
        color: Theme.text
        placeholderTextColor: Theme.textDim
        font.pixelSize: 12
        font.family: "Consolas"
        background: Rectangle {
            color: Theme.raised
            border.color: hexInput.activeFocus ? Theme.accent : Theme.border
            radius: 8
        }
        onAccepted: commit()
        onEditingFinished: commit()

        function commit() {
            const t = text.trim();
            if (t === "") {
                control.edited("");
            } else if (/^#?(?:[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})$/.test(t)) {
                const v = t.startsWith("#") ? t.toUpperCase() : ("#" + t).toUpperCase();
                control.edited(v);
            }
        }
    }

    Popup {
        id: picker
        property string hexText: "#7c5cff"

        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        parent: control
        x: 0
        y: control.height + 6
        width: 268
        height: pickerCol.implicitHeight + 28
        padding: 14

        background: Rectangle {
            color: Theme.card
            border.color: Theme.border
            radius: 12
        }

        ColumnLayout {
            id: pickerCol
            anchors.fill: parent
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                height: 40
                radius: 8
                color: /^#[0-9A-Fa-f]{6}$/.test(picker.hexText) ? picker.hexText : Theme.raised
                border.color: Theme.border
                Text {
                    anchors.centerIn: parent
                    text: picker.hexText
                    color: "#ffffff"
                    style: Text.Outline
                    styleColor: "#80000000"
                    font.pixelSize: 13
                    font.weight: Font.Bold
                }
            }

            TextField {
                id: pickerHex
                Layout.fillWidth: true
                text: picker.hexText
                color: Theme.text
                font.pixelSize: 13
                font.family: "Consolas"
                background: Rectangle {
                    color: Theme.raised
                    border.color: Theme.border
                    radius: 8
                }
                onTextChanged: {
                    if (/^#[0-9A-Fa-f]{6}$/.test(text))
                        picker.hexText = text;
                }
            }

            Repeater {
                model: ["R", "G", "B"]
                delegate: RowLayout {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: modelData
                        color: Theme.accentText
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        Layout.preferredWidth: 14
                    }
                    Slider {
                        id: rgbSlider
                        Layout.fillWidth: true
                        from: 0
                        to: 255
                        value: parseInt(picker.hexText.substring(1 + index * 2, 3 + index * 2), 16) || 0
                        onMoved: {
                            const parts = picker.hexText.substring(1).match(/.{2}/g) || ["00", "00", "00"];
                            parts[index] = ("0" + Math.round(value).toString(16)).slice(-2);
                            picker.hexText = "#" + parts.join("").toUpperCase();
                        }
                    }
                }
            }

            Grid {
                Layout.fillWidth: true
                columns: 9
                spacing: 5
                Repeater {
                    model: ["#000000", "#ffffff", "#7c5cff", "#5aa7ff", "#38d996", "#ffb454",
                            "#ff6b6b", "#e05cff", "#00c2ff", "#1a1a2e", "#16213e", "#0f3460",
                            "#e94560", "#f5f5f5", "#808080", "#3a3a44", "#0d0f14", "#151821"]
                    delegate: Rectangle {
                        required property string modelData
                        width: 22
                        height: 18
                        radius: 4
                        color: modelData
                        border.width: 1
                        border.color: Theme.border
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: picker.hexText = modelData.toUpperCase()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton {
                    text: qsTr("Cancel")
                    onClicked: picker.close()
                }
                ActionButton {
                    accent: true
                    text: qsTr("OK")
                    onClicked: {
                        if (/^#[0-9A-Fa-f]{6}$/.test(picker.hexText)) {
                            hexInput.text = picker.hexText;
                            control.edited(picker.hexText);
                        }
                        picker.close();
                    }
                }
            }
        }
    }
}
