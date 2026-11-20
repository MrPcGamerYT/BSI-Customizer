// Self-contained paint: no layer/FBO, opacity mask or host-style dependency.
import QtQuick 2.15
Rectangle {
    id: surface
    property color base: "transparent"
    property real reflection: 1.12
    color: base
    gradient: Gradient {
        GradientStop { color: Qt.lighter(surface.base, surface.reflection) }
        GradientStop { position: 1; color: surface.base }
    }
}
