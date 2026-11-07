"""Real Qt Quick shell test with model/page doubles, NOT a live BlueStacks test.
Requires PySide6 6.8.3 and its normal native platform dependencies.
QT_QPA_PLATFORM=minimal QT_QUICK_BACKEND=software python tests/settings_outline_runtime_test.py
"""
import os
from pathlib import Path
import tempfile
import time
os.environ.setdefault('QT_QPA_PLATFORM','minimal')
os.environ.setdefault('QT_QUICK_BACKEND','software')
os.environ.setdefault('QT_QUICK_CONTROLS_STYLE','Basic')
from PySide6.QtCore import QObject, QUrl, Qt, QPoint, QPointF
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtTest import QTest
from PySide6.QtQuick import QQuickWindow

ROOT=Path(__file__).resolve().parents[1]
REFERENCE=bool(os.environ.get('BSI_SETTINGS_REFERENCE'))
app=QGuiApplication([])
engine=QQmlEngine()
warnings=[]
engine.warnings.connect(lambda errors:warnings.extend(e.toString() for e in errors))
template=(ROOT/'assets/emulator-settings/OutlinePanel.qml').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp)
    module=temp/'BsiSettingsFixture'
    module.mkdir()
    (module/'qmldir').write_text('module BsiSettingsFixture\nUiShadowContainer 1.0 UiShadowContainer.qml\nsingleton UiTheme 1.0 UiTheme.qml\n')
    (module/'UiShadowContainer.qml').write_text('import QtQuick 2.15\nItem { property int shadowThickness: 0 }')
    (module/'UiTheme.qml').write_text('''pragma Singleton
import QtQuick 2.15
QtObject {
 property QtObject colors: QtObject {
  property color primary10: "#e3edf9"
  property color primary70: "#141f30"
  property color primary80: "#0e1725"
  property color accentNormal: "#86b5ff"
 }
 property QtObject fonts: QtObject {
  property font bodyMedium: Qt.font({family:"Sans Serif",pixelSize:12})
  property font bodyLarge: Qt.font({family:"Sans Serif",pixelSize:17,weight:600})
 }
}''')
    page='''import QtQuick 2.15
import QtQuick.Controls 2.15
import BsiSettingsFixture 1.0
Item {
 property string nativePageName: ""
 property bool initialized: false
 function fOnQmlLoaded() { initialized=true }
 Column { anchors.fill:parent; anchors.margins:24; spacing:20
  Text { text:parent.parent.nativePageName; color:UiTheme.colors.primary10; font.pixelSize:20 }
  Text { text:"Fixture controls — actual emulator pages are reused unchanged"; color:"#7c94b4"; font.pixelSize:11 }
  CheckBox { text:"Enable high frame rate"; checked:true }
  ComboBox { width:280; model:["High performance", "Balanced", "Low memory"] }
  Slider { width:280; from:0;to:100;value:65 }
  Button { text:"Save changes" }
 }
}'''
    if os.environ.get('BSI_SETTINGS_MONOCHROME'):
        page=(ROOT/'tests/fixtures/settings/MonochromePage.qml').read_text()
    (temp/'Page.qml').write_text(page)
    if os.environ.get('BSI_SETTINGS_EXTERNAL'):
        import sys
        sys.path.insert(0,str(ROOT/'tools/settings-panel'))
        from build_external import external_style,bridge
        (temp/'Assets').mkdir()
        external=temp/'Assets/BSISettingsOutline.qml'
        external.write_text((ROOT/'assets/emulator-settings/ReferencePanel.qml').read_text() if REFERENCE else (ROOT/'assets/emulator-settings/MonochromePanel.qml').read_text() if os.environ.get('BSI_SETTINGS_MONOCHROME') else external_style(template),encoding='utf-8')
        template=bridge(QUrl.fromLocalFile(str(external)).toString())
    entries=[]
    categories=[('engine','Performance'),('display','Display'),('graphics','Graphics'),('audio','Audio'),('gamepad','Gamepad'),('devices','Devices'),('preferences','Preferences'),('shortcuts','Shortcuts'),('advanceSettings','Advanced'),('about','About')]
    if REFERENCE:
        categories=[('engine','Performance'),('display','Display'),('graphics','Graphics'),('gameSettings','Game settings'),('gamepad','Gamepad'),('preferences','Preferences'),('phone','Phone'),('themes','Themes'),('advanceSettings','Advanced'),('about','About')]
    for entry_index,(key,label) in enumerate(categories):
        entries.append('QtObject { objectName:"nativeEntry_'+str(entry_index)+'";property string pButtonId:"'+key+'";property string pButtonText:"'+label+'"; property bool pShowRedDot:false;property var pComponentObj:null;property string pQmlSource:"Page.qml";property var pComponentArgs:({nativePageName:"'+label+'"}) }')
    wrapper='''import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import BsiSettingsFixture 1.0
ApplicationWindow {
 id:iSettingsWindow
 palette.windowText:"#e3edf9";palette.text:"#e3edf9";palette.buttonText:"#e3edf9"
 palette.base:"#101a29";palette.button:"#253a58";palette.highlight:"#86b5ff";palette.highlightedText:"#0e1725"
 width:1100;height:740;visible:true
 property int pCurrentTabIndex:0
 property int requestedTab:-1
 property bool blockSwitch:false
 property bool closeRequested:false
 property QtObject pSettingsItems: QtObject { property list<QtObject> pSettingElements: [ENTRIES] }
 function fChangeSelectedSetting(index) { requestedTab=index;if(!blockSwitch)pCurrentTabIndex=index }
 onClosing: function(e) { closeRequested=true;e.accepted=false }
 TEMPLATE
}'''.replace('ENTRIES',','.join(entries)).replace('TEMPLATE',template)
    if os.environ.get('BSI_SETTINGS_MONOCHROME'):
        wrapper=wrapper.replace('width:1100;height:740', 'width:940;height:540' if REFERENCE else 'width:940;height:610')
    if REFERENCE:
        at=wrapper.rfind('}')
        caption='Text {z:1000;anchors.bottom:parent.bottom;anchors.bottomMargin:1;anchors.horizontalCenter:parent.horizontalCenter;text:"Qt fixture — native-page doubles, NOT live MSI";color:"#8a8a93";font.pixelSize:9}'
        wrapper=wrapper[:at]+caption+'\n'+wrapper[at:]
    (temp/'Window.qml').write_text(wrapper)
    engine.addImportPath(str(temp))
    component=QQmlComponent(engine,QUrl.fromLocalFile(str(temp/'Window.qml')))
    assert not component.isError(), '\n'.join(e.toString() for e in component.errors())
    window=component.create()
    assert window, '\n'.join(e.toString() for e in component.errors())
    for _ in range(30):app.processEvents();time.sleep(.01)
    model=[window.findChild(QObject,'nativeEntry_'+str(i)) for i in range(len(categories))]
    assert len(model)==len(categories)
    for entry in model:
        page=entry.property('pComponentObj')
        assert page and page.property('initialized'), 'Native page initialization callback was lost'
        assert page.property('nativePageName')==entry.property('pButtonText'), 'Native loader arguments were lost'
    def visual(name,item=None):
        item=item or window.contentItem()
        if item.objectName()==name:return item
        for child in item.childItems():
            found=visual(name,child)
            if found:return found
        return None
    tab=visual('bsiSettingsTab_2')
    assert tab
    point=tab.mapToScene(QPointF(tab.width()/2,tab.height()/2))
    QTest.mouseClick(window,Qt.LeftButton,Qt.NoModifier,QPoint(round(point.x()),round(point.y())))
    app.processEvents()
    assert window.property('requestedTab')==2 and window.property('pCurrentTabIndex')==2
    window.setProperty('blockSwitch',True)
    visual('bsiSettingsTab_0').clicked.emit()
    app.processEvents()
    assert window.property('requestedTab')==0 and window.property('pCurrentTabIndex')==2, 'Shell bypassed native discard guard'
    if os.environ.get('BSI_SETTINGS_SCREENSHOT'):
        if os.environ.get('BSI_SETTINGS_MONOCHROME'):
            window.setProperty('blockSwitch',False)
            visual('bsiSettingsTab_0').clicked.emit()
            QTest.mouseMove(window,QPoint(450,60))
            app.processEvents()
        for _ in range(20):app.processEvents();time.sleep(.01)
        image=window.grabWindow()
        assert not image.isNull(),'Software preview capture failed'
        assert image.save(os.environ['BSI_SETTINGS_SCREENSHOT'])
    for width,height in [(840,600),(700,480),(1200,760),(720,520),(719,520)]:
        window.setWidth(width);window.setHeight(height)
        for _ in range(5):app.processEvents()
        assert visual('bsiSettingsShell').property('bsiCompact')==(width<(720 if REFERENCE else 880))
        if REFERENCE:
            assert visual('bsiSettingsTab_0' if os.environ.get('BSI_SETTINGS_SCREENSHOT') else 'bsiSettingsTab_2').property('selected'), 'Shell selection diverged from native host'
    visual('bsiSettingsClose').clicked.emit()
    app.processEvents()
    assert window.property('closeRequested'),'Close button bypassed original Window.onClosing'
    assert not warnings,'\n'.join(warnings)
    window.setProperty('visible',False)
    del window
print('PASS: Qt Quick creation, vector painting, 10 original page loaders/arguments/callbacks, guarded navigation, close callback and compact layouts.')
