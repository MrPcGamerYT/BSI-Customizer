"""Qt component/input tests with native-page DOUBLES, never a live-player certification.
Requires PySide6 6.8.3 and normal Qt platform dependencies. No helper is launched.
Optional --screenshot PATH writes a clearly labelled fixture capture, not a product preview.
"""
import argparse
import os
from pathlib import Path
import tempfile
import time
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QT_QUICK_BACKEND','software')
os.environ.setdefault('QT_QUICK_CONTROLS_STYLE','Basic')
if os.name == 'nt':
    os.environ.setdefault('QT_QPA_FONTDIR',str(Path(os.environ['WINDIR'])/'Fonts'))
from PySide6.QtCore import QObject, QPoint, QPointF, Qt, QUrl
from PySide6.QtGui import QGuiApplication, QFontDatabase, QFont, QRawFont
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtQuick import QQuickWindow
from PySide6.QtTest import QTest

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser();parser.add_argument('--screenshot',type=Path,default=ROOT/'.cache/full-ui/suite07-fixture.png');args=parser.parse_args()
app=QGuiApplication([])
if os.name == 'nt':
    for name in ('segoeui.ttf','seguisb.ttf','seguisym.ttf','cambria.ttc'):
        font=Path(os.environ['WINDIR'])/'Fonts'/name
        if font.is_file():assert QFontDatabase.addApplicationFont(str(font))>=0
    assert QRawFont.fromFont(QFont('Segoe UI')).supportsCharacter(ord('A'))
engine=QQmlEngine();warnings=[]
engine.warnings.connect(lambda errors:warnings.extend(e.toString() for e in errors if "Settings type from Qt.labs.settings is deprecated" not in e.toString()))
url=QUrl.fromLocalFile(str(ROOT/'assets/full-emulator-ui')).toString()

def settle():
    for _ in range(30):app.processEvents();time.sleep(.01)

def visual(item,name):
    if item.objectName()==name:return item
    for child in item.childItems():
        result=visual(child,name)
        if result is not None:return result
    return None

def click(item):
    assert item is not None
    p=item.mapToScene(QPointF(item.width()/2,item.height()/2))
    QTest.mouseClick(window,Qt.LeftButton,Qt.NoModifier,QPoint(round(p.x()),round(p.y())))
    settle()

with tempfile.TemporaryDirectory() as temporary:
    temp=Path(temporary)
    (temp/'UiImageButton.qml').write_text('''import QtQuick 2.15
import QtQuick.Controls 2.15
Button { property string asset:"SidebarEcoMode"; property string pVoidElementId:"settings"; property bool pImageAnimationRunning:false; width:40; height:40 }
''')
    for name in ['UiSlider.qml','UiRowLayout.qml','UiColumnLayout.qml']:
        (temp/name).write_bytes((ROOT/'reference/full-emulator-ui/controls'/name).read_bytes())
    source='''import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import "ASSETS" as Skin
Window {
 id:host; width:940; height:540; visible:true
 property int pCurrentTabIndex:0
 property bool pVoidOptimizerActive:false
 property bool blockSwitch:false
 property int requested:-1
 property bool closeRequested:false
 property int saved:0
 property string requestedTheme:"oled"
 function chooseRequestedTheme(){Skin.Theme.choose(requestedTheme)}
 property string themeName:Skin.Theme.name
 property color themeSurface:Skin.Theme.surface
 function prepareAppearance(){Skin.Theme.preferencesFile="PREFERENCES";Skin.Theme.reset()}
 function forestAppearance(){Skin.Theme.choose("forest")}
 function customAppearance(){Skin.Theme.applyCustom({background:"#010203",surface:"#101820",raised:"#202830",border:"#405060",text:"#f0f0f0",muted:"#a0b0c0",accent:"#70ddb0"});Skin.Theme.setCorner(21);Skin.Theme.setMotion(false)}
 function resetAppearance(){Skin.Theme.reset()}

 QtObject { id: initialSliderSize
   property int handleWidth:20; property int handleHeight:20; property int sliderHeight:4
   property font selectedValueFont:Qt.font({pixelSize:16})
   property font recommendedValueFont:Qt.font({pixelSize:14})
   property font rangeValueFont:Qt.font({pixelSize:14})
 }
 QtObject { id: initialSliderColor
   property color handleBackground:"purple";property color handleBackgroundDisabled:"grey";property color handleBorder:"purple"
   property color sliderBackground:"grey";property color sliderBackgroundDisabled:"grey";property color sliderFill:"purple";property color sliderFillDisabled:"grey"
   property color selectedValue:"white";property color recommendedValue:"white";property color rangeValue:"white"
 }
 property var pKebabMenuWin:null
 property QtObject pSettingsItems: QtObject { property list<QtObject> pSettingElements:[
 QtObject{property string pButtonId:"engine";property string pButtonText:"Performance";property bool pShowRedDot:false},
 QtObject{property string pButtonId:"display";property string pButtonText:"Display";property bool pShowRedDot:false},
 QtObject{property string pButtonId:"graphics";property string pButtonText:"Graphics";property bool pShowRedDot:false}
 ] }
 function fVoidSelectOptimizer(){requested=-2;if(!blockSwitch)pVoidOptimizerActive=true}
 function fVoidSelectNative(index){requested=index;if(!blockSwitch){pVoidOptimizerActive=false;pCurrentTabIndex=index}}
 onClosing:function(event){event.accepted=false;closeRequested=true}
 Skin.SettingsShell {id:shell; anchors.fill:parent; bsiHost:host
   Component.onCompleted: {var page=pageFactory.createObject(shell.pageContainer);shell.stylePage(page)}
 }
 Component { id:pageFactory
   Item { width:parent.width; height:parent.height; objectName:"testNativePage"
     Column {spacing:15
       Text{text:"Native input fixture — NOT a live OEM screenshot";color:"#aaaabb"}
       ComboBox{property bool testPopupOpen:popup.opened;objectName:"testCombo";width:174;height:27;model:["High (4 Cores)","Medium (2 Cores)"]}
       UiSlider{objectName:"testSlider";width:460;from:1;to:240;value:120;pSize:initialSliderSize;pColor:initialSliderColor;pSelectedValueText:"Frame rate: "+Math.round(value);pRangeValueVisibility:true}
       Switch{objectName:"testSwitch";text:"Native switch"}
       CheckBox{objectName:"testCheck";text:"Native checkbox"}
       TextField{objectName:"testField";width:174;placeholderText:"Native field"}
       Button{objectName:"testSave";width:112;height:29;text:"Save changes";onClicked:host.saved++}
     }
   }
 }
 // Separate passive-sidebar fixture, initially empty to exercise late delegate discovery.
 Item{id:sidebar;objectName:"testSidebar";x:880;y:180;width:40;height:100;property var pKebabMenuWin:null}
 Skin.SidebarSkin{id:sidebarSkin;host:sidebar}
 Component{id:lateButton;UiImageButton{objectName:"testLateSidebar"}}
 function addLateSidebarButton(){lateButton.createObject(sidebar)}
 Text{z:1000;anchors.bottom:parent.bottom;anchors.horizontalCenter:parent.horizontalCenter;text:"Qt component test — native-page doubles; NOT live MSI";color:"#aaaaaa";font.pixelSize:9}
}'''.replace('ASSETS',url).replace('PREFERENCES',str(temp/'appearance.ini').replace('\\','/'))
    (temp/'Window.qml').write_text(source)
    component=QQmlComponent(engine,QUrl.fromLocalFile(str(temp/'Window.qml')))
    assert not component.isError(),'\n'.join(e.toString() for e in component.errors())
    window=component.create();assert window,'\n'.join(e.toString() for e in component.errors())
    from PySide6.QtCore import QMetaObject
    QMetaObject.invokeMethod(window,'prepareAppearance');settle()
    assert isinstance(window,QQuickWindow),type(window)
    root=window.contentItem();shell=visual(root,'bsiSettingsShell');page=visual(root,'testNativePage')
    assert page is not None
    p=page.mapToScene(QPointF(0,0));assert abs(p.x()-224)<1 and abs(p.y()-88)<1,(p.x(),p.y())
    click(visual(root,'testSave'));assert window.property('saved')==1
    switch=visual(root,'testSwitch');click(switch);assert switch.property('checked')
    check=visual(root,'testCheck');click(check);assert check.property('checked')
    slider=visual(root,'testSlider')
    assert slider.property('pSize').property('handleWidth')==14
    assert slider.property('contentItem').property('spacing')==12
    assert visual(slider,'voidReferenceVisualTrack') is None, 'A second track must not overlay native UiSlider captions'
    before=slider.property('value');click(slider);assert slider.property('value')!=before
    combo=visual(root,'testCombo');click(combo);assert combo.property('testPopupOpen')
    QTest.keyClick(window,Qt.Key_Escape);settle()
    field=visual(root,'testField');click(field);[QTest.keyClick(window,key) for key in (Qt.Key_N,Qt.Key_A,Qt.Key_T,Qt.Key_I,Qt.Key_V,Qt.Key_E)];assert field.property('text')=='native'
    style=visual(root,'voidNativeControlStyle');assert style.property('styledControls')>=6
    # No extra optimizer tab/page or shortcut. Native guard still owns switching.
    assert visual(root,'bsiSettingsTab_3') is None
    assert visual(root,'voidSettingsOptimizerLoader') is None
    window.setProperty('blockSwitch',True);click(visual(root,'bsiSettingsTab_1'))
    assert window.property('requested')==1 and window.property('pCurrentTabIndex')==0
    window.setProperty('blockSwitch',False);click(visual(root,'bsiSettingsTab_1'))
    assert window.property('pCurrentTabIndex')==1
    from PySide6.QtCore import QMetaObject
    QMetaObject.invokeMethod(window,'addLateSidebarButton');settle()
    sidebar=visual(root,'testLateSidebar');face=visual(sidebar,'voidReferenceSidebarFace')
    assert face is not None, 'late sidebar face missing'
    assert face.property('iconKey')=='advanced', face.property('iconKey')
    assert face.property('known'), str(warnings)
    assert sidebar.property('contentItem').property('opacity')==0
    import json
    for preset in json.loads((ROOT/'assets/full-emulator-ui/themes.json').read_text()):
        window.setProperty('requestedTheme',preset['id'])
        QMetaObject.invokeMethod(window,'chooseRequestedTheme');settle()
        assert window.property('themeName')==preset['name']
        assert window.property('themeSurface').name()==preset['palette']['surface']
        assert slider.property('pColor').property('sliderFill').name()==preset['palette']['accent']
    # Open the real Appearance popup and use its preset card, not an independent mock.
    click(visual(root,'openAppearance'))
    card=visual(root,'themePreset_forest');assert card is not None,str(warnings)
    click(card);assert window.property('themeName')=='Forest'
    assert slider.property('pColor').property('sliderFill').name()=='#82dfb1'
    click(visual(root,'closeAppearance'))
    QMetaObject.invokeMethod(window,'customAppearance');settle()
    assert window.property('themeName')=='My theme'
    assert slider.property('pColor').property('sliderFill').name()=='#70ddb0'
    assert window.property('themeSurface').name()=='#101820'
    assert visual(field,'voidReferenceVisualBackground').property('radius')==18
    # Persistence is tested in a fresh QML engine, not just the same singleton.
    probe_engine=QQmlEngine()
    probe_engine.warnings.connect(lambda errors:warnings.extend(e.toString() for e in errors if "Settings type from Qt.labs.settings is deprecated" not in e.toString()))
    probe_component=QQmlComponent(probe_engine)
    probe_source='''import QtQuick 2.15
import "ASSETS" as Skin
QtObject {
 property string selected:Skin.Theme.selectedId
 property color surface:Skin.Theme.surface
 property int corner:Skin.Theme.corner
 property bool motion:Skin.Theme.motionEnabled
 Component.onCompleted:{Skin.Theme.preferencesFile="PREFERENCES";Skin.Theme.sync()}
}'''.replace('ASSETS',url).replace('PREFERENCES',str(temp/'appearance.ini').replace('\\','/'))
    probe_component.setData(probe_source.encode(),QUrl.fromLocalFile(str(temp/'Probe.qml')))
    probe=probe_component.create();assert probe,'\n'.join(e.toString() for e in probe_component.errors())
    settle()
    assert probe.property('selected')=='custom'
    assert probe.property('surface').name()=='#101820'
    assert probe.property('corner')==21 and not probe.property('motion')
    probe.deleteLater();settle();probe_engine.deleteLater();settle()
    QMetaObject.invokeMethod(window,'resetAppearance');settle()
    if args.screenshot:
        args.screenshot.parent.mkdir(parents=True,exist_ok=True)
        assert window.grabWindow().save(str(args.screenshot))
    click(visual(root,'bsiSettingsClose'));assert window.property('closeRequested')
    assert not warnings,'\n'.join(warnings)
    window.setProperty('visible',False)
    window.deleteLater();settle()
print('PASS: Qt component/input fixture: reference offsets, native Save/toggle/checkbox/field/dropdown input and ACTUAL src-code UiSlider/row/column rendering, guarded native tab, no optimizer, late sidebar glyph. NOT live emulator testing.')
