#pragma once
#include <QString>

// Earlier BSI-authored planning format; never deployed to players. Keep exact
// removal coverage when compact resource encoding is used for new v2 plans.
namespace ShaderSettingsLegacyFixture {
inline const QString player = QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import BsiNativeFixture 1.0
Window {
id:iAppwin
objectName:'nativeShaderHost'
width:1440;height:900;visible:true
property QtObject pSettingsWindow:null
property bool pCompLoaded:false
Component.onCompleted:{/*BSI-SHADERS-START:1*/fBsiStartShaders();/*BSI-SHADERS-START-END*/pCompLoaded=true}
Rectangle{width:40;height:40;color:'#123456'
MouseArea{objectName:'nativeSettingsButton';anchors.fill:parent;onClicked:fOpenSettingsWindow()}}
Component{id:iSettingsItems;SettingElementModelList{
SettingElementModel{pButtonText:'Performance';pQmlSource:'UiPerformanceSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Display';pQmlSource:'UiDisplaySetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Graphics';pQmlSource:'UiGraphicsSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Game settings';pQmlSource:'PlrGameSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Devices';pQmlSource:'PlrAudioSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Gamepad';pQmlSource:'PlrGamepadSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Preferences';pQmlSource:'PlrPreferencesSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Phone';pQmlSource:'PlrDeviceSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Shortcuts';pQmlSource:'PlrShortcutsSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'User data';pQmlSource:'PlrUserDataSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'Advanced';pQmlSource:'PlrAdvancedSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}
SettingElementModel{pButtonText:'About';pQmlSource:'PlrAboutSetting.qml';pComponentArgs:{'pParentWindow':'plr'}}/*BSI-SHADERS-ROW:1*/
SettingElementModel{pButtonText:qsTr('Shaders');pQmlSource:'qrc:/bsi-shaders/BsiShadersSetting.qml';pIsVisible:iAppwin.bsiShaderReady;pComponentObj:QtObject{}
pComponentArgs:{'pParentWindow':'plr','pVmName':plrBackend.getVmName(),'bsiTabIndex':12,'bsiPalette':UiTheme.colors,'bsiFont':UiTheme.fonts.bodyMedium}}/*BSI-SHADERS-ROW-END*/
}}
function fOpenSettingsWindow(){
if(pSettingsWindow!==null)return
var component=Qt.createComponent('UiSettingsWindow.qml')
if(component.status!==Component.Ready)throw new Error(component.errorString())
pSettingsWindow=component.createObject(iAppwin,{'pParentWindow':'plr','pSettingsItems':iSettingsItems.createObject()})
if(pSettingsWindow===null)throw new Error(component.errorString())
pSettingsWindow.show()
}
/*BSI-SHADERS-BOOT:1*/
property bool bsiShaderReady:false
property var bsiShaderBootstrap:null
function fBsiStartShaders(){
try{
bsiShaderBootstrap=Qt.createQmlObject('import BSIShaders 1.0; Bootstrap{hostWindow:iAppwin;instanceName:plrBackend.getVmName();graphicsRenderer:uiBackend.confGetInstanceString("graphics_renderer")}',iAppwin,'BsiShaderBootstrap')
bsiShaderReady=bsiShaderBootstrap!==null
}catch(error){console.warn('BSI shaders unavailable; native player startup continues: '+error)}
}
/*BSI-SHADERS-BOOT-END*/
}
)QML");
inline const QString window = QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Window 2.15
Window {
id:iSettingsWindow
function fChangeSelectedSetting(index){if(uiBackend.confIsModified()/*BSI-SHADERS-DIRTY:1*/||bsiShaderDirty/*BSI-SHADERS-DIRTY-END*/)prompt()}
onClosing:{if(uiBackend.confIsModified()/*BSI-SHADERS-DIRTY:1*/||bsiShaderDirty/*BSI-SHADERS-DIRTY-END*/)prompt()}
function discardClose(){uiBackend.confDiscard()/*BSI-SHADERS-CLOSE:1*/;bsiShaderDirty=false;/*BSI-SHADERS-CLOSE-END*/}
Item{//BSI v17
property var bIcons:({})
Item{property var c:/*BSI-SHADERS-VISIBLE:1*/modelData.pIsVisible===false?'':/*BSI-SHADERS-VISIBLE-END*/iSettingsWindowShadowContainer.bIcons[index]}
Repeater{model:pSettingsItems.pSettingElements;delegate:Loader{/*BSI-SHADERS-PAGE:1*/active:modelData.pIsVisible!==false;/*BSI-SHADERS-PAGE-END*/Component.onCompleted:setSource(modelData.pQmlSource,modelData.pComponentArgs||{})}}
}
/*BSI-SHADERS-WINDOW:1*/
property bool bsiShaderDirty:false
/*BSI-SHADERS-WINDOW-END*/
}
)QML");
}
