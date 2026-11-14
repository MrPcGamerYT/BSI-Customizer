import QtQuick
import QtQuick.Layouts
import BSICustomizer

Loader {
    id: feature
    required property int pageIndex
    required property int currentPage
    required property bool authorized
    property bool visited: false
    property url pageSource
    signal notify(string message)
    signal navigate(int index)
    Layout.fillWidth: true; Layout.fillHeight: true
    // First use only; retain each editor's unsaved state after navigation.
    active: authorized && (currentPage === pageIndex || visited)
    asynchronous: true
    source: active ? pageSource : ""
    property var dynamicItem: item
    function preview() {
        if(dynamicItem && typeof dynamicItem.previewSelected === "function") dynamicItem.previewSelected();
    }
    onAuthorizedChanged: if (!authorized) visited = false
    function reveal() {
        entry.stop();
        opacity = 1;
        if (!Theme.reduceMotion && visible && status === Loader.Ready) entry.start();
    }
    onVisibleChanged: if (visible) reveal()
    onLoaded: { visited = true; reveal(); }
    NumberAnimation { id: entry; target: feature; property: "opacity"; from: 0; to: 1; duration: Theme.revealDuration; easing.type: Easing.OutCubic }
    Connections {
        target: Theme
        function onReduceMotionChanged() { if (Theme.reduceMotion) { entry.stop(); feature.opacity = 1; } }
    }
    Connections {
        target: feature.item
        ignoreUnknownSignals: true
        function onNotify(message) { feature.notify(message); }
        function onNavigate(index) { feature.navigate(index); }
    }
}
