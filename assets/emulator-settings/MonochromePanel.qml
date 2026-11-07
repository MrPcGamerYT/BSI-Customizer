// BSI Settings — monochrome reference skin. Visual-only; native pages stay embedded.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: shell
    objectName: "bsiSettingsShell"
    property var bsiHost
    property var bsiTheme
    readonly property bool bsiCompact: width < 880
    readonly property int bsiRailWidth: bsiCompact ? 58 : 174
    readonly property var bsiTabs: bsiHost ? bsiHost.pSettingsItems.pSettingElements : []
    readonly property int selectedIndex: bsiHost ? bsiHost.pCurrentTabIndex : 0
    property alias pageContainer: bsiPageBody

    function iconKind(id) {
        var key = String(id || "").toLowerCase().replace(/[^a-z0-9]/g, "")
        if (/performance|engine/.test(key)) return "performance"
        if (/display|resolution/.test(key)) return "display"
        if (/graphic/.test(key)) return "layers"
        if (/gamesetting/.test(key)) return "person"
        if (/gamepad|controller/.test(key)) return "gamepad"
        if (/phone|device/.test(key)) return "phone"
        if (/theme/.test(key)) return "theme"
        if (/advance/.test(key)) return "sliders"
        if (/shortcut|keyboard/.test(key)) return "keyboard"
        if (/about|info/.test(key)) return "info"
        if (/audio|sound/.test(key)) return "audio"
        if (/storage|userdata/.test(key)) return "storage"
        return "sliders"
    }
    component BsiOutlineIcon: Canvas {
        property string kind: "sliders"
        property color stroke: "#e8e8ec"
        implicitWidth: 24
        implicitHeight: 24
        onKindChanged: requestPaint()
        onStrokeChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            var c = getContext("2d")
            c.reset()
            c.clearRect(0, 0, width, height)
            c.save()
            c.scale(width / 24, height / 24)
            c.strokeStyle = stroke
            c.lineWidth = 1.65
            c.lineCap = "round"
            c.lineJoin = "round"
            function line(x1,y1,x2,y2) { c.moveTo(x1,y1); c.lineTo(x2,y2) }
            function box(x,y,w,h) { c.rect(x,y,w,h) }
            function ring(x,y,r) { c.moveTo(x+r,y); c.arc(x,y,r,0,Math.PI*2) }
            c.beginPath()
            switch (kind) {
            case "performance":
                c.moveTo(3,17);c.arc(12,15,9,Math.PI,Math.PI*2);line(3,17,6,17);line(18,17,21,17)
                line(12,15,16,9);ring(12,15,1.2);break
            case "person":
                ring(12,7,3);c.moveTo(5,21);c.lineTo(5,19);c.bezierCurveTo(5,13,19,13,19,19);c.lineTo(19,21);line(9,21,15,21);break
            case "theme":
                ring(12,12,9);c.moveTo(12,3);c.bezierCurveTo(5,6,5,18,12,21);line(12,3,12,21);break
            case "cpu":
                box(6,6,12,12); box(9,9,6,6)
                for(var n=8;n<=16;n+=4) { line(n,3,n,6);line(n,18,n,21);line(3,n,6,n);line(18,n,21,n) }
                break
            case "display":
                box(3,4,18,13);line(8,21,16,21);line(12,17,12,21);break
            case "layers":
                c.moveTo(12,3);c.lineTo(21,8);c.lineTo(12,13);c.lineTo(3,8);c.closePath()
                c.moveTo(3,12);c.lineTo(12,17);c.lineTo(21,12)
                c.moveTo(3,16);c.lineTo(12,21);c.lineTo(21,16);break
            case "audio":
                c.moveTo(4,9);c.lineTo(8,9);c.lineTo(13,5);c.lineTo(13,19);c.lineTo(8,15);c.lineTo(4,15);c.closePath()
                c.moveTo(16,8);c.quadraticCurveTo(20,12,16,16)
                c.moveTo(19,5);c.quadraticCurveTo(25,12,19,19);break
            case "keyboard":
                box(2.5,5,19,14)
                for(var row=8;row<=11;row+=3) for(var col=6;col<=18;col+=4) line(col,row,col+.5,row)
                line(6,15,8,15);line(11,15,18,15);break
            case "code":
                c.moveTo(8,7);c.lineTo(3,12);c.lineTo(8,17)
                c.moveTo(16,7);c.lineTo(21,12);c.lineTo(16,17);line(14,4,10,20);break
            case "gamepad":
                c.moveTo(7,7);c.lineTo(17,7);c.quadraticCurveTo(20,7,21,12)
                c.lineTo(22,17);c.quadraticCurveTo(22,21,17,16);c.lineTo(7,16)
                c.quadraticCurveTo(2,21,2,17);c.lineTo(3,12);c.quadraticCurveTo(4,7,7,7)
                line(6,11,10,11);line(8,9,8,13);ring(16,10,0.65);ring(18,13,0.65);break
            case "phone":
                box(6,2,12,20);line(10,5,14,5);line(11,19,13,19);break
            case "mouse":
                c.moveTo(6,10);c.bezierCurveTo(6,1,18,1,18,10);c.lineTo(18,15)
                c.bezierCurveTo(18,24,6,24,6,15);c.closePath();line(12,4,12,10);line(6,11,18,11);break
            case "storage":
                box(3,4,18,16);line(3,12,21,12);ring(17,16,0.7);line(6,8,12,8);break
            case "info":
                ring(12,12,9);line(12,11,12,17);ring(12,7,0.5);break
            case "close":
                line(7,7,17,17);line(17,7,7,17);break
            default:
                line(4,6,8,6);line(12,6,20,6);ring(10,6,2)
                line(4,12,14,12);line(18,12,20,12);ring(16,12,2)
                line(4,18,6,18);line(10,18,20,18);ring(8,18,2);break
            }
            c.stroke()
            c.restore()
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 15
        clip: true
        gradient: Gradient {
            GradientStop { position: 0; color: "#0c0c0d" }
            GradientStop { position: 0.24; color: "#030304" }
            GradientStop { position: 0.74; color: "#030304" }
            GradientStop { position: 1; color: "#111114" }
        }
        border.width: 1
        border.color: "#56565c"
        Rectangle {
            anchors.fill: parent; anchors.margins: 1
            radius: 14; color: "transparent"
            border.width: 1; border.color: "#17171b"
        }
        Item {
            id: header
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 88
            MouseArea {
                anchors.fill: parent; anchors.rightMargin: 64
                onPressed: if (shell.bsiHost) shell.bsiHost.startSystemMove()
            }
            Text {
                anchors.left: parent.left; anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter; anchors.verticalCenterOffset: 8
                text: qsTranslate("QObject", "Settings")
                color: "#f3f3f5"; font.pixelSize: 20; font.weight: Font.DemiBold
            }
            ToolButton {
                id: closeButton
                objectName: "bsiSettingsClose"
                anchors.right: parent.right; anchors.rightMargin: 22
                anchors.top: parent.top; anchors.topMargin: 23
                width: 30; height: 30; hoverEnabled: true
                padding: 6
                Accessible.name: qsTranslate("QObject", "Close")
                contentItem: BsiOutlineIcon { kind: "close"; stroke: closeButton.hovered ? "#ffffff" : "#b7b7be" }
                background: Rectangle {
                    radius: 7
                    color: closeButton.hovered ? "#29292e" : "transparent"
                    Behavior on color { ColorAnimation { duration: 110 } }
                }
                onClicked: if (shell.bsiHost) shell.bsiHost.close()
                ToolTip.visible: hovered; ToolTip.text: qsTranslate("QObject", "Close")
            }
        }
        Item {
            id: rail
            anchors.left: parent.left; anchors.leftMargin: 16
            anchors.top: header.bottom; anchors.topMargin: 8
            anchors.bottom: parent.bottom; anchors.bottomMargin: 16
            width: shell.bsiRailWidth
            ListView {
                id: navigation
                objectName: "bsiSettingsNavigation"
                anchors.fill: parent
                model: shell.bsiTabs
                currentIndex: shell.selectedIndex
                clip: true; spacing: 5
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ItemDelegate {
                    id: tab
                    objectName: "bsiSettingsTab_" + index
                    width: navigation.width; height: 34
                    property var nativeEntry: model.modelData
                    readonly property bool selected: index === shell.selectedIndex
                    hoverEnabled: true
                    leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
                    Accessible.name: nativeEntry.pButtonText
                    contentItem: Item {
                        BsiOutlineIcon {
                            id: tabIcon
                            anchors.left: parent.left
                            anchors.leftMargin: shell.bsiCompact ? 20 : 17
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16; height: 16
                            kind: shell.iconKind(tab.nativeEntry.pButtonId)
                            stroke: tab.selected || tab.hovered ? "#f4f4f6" : "#b4b4bd"
                        }
                        Text {
                            id: tabLabel
                            anchors.left: tabIcon.right; anchors.leftMargin: 12
                            anchors.right: parent.right; anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !shell.bsiCompact
                            text: tab.nativeEntry.pButtonText
                            font.pixelSize: 12; font.weight: tab.selected ? Font.DemiBold : Font.Normal
                            color: tab.selected || tab.hovered ? "#f7f7fa" : "#c6c6cd"
                            elide: Text.ElideRight
                        }
                    }
                    background: Rectangle {
                        radius: 11
                        border.width: 1
                        border.color: tab.selected || tab.activeFocus ? "#e4e4eb" : tab.hovered ? "#65656d" : "transparent"
                        gradient: Gradient {
                            GradientStop { position: 0; color: tab.selected ? "#29292e" : tab.hovered ? "#19191e" : "#00000000" }
                            GradientStop { position: 1; color: tab.selected ? "#131316" : tab.hovered ? "#101014" : "#00000000" }
                        }
                        Behavior on border.color { ColorAnimation { duration: 110 } }
                        Rectangle {
                            anchors.fill: parent; anchors.margins: 2
                            radius: 9; color: "transparent"
                            border.width: 1; border.color: "#35353e"
                            visible: tab.selected
                        }
                        Rectangle {
                            anchors.left: parent.left; anchors.leftMargin: 5
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2; height: 17; radius: 1
                            color: "#f3f3f7"; visible: tab.selected
                        }
                        Rectangle {
                            width: 5; height: 5; radius: 2.5
                            anchors.right: parent.right; anchors.rightMargin: 6
                            anchors.top: parent.top; anchors.topMargin: 6
                            color: "#e6a2aa"; visible: !!tab.nativeEntry.pShowRedDot
                        }
                    }
                    // Native code alone decides whether an unsaved page can be left.
                    onClicked: if (shell.bsiHost) shell.bsiHost.fChangeSelectedSetting(index)
                    ToolTip.visible: hovered && (shell.bsiCompact || tabLabel.truncated)
                    ToolTip.text: nativeEntry.pButtonText; ToolTip.delay: 400
                }
            }
        }
        Rectangle {
            anchors.left: rail.right; anchors.leftMargin: 14
            anchors.top: rail.top; anchors.topMargin: 5
            anchors.bottom: parent.bottom; anchors.bottomMargin: 22
            width: 1; color: "#141417"
        }
        // Reparented native StackLayout lives here. No fake settings controls,
        // duplicate Save button, duplicate page title or substitute backend.
        Item {
            id: bsiPageBody
            objectName: "bsiSettingsPageContainer"
            anchors.left: rail.right; anchors.leftMargin: 36
            anchors.right: parent.right; anchors.rightMargin: 16
            anchors.top: header.bottom; anchors.topMargin: 8
            anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        }
    }
}
