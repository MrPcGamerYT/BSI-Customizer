import QtQuick
import QtQuick.Layouts

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
    onLoaded: visited = true
    Connections {
        target: feature.item
        ignoreUnknownSignals: true
        function onNotify(message) { feature.notify(message); }
        function onNavigate(index) { feature.navigate(index); }
    }
}
