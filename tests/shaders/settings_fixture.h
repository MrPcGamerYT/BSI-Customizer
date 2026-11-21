#pragma once
#include <QHash>
#include <QString>
#include <QStringList>

namespace ShaderSettingsFixture {
// Namespace-only fixtures. The actual control implementations/theme are tested
// separately from read-only, hash-pinned player resources, never copied here.
inline const QStringList nativeControls{
    QStringLiteral("UiThemeObject.qml"), QStringLiteral("UiButton.qml"),
    QStringLiteral("UiColumnLayout.qml"), QStringLiteral("UiRowLayout.qml"),
    QStringLiteral("UiCheckBox.qml"), QStringLiteral("UiComboBox.qml"),
    QStringLiteral("UiSlider.qml"), QStringLiteral("UiScrollView.qml"),
    QStringLiteral("UiScrollBar.qml"), QStringLiteral("UiImage.qml"),
    QStringLiteral("UiTextField.qml"), QStringLiteral("UiMessageDialog.qml"),
    QStringLiteral("UiDialog.qml"), QStringLiteral("UiShadowContainer.qml"),
    QStringLiteral("UiDropShadowEffect.qml"), QStringLiteral("UiImageButton.qml")
};
inline const QStringList pages{
    QStringLiteral("UiPerformanceSetting.qml"), QStringLiteral("UiDisplaySetting.qml"),
    QStringLiteral("UiGraphicsSetting.qml"), QStringLiteral("PlrGameSetting.qml"),
    QStringLiteral("PlrAudioSetting.qml"), QStringLiteral("PlrGamepadSetting.qml"),
    QStringLiteral("PlrPreferencesSetting.qml"), QStringLiteral("PlrDeviceSetting.qml"),
    QStringLiteral("PlrShortcutsSetting.qml"), QStringLiteral("PlrUserDataSetting.qml"),
    QStringLiteral("PlrAdvancedSetting.qml"), QStringLiteral("PlrAboutSetting.qml")
};
inline const QStringList labels{
    QStringLiteral("Performance"), QStringLiteral("Display"), QStringLiteral("Graphics"),
    QStringLiteral("Game settings"), QStringLiteral("Devices"), QStringLiteral("Gamepad"),
    QStringLiteral("Preferences"), QStringLiteral("Phone"), QStringLiteral("Shortcuts"),
    QStringLiteral("User data"), QStringLiteral("Advanced"), QStringLiteral("About")
};
inline QHash<QString, int> pageCounts()
{
    QHash<QString, int> out;
    for (const auto &page : pages) out.insert(page.toCaseFolded(), 1);
    return out;
}
inline QString player(bool list = false, bool persistentModel = false)
{
    QString source = QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import BsiNativeFixture 1.0
Window {
id:iAppwin
objectName:'nativeShaderHost'
width:1440;height:900;visible:true
property QtObject pSettingsWindow:null
property bool pCompLoaded:false
Component.onCompleted:{pCompLoaded=true}
Rectangle{width:40;height:40;color:'#123456'
MouseArea{objectName:'nativeSettingsButton';anchors.fill:parent;onClicked:fOpenSettingsWindow()}}
)QML");
    source += persistentModel ? QStringLiteral("property SettingElementModelList pSettingsItems:SettingElementModelList{\n")
                              : QStringLiteral("Component{id:iSettingsItems;SettingElementModelList{\n");
    if (list) source += QStringLiteral("pSettingElements:[\n");
    for (int i = 0; i < pages.size(); ++i) {
        source += QStringLiteral("SettingElementModel{pButtonText:'%1';pQmlSource:'%2';pComponentArgs:{'pParentWindow':'plr'}}")
                      .arg(labels.at(i), pages.at(i));
        source += list && i + 1 < pages.size() ? QStringLiteral(",\n") : QStringLiteral("\n");
    }
    if (list) source += QStringLiteral("]\n");
    source += persistentModel ? QStringLiteral("}\n") : QStringLiteral("}}\n");
    source += QStringLiteral(R"QML(function fOpenSettingsWindow(){
if(pSettingsWindow!==null)return
var component=Qt.createComponent('UiSettingsWindow.qml')
if(component.status!==Component.Ready)throw new Error(component.errorString())
pSettingsWindow=component.createObject(iAppwin,{'pParentWindow':'plr','pSettingsItems':iSettingsItems.createObject()})
if(pSettingsWindow===null)throw new Error(component.errorString())
pSettingsWindow.show()
}
}
)QML");
    if (persistentModel) source.replace(QStringLiteral("iSettingsItems.createObject()"), QStringLiteral("pSettingsItems"));
    return source;
}
inline QString window()
{
    return QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Window 2.15
Window {
id:iSettingsWindow
function fChangeSelectedSetting(index){if(uiBackend.confIsModified())prompt()}
onClosing:{if(uiBackend.confIsModified())prompt()}
function discardClose(){uiBackend.confDiscard()}
Item{//BSI v17
property var bIcons:({})
Item{property var c:iSettingsWindowShadowContainer.bIcons[index]}
Repeater{model:pSettingsItems.pSettingElements;delegate:Loader{Component.onCompleted:setSource(modelData.pQmlSource,modelData.pComponentArgs||{})}}
}
}
)QML");
}
} // namespace ShaderSettingsFixture
