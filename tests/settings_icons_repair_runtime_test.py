"""Real Qt fixture: actual icon identities, opaque IDs and native guard delegation.
Does not execute an OEM emulator or certify which shell its loader selects.
"""
from pathlib import Path
import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QSG_RHI_BACKEND','software')
from PySide6.QtCore import QUrl, QMetaObject, Qt, QPoint, Q_ARG
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtQuick import QQuickWindow, QQuickItem
from PySide6.QtTest import QTest

app=QGuiApplication([])
engine=QQmlEngine()
errors=[]
engine.warnings.connect(lambda messages: errors.extend(m.toString() for m in messages))
folder=Path(__file__).resolve().parents[1]/'assets/settings-icons-repair'
component=QQmlComponent(engine)
component.setData(b'''import QtQuick 2.15
import QtQuick.Window 2.15
import "."
Window {
 id: host
 width: 1000; height: 760; visible: true
 property int pCurrentTabIndex: 0
 property int requested: -1
 property var pSettingsItems: ({pSettingElements:makeEntries(0)})
 function makeEntries(mode) {
  var labels=["Performance","Display","Graphics","Game settings","Devices","Gamepad","Preferences","Phone","Shortcuts","User data","Advanced","About","VOID Optimizer"];
  var ids=["engine","display","graphics","gameSettings","devices","gamepad","preferences","phone","shortcuts","userData","advanced","about","bsiOptimizer"];
  var pages=["EngineSetting.qml","DisplaySetting.qml","GraphicsSetting.qml","PlrGameSetting.qml","PlrDeviceSetting.qml","PlrGamepadSetting.qml","PlrPreferencesSetting.qml","PhoneSetting.qml","PlrShortcutsSetting.qml","PlrUserDataSetting.qml","PlrAdvancedSetting.qml","PlrAboutSetting.qml","VOIDOptimizerPanel.qml"];
  var result=[];
  for(var i=0;i<labels.length;i++) {
   result.push({pButtonId:mode===2?ids[i]:i,
    pQmlSource:mode===1?"qrc:/NativeSettings/"+pages[i]:"",
    pButtonText:mode===1?"Localized label "+i:labels[i],pShowRedDot:false});
  }
  if(mode===3) { var first=result[0];result[0]=result[1];result[1]=first; }
  return result;
 }
 function setMode(mode: int) { pSettingsItems={pSettingElements:makeEntries(mode)}; }
 function fChangeSelectedSetting(index) { requested=index; }
 SettingsShell { id: panel; objectName:"repairShell"; anchors.fill:parent; bsiHost:host; bsiAnimations:false }
 Item { id: nativePage; objectName:"nativePageFixture"; width:20; height:20 }
 Component.onCompleted: nativePage.parent=panel.pageContainer
 function enableSvg() { panel.bsiPreferPngIcons=false; }
}''',QUrl.fromLocalFile(str(folder/'fixture.qml')))
window=component.create()
assert window is not None,[e.toString() for e in component.errors()]
QTest.qWait(200)
def items():
    result=[]
    def visit(item):
        result.append(item)
        for child in item.childItems():visit(child)
    visit(window.contentItem())
    return result
def named(name):return next(item for item in items() if item.objectName()==name)
def glyphs():return [item for item in items() if item.objectName()=='bsiSettingsSvgGlyph']
expected_order=['performance.png','display.png','graphics.png','game-settings.png','devices.png',
          'gamepad.png','preferences.png','phone.png','shortcuts.png','user-data.png',
          'advanced.png','about.png','optimizer.png']
expected=set(expected_order+['close.png'])
def assert_png_identity(mode=0):
    order=expected_order.copy()
    if mode==3:order[0],order[1]=order[1],order[0]
    images=glyphs()
    assert len(images)==14,len(images)
    actual=[]
    for image in images:
        assert image.parentItem().property('ready') is True,str(image.property('source'))
        url=image.property('source')
        assert url.isLocalFile() and Path(url.toLocalFile()).is_file(),str(url)
        filename=Path(url.toLocalFile()).name
        actual.append(filename)
        owner=image.parentItem()
        while owner is not None and not owner.objectName().startswith('bsiSettingsTab_'):
            owner=owner.parentItem()
        if owner is not None:
            index=int(owner.objectName().split('_')[-1])
            assert filename==order[index],(index,filename,order[index])
        else:assert filename=='close.png'

    assert set(actual)==expected,actual
    assert len(actual)==len(set(actual)), 'Image.Ready alone must not pass 13 repeated preference icons'
    optimizer=named('bsiSettingsTab_12')
    assert any(child.parentItem().property('kind')=='optimizer' for child in glyphs()
               if child.parentItem().parentItem().parentItem()==optimizer)
shell=named('repairShell')
assert shell.property('bsiPreferPngIcons') is True
assert named('bsiSettingsIconVersion').property('text')=='BSI icons v3 / PNG'
assert_png_identity()
assert named('nativePageFixture').parentItem() is named('bsiSettingsPageContainer')
assert window.property('requested')==-1
button=named('bsiSettingsTab_1')
point=button.mapToScene(button.boundingRect().center())
QTest.mouseClick(window,Qt.LeftButton,Qt.NoModifier,QPoint(round(point.x()),round(point.y())))
QTest.qWait(10)
assert window.property('requested')==1
assert window.property('pCurrentTabIndex')==0,'shell must not bypass the native unsaved-changes guard'
window.setProperty('pCurrentTabIndex',1)
assert shell.property('selectedIndex')==1
for mode in [1,2,3]:
    assert QMetaObject.invokeMethod(window,'setMode',Q_ARG(int,mode))
    QTest.qWait(100)
    assert_png_identity(mode)
window.setWidth(640);QTest.qWait(100)
assert shell.property('bsiCompact') is True
assert_png_identity(3)
QMetaObject.invokeMethod(window,'enableSvg');QTest.qWait(100)
for image in glyphs():
    assert image.parentItem().property('ready') is True,str(image.property('source'))
assert not errors,errors
window.close()
print('PASS: 14 distinct correct Settings images, numeric-ID/label fallback, native-source/localized-label lookup, recognized IDs, reordered rows, compact layout, SVG, native guard/page reparenting and last Optimizer icon. OEM emulator not executed.')
