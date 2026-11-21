#pragma once
#include <QString>

// Authored native-contract scene, not extracted OEM code. It exercises both
// generations of visibility model and both actual owners of the menu glyph.
namespace TopbarUiFixture {
inline QString source(bool legacy, bool callback)
{
    QString qml = QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
Window {
    id: fixture
    width: 1068; height: 300; visible: true; color: "#101a27"
    property int barHeight: 48
    property int menuClicks: 0
    property int settingsClicks: 0
    property int menuVisibilityCallbacks: 0
    property bool onboarding: true
    property bool ready: false
    QtObject {
        id: plrBackend
        property string title: "MSI App Player"
        property string version: "5.15.4 test"
    }
    %MODELS%
    function forceNativeMenu() { iCfgBtn.visible = true }
    function forceNativeDetails() { iVersionImgLayout.visible = true }
    onWidthChanged: if (ready) fReloadTopbarElements()
    Component.onCompleted: { ready = true; fReloadTopbarElements() }
    Rectangle {
        id: bar; objectName: "playerTitleBar"
        width: parent.width; height: fixture.barHeight; color: "#1b2838"
        RowLayout {
            anchors.fill: parent; spacing: 0
            Rectangle { Layout.leftMargin: 12; width: 20; height: 20; color: "#54c8ff" }
            ColumnLayout {
                id: iTitleLayout
                Layout.leftMargin: 12; spacing: 0
                Text {
                    id: iBstTxt; objectName: "playerName"
                    text: plrBackend.title
                    color: "#f4f7fb"; font.pixelSize: 15
                }
                RowLayout {
                    id: iVersionImgLayout; objectName: "detailsRow"
                    Text { id: iVersionTxt; objectName: "versionLabel"; text: plrBackend.version; color: "#94a3b8"; font.pixelSize: 11 }
                    Text { id: iImageTxt; objectName: "instanceLabel"; text: "Nougat 32"; color: "#94a3b8"; font.pixelSize: 11 }
                }
            }
            Button { id: iBackBtn; objectName: "backButton"; text: "Back"; Layout.preferredHeight: 32 }
            Item { Layout.fillWidth: true }
            Button { id: iHelpBtn; text: "Help"; Layout.preferredHeight: 32 }
            %MENU%
            Button { id: iMinimizeBtn; objectName: "minimizeButton"; text: "_"; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
            Button { id: iCloseBtn; objectName: "closeButton"; text: "X"; Layout.preferredWidth: 32; Layout.preferredHeight: 32 }
        }
    }
    // The complete Settings area must survive both patch orders unchanged.
    Column {
        objectName: "settingsArea"
        x: 24; y: bar.height + 20; spacing: 12
        Text { objectName: "settingsHeading"; text: "Settings"; color: "#e7edf5"; font.pixelSize: 18 }
        Text { objectName: "settingsVersionText"; text: "Version and instance name"; color: "#e7edf5" }
        Button { objectName: "nativeSettingsButton"; text: "Settings"; onClicked: fixture.settingsClicks++ }
    }
}
)QML");
    const QString event = callback ? QStringLiteral("onVisibleChanged: { fixture.menuVisibilityCallbacks++ }\n") : QString();
    QString menu;
    if (legacy) {
        menu = QStringLiteral(R"QML(RowLayout {
                spacing: 0
                Button {
                    id: iCfgBtn; objectName: "topbarMenu"
                    text: "Menu"; Layout.preferredWidth: 48; Layout.preferredHeight: 32
                    %CALLBACK%
                    onClicked: fixture.menuClicks++
                }
                Rectangle { visible: fixture.onboarding && iCfgBtn.visible; width: 5; height: 5; color: "red" }
            })QML");
    } else {
        menu = QStringLiteral(R"QML(Item {
                id: iCfgBtn; objectName: "topbarMenu"
                Layout.preferredWidth: 48; Layout.preferredHeight: 32
                %CALLBACK%
                Button { objectName: "menuGlyph"; anchors.fill: parent; text: "Menu"; onClicked: fixture.menuClicks++ }
                Rectangle { visible: fixture.onboarding; anchors.right: parent.right; width: 5; height: 5; color: "red" }
            })QML");
    }
    menu.replace(QStringLiteral("%CALLBACK%"), event);
    QString models;
    if (legacy) {
        models = QStringLiteral(R"QML(ListModel { id: iTopbarItems }
    function fReloadTopbarElements() {
        iTopbarItems.clear()
        iTopbarItems.append({ "id": iCfgBtn, "visible": true, "width": iCfgBtn.width })
        iTopbarItems.append({ "id": iBackBtn, "visible": true, "width": iBackBtn.width })
        iTopbarItems.append({ "id": iHelpBtn, "visible": true, "width": iHelpBtn.width })
        for (var i = 0; i < iTopbarItems.count; ++i)
            iTopbarItems.get(i).id.visible = iTopbarItems.get(i).visible
    })QML");
    } else {
        models = QStringLiteral(R"QML(component PlrTopbarElement: QtObject {
        property bool pVisible: true
        property real pWidth: 0
        signal changeVisibility(bool visibility)
    }
    property list<QtObject> pNonFixedTopbarItems: [
        PlrTopbarElement { pVisible: true; pWidth: iCfgBtn.width; onChangeVisibility: function(visibility) { iCfgBtn.visible = visibility } },
        PlrTopbarElement { pVisible: true; pWidth: iBackBtn.width; onChangeVisibility: function(visibility) { iBackBtn.visible = visibility } },
        PlrTopbarElement { pVisible: true; pWidth: iHelpBtn.width; onChangeVisibility: function(visibility) { iHelpBtn.visible = visibility } }
    ]
    function fReloadTopbarElements() {
        for (var i = 0; i < pNonFixedTopbarItems.length; ++i)
            pNonFixedTopbarItems[i].changeVisibility(pNonFixedTopbarItems[i].pVisible)
    })QML");
    }
    qml.replace(QStringLiteral("%MODELS%"), models);
    qml.replace(QStringLiteral("%MENU%"), menu);
    return qml;
}
} // namespace TopbarUiFixture
