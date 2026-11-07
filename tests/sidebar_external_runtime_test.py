"""Actual Qt Quick rendering/events with native-type doubles, NOT a live MSI test.
Needs PySide6 and its platform dependencies. Software/offscreen; QML imports 2.15.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
os.environ.setdefault('QT_QUICK_BACKEND', 'software')
QT5 = os.environ.get('BSI_QT_API') == 'pyqt5'
if QT5:
    from PyQt5.QtCore import QObject, QUrl, Qt, QPoint, QPointF, QByteArray, qCompress, qUncompress, QT_VERSION_STR
    from PyQt5.QtGui import QGuiApplication
    from PyQt5.QtQml import QQmlEngine, QQmlComponent
    from PyQt5.QtQuick import QQuickWindow
    from PyQt5.QtTest import QTest
else:
    from PySide6.QtCore import QObject, QUrl, Qt, QPoint, QPointF, QByteArray, qCompress, qUncompress, qVersion
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQml import QQmlEngine, QQmlComponent
    from PySide6.QtQuick import QQuickWindow
    from PySide6.QtTest import QTest
    QT_VERSION_STR = qVersion()

def enum_value(value):
    return int(value) if QT5 else value.value

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sidebar_build', ROOT/'tools/sidebar-panel/build_external.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)
INLINE = os.environ.get('BSI_SIDEBAR_INLINE') == '1'
if INLINE:
    sys.path.insert(0, str(ROOT/'tools/sidebar-panel'))
    import build_inline

def restyle(source, url):
    output, buttons = build_inline.restyle(source) if INLINE else build.restyle(source, url)
    if INLINE and os.environ.get('BSI_SIDEBAR_COMPACT') == '1':
        from compact_inline import compact
        output = compact(output)
    return output, buttons

app = QGuiApplication([])
engine = QQmlEngine()
warnings = []
engine.warnings.connect(lambda errors:warnings.extend(e.toString() for e in errors))

def pump(ms=60):
    until = time.monotonic() + ms/1000
    while time.monotonic() < until:
        app.processEvents()
        time.sleep(.003)

def visual(item, name):
    if item.objectName() == name:
        return item
    for child in item.childItems():
        found = visual(child, name)
        if found is not None:
            return found
    return None

with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    shutil.copytree(ROOT/'assets/emulator-sidebar', temp/'Assets')
    (temp/'UiColumnLayout.qml').write_text('import QtQuick 2.15\nRectangle {}')
    # Native glyph, hover state and sizing are modeled; callbacks are kept in the input file.
    (temp/'UiImageButton.qml').write_text('''import QtQuick 2.15
Item {
 property string asset: ""
 readonly property bool hovered: tracker.hovered
 HoverHandler { id: tracker }
 Rectangle { objectName:"stockGlyph"; anchors.centerIn:parent; width:14; height:14;
             radius:2; color:"#ad9cbd"; visible:parent.asset !== "" }
}''')
    source = (ROOT/'tests/fixtures/sidebar/NativeSidebar.qml').read_text()
    styled, _ = restyle(source, QUrl.fromLocalFile(str(temp/'Assets/BSISidebarVisual.qml')).toString())
    payload = bytes(qCompress(QByteArray(styled.encode()), compressionLevel=9))
    assert payload == build.qcompress(styled.encode())
    assert bytes(qUncompress(QByteArray(payload))) == styled.encode()
    (temp/'NativeSidebar.qml').write_text(styled)
    if INLINE:
        shutil.rmtree(temp/'Assets')  # Prove there is no external skin dependency.
    (temp/'Window.qml').write_text('''import QtQuick 2.15
import QtQuick.Window 2.15
Window {
 width:540; height:494; visible:true; color:"#0e0d12"
 Rectangle { width:parent.width; height:34; color:"#29232e"
  Text { x:14; anchors.verticalCenter:parent.verticalCenter; text:"MSI sidebar / visual fixture"; color:"#d6cddd"; font.pixelSize:12 }
  Text { anchors.right:parent.right; anchors.rightMargin:12; anchors.verticalCenter:parent.verticalCenter; text:"—   □   ×"; color:"#b9afc1"; font.pixelSize:15 }
 }
 Rectangle { x:0; y:34; width:parent.width-32; height:parent.height-34; color:"#101015"
  Column { anchors.centerIn:parent; spacing:12
   Text { anchors.horizontalCenter:parent.horizontalCenter; text:"SIDEBAR"; color:"#cab5df"; font.pixelSize:28; font.letterSpacing:5 }
   Text { anchors.horizontalCenter:parent.horizontalCenter; text:"Violet rail · Outline icons · 32px cells"; color:"#877b94"; font.pixelSize:12 }
  }
  Text { x:16; anchors.bottom:parent.bottom; anchors.bottomMargin:16; text:"Qt test fixture — not a live emulator screenshot\\nTop window bar is unchanged by this package"; color:"#6d6478"; font.pixelSize:10; lineHeight:1.5 }
 }
 NativeSidebar { x:parent.width-width; y:34; height:parent.height-34 }
}''')
    component = QQmlComponent(engine, QUrl.fromLocalFile(str(temp/'Window.qml')))
    assert not component.isError(), '\n'.join(e.toString() for e in component.errors())
    window = component.create()
    assert window is not None, '\n'.join(e.toString() for e in component.errors())
    pump(250)
    assert isinstance(window, QQuickWindow)
    sidebar = visual(window.contentItem(), 'sidebar')
    assert sidebar.width() == 32
    def native(element):
        found = visual(sidebar, 'native_'+element)
        assert found is not None, element
        return found
    def glyph(element):
        result = visual(native(element), 'bsiOutlineGlyph')
        assert result is not None
        return result
    for element in ['fullscreen','volume','gameControl','memoryTrim','installApk','more','settings','back','home','recent']:
        assert glyph(element).parentItem().parentItem().property('known') is True, (element, glyph(element).property('source'))
        assert native(element).property('asset') == '', element
    assert native('unknownNativeTool').property('asset') == 'OemNativeTool'
    assert visual(sidebar, 'native_screenshot') is None, 'Disabled feature was reenabled'
    assert native('memoryTrim').property('enabled') is False
    def point(element):
        button = native(element)
        p = button.mapToScene(QPointF(button.width()/2, button.height()/2))
        return QPoint(round(p.x()), round(p.y()))
    QTest.mouseMove(window, point('fullscreen'))
    pump(150)
    assert native('fullscreen').property('hovered') is True
    assert glyph('fullscreen').parentItem().parentItem().property('hovered') is True
    # Existing callback executes ON PRESS and ON RELEASE separately, with original modifiers.
    modifiers = Qt.ControlModifier | Qt.AltModifier
    QTest.mousePress(window, Qt.RightButton, modifiers, point('gameControl'))
    pump()
    assert sidebar.property('clickedCount') == 1 and sidebar.property('releasedCount') == 0
    assert sidebar.property('lastElement') == 'gameControl'
    assert sidebar.property('lastButton') == enum_value(Qt.RightButton)
    assert sidebar.property('lastModifiers') == enum_value(modifiers)
    assert glyph('gameControl').parentItem().parentItem().property('pressed') is True
    QTest.mouseRelease(window, Qt.RightButton, modifiers, point('gameControl'))
    pump()
    assert sidebar.property('releasedCount') == 1
    assert glyph('gameControl').parentItem().parentItem().property('pressed') is False
    QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, point('memoryTrim'))
    pump()
    assert sidebar.property('clickedCount') == 1, 'Skin bypassed disabled state'
    QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, point('volume'))
    pump()
    assert sidebar.property('clickedCount') == 2 and sidebar.property('releasedCount') == 2
    original_volume = glyph('volume').property('source').toString()
    sidebar.setProperty('pIsMuted', True)
    pump()
    assert glyph('volume').property('source').toString() != original_volume, 'Mute binding was frozen'
    for element in ['back','home','recent']:
        QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, point(element))
    pump()
    assert sidebar.property('navigationCount') == 3
    # Native trim progress/animation is retained; new outline rotates too.
    trim = native('memoryTrim')
    sidebar.setProperty('trimEnabled', True)
    sidebar.setProperty('trimAnimating', True)
    pump()
    trim_skin = glyph('memoryTrim').parentItem().parentItem()
    assert trim.property('enabled') and trim.property('pImageAnimationRunning')
    assert trim_skin.property('animating')
    assert visual(trim.parentItem(), 'nativeBusyBadge').property('visible')
    pump(150)
    assert glyph('memoryTrim').property('rotation') > 0
    sidebar.setProperty('trimAnimating', False)
    sidebar.setProperty('trimEnabled', False)
    sidebar.setProperty('pIsMuted', False)
    QTest.mouseMove(window, QPoint(100, 100))
    pump(180)
    if os.environ.get('BSI_SIDEBAR_SCREENSHOT'):
        # Unknown OEM icon is a deliberate test double, not part of the visual design.
        native('unknownNativeTool').parentItem().setVisible(False)
        pump()
        image = window.grabWindow()
        assert not image.isNull()
        assert image.save(os.environ['BSI_SIDEBAR_SCREENSHOT'])
    for height in [300, 600, 460]:
        window.setHeight(height)
        pump()
        assert sidebar.width() == 32 and native('home').height() == 32
    # Component-owned input: test both absence and presence of a pressed property.
    native_input = source.replace(
        'MouseArea { anchors.fill:parent; onPressed: navigationCount++; onReleased: releasedCount++ }',
        'onHoveredChanged: { /* input belongs to native component */ }')
    native_input = native_input.replace('id: iHomeBtn', 'id: iHomeBtn\n            property bool pressed: true')
    native_input = native_input.replace('id: iRecentBtn', 'id: iRecentBtn\n            signal pressed()')
    native_input, _ = restyle(native_input, QUrl.fromLocalFile(str(temp/'Assets/BSISidebarVisual.qml')).toString())
    (temp/'ComponentInput.qml').write_text(native_input)
    native_component = QQmlComponent(engine, QUrl.fromLocalFile(str(temp/'ComponentInput.qml')))
    native_root = native_component.create()
    assert native_root is not None, '\n'.join(e.toString() for e in native_component.errors())
    pump()
    back = visual(native_root, 'native_back')
    home = visual(native_root, 'native_home')
    back_skin = visual(back, 'bsiOutlineGlyph').parentItem().parentItem()
    home_skin = visual(home, 'bsiOutlineGlyph').parentItem().parentItem()
    recent_skin = visual(visual(native_root, 'native_recent'), 'bsiOutlineGlyph').parentItem().parentItem()
    assert not recent_skin.property('pressed'), 'A pressed signal must not become an always-pressed boolean'
    assert back_skin.property('known') and not back_skin.property('pressed')
    assert home_skin.property('known') and home_skin.property('pressed')
    home.setProperty('pressed', False)
    pump()
    assert not home_skin.property('pressed')
    native_root.deleteLater()
    pump()
    assert not warnings, '\n'.join(warnings)
    window.setVisible(False)
    # Missing sidecar: native glyphs must stay in place, not be blanked out.
    fallback, _ = restyle(source, QUrl.fromLocalFile(str(temp/'missing/BSISidebarVisual.qml')).toString())
    (temp/'Missing.qml').write_text(fallback)
    missing_component = QQmlComponent(engine, QUrl.fromLocalFile(str(temp/'Missing.qml')))
    missing = missing_component.create()
    assert missing is not None
    pump()
    assert visual(missing, 'native_volume').property('asset') == ('' if INLINE else 'SidebarVolume')
    assert visual(missing, 'native_home').property('asset') == ('' if INLINE else 'NavBarHome')
    missing.deleteLater()
    window.deleteLater()
    pump()
print(('INLINE: ' if INLINE else 'EXTERNAL: ') + 'PASS: actual Qt ' + QT_VERSION_STR + ' SVG rendering, qCompress equality, dynamic mute, right-click+Ctrl/Alt, native press/release, disabled tools, navigation, trim animation, geometry and missing-asset fallback.')
