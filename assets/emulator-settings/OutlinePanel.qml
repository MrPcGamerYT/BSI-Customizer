UiShadowContainer {
    // BSI Settings v3 — Outline edition. Keep the v3 marker for native recovery compatibility.
    id: iSettingsWindowShadowContainer
    objectName: "bsiSettingsShell"
    anchors.fill: parent
    readonly property bool bsiCompact: width < 880
    readonly property int bsiRailWidth: bsiCompact ? 68 : 184
    readonly property var bsiTabs: pSettingsItems.pSettingElements

    function bsiIconKind(buttonId, title) {
        var key = String(buttonId || "").toLowerCase().replace(/[^a-z0-9]/g, "")
        var label = String(title || "").toLowerCase()
        if (/about|info/.test(key)) return "info"
        if (/shortcut|keymap|keyboard/.test(key)) return "keyboard"
        if (/advance/.test(key)) return "code"
        if (/gamesetting|gamepad|controller/.test(key)) return "gamepad"
        if (/performance|engine/.test(key)) return "cpu"
        if (/display|resolution/.test(key)) return "display"
        if (/graphic|render/.test(key)) return "layers"
        if (/audio|sound/.test(key)) return "audio"
        if (/phone|mobile/.test(key)) return "phone"
        if (/device/.test(key)) return "mouse"
        if (/userdata|storage|backup/.test(key)) return "storage"
        if (/preference|notification/.test(key)) return "sliders"
        // Translated labels are a fallback only; stable native IDs take priority.
        if (/about/.test(label)) return "info"
        if (/audio|sound/.test(label)) return "audio"
        if (/display/.test(label)) return "display"
        if (/graphics/.test(label)) return "layers"
        if (/performance/.test(label)) return "cpu"
        return "sliders"
    }
    function bsiCurrentTitle() {
        var entry = bsiTabs[pCurrentTabIndex]
        return entry ? entry.pButtonText : qsTranslate("QObject", "Settings")
    }

    // Local Qt Quick vectors: no emoji font, SVG plugin, external file or network dependency.
    component BsiOutlineIcon: Canvas {
        property string kind: "sliders"
        property color stroke: UiTheme.colors.primary10
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
        radius: 14
        color: UiTheme.colors.primary80
        border.color: Qt.rgba(UiTheme.colors.primary10.r, UiTheme.colors.primary10.g, UiTheme.colors.primary10.b, 0.13)
        border.width: 1
        clip: true

        // The drag surface stops before Close; original Window.onClosing still owns discard prompts.
        Item {
            id: bsiHeader
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            height: 64
            MouseArea {
                anchors.fill: parent; anchors.rightMargin: 62
                onPressed: iSettingsWindow.startSystemMove()
            }
            Rectangle {
                x: 18; y: 16; width: 32; height: 32; radius: 9
                color: Qt.rgba(UiTheme.colors.accentNormal.r, UiTheme.colors.accentNormal.g, UiTheme.colors.accentNormal.b, 0.13)
                BsiOutlineIcon { anchors.centerIn: parent; width: 19; height: 19; stroke: UiTheme.colors.accentNormal }
            }
            Column {
                anchors.left: parent.left; anchors.leftMargin: 61; anchors.verticalCenter: parent.verticalCenter
                spacing: 3
                Text { text: qsTranslate("QObject", "Settings"); color: UiTheme.colors.primary10; font: UiTheme.fonts.bodyLarge }
                Text { text: "PLAYER WORKSPACE"; color: Qt.rgba(UiTheme.colors.primary10.r,UiTheme.colors.primary10.g,UiTheme.colors.primary10.b,0.48); font.pixelSize: 9; font.letterSpacing: 1.4 }
            }
            ToolButton {
                id: bsiClose
                objectName: "bsiSettingsClose"
                anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                width: 34; height: 34; hoverEnabled: true
                Accessible.name: qsTranslate("QObject", "Close")
                contentItem: BsiOutlineIcon { kind: "close"; stroke: bsiClose.hovered ? "#ff9b9b" : UiTheme.colors.primary10 }
                background: Rectangle { radius: 8; color: bsiClose.hovered ? "#25e16c75" : "transparent"; Behavior on color { ColorAnimation { duration: 120 } } }
                onClicked: iSettingsWindow.close()
                ToolTip.visible: hovered; ToolTip.text: qsTranslate("QObject", "Close")
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Qt.rgba(UiTheme.colors.primary10.r,UiTheme.colors.primary10.g,UiTheme.colors.primary10.b,0.08) }
        }
        Rectangle {
            id: bsiRail
            anchors.left: parent.left; anchors.top: bsiHeader.bottom; anchors.bottom: parent.bottom
            width: iSettingsWindowShadowContainer.bsiRailWidth
            color: Qt.rgba(UiTheme.colors.primary10.r,UiTheme.colors.primary10.g,UiTheme.colors.primary10.b,0.025)
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: Qt.rgba(UiTheme.colors.primary10.r,UiTheme.colors.primary10.g,UiTheme.colors.primary10.b,0.08) }
            ListView {
                id: bsiNavigation
                objectName: "bsiSettingsNavigation"
                anchors.fill: parent; anchors.margins: 10
                clip: true; spacing: 5
                boundsBehavior: Flickable.StopAtBounds
                currentIndex: pCurrentTabIndex
                model: pSettingsItems.pSettingElements
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: ItemDelegate {
                    id: bsiTab
                    objectName: "bsiSettingsTab_" + index
                    width: bsiNavigation.width; height: 44
                    property var nativeEntry: model.modelData
                    property bool selected: index === pCurrentTabIndex
                    hoverEnabled: true
                    Accessible.name: nativeEntry.pButtonText
                    contentItem: Item {
                        BsiOutlineIcon {
                            id: bsiTabIcon
                            anchors.left: parent.left; anchors.leftMargin: 7; anchors.verticalCenter: parent.verticalCenter
                            width: 21; height: 21
                            kind: iSettingsWindowShadowContainer.bsiIconKind(bsiTab.nativeEntry.pButtonId,bsiTab.nativeEntry.pButtonText)
                            stroke: bsiTab.selected ? UiTheme.colors.accentNormal : UiTheme.colors.primary10
                            opacity: bsiTab.selected || bsiTab.hovered ? 1 : 0.62
                        }
                        Text {
                            id: bsiTabLabel
                            anchors.left: bsiTabIcon.right; anchors.leftMargin: 11; anchors.right: parent.right; anchors.rightMargin: 9; anchors.verticalCenter: parent.verticalCenter
                            visible: !iSettingsWindowShadowContainer.bsiCompact
                            text: bsiTab.nativeEntry.pButtonText; elide: Text.ElideRight
                            font: UiTheme.fonts.bodyMedium
                            color: bsiTab.selected ? UiTheme.colors.accentNormal : UiTheme.colors.primary10
                            opacity: bsiTab.selected || bsiTab.hovered ? 1 : 0.68
                        }
                    }
                    background: Rectangle {
                        radius: 8
                        border.width: 1
                        border.color: bsiTab.activeFocus ? UiTheme.colors.accentNormal : "transparent"
                        color: bsiTab.selected ? Qt.rgba(UiTheme.colors.accentNormal.r,UiTheme.colors.accentNormal.g,UiTheme.colors.accentNormal.b,0.13) : bsiTab.hovered ? Qt.rgba(UiTheme.colors.primary10.r,UiTheme.colors.primary10.g,UiTheme.colors.primary10.b,0.045) : "transparent"
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Rectangle { width: 3; height: 17; radius: 1.5; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; color: UiTheme.colors.accentNormal; visible: bsiTab.selected }
                        Rectangle { width: 5; height: 5; radius: 2.5; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 6; color: "#ffad76"; visible: !!bsiTab.nativeEntry.pShowRedDot }
                    }
                    // Do not assign pCurrentTabIndex here: native discard logic decides whether switching is allowed.
                    onClicked: fChangeSelectedSetting(index)
                    ToolTip.visible: hovered && (iSettingsWindowShadowContainer.bsiCompact || bsiTabLabel.truncated)
                    ToolTip.delay: 400; ToolTip.text: nativeEntry.pButtonText
                }
            }
        }
        Column {
            id: bsiPageHeading
            anchors.left: bsiRail.right; anchors.right: parent.right; anchors.top: bsiHeader.bottom
            anchors.leftMargin: 20; anchors.rightMargin: 20; anchors.topMargin: 16
            spacing: 4
            Text { text: iSettingsWindowShadowContainer.bsiCurrentTitle(); width: parent.width; elide: Text.ElideRight; color: UiTheme.colors.primary10; font: UiTheme.fonts.bodyLarge }
            Rectangle { width: 26; height: 2; radius: 1; color: UiTheme.colors.accentNormal }
        }
        Rectangle {
            anchors.left: bsiRail.right; anchors.right: parent.right; anchors.top: bsiPageHeading.bottom; anchors.bottom: parent.bottom
            anchors.leftMargin: 14; anchors.rightMargin: 14; anchors.topMargin: 12; anchors.bottomMargin: 14
            radius: 10; color: UiTheme.colors.primary70
            border.width: 1; border.color: Qt.rgba(UiTheme.colors.primary10.r,UiTheme.colors.primary10.g,UiTheme.colors.primary10.b,0.06)
            clip: true
            StackLayout {
                id: iSelectedSettingControl
                anchors.fill: parent; anchors.margins: 8
                currentIndex: pCurrentTabIndex
                Repeater {
                    model: pSettingsItems.pSettingElements
                    delegate: Loader {
                        Component.onCompleted: {
                            var args = model.modelData.pComponentArgs
                            args ? setSource(model.modelData.pQmlSource,args) : setSource(model.modelData.pQmlSource)
                        }
                        onLoaded: {
                            model.modelData.pComponentObj = item
                            if (item.fOnQmlLoaded) item.fOnQmlLoaded()
                        }
                    }
                }
            }
        }
    }
}
