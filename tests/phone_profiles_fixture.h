#pragma once
#include <QString>

// Independently authored contract scene, NOT a distributed OEM page. It models
// the native public configuration/callback API consumed by PhoneProfiles.
namespace PhoneProfilesFixture {
inline QString source()
{
    return QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
Item {
objectName: "deviceProfile"
property bool pCompLoaded:false
property bool pProfileLoadedFromCloud:false
property bool pEnableSave:false
property var pDeviceProfileMap:({})
property var pNetworkOperatorMap:({})
property ListModel pDeviceProfileModel:ListModel{}
property ListModel pNetworkOperatorModel:ListModel{}
property int pCallbackExpectedCount:0
property int pCallbackCount:0
property int pSuccessClbkCount:0
property string pProfile:""
property string pCarrier:""
property string pDeviceManufacturer:""
property string pDeviceBrand:""
property string pDeviceModel:""
property string pProfileMessage:""
ButtonGroup{id:choices;onCheckedButtonChanged:fMonitorChange()}
Column {
 anchors.fill:parent;spacing:10
 RadioButton{id:iPredefinedDeviceProfileBtn;objectName:"predefined";text:"Choose a predefined profile";ButtonGroup.group:choices}
 UiComboBox {
  id:iDeviceProfileCombobox;objectName:"phoneDropdown"
  width:256;model:pDeviceProfileModel;visible:iPredefinedDeviceProfileBtn.checked
  onActivated:{
   if(pCompLoaded&&pProfileLoadedFromCloud&&currentIndex>=0&&currentIndex<pDeviceProfileModel.count){
    uiBackend.confSetInstanceString("device_profile_code",pDeviceProfileMap[pDeviceProfileModel.get(currentIndex).name]);
    fMonitorChange();
   }
  }
 }
 RadioButton{id:iCustomDeviceProfileBtn;objectName:"custom";text:"Create a custom profile";ButtonGroup.group:choices}
 Row {
  visible:iCustomDeviceProfileBtn.checked;spacing:4
  TextField{id:iDeviceManufacturerTxt;objectName:"manufacturer";width:160;onTextChanged:{if(pCompLoaded){uiBackend.confSetInstanceString("device_custom_manufacturer",text);fMonitorChange();}}}
  TextField{id:iDeviceBrandTxt;objectName:"brand";width:160;onTextChanged:{if(pCompLoaded){uiBackend.confSetInstanceString("device_custom_brand",text);fMonitorChange();}}}
  TextField{id:iDeviceModelTxt;objectName:"model";width:160;onTextChanged:{if(pCompLoaded){uiBackend.confSetInstanceString("device_custom_model",text);fMonitorChange();}}}
 }
 UiComboBox{id:iNetworkOperatorCombobox;objectName:"carrierDropdown";width:256;model:pNetworkOperatorModel;onActivated:{uiBackend.confSetInstanceString("device_carrier_code",pNetworkOperatorMap[pNetworkOperatorModel.get(currentIndex).name]);fMonitorChange();}}
 Button{objectName:"save";text:"Save changes";enabled:pEnableSave;opacity:enabled?1:.4;onClicked:fHandleConfChange()}
}
function fOnQmlLoaded(){if(!pCompLoaded)fInitUi();pCompLoaded=true;}
function fInitUi(){plrBackend.getDeviceProfileList();pEnableSave=false;}
function fDeviceProfilesReceivedFromCloud(json){
 pProfileLoadedFromCloud=false;
 var data=JSON.parse(json),deviceProfileArr=data.device_profile_list,networkOperatorArr=data.ca_selector_list;
 pDeviceProfileModel.clear();pNetworkOperatorModel.clear();
 var currentProfile=uiBackend.confGetInstanceString("device_profile_code");
 var currentCarrier=uiBackend.confGetInstanceString("device_carrier_code");
 iDeviceManufacturerTxt.text=uiBackend.confGetInstanceString("device_custom_manufacturer");
 iDeviceBrandTxt.text=uiBackend.confGetInstanceString("device_custom_brand");
 iDeviceModelTxt.text=uiBackend.confGetInstanceString("device_custom_model");
 if(currentProfile==="custom")iCustomDeviceProfileBtn.checked=true;
 else iPredefinedDeviceProfileBtn.checked=true;
 for(var i=0;i<deviceProfileArr.length;i++){
  pDeviceProfileMap[deviceProfileArr[i].display_name]=deviceProfileArr[i].pcode;
  pDeviceProfileModel.append({name:deviceProfileArr[i].display_name});
  if(deviceProfileArr[i].pcode===currentProfile)iDeviceProfileCombobox.currentIndex=i;
 }
 for(var j=0;j<networkOperatorArr.length;j++){
  pNetworkOperatorMap[networkOperatorArr[j].display_name]=networkOperatorArr[j].ca_selector;
  pNetworkOperatorModel.append({name:networkOperatorArr[j].display_name});
  if(networkOperatorArr[j].ca_selector===currentCarrier)iNetworkOperatorCombobox.currentIndex=j;
 }
 pProfileLoadedFromCloud=true;
}
function fMonitorChange(){
 var carrierChanged=uiBackend.confIsInstancePropModified("device_carrier_code");
 if(iPredefinedDeviceProfileBtn.checked&&(uiBackend.confIsInstancePropModified("device_profile_code")||carrierChanged))pEnableSave=true;
 else if(iCustomDeviceProfileBtn.checked&&iDeviceManufacturerTxt.text.length>0&&iDeviceBrandTxt.text.length>0&&iDeviceModelTxt.text.length>0&&(uiBackend.confIsInstancePropModified("device_custom_manufacturer")||uiBackend.confIsInstancePropModified("device_custom_brand")||uiBackend.confIsInstancePropModified("device_custom_model")||carrierChanged))pEnableSave=true;
 else pEnableSave=false;
}
function fSetPreviousState(){
 var code=uiBackend.confGetInstanceString("device_profile_code");
 iDeviceProfileCombobox.currentIndex=-1;
 for(var i=0;i<pDeviceProfileModel.count;i++)if(pDeviceProfileMap[pDeviceProfileModel.get(i).name]===code){iDeviceProfileCombobox.currentIndex=i;break;}
 if(code==="custom")iCustomDeviceProfileBtn.checked=true;else iPredefinedDeviceProfileBtn.checked=true;
 iDeviceManufacturerTxt.text=uiBackend.confGetInstanceString("device_custom_manufacturer");
 iDeviceBrandTxt.text=uiBackend.confGetInstanceString("device_custom_brand");
 iDeviceModelTxt.text=uiBackend.confGetInstanceString("device_custom_model");
 pCallbackExpectedCount=0;pCallbackCount=0;pSuccessClbkCount=0;pProfile="";pCarrier="";pDeviceManufacturer="";pDeviceBrand="";pDeviceModel="";
 fMonitorChange();
}
function fSaveChanges(){
 if(pCallbackCount!==pCallbackExpectedCount)return;
 uiBackend.confCommit();iSettingsWindow.fShowToast(pProfileMessage);fSetPreviousState();
}
function fDeviceProfileSettingChangedClbk(status){
 pCallbackCount++;
 if(status===0){
  pSuccessClbkCount++;
  if(iPredefinedDeviceProfileBtn.checked){
   uiBackend.confSetInstanceString("device_profile_code",pProfile);
   uiBackend.confSetInstanceString("device_custom_manufacturer","");uiBackend.confSetInstanceString("device_custom_brand","");uiBackend.confSetInstanceString("device_custom_model","");
  }else{
   uiBackend.confSetInstanceString("device_profile_code","custom");
   uiBackend.confSetInstanceString("device_custom_manufacturer",pDeviceManufacturer);uiBackend.confSetInstanceString("device_custom_brand",pDeviceBrand);uiBackend.confSetInstanceString("device_custom_model",pDeviceModel);
  }
  uiBackend.confSetInstanceString("device_carrier_code",pCarrier);pProfileMessage="Device profile changes saved";
 }else pProfileMessage="Failed to switch device profile";
 fSaveChanges();
}
function fHandleConfChange(){
 if(iPredefinedDeviceProfileBtn.checked&&(uiBackend.confIsInstancePropModified("device_profile_code")||uiBackend.confIsInstancePropModified("device_carrier_code"))){
  pProfile=uiBackend.confGetInstanceString("device_profile_code");pCarrier=uiBackend.confGetInstanceString("device_carrier_code");pCallbackExpectedCount++;
 }
 if(iCustomDeviceProfileBtn.checked&&(uiBackend.confIsInstancePropModified("device_custom_manufacturer")||uiBackend.confIsInstancePropModified("device_custom_brand")||uiBackend.confIsInstancePropModified("device_custom_model")||uiBackend.confIsInstancePropModified("device_carrier_code"))){
  pDeviceManufacturer=uiBackend.confGetInstanceString("device_custom_manufacturer");pDeviceBrand=uiBackend.confGetInstanceString("device_custom_brand");pDeviceModel=uiBackend.confGetInstanceString("device_custom_model");pCarrier=uiBackend.confGetInstanceString("device_carrier_code");pCallbackExpectedCount++;
 }
 uiBackend.confDiscard();
 if(iPredefinedDeviceProfileBtn.checked&&(pProfile!==""||pCarrier!==""))plrBackend.setDeviceProfile(pProfile,pCarrier);
 if(iCustomDeviceProfileBtn.checked&&pDeviceManufacturer!==""&&pDeviceBrand!==""&&pDeviceModel!==""&&pCarrier!=="")plrBackend.setCustomDeviceProfile(pDeviceManufacturer,pDeviceBrand,pDeviceModel,pCarrier);
}
function fDiscardChanges(){uiBackend.confDiscard();fInitUi();}
}
)QML");
}
} // namespace PhoneProfilesFixture
