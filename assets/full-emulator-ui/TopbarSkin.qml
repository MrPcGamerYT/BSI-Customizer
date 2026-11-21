// Passive titlebar glyphs: native UiImageButton retains input, tooltips and actions.
import QtQuick 2.15
import QtQuick.Window 2.15
import "Icons.js" as Icons
Item {
    id: skin
    objectName:"voidReferenceTopbarSkin"
    property var host
    property var watched:new Set()
    property var pending:new Set()
    property bool scheduled:false
    property int visitedNodes:0
    function enqueue(item) {
        if (!item) return
        pending.add(item)
        if(scheduled)return
        scheduled=true;Qt.callLater(flush)
    }
    function flush() {
        scheduled=false;var nodes=Array.from(pending);pending.clear()
        for(var i=0;i<nodes.length;++i) {
            var node=nodes[i]
            if(!node)continue
            if(!watched.has(node))walk(node,0)
            else if(node.children)for(var j=0;j<node.children.length;++j)walk(node.children[j],1)
        }
    }
    function walk(item,depth) {
        if(!item || depth>32 || watched.size>4096 || watched.has(item))return
        if(String(item.objectName||"").indexOf("voidReference")===0)return
        watched.add(item);visitedNodes++
        if(item.children!==undefined) {
            var seen=watched,queue=pending
            hook.createObject(item,{owner:item,cleanup:function(){seen.delete(item);queue.delete(item)}})
        }
        if(/UiImageButton/.test(String(item)) && item.contentItem && item.asset!==undefined)
            face.createObject(item,{control:item})
        if(item.children)for(var i=0;i<item.children.length;++i)walk(item.children[i],depth+1)
    }
    function icon(asset) {
        var key=String(asset||"").toLowerCase()
        var aliases={titlebarclose:"close",titlebarminimize:"minimize",titlebarmaximize:"maximize",titlebarrestore:"restore",titlebarcollapse:"collapse",titlebarexpand:"expand",titlebarmenu:"menu",titlebarhelp:"help",titlebardisablepintop:"lock",sidebarback:"back",sidebarhome:"home",sidebarrecents:"recent"}
        return aliases[key]||""
    }
    Component { id:hook; Connections {
        property var owner
        property var cleanup
        Component.onDestruction: if (cleanup) cleanup()
        target:owner
        function onChildrenChanged(){skin.enqueue(owner)}
    } }
    property bool initialized:false
    onHostChanged:if(initialized)walk(host,0)
    Component.onCompleted:{initialized=true;Theme.sync();walk(host,0)}
    Connections {
        target:skin.host ? skin.host.Window.window : null
        function onActiveChanged(){if(target && target.active)Theme.sync()}
    }
    Component { id:face
        Item {
            id:visual
            objectName:"voidReferenceTopbarFace"
            property var control
            readonly property string key:skin.icon(control.asset)
            readonly property bool ready:key!=="" && glyph.status!==Image.Error
            anchors.fill:parent;z:5
            Rectangle { anchors.fill:parent;anchors.margins:3;radius:9;color:control.down?Theme.raised:Theme.surface;visible:visual.ready && control.enabled && (control.hovered||control.down);border.width:1;border.color:Theme.border }
            ThemeIcon { id:glyph;tint:control.hovered||control.down?Theme.accent:Theme.text;anchors.centerIn:parent;width:16;height:16;sourceSize.width:32;sourceSize.height:32;source:visual.key?"SharedIcons/"+visual.key+".png":"";opacity:!control.enabled?0.3:control.hovered||control.down?1:0.75 }
            function updateNative() {
                control.contentItem.opacity=Qt.binding(function(){return visual.ready?0:1})
                var children=control.children
                for(var i=0;i<children.length;++i)if(children[i]!==visual && /BsiChromeFace|BsiTopbar/.test(String(children[i])))children[i].visible=Qt.binding(function(){return !visual.ready})
            }
            onReadyChanged:updateNative()
            Component.onCompleted:updateNative()
        }
    }
}
