import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Rectangle {
    id: panel

    // Main.qml binds the panel height to this, so a collapsed log hands its
    // ~110 px back to whatever page is on screen.
    property bool collapsed: false

    color: Theme.sidebar

    function append(msg, colorName) {
        logModel.append({ msg: msg, col: colorName || "gray" });
        if (logModel.count > 400)
            logModel.remove(0);
        view.positionViewAtEnd();
    }

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: Theme.border
    }

    ListModel { id: logModel }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: qsTr("LOG")
                color: Theme.accentText
                font.pixelSize: 11
                font.weight: Font.Bold
            }
            Text {
                visible: !panel.collapsed && logModel.count > 0
                text: qsTr("%1 lines").arg(logModel.count)
                color: Theme.textDim
                font.pixelSize: 10
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Clear")
                implicitHeight: 26
                enabled: !panel.collapsed && logModel.count > 0
                onClicked: logModel.clear()
            }
            Rectangle {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                radius: 7
                color: collapseArea.containsMouse ? Theme.raised : "transparent"
                border.width: 1
                border.color: Theme.border

                Text {
                    anchors.centerIn: parent
                    text: panel.collapsed ? "\u25b2" : "\u25bc"
                    color: Theme.textDim
                    font.pixelSize: 9
                }
                MouseArea {
                    id: collapseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: panel.collapsed = !panel.collapsed
                }
            }
        }

        ListView {
            id: view
            visible: !panel.collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: logModel
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar {}

            delegate: Text {
                required property string msg
                required property string col
                width: ListView.view.width
                text: msg
                color: Theme.logColor(col)
                font.family: "Consolas"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }
    }
}
