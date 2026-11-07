// Native-layout fixture copied from the supplied rendering structure; backend methods are doubles.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
Rectangle {
    id: iRoot
    objectName: "topbar"
    signal sidebarBtnClicked()
    signal sidebarHideClicked()
    signal hamburgerSettingClicked()
    property int pIconSize: 32
    property int menuRequests: 0
    property int closeRequests: 0
    property int pFixedElementsWidth: iMinimizeBtn.width + iFixedResizeBtn.width + iCloseBtn.width + (iSidebarShowBtn.visible ? iSidebarShowBtn.width : 0)
    component PlrTopbarElement: QtObject {
        property bool pVisible: true
        property int pWidth: 0
        signal changeVisibility(var visibility)
    }
    property list<QtObject> pNonFixedTopbarItems: [
        PlrTopbarElement {
            pVisible: true
            pWidth: iBackBtn.width
            onChangeVisibility: function(visibility) { iBackBtn.visible = visibility }
        },
        PlrTopbarElement {
            pVisible: false /* BSI CUSTOMIZER: clean player UI visible:dHJ1ZQ== */
            pWidth: iCfgBtn.width
            onChangeVisibility: function(visibility) {
                iCfgBtn.visible = visibility
            }
        }
    ]
    MouseArea {
        objectName: "nativeDragArea"
        anchors.fill: parent
        property var startingPosition
        property bool isPressed: false
        onPressed: function(mouse) {
            isPressed = true
            startingPosition = Qt.point(mouse.x, mouse.y)
        }
        onReleased: {
            isPressed = false
            if (iAppwin.y < Screen.virtualY) iAppwin.y = Screen.virtualY
        }
        onPositionChanged: function(mouse) {
            if (isPressed && iAppwin.visibility !== ApplicationWindow.Maximized) {
                iAppwin.x = mouseProvider.cursorPos().x - startingPosition.x - adsBackend.pExtendedBannerAdWidth
                iAppwin.y = mouseProvider.cursorPos().y - startingPosition.y
            }
        }
        onDoubleClicked: {
            iAppwin.visibility === ApplicationWindow.Windowed ? plrBackend.onMaximizeClicked() : plrBackend.onRestoreClicked()
        }
    }
    UiRowLayout {
        id: iTopbarRow
        anchors.fill: parent
        spacing: 0
        UiImage { id:iProductLogo; asset:"BlueStacksLogo"; pImageWidth:20; pImageHeight:20; Layout.leftMargin:10 }
        UiColumnLayout {
            Text {
                id: iBstTxt
                text: plrBackend.title
                color: "#aaaacc"
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.preferredWidth: 160
            }
        }
        UiImageButton {
            id: iBackBtn
            objectName: "native_back"
            asset: "SidebarBack"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            enabled: iAppwin.pIsBootComplete || iAppwin.pShowAndroidAd
            onClicked: {
                plrBackend.logUIAction("Topbar", "Back")
                plrBackend.onBackClicked()
            }
        }
        UiImageButton {
            id: iHomeBtn
            objectName: "native_home"
            asset: "SidebarHome"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            onClicked: { plrBackend.onHomeClicked() }
        }
        UiImageButton {
            id: iRecentBtn
            objectName: "native_recent"
            asset: "SidebarRecents"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            onClicked: { plrBackend.onRecentAppsClicked() }
        }
        Item { Layout.fillWidth: true }
        UiImageButton {
            id: iHelpBtn
            visible: false // BSI CUSTOMIZER: clean player UI
            asset: "TitlebarHelp"
            onClicked: { plrBackend.logUIAction("Topbar", "Help") }
        }
        Item {
            visible: false // BSI CUSTOMIZER: clean player UI
            id: iCfgBtn
            Layout.preferredWidth: pIconSize
            Layout.preferredHeight: pIconSize
            UiImageButton {
                objectName: "native_menu"
                asset: "TitlebarMenu"
                pImageWidth: pIconSize
                pImageHeight: pIconSize
                onPressed: {
                    plrBackend.logUIAction("Topbar", "HamburgerMenu")
                    fOpenHamburgerPopup()
                }
            }
        }
        UiImageButton {
            id: iMinimizeBtn
            objectName: "native_minimize"
            asset: "TitlebarMinimize"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            onClicked: {
                plrBackend.logUIAction("Topbar", "Minimize")
                iAppwin.showMinimized()
            }
        }
        UiImageButton {
            id: iFixedResizeBtn
            objectName: "native_resize"
            asset: iAppwin.visibility === ApplicationWindow.Windowed ? "TitlebarMaximize" : "TitlebarRestore"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            onClicked: {
                if (iAppwin.visibility === ApplicationWindow.Windowed) {
                    plrBackend.logUIAction("Topbar", "Maximize")
                    plrBackend.onMaximizeClicked()
                } else {
                    plrBackend.logUIAction("Topbar", "Restore")
                    plrBackend.onRestoreClicked()
                }
            }
        }
        UiImageButton {
            id:iCloseBtn
            objectName: "native_close"
            asset: "TitlebarClose"
            pImageWidth: pIconSize
            pImageHeight: pIconSize
            onClicked: {
                plrBackend.logUIAction("Topbar", "Close")
                fShowClosePlrMsg()
            }
        }
        UiImageButton {
            visible: false // BSI CUSTOMIZER: clean player UI
            id: iSidebarShowBtn
            objectName: "native_sidebar"
            asset: iSidebar.visible ? "TitlebarCollapse" : "TitlebarExpand"
            pImageHeight: iAppwin.pSidebarWidth
            pImageWidth: iAppwin.pSidebarWidth
            onClicked: {
                if (iSidebar.visible) {
                    plrBackend.logUIAction("Sidebar", "CollapseSidebar")
                    sidebarHideClicked()
                } else {
                    plrBackend.logUIAction("Topbar", "ExpandSidebar")
                    sidebarBtnClicked()
                }
            }
        }
    }
    Component.onCompleted: fReloadTopbarElements()
    function fReloadTopbarElements() {
        for(var i=0;i<pNonFixedTopbarItems.length;i++)pNonFixedTopbarItems[i].changeVisibility(pNonFixedTopbarItems[i].pVisible)
    }
    function fOpenHamburgerPopup() { menuRequests++ }
    function fShowClosePlrMsg() { closeRequests++ }
}
