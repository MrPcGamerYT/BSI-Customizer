import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BSICustomizer

Rectangle {
    id: panel
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
            Text {
                text: qsTr("LOG")
                color: Theme.accentText
                font.pixelSize: 11
                font.weight: Font.Bold
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("Clear")
                implicitHeight: 26
                onClicked: logModel.clear()
            }
        }

        ListView {
            id: view
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
