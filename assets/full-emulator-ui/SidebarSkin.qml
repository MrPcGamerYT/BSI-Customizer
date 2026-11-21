// Passive skin of real sidebar buttons. No replacement input/shortcut handlers.
import QtQuick 2.15
import QtQuick.Window 2.15
Item {
    id: skin
    property var host
    property var controls: new Set()
    property var watched: new Set()
    property var pendingNodes: new Set()
    property bool scheduled: false
    property int visitedNodes: 0
    property bool initialized: false
    function enqueue(item) {
        if (!item) return
        pendingNodes.add(item)
        if (scheduled) return
        scheduled=true
        Qt.callLater(flush)
    }
    function flush() {
        scheduled=false
        var nodes=Array.from(pendingNodes);pendingNodes.clear()
        for (var i=0;i<nodes.length;++i) {
            var item=nodes[i]
            if (!item) continue
            if (!watched.has(item)) walk(item,0)
            else if(item.children) for(var j=0;j<item.children.length;++j) walk(item.children[j],1)
        }
    }
    Component { id: watcher; Connections {
        property var owner
        property var cleanup
        Component.onDestruction: if (cleanup) cleanup()
        target: owner
        function onChildrenChanged() { skin.enqueue(owner) }
    } }
    function walk(item, depth) {
        if (!item || depth > 32 || controls.size > 512 || watched.size > 4096) return
        if (String(item.objectName || "").indexOf("voidReference") === 0) return
        if (watched.has(item)) return
        watched.add(item);visitedNodes++
        if (item.children !== undefined) {
            var seen=watched,queue=pendingNodes,buttons=controls
            watcher.createObject(item,{owner:item,cleanup:function(){seen.delete(item);queue.delete(item);buttons.delete(item)}})
        }
        if (/UiImageButton/.test(String(item)) && item.contentItem && item.pVoidElementId !== undefined) {
            if (!controls.has(item)) {
                controls.add(item)
                var inline=null
                if(item.children)for(var c=0;c<item.children.length;++c)
                    if(/BsiSidebarInlineVisual/.test(String(item.children[c])))inline=item.children[c]
                // The direct inline bridge already owns these visuals. Do not
                // compete with its asset guard or create a second icon face.
                if(!inline)visual.createObject(item,{control:item})
            }
        }
        if (item.children) for (var j=0;j<item.children.length;++j) walk(item.children[j],depth+1)
    }
    function refresh() {
        enqueue(host)
        if (host && host.pKebabMenuWin) enqueue(host.pKebabMenuWin.contentItem)
    }
    onHostChanged: { if(initialized){Theme.sync();walk(host,0)}; SurfaceRegistry.attach(skin, host ? host.Window.window : null) }
    Component.onDestruction: SurfaceRegistry.detach(skin)
    Component.onCompleted: { initialized=true;Theme.sync();walk(host,0);refresh() }
    Connections {
        target:skin.host ? skin.host.Window.window : null
        function onActiveChanged(){if(target && target.active)Theme.sync()}
        function onVisibleChanged(){if(target && target.visible){Theme.sync();skin.refresh()}}
    }
    Connections {
        target: skin.host
        function onPKebabMenuWinChanged() { skin.refresh() }
    }
    Component {
        id: visual
        SidebarVisual {
            id: face
            property var control
            objectName: "voidReferenceSidebarFace"
            anchors.fill: parent; z: 3
            elementId: control.pVoidElementId || ""
            assetName: control.asset
            hovered: control.hovered
            pressed: control.down
            available: control.enabled
            animating: control.pImageAnimationRunning
            active: control.checked === true || /Active/.test(String(control.asset))
            function updateNative() {
                if (!control || !control.contentItem) return
                control.contentItem.opacity = Qt.binding(function() { return face.known ? 0 : 1 })
                // Old passive adapter layers are not input owners. Avoid double icons.
                var children = control.children
                if (children) for (var i=0;i<children.length;++i) {
                    var child=children[i]
                    if (child!==face && /BsiSidebarInlineVisual/.test(String(child)))
                        child.visible=Qt.binding(function(){return !face.known})
                }
            }
            onKnownChanged: updateNative()
            Component.onCompleted: updateNative()
        }
    }
}
