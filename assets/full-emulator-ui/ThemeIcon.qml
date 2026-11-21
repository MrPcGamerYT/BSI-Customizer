import QtQuick 2.15
import "IconMasks.js" as Masks

Item {
    id: icon
    property url source
    property color tint: Theme.text
    property size sourceSize: Qt.size(64,64)
    property int fillMode: Image.PreserveAspectFit
    readonly property int status: image.status
    Image {
        id: image
        anchors.fill:parent
        // Indexed PNG: recolor just the palette, with offline-compressed alpha.
        // No Canvas availability/paint tick and no SVG or GPU effect dependency.
        source:icon.source.toString().length ? Masks.colored(icon.source,icon.tint) : ""
        sourceSize:icon.sourceSize
        fillMode:icon.fillMode
        asynchronous:false
        cache:true
        smooth:true
    }
}
