import QtQuick
import BSICustomizer
import "../Icons.js" as Icons
Rectangle {
    id: mini
    property color tint: "#837a9b"
    color: "#0c0c11"; radius: 5; border.color: "#36343f"
    Rectangle { x: 1; y: 1; width: parent.width-2; height: 14; radius: 4; color: "#1b1a22"
        Row { x: 6; y: 6; spacing: 3
            Repeater { model: 3; Rectangle { width: 2; height: 2; radius: 1; color: "#77717f" } }
        }
    }
    Rectangle { x: 1; y: 15; width: 17; height: parent.height-17; color: "#14131b"; radius: 2 }
    Column { x: 6; y: 24; spacing: 10
        Repeater { model: [Icons.dashboard, Icons.settings, Icons.images]
            Image { required property string modelData; source: Icons.icon(modelData,mini.tint); sourceSize: Qt.size(7,7); width: 7; height: 7 }
        }
    }
    Rectangle { x: 27; y: 25; width: Math.max(1,parent.width-41); height: 3; radius: 1; color: "#66616e" }
    Rectangle { x: 27; y: 34; width: Math.max(1,parent.width-57); height: 2; radius: 1; color: "#302d3b" }
    Row { x: 27; y: 46; spacing: 5
        Repeater { model: 3
            Rectangle { width: Math.max(8,(mini.width-48)/3); height: 27; radius: 3; color: Qt.alpha(mini.tint,0.18); border.color: Qt.alpha(mini.tint,0.22) }
        }
    }
    Rectangle { x: 27; y: Math.max(80,parent.height-20); width: 27; height: 8; radius: 4; color: mini.tint }
}
