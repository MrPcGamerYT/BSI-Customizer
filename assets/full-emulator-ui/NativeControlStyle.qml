// Page-scoped passive visuals. Original controls, input areas, values and callbacks stay alive.
import QtQuick 2.15

Item {
    id: skin
    property var pages: null
    property var styled: new Set()
    property var styledTargets: new Set()
    property var watched: new Set()
    property int styledControls: 0
    property int visitedNodes: 0
    property int flushCount: 0
    property var pendingNodes: new Set()
    property bool scheduled: false
    function schedule(item) {
        if (!item) return
        pendingNodes.add(item)
        if (scheduled) return
        scheduled = true
        Qt.callLater(flush)
    }
    function flush() {
        scheduled = false
        var batch = Array.from(pendingNodes)
        pendingNodes.clear()
        flushCount++
        for (var i=0;i<batch.length;++i) {
            var item=batch[i]
            if (!item) continue
            if (!styled.has(item)) install(item,0)
            else installChildren(item,0)
        }
    }
    function installChildren(item,depth) {
        if (!item || !item.visible) return
        var children=item.children
        if (children) for (var i=0;i<children.length;++i) install(children[i],depth+1)
    }
    Component { id: childHook; Connections {
        property var owner
        property var cleanup
        Component.onDestruction: if (cleanup) cleanup()
        target: owner
        function onChildrenChanged() { skin.schedule(owner) }
        function onVisibleChanged() { if (owner && owner.visible) skin.schedule(owner) }
    } }
    property bool motionEnabled: Theme.motionEnabled
    function findSliderTrack(node,owner,depth) {
        if (!node || depth>12 || node===owner.handle || node===owner.background) return null
        if (node!==owner && node.color!==undefined && node.implicitHeight===owner.pSize.sliderHeight) return node
        if(node.children) for(var i=0;i<node.children.length;++i) {
            var found=findSliderTrack(node.children[i],owner,depth+1)
            if(found)return found
        }
        return null
    }
    function nativeSlider(item) {
        item.pSize=sliderSize.createObject(item)
        item.pColor=sliderColor.createObject(item)
        // UiSlider paints its real track INSIDE contentItem, not background.
        // Keep its real labels/ranges; never draw a second track over the caption.
        if(item.contentItem && item.contentItem.spacing!==undefined)item.contentItem.spacing=12
        var track=findSliderTrack(item.contentItem,item,0)
        if(track && item.handle) {
            item.handle.border.width=0
            item.handle.y=Qt.binding(function(){
                var position=0,node=track
                while(node && node!==item){position+=node.y;node=node.parent}
                return position+track.height/2-item.handle.height/2
            })
        }
    }
    function nativeSize(item,name) {
        if(item.pSize===undefined || !item.pSize)return
        var base=item.pSize, component=null
        if(/UiComboBox/.test(name))component=comboSize
        else if(/UiTextField/.test(name))component=fieldSize
        else if(/UiButton/.test(name))component=buttonSize
        else if(/UiToggleButton|UiCheckBox|UiRadioButton/.test(name))component=choiceSize
        if(component)item.pSize=component.createObject(item,{base:base})
    }
    function overlay(component, owner, target, kind) {
        if (!target || !target.parent || target.opacity === undefined) return
        if (styledTargets.has(target)) return
        styledTargets.add(target)
        var targets=styledTargets
        var visual = component.createObject(target.parent, { control: owner, targetVisual: target, visualKind: kind || "", cleanup:function(){targets.delete(target)} })
        if (visual) target.opacity = 0
    }
    function install(item, depth) {
        if (!item || (depth || 0) > 40 || styled.size >= 8192) return
        if (String(item.objectName || "").indexOf("voidReferenceVisual") === 0) return
        if (styled.has(item)) return
        if (!watched.has(item) && item.children !== undefined) {
            watched.add(item)
            var seen=styled,hooks=watched,queue=pendingNodes
            childHook.createObject(item,{owner:item,cleanup:function(){seen.delete(item);hooks.delete(item);queue.delete(item)}})
        }
        // Invisible pages get only a visibility hook; style them on first use.
        if (!item.visible) return
        styled.add(item)
        visitedNodes++
        var already = false
        var name = String(item)
        if (/UiSlider/.test(name) && item.pSelectedValueText!==undefined && item.pSize && item.pColor) {
            if(!already){nativeSlider(item);styledControls++}
            return
        }
        if (!already) {
        nativeSize(item,name)
        if (/CheckBox|RadioButton/.test(name) && item.indicator) {
            overlay(checkVisual,item,item.indicator,/Radio/.test(name)?"radio":"check"); styledControls++
        } else if (/UiToggleButton|Switch(_|\()/.test(name) && item.indicator) {
            overlay(toggleVisual, item, item.indicator, "switch"); styledControls++
        } else if (/Slider(_|\()|UiSlider/.test(name) && item.handle && item.background) {
            overlay(trackVisual, item, item.background, "track")
            overlay(thumbVisual, item, item.handle, "thumb"); styledControls++
        } else if (/ComboBox|UiComboBox/.test(name) && item.background) {
            overlay(buttonVisual, item, item.background, "combo")
            if (item.indicator) overlay(chevronVisual, item, item.indicator, "chevron")
            if (item.popup) popupHook.createObject(skin, {control:item})
            styledControls++
        } else if (/Button(_|\()|UiButton|ItemDelegate|TextField|SpinBox/.test(name) && item.background) {
            overlay(buttonVisual, item, item.background, "button"); styledControls++
        }
        // Text/panel palettes are edited in native sources, not assigned here.
        // Never replace a live validation/error-state color binding at runtime.
        }
        // Snapshot children before adding any more visuals; no idle scanning timer.
        installChildren(item,depth || 0)
    }
    function stylePopup(control) {
        if (control.popup && control.popup.background) overlay(buttonVisual, control, control.popup.background, "popup")
        if (control.popup && control.popup.contentItem) install(control.popup.contentItem, 0)
    }
    onPagesChanged: schedule(pages)
    Component.onCompleted: schedule(pages)
    // Instance-local visual sizes; never mutate shared UiTheme/boot components.
    Component { id:fieldSize; QtObject {
        property var base
        property int height:34
        property int horizontalPadding:9
        property font textFont:Qt.font({family:Theme.fontFamily,pixelSize:13})
    } }
    Component { id:comboSize; QtObject {
        property var base
        property int height:34
        property int indicatorWidth:28
        property int indicatorImageWidth:12
        property int indicatorImageHeight:16
        property string indicatorAsset:base.indicatorAsset
        property real maxDropDownHeight:base.maxDropDownHeight
        property QtObject textField:QtObject {
            property int height:34
            property int horizontalPadding:9
            property font textFont:Qt.font({family:Theme.fontFamily,pixelSize:13})
        }
    } }
    Component { id:buttonSize; QtObject {
        property var base
        property int height:34
        property int horizontalPadding:16
        property int spacing:8
        property int radius:Math.max(0,Theme.corner-3)
        property int iconWidth:base.iconWidth
        property int iconHeight:base.iconHeight
        property font textFont:Qt.font({family:Theme.fontFamily,pixelSize:13})
        property font subTextFont:Qt.font({family:Theme.fontFamily,pixelSize:13})
    } }
    Component { id:choiceSize; QtObject {
        property var base
        property int spacing:8
        property int iconWidth:base.iconWidth
        property int iconHeight:base.iconHeight
        property string iconAsset:base.iconAsset
        property font textFont:Qt.font({family:Theme.fontFamily,pixelSize:13})
    } }
    Component { id:sliderSize; QtObject {
        property int handleWidth:14
        property int handleHeight:14
        property int sliderHeight:3
        property font selectedValueFont:Qt.font({family:Theme.fontFamily,pixelSize:13})
        property font recommendedValueFont:Qt.font({family:Theme.fontFamily,pixelSize:13})
        property font rangeValueFont:Qt.font({family:Theme.fontFamily,pixelSize:13})
    } }
    Component { id:sliderColor; QtObject {
        property color handleBackground:Theme.text
        property color handleBackgroundDisabled:Theme.border
        property color handleBorder:Theme.text
        property color selectedValue:Theme.text
        property color recommendedValue:Theme.muted
        property color rangeValue:Theme.muted
        property color sliderBackground:Theme.border
        property color sliderBackgroundDisabled:Theme.border
        property color sliderFill:Theme.text
        property color sliderFillDisabled:Theme.border
    } }
    Component {
        id: popupHook
        Connections {
            property var control
            target: control ? control.popup : null
            function onOpened() { skin.stylePopup(control) }
        }
    }
    Component {
        id: buttonVisual
        Rectangle {
            property var control
            property var targetVisual
            property var cleanup
            Component.onDestruction: if (cleanup) cleanup()
            property string visualKind
            objectName: "voidReferenceVisualBackground"
            x: targetVisual.x; y: targetVisual.y; width: targetVisual.width; height: targetVisual.height
            z: targetVisual.z + 0.01; visible: targetVisual.visible
            radius: visualKind === "popup" ? Theme.corner : Math.max(0,Theme.corner-3)
            gradient: Gradient {
                GradientStop { position: 0; color: visualKind === "button" ? (control.down ? Theme.raised : control.hovered ? Theme.raised : Theme.surface) : Theme.surface }
                GradientStop { position: 1; color: visualKind === "button" ? Theme.raised : Theme.surface }
            }
            border.width: 1; border.color: control.activeFocus ? Theme.muted : control.hovered ? Theme.border : Theme.border
            opacity: control.enabled === false ? 0.5 : 1

            Behavior on border.color { enabled: skin.motionEnabled; ColorAnimation { duration: Theme.duration } }
        }
    }
    Component {
        id: checkVisual
        Rectangle {
            property var control
            property var targetVisual
            property var cleanup
            Component.onDestruction: if (cleanup) cleanup()
            property string visualKind
            objectName: "voidReferenceVisualCheck"
            x: targetVisual.x; y: targetVisual.y; width: targetVisual.width; height: targetVisual.height
            z: targetVisual.z+0.01; visible: targetVisual.visible
            radius: visualKind === "radio" ? height/2 : 3
            color: control.checked ? Theme.text : Theme.surface
            border.width:1; border.color:control.activeFocus || control.hovered ? Theme.text : Theme.border
            Text { anchors.centerIn:parent; text: visualKind === "radio" ? "●" : control.checkState === 1 ? "−" : "✓"; visible:control.checked || control.checkState === 1; color:Theme.surface; font.pixelSize:14 }
            opacity: control.enabled === false ? 0.5 : 1
        }
    }
    Component {
        id: toggleVisual
        Rectangle {
            property var control
            property var targetVisual
            property var cleanup
            Component.onDestruction: if (cleanup) cleanup()
            property string visualKind
            objectName: "voidReferenceVisualSwitch"
            x: targetVisual.x; y: targetVisual.y; width: targetVisual.width; height: targetVisual.height
            z: targetVisual.z + 0.01; visible: targetVisual.visible; radius: height / 2
            color: control.checked ? Theme.text : Theme.raised
            border.width: 1; border.color: control.hovered ? Theme.text : Theme.border
            Rectangle {
                width: Math.max(4,parent.height-4); height: width; radius: width/2; y: 2
                x: control.checked ? parent.width-width-2 : 2
                color: control.checked ? Theme.surface : Theme.muted
                Behavior on x { enabled: skin.motionEnabled; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            }
            Behavior on color { enabled: skin.motionEnabled; ColorAnimation { duration: 150 } }
        }
    }
    Component {
        id: trackVisual
        Item {
            property var control
            property var targetVisual
            property var cleanup
            Component.onDestruction: if (cleanup) cleanup()
            property string visualKind
            objectName: "voidReferenceVisualTrack"
            x: targetVisual.x; y: targetVisual.y; width: targetVisual.width; height: targetVisual.height
            z: targetVisual.z + 0.01; visible: targetVisual.visible
            Rectangle { width: parent.width; height: 2; anchors.verticalCenter: parent.verticalCenter; radius: 1; color: Theme.muted }
            Rectangle { width: parent.width * control.visualPosition; height: 2; anchors.verticalCenter: parent.verticalCenter; radius: 1; color: Theme.text }
        }
    }
    Component {
        id: thumbVisual
        Item {
            property var control
            property var targetVisual
            property var cleanup
            Component.onDestruction: if (cleanup) cleanup()
            property string visualKind
            objectName: "voidReferenceVisualThumb"
            x: targetVisual.x; y: targetVisual.y; width: targetVisual.width; height: targetVisual.height
            z: targetVisual.z + 0.01; visible: targetVisual.visible
            Rectangle { width: Math.min(12,parent.width); height: width; radius: width/2; anchors.centerIn: parent
                color: control.pressed ? Theme.text : Theme.text
                scale: control.pressed ? 1.08 : 1
                Behavior on scale { enabled: skin.motionEnabled; NumberAnimation { duration: 120 } }
            }
        }
    }
    Component {
        id: chevronVisual
        Item {
            property var control
            property var targetVisual
            property var cleanup
            Component.onDestruction: if (cleanup) cleanup()
            property string visualKind
            objectName: "voidReferenceVisualChevron"
            x: targetVisual.x; y: targetVisual.y; width: targetVisual.width; height: targetVisual.height
            z: targetVisual.z + 0.01; visible: targetVisual.visible
            Canvas {
                anchors.centerIn: parent; width:14; height:20
                onPaint: {
                    var c=getContext("2d");c.reset();c.strokeStyle=Theme.border;c.lineWidth=1.5
                    c.beginPath();c.moveTo(3,7);c.lineTo(7,3);c.lineTo(11,7)
                    c.moveTo(3,13);c.lineTo(7,17);c.lineTo(11,13);c.stroke()
                }
            }
        }
    }
}
