import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

// A mock UI, not an emulator renderer. Symbolic originals fall back to neutral
// colors instead of feeding invalid aliases into QML color properties.
RowLayout {
    id: root
    property var entries: []
    function swatch(name, staged, fallback, depth) {
        depth = depth || 0;
        if (depth > 8) return fallback; // Cyclic aliases never recurse forever.
        for (const e of entries) {
            if (String(e.name).toLowerCase() !== name) continue;
            const s = String(staged && e.edit ? e.edit : e.value).replace(/["']/g, "");
            if (/^#(?:[0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i.test(s)) return s;
            if (/^(?:\w+\.)*\w+$/.test(s)) {
                const alias = s.split(".").pop().toLowerCase();
                if (["transparent", "black", "white", "red", "blue", "green"].indexOf(alias) >= 0) return alias;
                return swatch(alias, staged, fallback, depth + 1);
            }
        }
        return fallback;
    }
    spacing: 12
    Repeater {
        model: [false, true]
        Rectangle {
            id: mock
            required property bool modelData
            Layout.fillWidth: true
            implicitHeight: 185
            radius: 10
            color: root.swatch("primary80", modelData, "#21242c")
            border.color: Theme.border
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 8
                Label { text: mock.modelData ? qsTr("Staged palette — mock preview") : qsTr("Original — mock preview"); color: root.swatch("primary10", mock.modelData, "#ffffff") }
                RowLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Rectangle { Layout.preferredWidth: 65; Layout.fillHeight: true; radius: 5; color: root.swatch("primary70", mock.modelData, "#000000"); Label { anchors.centerIn: parent; text: "MENU"; color: root.swatch("primary10", mock.modelData, "#ffffff") } }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Settings / sample text"); color: root.swatch("primary10", mock.modelData, "#ffffff") }
                        Rectangle { Layout.fillWidth: true; height: 28; radius: 5; color: root.swatch("primary60", mock.modelData, "#454954"); Label { anchors.centerIn: parent; text: qsTr("Hover surface"); color: root.swatch("primary10", mock.modelData, "#ffffff") } }
                        Rectangle { Layout.fillWidth: true; height: 28; radius: 5; color: root.swatch("accentnormal", mock.modelData, "#168bff"); Label { anchors.centerIn: parent; text: qsTr("Action button"); color: root.swatch("primary10", mock.modelData, "#ffffff") } }
                        RowLayout {
                            Rectangle {
                                width: 38; height: 20; radius: 10
                                color: root.swatch("accentnormal", mock.modelData, "#168bff")
                                Rectangle { anchors.right: parent.right; anchors.margins: 3; anchors.verticalCenter: parent.verticalCenter; width: 14; height: 14; radius: 7; color: root.swatch("primary10", mock.modelData, "#ffffff") }
                            }
                            Label { text: qsTr("Sample switch"); color: root.swatch("primary10", mock.modelData, "#ffffff") }
                        }
                    }
                }
            }
        }
    }
}
