"""First paint + live sidebar regressions; native input doubles, not live OEM proof."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import tempfile
import time
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QT_QUICK_BACKEND','software')
os.environ.setdefault('QT_QUICK_CONTROLS_STYLE','Basic')
from PySide6.QtCore import QObject, QMetaObject, QPoint, QPointF, Qt, QSettings, QUrl
from PySide6.QtGui import QGuiApplication, QFontDatabase
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest
from PySide6.QtQuick import QQuickWindow

ROOT=Path(__file__).resolve().parents[1]
sp=importlib.util.spec_from_file_location('chrome_suite',ROOT/'tools/full-emulator-ui/suite.py')
S=importlib.util.module_from_spec(sp);sp.loader.exec_module(S);U=S.runtime()
app=QGuiApplication([])
if os.name=='nt':
    for name in ('segoeui.ttf','seguisb.ttf','seguisym.ttf'):
        font=Path(os.environ['WINDIR'])/'Fonts'/name
        if font.is_file():QFontDatabase.addApplicationFont(str(font))
warnings=[]

def settle():
    for _ in range(30):app.processEvents();time.sleep(.01)

def find(item,name):
    if item.objectName()==name:return item
    if hasattr(item,'childItems'):
        for child in item.childItems():
            result=find(child,name)
            if result is not None:return result
    return None

with tempfile.TemporaryDirectory() as folder:
    temp=Path(folder);assets=temp/'Skin';shutil.copytree(ROOT/'assets/full-emulator-ui',assets)
    ini=temp/'appearance.ini'
    theme=(assets/'Theme.qml').read_text()
    theme=theme.replace('StandardPaths.writableLocation(StandardPaths.GenericConfigLocation) + "/VOID-Emulator-Themes-v9.ini"',json.dumps(ini.as_posix()))
    (assets/'Theme.qml').write_text(theme)
    preferences=QSettings(str(ini),QSettings.IniFormat);preferences.beginGroup('NativeAppearance')
    preferences.setValue('selectedPreset','forest');preferences.sync()
    url=QUrl.fromLocalFile(str(assets)).toString()+'/'
    # Exercise the exact old inline class from the reviewed native sidebar, then
    # run it through the same bridge replacement used for embedded resources.
    native=U.payload()['baseline/PlrSidebar.qml'].decode('utf-8-sig')
    tokens,pairs=U.scan(U.manager(),native)
    i=next(i for i in range(len(tokens)-4) if [t.text for t in tokens[i:i+5]]==['component','BsiSidebarInlineVisual',':','Item','{'])
    old_inline=native[tokens[i].start:tokens[pairs[i+4]].end]
    (temp/'UiImageButton.qml').write_text('''import QtQuick 2.15
import QtQuick.Controls 2.15
Button {
 id:nativeButton
 property string asset:"SidebarSettings"
 property string pVoidElementId:"settings"
 property bool pImageAnimationRunning:false
 width:42;height:42
 contentItem:Rectangle {color:"#ff00ff";visible:nativeButton.asset.length>0}
 background:Rectangle {color:"transparent"}
}
''')
    source='''import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import "SKIN" as Skin
Window {
 id:host;width:500;height:400;visible:true;color:Skin.Theme.background
 property int clicked:0
 property string requestedTheme:"forest"
 function choose(){Skin.Theme.choose(requestedTheme)}
 property color railInk:Skin.Theme.accent
 OLDINLINE
 Rectangle {
  id:iSidebar;objectName:"sidebar";x:430;y:0;width:56;height:400;color:"#ff00ff"
  property var pKebabMenuWin:null
  BsiSidebarInlineVisual {id:rail;objectName:"inlineRail";anchors.fill:parent;mode:"rail"}
  UiImageButton {
   id:settings;objectName:"settingsNative";x:7;y:40
   asset:inlineFace.known?"":"SidebarSettings"
   BsiSidebarInlineVisual {id:inlineFace;objectName:"inlineFace";anchors.fill:parent;elementId:"settings";assetName:"SidebarSettings";hovered:settings.hovered;pressed:settings.down;available:settings.enabled}
   onClicked:host.clicked++
  }
  // No inline face: overflow/native late instances still use discovery.
  UiImageButton {objectName:"fallbackNative";x:7;y:100;onClicked:host.clicked++}
  Skin.SidebarSkin {host:iSidebar}
 }
 Text{x:20;y:20;width:360;wrapMode:Text.Wrap;text:"Studio 10 / command rail fixture\\nNOT a live emulator capture";color:Skin.Theme.text}
 Text{x:20;y:90;text:Skin.Theme.name;color:Skin.Theme.accent;font.pixelSize:24}
}'''.replace('SKIN',url).replace('OLDINLINE',old_inline)
    patched=S.first_frame_sources(U,{'PlrSidebar.qml':source.encode()},url)['PlrSidebar.qml']
    (temp/'Window.qml').write_bytes(patched)
    engine=QQmlEngine();engine.warnings.connect(lambda errors:warnings.extend(e.toString() for e in errors if 'Settings type from Qt.labs.settings is deprecated' not in e.toString()))
    component=QQmlComponent(engine,QUrl.fromLocalFile(str(temp/'Window.qml')))
    assert not component.isError(),'\n'.join(e.toString() for e in component.errors())
    window=component.create();assert window,'\n'.join(e.toString() for e in component.errors())
    # Deliberately do NOT settle first: no delayed second skin adoption allowed.
    root=window.contentItem();button=find(root,'settingsNative');inline=find(root,'inlineFace')
    assert inline.property('known') is True, str(warnings)
    assert button.property('asset')=='','recognized stock glyph remained enabled during construction'
    assert find(button,'voidReferenceSidebarFace') is None,'duplicate inline + discovery face'
    fallback=find(root,'fallbackNative')
    assert find(fallback,'voidReferenceSidebarFace') is not None,'initial discovery deferred beyond creation'
    assert fallback.property('contentItem').property('opacity')==0
    assert window.property('railInk').name()=='#82dfb1','persisted palette not ready at construction'
    first=window.grabWindow();assert not first.isNull()
    # No 'blank instead of stock' loophole: both themed glyphs must already exist.
    for top in (40,100):
        count=sum(1 for y in range(top+10,top+32) for x in range(447,469) if first.pixelColor(x,y).green()>130 and first.pixelColor(x,y).red()>50 and first.pixelColor(x,y).green()>first.pixelColor(x,y).red()*1.25)
        assert count>10,('themed icon missing in initial capture',top,count)
    # Old external rail and stock icons are magenta in this fixture.
    assert not any(first.pixelColor(x,y).name()=='#ff00ff' for x in range(430,486) for y in range(first.height())), 'stock skin visible in first capture'
    settle()
    rail=find(root,'voidRailBackground');edge=find(root,'voidRailEdge')
    assert rail is not None and edge is not None
    glyph=find(inline,'bsiOutlineGlyph');assert glyph is not None
    for preset in json.loads((assets/'themes.json').read_text()):
        window.setProperty('requestedTheme',preset['id']);QMetaObject.invokeMethod(window,'choose');settle()
        assert edge.property('color').name()==preset['palette']['border'],preset['id']
        assert glyph.property('tint').name()==preset['palette']['accent'],preset['id']
        assert find(fallback,'bsiOutlineGlyph').property('tint').name()==preset['palette']['accent']
    p=button.mapToScene(QPointF(button.width()/2,button.height()/2))
    QTest.mouseClick(window,Qt.LeftButton,Qt.NoModifier,QPoint(round(p.x()),round(p.y())));settle()
    assert window.property('clicked')==1,'native sidebar callback lost'
    # Missing/unknown glyphs retain original content; no broad asset erasure.
    fallback.setProperty('pVoidElementId','unmapped-tool');fallback.setProperty('asset','UnknownTool');settle()
    assert fallback.property('contentItem').property('opacity')==1
    output=ROOT/'.cache/full-ui/chrome-first-frame.png';output.parent.mkdir(parents=True,exist_ok=True);assert first.save(str(output))
    window.setVisible(False);window.deleteLater();settle()
    assert not warnings,'\n'.join(warnings)
print('PASS: synchronous owned sidebar bridge, no stock magenta in initial capture, persisted theme at construction, six live rails/icons without hover, no duplicate face, native callback + unknown fallback. NOT live OEM certification.')
