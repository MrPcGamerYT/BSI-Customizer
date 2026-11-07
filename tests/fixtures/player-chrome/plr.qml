import QtQuick 2.15
import QtQuick.Controls 2.15
ApplicationWindow {property bool pBootVideoDone:false;Timer{id:bvDone;interval:3000;running:pIsBootComplete&&!pBootVideoDone;repeat:false;onTriggered:pBootVideoDone=true}
    id: iAppwin
    width: 960
    height: 540
    flags: Qt.Window | Qt.FramelessWindowHint
    color: uiBackend.oem === "msi5" ? UiTheme.colors.overLayTint : UiTheme.colors.primary60
    property int pBorderWidth: 1
    property int pSidebarWidth: 32
    property bool pIsBootComplete: false
    onClosing: function(close) {
        close.accepted = false
        iTopbar.fShowClosePlrMsg()
    }
    Component {
        id: popupWithSameId
        Rectangle {
            id: iMainContentLayout
            color: "#456789"
        }
    }
    Rectangle {
        id: iMainContentLayout
        anchors.fill: parent
        anchors.margins: iAppwin.visibility == ApplicationWindow.FullScreen ? 0 : pBorderWidth
        color: uiBackend.oem === "msi5" ? UiTheme.colors.primary80 :"#000000"
        Item {
            anchors.right: iSidebar.visible ? iSidebar.left : parent.right
            anchors.top: iTopbar.bottom
            anchors.bottom: parent.bottom
            Loader { sourceComponent:((pIsBootComplete&&pBootVideoDone)||pShowAndroidAd)?iGlContainerComponent:iBootProgressComponent }
        }
        PlrTopbar {
            id: iTopbar
            height: 32
            color: uiBackend.oem === "msi5" ? UiTheme.colors.primary70 : UiTheme.colors.primary80
            onSidebarBtnClicked: {
                iSidebar.visible = true
                fSetUiParams(true)
            }
        }
        PlrSidebar {
            id: iSidebar
            anchors.top: iTopbar.bottom
            anchors.right: parent.right
            color: uiBackend.oem === "msi5" ? UiTheme.colors.primary70 : UiTheme.colors.primary80
            width: pSidebarWidth
            visible: uiBackend.confGetInstanceBool("show_sidebar")
        }
    }
    function fSetUiParams(sidebarVisible) {
        plrBackend.setBackendUiParams(pBorderWidth, iTopbar.height+pBorderWidth, pBorderWidth+(sidebarVisible?pSidebarWidth:0), pBorderWidth, 7*iTopbar.height+50+pBorderWidth*2)
    }
}
