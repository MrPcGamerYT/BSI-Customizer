import QtQuick
import BSICustomizer
Canvas {
    id: backdrop
    // Painted only when resized/theme changed. No blur passes or idle animations.
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    Connections { target: Theme; function onDarkChanged() { backdrop.requestPaint(); } }
    onPaint: {
        var c = getContext("2d"); c.clearRect(0,0,width,height);
        if (!Theme.dark) return;
        var r = Math.max(width,height) * 0.65;
        var a = c.createRadialGradient(width,0,0,width,0,r);
        a.addColorStop(0,"rgba(124,92,255,0.08)"); a.addColorStop(1,"rgba(124,92,255,0)");
        c.fillStyle=a; c.fillRect(0,0,width,height);
        var b=c.createRadialGradient(0,height,0,0,height,r);
        b.addColorStop(0,"rgba(124,92,255,0.08)"); b.addColorStop(1,"rgba(124,92,255,0)");
        c.fillStyle=b; c.fillRect(0,0,width,height);
    }
}
