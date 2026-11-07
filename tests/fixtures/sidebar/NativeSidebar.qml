import QtQuick 2.15
Rectangle {
    id: iSidebar
    objectName: "sidebar"
    width: pSidebarElementSize; height: 460
    color: "#251a30"
    property int pSidebarElementSize: 32
    property bool pIsMuted: false
    property bool trimEnabled: false
    property bool trimAnimating: false
    property int clickedCount: 0
    property int releasedCount: 0
    property int lastButton: 0
    property int lastModifiers: 0
    property string lastElement: ""
    property int navigationCount: 0
    property list<QtObject> pAllSidebarItems: [
        QtObject { property string pElementId:"fullscreen"; property string pAsset:"SidebarFullScreen"; property bool pIsEnable:true; property bool pIsFeatureEnable:true; property bool pRunImageAnimation:false },
        QtObject { property string pElementId:"volume"; property string pAsset:iSidebar.pIsMuted ? "SidebarMute" : "SidebarVolume"; property bool pIsEnable:true; property bool pIsFeatureEnable:true; property bool pRunImageAnimation:false },
        QtObject { property string pElementId:"gameControl"; property string pAsset:"SidebarGameControlsActive"; property bool pIsEnable:true; property bool pIsFeatureEnable:true; property bool pRunImageAnimation:false },
        QtObject { property string pElementId:"memoryTrim"; property string pAsset:"SidebarMemoryTrim"; property bool pIsEnable:iSidebar.trimEnabled; property bool pIsFeatureEnable:true; property bool pRunImageAnimation:iSidebar.trimAnimating },
        QtObject { property string pElementId:"installApk"; property string pAsset:"SidebarInstallApk"; property bool pIsEnable:true; property bool pIsFeatureEnable:true; property bool pRunImageAnimation:false },
        QtObject { property string pElementId:"unknownNativeTool"; property string pAsset:"OemNativeTool"; property bool pIsEnable:true; property bool pIsFeatureEnable:true; property bool pRunImageAnimation:false },
        // BSI CUSTOMIZER sidebar clean -- must never be reenabled by the skin.
        QtObject { property string pElementId:"screenshot"; property string pAsset:"SidebarScreenshot"; property bool pIsEnable:true; property bool pIsFeatureEnable:false; property bool pRunImageAnimation:false }
    ]
    property var pSidebarItems: Array.prototype.filter.call(pAllSidebarItems, function(entry) { return entry.pIsFeatureEnable; })
    function clicked(element, event) {
        clickedCount++;
        lastElement=element;
        lastButton=event.button;
        lastModifiers=event.modifiers;
    }
    function mouseReleased(element, event) { releasedCount++; }
    function fResetSidebar() { return pSidebarElementSize * 2; }
    function fGetPopupTopPoint() { return mapToGlobal(0, pSidebarElementSize); }
    function regexGuard() { return /[{}"/]/.test("}") && 32 / 2 === 16; }

    UiColumnLayout {
        anchors.fill: parent
        Repeater {
            model: pSidebarItems
            delegate: Rectangle {
                y: index * 32
                width: 32; height: 32; color: "transparent"
                UiImageButton {
                    id: iBtn
                    objectName: "native_" + model.modelData.pElementId
                    width:32; height:32
                    asset: model.modelData.pAsset
                    enabled: model.modelData.pIsEnable
                    property bool pImageAnimationRunning: model.modelData.pRunImageAnimation
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: function(mouse) { iSidebar.clicked(model.modelData.pElementId, mouse); }
                        onReleased: function(mouse) { iSidebar.mouseReleased(model.modelData.pElementId, mouse); }
                        onPositionChanged: function(mouse) { /* Native popup/backend notification kept. */ }
                    }
                }
                Rectangle {
                    objectName: "nativeBusyBadge"
                    visible: model.modelData.pRunImageAnimation
                    width:5; height:5; radius:2
                    anchors.right:parent.right; anchors.bottom:parent.bottom; color:"#d8c3f3"
                }
            }
        }
        UiImageButton {
            id: iKebabMenuBtn
            objectName: "native_more"
            y: 204; width:32; height:32
            asset: iSidebar.clickedCount > 0 ? "SidebarKebabActive" : "SidebarKebab"
            MouseArea { anchors.fill:parent
                onPressed: function(mouse) { iSidebar.clicked("more", mouse); }
                onReleased: function(mouse) { iSidebar.mouseReleased("more", mouse); }
            }
        }
        Item {
            id: iSettingsBtn
            x:0; y:parent.height-128; width:32; height:32
            UiImageButton {
                objectName: "native_settings"
                width:32; height:32
                asset: "SidebarSettings"
                MouseArea { anchors.fill:parent
                    onPressed: function(mouse) { iSidebar.clicked("settings", mouse); }
                    onReleased: function(mouse) { iSidebar.mouseReleased("settings", mouse); }
                }
            }
        }
        UiImageButton {
            id: iBackBtn
            objectName: "native_back"
            y:parent.height-96; width:32; height:32
            asset: "NavBarBack"
            MouseArea { anchors.fill:parent; onPressed: navigationCount++; onReleased: releasedCount++ }
        }
        UiImageButton {
            id: iHomeBtn
            objectName: "native_home"
            y:parent.height-64; width:32; height:32
            asset: "NavBarHome"
            MouseArea { anchors.fill:parent; onPressed: navigationCount++; onReleased: releasedCount++ }
        }
        UiImageButton {
            id: iRecentBtn
            objectName: "native_recent"
            y:parent.height-32; width:32; height:32
            asset: "NavBarRecent"
            MouseArea { anchors.fill:parent; onPressed: navigationCount++; onReleased: releasedCount++ }
        }
    }
    // A popup's native visuals are deliberately OUTSIDE the restyled main column.
    Component {
        id: nativePopup
        UiColumnLayout {
            UiImageButton { id: iPopupButton; asset: "PopupNativeAsset" }
        }
    }
}
