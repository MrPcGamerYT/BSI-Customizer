import QtQuick
import BSICustomizer
Item {
    id: frame
    property color tint: "#555560"
    property int tick: 9
    // Eight small native rectangles; no shader, texture layer or running timer.
    Repeater {
        model: 4
        Item {
            required property int index
            x: index % 2 ? frame.width - frame.tick : 0
            y: index > 1 ? frame.height - frame.tick : 0
            width: frame.tick; height: frame.tick
            Rectangle { width: parent.width; height: 1; y: index > 1 ? parent.height-1 : 0; color: frame.tint }
            Rectangle { height: parent.height; width: 1; x: index % 2 ? parent.width-1 : 0; color: frame.tint }
        }
    }
}
