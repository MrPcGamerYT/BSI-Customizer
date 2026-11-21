import QtQuick 2.15

Item {
    id: icon
    property url source
    property color tint: Theme.text
    property size sourceSize: Qt.size(64,64)
    property int fillMode: Image.PreserveAspectFit
    readonly property int status: mask.status
    function update() {
        if(!canvas || !canvas.available)return
        if(source.toString().length) {
            canvas.loadImage(source)
        }
        canvas.requestPaint()
    }
    onSourceChanged: update()
    onTintChanged: if(canvas)canvas.requestPaint()
    onWidthChanged: if(canvas)canvas.requestPaint()
    onHeightChanged: if(canvas)canvas.requestPaint()
    Component.onCompleted: update()
    // Synchronous PNG decode supplies deterministic known/error state before
    // first paint. The Canvas only tints; it never decides native visibility.
    Image {
        id:mask
        visible:false
        source:icon.source
        sourceSize:icon.sourceSize
        asynchronous:false
        cache:true
        onStatusChanged:if(status===Image.Ready)icon.update()
    }
    Canvas {
        id: canvas
        anchors.fill: parent
        // Keep logical canvas and viewport equal: a larger canvas clips icons.
        canvasSize: Qt.size(Math.max(1,width),Math.max(1,height))
        onAvailableChanged: if(available)icon.update()
        onImageLoaded: requestPaint()
        onPaint: {
            var ctx=getContext("2d");ctx.reset()
            if(!icon.source.toString().length || !isImageLoaded(icon.source))return
            ctx.drawImage(icon.source,0,0,canvasSize.width,canvasSize.height)
            ctx.globalCompositeOperation="source-in"
            ctx.fillStyle=icon.tint;ctx.fillRect(0,0,canvasSize.width,canvasSize.height)
            ctx.globalCompositeOperation="source-over"
        }
    }
}
