"""Real Qt6 fixture for native titlebar routing; not live MSI/Windows integration."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QT_QUICK_BACKEND','software')
os.environ.setdefault('QT_QUICK_CONTROLS_STYLE','Basic')
from PySide6.QtCore import QUrl,QPoint,QPointF,Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickWindow
from PySide6.QtQml import QQmlEngine,QQmlComponent
from PySide6.QtTest import QTest
ROOT=Path(__file__).resolve().parents[1]
subprocess.run(['node','tests/player_chrome_test.cjs'],cwd=ROOT,check=True)
app=QGuiApplication([]);engine=QQmlEngine();warnings=[]
engine.warnings.connect(lambda errors:warnings.extend(e.toString() for e in errors))
def pump(ms=100):
    end=time.monotonic()+ms/1000
    while time.monotonic()<end:app.processEvents();time.sleep(.003)
def visual(item,name):
    if item.objectName()==name:return item
    for child in item.childItems():
        match=visual(child,name)
        if match is not None:return match
    return None
with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp)
    shutil.copy(ROOT/'.cache/player-chrome/PlrTopbar.qml',temp/'PlrTopbar.qml')
    (temp/'UiRowLayout.qml').write_text('''import QtQuick 2.15
import QtQuick.Layouts 1.15
Rectangle { id:root; default property alias content:row.data; property alias spacing:row.spacing
 RowLayout { id:row; anchors.fill:parent; spacing:0 }
}''')
    (temp/'UiColumnLayout.qml').write_text('import QtQuick.Layouts 1.15\nColumnLayout {}')
    (temp/'UiImage.qml').write_text('''import QtQuick 2.15
Item { property string asset:""; property int pImageWidth:20; property int pImageHeight:20
 implicitWidth:pImageWidth; implicitHeight:pImageHeight
 Rectangle { anchors.fill:parent; radius:3; color:"#42424a";border.color:"#a4a4ac" }
}''')
    (temp/'UiImageButton.qml').write_text('''import QtQuick 2.15
import QtQuick.Controls 2.15
Button { id:button;property string asset:""; property int pImageWidth:32;property int pImageHeight:32
 implicitWidth:pImageWidth;implicitHeight:pImageHeight; padding:0;hoverEnabled:true
 background:Item{}
 contentItem:Item{ Rectangle { anchors.centerIn:parent; width:8;height:8;color:"#777777";visible:button.asset!=="" } }
}''')
    (temp/'Window.qml').write_text('''import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
ApplicationWindow {
 id:iAppwin; width:960;height:540;visible:true;color:"#34343b"
 property bool pIsBootComplete:true
 property bool pShowAndroidAd:false
 property int pSidebarWidth:32
 QtObject { id:plrBackend;property string title:"MSI App Player"
  property int maxCalls:0;property int restoreCalls:0;property int backCalls:0;property int homeCalls:0;property int recentCalls:0
  function logUIAction(a,b) {}
  function onMaximizeClicked(){maxCalls++;iAppwin.visibility=ApplicationWindow.Maximized}
  function onRestoreClicked(){restoreCalls++;iAppwin.visibility=ApplicationWindow.Windowed}
  function onBackClicked(){backCalls++}
  function onHomeClicked(){homeCalls++}
  function onRecentAppsClicked(){recentCalls++}
 }
 QtObject { id:adsBackend;property int pExtendedBannerAdWidth:0 }
 QtObject { id:mouseProvider;function cursorPos(){return Qt.point(iAppwin.x+460,iAppwin.y+30)} }
 readonly property int backCalls:plrBackend.backCalls
 readonly property int homeCalls:plrBackend.homeCalls
 readonly property int recentCalls:plrBackend.recentCalls
 readonly property int maxCalls:plrBackend.maxCalls
 readonly property int restoreCalls:plrBackend.restoreCalls
 Rectangle { anchors.fill:parent;anchors.margins:1;color:"#050506"
  PlrTopbar { id:iTopbar;objectName:"fixtureTopbar";height:32;anchors.left:parent.left;anchors.right:parent.right
   onSidebarHideClicked:iSidebar.visible=false
   onSidebarBtnClicked:iSidebar.visible=true
  }
  Rectangle { id:iSidebar;objectName:"fixtureSidebar";width:32;anchors.right:parent.right;anchors.top:iTopbar.bottom;anchors.bottom:parent.bottom;color:"#080809" }
  Item { anchors.top:iTopbar.bottom;anchors.left:parent.left;anchors.right:iSidebar.visible?iSidebar.left:parent.right;anchors.bottom:parent.bottom
   Column { anchors.centerIn:parent;spacing:12
    Text { anchors.horizontalCenter:parent.horizontalCenter;text:"NATIVE GAME VIEWPORT";color:"#cacacf";font.pixelSize:22;font.letterSpacing:2 }
    Text { anchors.horizontalCenter:parent.horizontalCenter;text:"Not modified by the chrome skin";color:"#74747d";font.pixelSize:12 }
   }
   Text { x:16;anchors.bottom:parent.bottom;anchors.bottomMargin:18;text:"Qt fixture — not live MSI Player. The game image is not part of the UI patch.";color:"#787880";font.pixelSize:11 }
  }
 }
}''')
    c=QQmlComponent(engine,QUrl.fromLocalFile(str(temp/'Window.qml')))
    assert not c.isError(),'\n'.join(e.toString() for e in c.errors())
    w=c.create();assert w is not None
    pump(250)
    assert isinstance(w,QQuickWindow)
    bar=visual(w.contentItem(),'fixtureTopbar')
    def button(kind):
        b=visual(bar,'native_'+kind);assert b is not None;return b
    def point(kind):
        b=button(kind);p=b.mapToScene(QPointF(b.width()/2,b.height()/2));return QPoint(round(p.x()),round(p.y()))
    def skin(kind):
        for child in button(kind).childItems():
            if child.property('kind') is not None:return child
        raise AssertionError('No skin for '+kind)
    for kind in ['back','home','recent','menu','minimize','resize','close','sidebar']:
        assert button(kind).property('asset')=='',kind
        assert skin(kind).property('ready') is True,kind
        assert button(kind).width()==32 and button(kind).height()==32
    for kind in ['back','home','recent']:
        QTest.mouseClick(w,Qt.LeftButton,Qt.NoModifier,point(kind));pump()
        assert w.property(kind+'Calls')==1
    QTest.mousePress(w,Qt.LeftButton,Qt.NoModifier,point('menu'));pump()
    assert bar.property('menuRequests')==1,'Menu must fire on native press'
    assert skin('menu').property('downState')
    QTest.mouseRelease(w,Qt.LeftButton,Qt.NoModifier,point('menu'));pump()
    assert not skin('menu').property('downState')
    QTest.mouseClick(w,Qt.LeftButton,Qt.NoModifier,point('close'));pump()
    assert bar.property('closeRequests')==1 and w.isVisible(),'Close must retain confirmation flow, not close directly'
    QTest.mouseClick(w,Qt.LeftButton,Qt.NoModifier,point('sidebar'));pump()
    assert not visual(w.contentItem(),'fixtureSidebar').isVisible()
    assert skin('sidebar').property('drawing')=='expand'
    QTest.mouseClick(w,Qt.LeftButton,Qt.NoModifier,point('sidebar'));pump()
    assert visual(w.contentItem(),'fixtureSidebar').isVisible()
    assert skin('sidebar').property('drawing')=='collapse'
    QTest.mouseClick(w,Qt.LeftButton,Qt.NoModifier,point('resize'));pump()
    assert w.property('maxCalls')==1 and skin('resize').property('drawing')=='restore'
    QTest.mouseClick(w,Qt.LeftButton,Qt.NoModifier,point('resize'));pump()
    assert w.property('restoreCalls')==1 and skin('resize').property('drawing')=='maximize'
    QTest.mouseDClick(w,Qt.LeftButton,Qt.NoModifier,QPoint(450,16));pump()
    assert w.property('maxCalls')==2,'Native drag-area double-click routing changed'
    w.setVisibility(QQuickWindow.Windowed);pump()
    button('back').setEnabled(False)
    QTest.mouseClick(w,Qt.LeftButton,Qt.NoModifier,point('back'));pump()
    assert w.property('backCalls')==1,'Disabled native tool accepted input'
    button('back').setEnabled(True)
    for width in [720,1100,960]:
        w.setWidth(width);pump();assert bar.height()==32
    if os.environ.get('BSI_CHROME_SCREENSHOT'):
        QTest.mouseMove(w,QPoint(450,100));pump(150)
        image=w.grabWindow();assert not image.isNull();assert image.save(os.environ['BSI_CHROME_SCREENSHOT'])
    QTest.mouseClick(w,Qt.LeftButton,Qt.NoModifier,point('minimize'));pump()
    assert w.visibility()==QQuickWindow.Minimized
    assert not warnings,'\n'.join(warnings)
    w.setVisible(False);w.deleteLater();pump()
print('PASS: Qt6 Canvas outlines, native menu on-press, minimize/maximize/restore, close confirmation, collapse/expand, disabled input, double-click and resize geometry. Not live Windows integration.')
