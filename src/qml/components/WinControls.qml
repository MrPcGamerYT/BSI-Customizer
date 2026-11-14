import QtQuick
import QtQuick.Window
import BSICustomizer

// ---------------------------------------------------------------------------
// WinControls — the frameless window's control box (minimize / maximize /
// close), drawn exactly like Premium-Optimizer's .win-controls: Segoe glyph
// buttons on a transparent strip, hover glow, red close.
// ---------------------------------------------------------------------------
Row {
    id: controls

    // The ApplicationWindow being controlled (set by the instantiator).
    property var appWindow
    // 48 matches the topbar height; the gate's window row uses the same value.
    property int barHeight: 48

    spacing: 0

    function glyphMinimize() { return "\uE921" }  // ChromeMinimize
    function glyphMaximize() {
        return appWindow && appWindow.visibility === Window.Maximized ? "\uE923" // ChromeRestore
                                                                    : "\uE922" // ChromeMaximize
    }
    function glyphClose() { return "\uE8BB" }     // ChromeClose

    component WinButton: Rectangle {
        id: btn
        property string glyph: ""
        property bool closeButton: false
        property var windowRef: null
        signal activated()

        width: 46
        height: controls.barHeight
        color: area.containsMouse ? (closeButton ? "#c42b1c" : Theme.hoverFill)
                                  : "transparent"

        radius: 8
        Behavior on color { ColorAnimation { duration: Theme.motionDuration } }

        Text {
            anchors.centerIn: parent
            text: btn.glyph
            color: area.containsMouse ? "#ffffff" : Theme.soft
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: 10
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: btn.activated()
        }
    }

    WinButton {
        glyph: controls.glyphMinimize()
        windowRef: controls.appWindow
        onActivated: if (controls.appWindow) controls.appWindow.showMinimized()
    }
    WinButton {
        glyph: controls.glyphMaximize()
        windowRef: controls.appWindow
        onActivated: {
            if (!controls.appWindow)
                return;
            if (controls.appWindow.visibility === Window.Maximized)
                controls.appWindow.showNormal();
            else
                controls.appWindow.showMaximized();
        }
    }
    WinButton {
        glyph: controls.glyphClose()
        closeButton: true
        windowRef: controls.appWindow
        onActivated: if (controls.appWindow) controls.appWindow.close()
    }
}
