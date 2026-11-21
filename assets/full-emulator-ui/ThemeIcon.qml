import QtQuick 2.15

Item {
    id: icon
    property url source
    property color tint: Theme.text
    property size sourceSize: Qt.size(64,64)
    property int fillMode: Image.PreserveAspectFit
    property int status: Image.Null
    function update() {
        status=source.toString().length ? Image.Loading : Image.Null
        if(source.toString().length)canvas.loadImage(source)
        canvas.requestPaint()
    }
    onSourceChanged: update()
    onTintChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    Component.onCompleted: update()
    Canvas {
        id: canvas
        anchors.fill: parent
        canvasSize: Qt.size(Math.max(1,width*2),Math.max(1,height*2))
        onImageLoaded: {icon.status=Image.Ready;requestPaint()}
        onPaint: {
            var ctx=getContext("2d");ctx.reset()
            if(!icon.source.toString().length || !isImageLoaded(icon.source))return
            ctx.drawImage(icon.source,0,0,canvasSize.width,canvasSize.height)
            ctx.globalCompositeOperation="source-in"
            ctx.fillStyle=icon.tint;ctx.fillRect(0,0,canvasSize.width,canvasSize.height)
            ctx.globalCompositeOperation="source-over"
            icon.status=Image.Ready
        }
    }
}
