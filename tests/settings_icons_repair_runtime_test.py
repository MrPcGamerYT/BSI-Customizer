"""Real Qt loading test for the isolated supplied shell. No OEM host executed."""
from pathlib import Path
import os
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QSG_RHI_BACKEND','software')
from PySide6.QtCore import QUrl, QMetaObject, Qt, QPoint
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
 property var pSettingsItems: ({pSettingElements: [
  {pButtonId:"performance", pButtonText:"Performance", pShowRedDot:false},
  {pButtonId:"display", pButtonText:"Display", pShowRedDot:false},
  {pButtonId:"graphics", pButtonText:"Graphics", pShowRedDot:false},
  {pButtonId:"gameSettings", pButtonText:"Game settings", pShowRedDot:false},
  {pButtonId:"devices", pButtonText:"Devices", pShowRedDot:false},
  {pButtonId:"gamepad", pButtonText:"Gamepad", pShowRedDot:false},
  {pButtonId:"preferences", pButtonText:"Preferences", pShowRedDot:false},
  {pButtonId:"phone", pButtonText:"Phone", pShowRedDot:false},
  {pButtonId:"shortcuts", pButtonText:"Shortcuts", pShowRedDot:false},
  {pButtonId:"userdata", pButtonText:"User data", pShowRedDot:false},
  {pButtonId:"advanceSettings", pButtonText:"Advanced", pShowRedDot:false},
  {pButtonId:"about", pButtonText:"About", pShowRedDot:false}
 ]})
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
shell=named('repairShell')
assert shell.property('bsiPreferPngIcons') is True
glyphs=[item for item in items() if item.objectName()=='bsiSettingsSvgGlyph']
assert len(glyphs)==13,len(glyphs)
for glyph in glyphs:
    assert glyph.parentItem().property('ready') is True,str(glyph.property('source'))
    url=glyph.property('source')
    assert url.isLocalFile() and Path(url.toLocalFile()).is_file(),str(url)
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
# SVG source stays available for players that support it.
QMetaObject.invokeMethod(window,'enableSvg');QTest.qWait(100)
for glyph in glyphs:assert glyph.parentItem().property('ready') is True,str(glyph.property('source'))
assert not errors,errors
window.close()
print('PASS: 13 Settings images load in real Qt (PNG-first and SVG); native callback/guard and page reparenting contract preserved. OEM emulator not executed.')
