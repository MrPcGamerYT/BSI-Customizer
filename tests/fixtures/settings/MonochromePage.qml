// Preview/test double only. This is NEVER installed in the emulator.
import QtQuick 2.15
import QtQuick.Controls 2.15
Item {
    property string nativePageName: ""
    property bool initialized: false
    function fOnQmlLoaded() { initialized = true }
    Text { visible: nativePageName !== "Performance"; text: nativePageName; color: "#f2f2f5"; font.pixelSize: 18 }
    Column {
        visible: nativePageName === "Performance"
        anchors.left: parent.left; anchors.top: parent.top
        anchors.topMargin: 2; width: Math.min(460, parent.width - 8); spacing: 12
        Text { text: "CPU allocation"; color: "#eeeeF2"; font.pixelSize: 12 }
        ComboBox {
            id: cpu
            width: 174; height: 27
            model: ["High (4 Cores)", "Medium (2 Cores)", "Low (1 Core)"]
            background: Rectangle { radius: 4; color: "#151519"; border.color: "#48484f" }
            contentItem: Text { leftPadding: 9; text: cpu.displayText; color: "#eeeef3"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
        }
        Text { text: "Assigning all CPU cores may cause your system to lag/slow down"; color: "#e6a1b4"; font.pixelSize: 10; wrapMode: Text.Wrap; width: parent.width }
        Rectangle { width: parent.width; height: 1; color: "#303034" }
        Text { text: "Memory allocation"; color: "#eeeef2"; font.pixelSize: 12 }
        ComboBox {
            id: memory
            width: 174; height: 27
            model: ["High (8 GB)", "Enhanced (4 GB)", "Standard (2 GB)"]
            background: Rectangle { radius: 4; color: "#151519"; border.color: "#48484f" }
            contentItem: Text { leftPadding: 9; text: memory.displayText; color: "#eeeef3"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
        }
        Rectangle { width: parent.width; height: 1; color: "#303034" }
        Text { text: "Performance mode"; color: "#eeeef2"; font.pixelSize: 12 }
        ComboBox {
            id: performance
            width: 174; height: 27
            model: ["High Performance", "Balanced", "Low Memory"]
            background: Rectangle { radius: 4; color: "#151519"; border.color: "#48484f" }
            contentItem: Text { leftPadding: 9; text: performance.displayText; color: "#eeeef3"; verticalAlignment: Text.AlignVCenter; font.pixelSize: 11 }
        }
        Rectangle { width: parent.width; height: 1; color: "#303034" }
        Text { text: "Frame rate: " + Math.round(fps.value); color: "#eeeef2"; font.pixelSize: 12 }
        Slider {
            id: fps
            width: parent.width; height: 16; from: 1; to: 240; value: 240
            background: Rectangle { x:fps.leftPadding; y:fps.topPadding+fps.availableHeight/2-height/2; width:fps.availableWidth; height:2; color:"#74747b"
                Rectangle { width:fps.visualPosition*parent.width; height:parent.height; color:"#ededf2" }
            }
            handle: Rectangle { x:fps.leftPadding+fps.visualPosition*(fps.availableWidth-width); y:fps.topPadding+fps.availableHeight/2-height/2; width:12; height:12; radius:6; color:"#eeeef4" }
        }
        Row { width:parent.width
            Text { width:parent.width/2; text:"1"; color:"#bdbdc5"; font.pixelSize:10 }
            Text { width:parent.width/2; horizontalAlignment:Text.AlignRight; text:"240"; color:"#bdbdc5"; font.pixelSize:10 }
        }
        Text { width:parent.width; text:"Recommended FPS\nPlay at 60 FPS for smooth gameplay. If you're running multiple instances, we recommend selecting 20 FPS. Some apps may need an App Player restart for changes to take effect."; wrapMode:Text.Wrap; color:"#c7c7ce"; font.pixelSize:10 }
    }
    Text { anchors.left:parent.left; anchors.bottom:parent.bottom; anchors.bottomMargin:3; text:"Some changes will apply on next launch"; color:"#c9c9d1"; font.pixelSize:11 }
    Button {
        anchors.right:parent.right; anchors.bottom:parent.bottom
        width:112; height:29; text:"Save changes"
        background: Rectangle { radius:3; border.width:1; border.color:"#adadb7"; gradient:Gradient { GradientStop {position:0;color:"#5b5b63"} GradientStop {position:1;color:"#303035"} } }
        contentItem: Text { text:parent.text; color:"#f3f3f6"; font.pixelSize:11; horizontalAlignment:Text.AlignHCenter; verticalAlignment:Text.AlignVCenter }
    }
}
