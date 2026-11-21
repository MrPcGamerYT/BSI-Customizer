"""Real Qt windows/popups + reviewed UiDialog/UiButton/layout/tile sources.
OEM-only enums/models/tooltips have test doubles; this is NOT live OEM certification.
"""
import importlib.util
import os
from pathlib import Path
import re
import tempfile
import time
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QT_QUICK_BACKEND','software')
os.environ.setdefault('QT_QUICK_CONTROLS_STYLE','Basic')
from PySide6.QtCore import QPoint, QPointF, Qt, QUrl, QMetaObject
from PySide6.QtGui import QGuiApplication, QFontDatabase
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest
from PySide6.QtQuick import QQuickItem, QQuickWindow

ROOT=Path(__file__).resolve().parents[1]
sp=importlib.util.spec_from_file_location('surface_suite',ROOT/'tools/full-emulator-ui/suite.py')
S=importlib.util.module_from_spec(sp);sp.loader.exec_module(S);U=S.runtime()
app=QGuiApplication([])
if os.name=='nt':
    for name in ('segoeui.ttf','seguisb.ttf','seguisym.ttf'):
        p=Path(os.environ['WINDIR'])/'Fonts'/name
        if p.is_file():QFontDatabase.addApplicationFont(str(p))
engine=QQmlEngine();warnings=[]
engine.warnings.connect(lambda errors:warnings.extend(e.toString() for e in errors if "Settings type from Qt.labs.settings is deprecated" not in e.toString()))

def settle():
    for _ in range(35):app.processEvents();time.sleep(.008)

def find(item,name):
    if item.objectName()==name:return item
    if hasattr(item,'childItems'):
        for child in item.childItems():
            result=find(child,name)
            if result is not None:return result
    return None

def click(window,item):
    assert item is not None
    p=item.mapToScene(QPointF(item.width()/2,item.height()/2))
    QTest.mouseClick(window,Qt.LeftButton,Qt.NoModifier,QPoint(round(p.x()),round(p.y())))
    settle()

with tempfile.TemporaryDirectory() as folder:
    temp=Path(folder);engine.addImportPath(str(temp))
    url=QUrl.fromLocalFile(str(ROOT/'assets/full-emulator-ui')).toString()+'/'
    def put(name,text):
        p=temp/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text,encoding='utf-8')
    put('qmldir','singleton UiTheme 1.0 UiTheme.qml\n')
    dialog=(ROOT/'reference/full-emulator-ui/native/UiDialog.qml').read_bytes()
    put('UiDialog.qml',S.scoped_surface_hook(U,U.manager(),'UiDialog.qml',dialog,url).decode())
    popup=(ROOT/'reference/full-emulator-ui/native/UiWindowedPopup.qml').read_bytes()
    put('UiWindowedPopup.qml',S.scoped_surface_hook(U,U.manager(),'UiWindowedPopup.qml',popup,url).decode())
    for name in ('UiColumnLayout.qml','UiRowLayout.qml'):
        put(name,(ROOT/'reference/full-emulator-ui/controls'/name).read_text())
    button=(ROOT/'reference/full-emulator-ui/native/UiButton.qml').read_text()
    button=re.sub(r'^import UiToolTipControl[^\n]*\n','',button,flags=re.M)
    button=re.sub(r'^\s*UiToolTip\.(?:visible|text):[^\n]*\n','\n',button,flags=re.M)
    put('UiButton.qml',button)
    tile=(ROOT/'reference/full-emulator-ui/native/KmmEditorDragControls.qml').read_text()
    tile=re.sub(r'^import UiToolTipControl[^\n]*\n','',tile,flags=re.M)
    put('KmmEditorDragControls.qml',tile)
    put('KmmUtils.js','.pragma library\nvar starts=0; var ends=0; function startDrag(mouse,type){starts++} function continueDrag(mouse,type){} function endDrag(mouse,type){ends++}\n')
    put('UiImage.qml','import QtQuick 2.15\nImage { property string asset:""; property string extn:".svg"; property real pImageWidth:20; property real pImageHeight:20; width:pImageWidth; height:pImageHeight; source:""; fillMode:Image.PreserveAspectFit }')
    put('UiImageButton.qml','import QtQuick 2.15\nimport QtQuick.Controls 2.15\nButton { property string asset:""; property real pImageWidth:20; property real pImageHeight:20; width:24; height:24; implicitWidth:24; implicitHeight:24; contentItem:Text {text:"×";color:"#eeeef4";horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter} background:Rectangle {color:"transparent"} }')
    put('UiCheckBox.qml','import QtQuick 2.15\nimport QtQuick.Controls 2.15\nCheckBox {property QtObject pSize; palette.windowText:"white"}')
    put('KmmEditorTextEditBox.qml','import QtQuick 2.15\nRectangle {width:34;height:22;Text {objectName:"keyChipText";anchors.centerIn:parent;text:"W";color:"#111111"}}')
    put('UiShadowContainer.qml','import QtQuick 2.15\nItem { property int shadowThickness:8; property bool shadowVisibility:true; anchors.fill:parent }')
    put('DialogButtonModelList/qmldir','module DialogButtonModelList\nDialogButtonModelList 1.0 Model.qml\n')
    put('DialogButtonModelList/Model.qml','import QtQuick 2.15\nQtObject {property list<QtObject> pButtons:[]}')
    put('UiDialogWindow/qmldir','module UiDialogWindow\nUiDialogWindow 1.0 Enum.qml\n')
    put('UiDialogWindow/Enum.qml','import QtQuick 2.15\nQtObject {enum Kind {SimpleNarrow,Simple,Descriptive,DescriptiveNarrow,Tall,TallAlert,Graphic,GraphicSimple}}')
    keys=sorted(set(re.findall(r'pSize\.(\w+)',dialog.decode())))
    sizes=';'.join('property font '+key+':Qt.font({family:"Segoe UI",pixelSize:14})' if key.endswith('Font') else 'property real '+key+':'+str(400 if key=='width' else 18 if 'Padding' in key else 8 if 'Spacing' in key else 24) for key in keys)
    button_size='property int height:36; property int horizontalPadding:14; property int spacing:8; property int radius:0; property int iconWidth:16; property int iconHeight:16; property font textFont:Qt.font({pixelSize:13}); property font subTextFont:textFont'
    normal='QtObject {property color background:"#222532";property color borderColor:"#f01050";property color textColor:"white";property color subTextColor:"#aaaaaa";property Gradient borderGradient:null}'
    put('UiTheme.qml','''pragma Singleton
import QtQuick 2.15
QtObject {
 property QtObject colors:QtObject {property color accentNormal:"#f01050"; property color primary50:"#252735";property color primary60:"#444655";property color primary10:"white"}
 property QtObject fonts:QtObject {property font bodySmall:Qt.font({pixelSize:13});property font bodyExtraSmall:bodySmall}
 property QtObject checkBoxes:QtObject {property QtObject sizes:QtObject {property QtObject medium:QtObject {}}}
 property QtObject buttons:QtObject {
  property QtObject sizes:QtObject {property QtObject medium:QtObject {BUTTON_SIZE}}
  property QtObject colors:QtObject {property QtObject accent:QtObject {property QtObject normal:NORMAL;property QtObject hover:normal;property QtObject click:normal}}
 }
 property QtObject dialogs:QtObject {
  property QtObject sizes:QtObject {property QtObject simple:QtObject {SIZES};property QtObject simpleNarrow:simple;property QtObject descriptive:simple;property QtObject descriptiveNarrow:simple;property QtObject tall:simple;property QtObject tallAlert:simple;property QtObject graphic:simple;property QtObject graphicSimple:simple}
  property QtObject colors:QtObject {property QtObject accent:QtObject {property color background:"#222532";property color border:"#555566";property color title:"white";property color description:"#dddddd";property color subtitle:"white";property color notice:"#ffcc66"}}
 }
}'''.replace('BUTTON_SIZE',button_size).replace('NORMAL',normal).replace('SIZES',sizes))
    put('Window.qml','''import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import UiDialogWindow 1.0
import DialogButtonModelList 1.0
import "ASSETS" as Skin
import "KmmUtils.js" as Utils
Window {
 id:host;objectName:"nativeHost";width:1100;height:720;visible:true;color:"#080809"
 property bool pIsBootComplete:false
 property int saved:0
 property int muted:0
 property bool dragCheck:false
 property var tileImages:["Tap","RepeatedTap","Dpad","Aim","FreeLook","Script","Swipe","Zoom","Tilt","MOBA-PAD","SkillPad","Rotate","EdgeScroll","Scroll","MouseZoom"]
 Text{x:24;y:20;text:"OLED • native surface fixture — NOT a live emulator capture";color:"white";font.pixelSize:16}
 Item{id:rail;property var pKebabMenuWin:null}
 Skin.SidebarSkin{host:rail}
 Flow {id:tiles;x:24;y:65;width:330;spacing:10
  Repeater {model:host.tileImages
   KmmEditorDragControls {property string image:modelData;property string type:modelData;property string label:modelData;objectName:"tile_"+modelData;Component.onCompleted:pMouseArea.objectName="nativeDrag_"+modelData;width:95;height:92}
  }
 }
 KmmEditorTextEditBox {objectName:"keyChip";x:410;y:370}
 Skin.NativeControlStyle {objectName:"editorStyle";pages:host;stylePanels:true;registerNativeWindow:true;visible:false}
 UiDialog {
  id:restart;objectName:"restartDialog";x:400;y:90;title:"Restart needed";pDescription:"Some changes need a restart. Restart now?";pDialogType:UiDialogWindow.Simple
  pButtonsList:DialogButtonModelList {pButtons:[QtObject {property QtObject pSize:null;property QtObject pColor:null;property string pText:"Restart now";property string pImage:"";property string pExtn:".svg";property string pSubText:"";property bool pImageAfterText:false;signal clicked();onClicked:host.saved++}]}
 }
 UiWindowedPopup {
  id:volume;objectName:"volumeWindow";transientParent:host;contentWidth:240;contentHeight:130;visible:true;closeOnPressOutside:false
  UiColumnLayout {anchors.fill:parent;color:"#222532";padding:16;spacing:10
   Slider {objectName:"volumeSlider";from:0;to:100;value:30;Layout.fillWidth:true}
   UiButton {objectName:"muteAll";text:"Mute all instances";pSize:UiTheme.buttons.sizes.medium;pColor:UiTheme.buttons.colors.accent;onClicked:host.muted++}
  }
 }
 Window {id:standalone;objectName:"unregisteredWindow";transientParent:null;width:420;height:220;visible:true;color:"#222532"
  UiDialog {id:untouched;objectName:"bootDialog";title:"Protected host";pDescription:"No registered native parent";pDialogType:UiDialogWindow.Simple}
 }
 function resetAppearance(){Skin.Theme.reset()}
 function forestAppearance(){Skin.Theme.choose("forest")}
 function openDialogs(){restart.open();untouched.open()}
 function closeAll(){restart.close();untouched.close();volume.visible=false;standalone.visible=false;host.visible=false}
 function checkDrag(){dragCheck=Utils.starts>0 && Utils.ends>0}
}'''.replace('ASSETS',url))
    component=QQmlComponent(engine,QUrl.fromLocalFile(str(temp/'Window.qml')))
    assert not component.isError(),'\n'.join(e.toString() for e in component.errors())
    window=component.create();assert window,'\n'.join(e.toString() for e in component.errors())
    QMetaObject.invokeMethod(window,'resetAppearance');QMetaObject.invokeMethod(window,'openDialogs');settle()
    from PySide6.QtCore import QObject
    restart=window.findChild(QObject,'restartDialog');untouched=window.findChild(QObject,'bootDialog')
    assert restart is not None and untouched is not None
    assert restart.property('pColor').property('background').name()=='#222532','boot-incomplete host must not opt in'
    window.setProperty('pIsBootComplete',True);settle()
    assert restart.property('pColor').property('background').name()=='#000000'
    assert untouched.property('pColor').property('background').name()=='#222532', 'unregistered host changed: '+str(warnings)
    assert restart.property('pColor').property('notice').name()=='#ffcc66','notice semantics lost'
    body=restart.property('contentItem');background=restart.property('background')
    # Dialog root + separate header/footer must all be traversed, not only contentItem.
    face=next((child for child in background.parentItem().childItems() if child.property('visualKind')=='dialog'),None)
    assert face is not None and face.property('radius')==16, str([(child.objectName(),child.property('visualKind'),child.property('radius')) for child in background.parentItem().childItems()])+str(warnings)
    footer=restart.property('footer')
    def by_text(item,text):
        if item.property('text')==text:return item
        for child in item.childItems():
            match=by_text(child,text)
            if match is not None:return match
    save=by_text(footer,'Restart now');assert save is not None
    click(window,save);assert window.property('saved')==1,'native dialog button callback lost: '+str(warnings)
    volume=window.findChild(QObject,'volumeWindow');mute=find(volume.contentItem(),'muteAll')
    assert mute is not None and mute.property('pColor').property('normal').property('borderColor').name()=='#2c3b4e', str(warnings)
    slider=find(volume.contentItem(),'volumeSlider');before=slider.property('value')
    click(volume,slider);assert slider.property('value')!=before
    click(volume,mute);assert window.property('muted')==1
    tile=find(window.contentItem(),'tile_Tap');assert tile is not None
    assert tile.property('radius')==14
    drag=find(window.contentItem(),'nativeDrag_Tap')
    assert drag is not None and drag.property('enabled')
    QMetaObject.invokeMethod(restart,'close');settle()
    p=tile.mapToScene(QPointF(tile.width()/2,tile.height()/2))
    point=QPoint(round(p.x()),round(p.y()))
    QTest.mousePress(window,Qt.LeftButton,Qt.NoModifier,point)
    QTest.mouseMove(window,point+QPoint(8,4))
    QTest.mouseRelease(window,Qt.LeftButton,Qt.NoModifier,point+QPoint(8,4));settle()
    QMetaObject.invokeMethod(window,'checkDrag');settle()
    assert window.property('dragCheck'), 'native tile drag press/release callbacks lost'
    icon=next(child for child in tile.findChildren(QObject) if child.property('asset')=='Tap')
    assert icon.property('source').toString().endswith('ControlIcons/Tap.png')
    QMetaObject.invokeMethod(restart,'open');settle()
    chip=find(window.contentItem(),'keyChip')
    assert chip.property('color').name()=='#0b0f14'
    assert find(chip,'keyChipText').property('color').name()=='#f2f7ff'
    # Already-open native dialogs, popups, key chips and drag cards update live.
    QMetaObject.invokeMethod(window,'forestAppearance');settle()
    assert restart.property('pColor').property('background').name()=='#07120f'
    assert mute.property('pColor').property('normal').property('borderColor').name()=='#345548'
    assert chip.property('color').name()=='#0f211c'
    assert tile.property('radius')==10
    assert untouched.property('pColor').property('background').name()=='#222532'
    QMetaObject.invokeMethod(window,'resetAppearance');settle()
    # Late delegate discovery + no timer/polling work while idle.
    style=window.findChild(QObject,'editorStyle');count=style.property('visitedNodes');flush=style.property('flushCount')
    settle();assert style.property('visitedNodes')==count and style.property('flushCount')==flush
    output=ROOT/'.cache/full-ui/surface-fixture.png';output.parent.mkdir(parents=True,exist_ok=True)
    assert window.grabWindow().save(str(output))
    assert volume.grabWindow().save(str(output.with_name('volume-fixture.png')))
    QMetaObject.invokeMethod(window,'closeAll');settle();window.deleteLater();settle()
    assert not warnings,'\n'.join(warnings)
print('PASS: native shared UiDialog and UiWindowedPopup opt-in, boot/unregistered isolation, real footer callback/volume input, native tile icon/rounded styling, zero idle polling. NOT a live OEM test.')
