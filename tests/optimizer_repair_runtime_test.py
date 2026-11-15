"""Real Qt review-URI matrix. Preview mode suppresses external helper launch."""
from pathlib import Path
import itertools
import os
import shutil
import tempfile
from urllib.parse import urlsplit, parse_qs
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QSG_RHI_BACKEND','software')
# The OEM Qt 5 player uses customizable Controls; Qt 6 on Windows defaults to
# a native style that deliberately rejects custom backgrounds/indicators.
os.environ.setdefault('QT_QUICK_CONTROLS_STYLE','Basic')
from PySide6.QtCore import QUrl, QMetaObject, QPointF, Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtQuick import QQuickWindow, QQuickItem
from PySide6.QtTest import QTest

root=Path(__file__).resolve().parents[1]
app=QGuiApplication([])
with tempfile.TemporaryDirectory() as directory:
    base=Path(directory)
    shutil.copyfile(root/'assets/optimizer-repair/Assets/OptimizerPage.qml',base/'OptimizerPage.qml')
    shutil.copytree(root/'assets/optimizer-repair/Assets/VoidOptimizerUI',base/'VoidOptimizerUI')
    engine=QQmlEngine();errors=[]
    engine.warnings.connect(lambda values: errors.extend(v.toString() for v in values))
    component=QQmlComponent(engine,QUrl.fromLocalFile(str(base/'OptimizerPage.qml')))
    page=component.createWithInitialProperties({'previewOnly':True})
    assert page is not None,[e.toString() for e in component.errors()]
    window=QQuickWindow();window.setGeometry(0,0,1000,760)
    page.setParentItem(window.contentItem());page.setWidth(1000);page.setHeight(760)
    window.show();QTest.qWait(100)
    # Exercise actual delegate controls instead of only mutating page properties.
    def find(item, name):
        if item.objectName() == name: return item
        for child in item.childItems():
            found = find(child, name)
            if found is not None: return found
        return None
    def click(name):
        item = find(page, name)
        assert item is not None, name
        position = item.mapToScene(QPointF(item.width()/2, item.height()/2)).toPoint()
        QTest.mouseClick(window, Qt.LeftButton, Qt.NoModifier, position)
        QTest.qWait(200)
    click('voidProfile_performance')
    assert page.property('profile') == 'performance'
    click('voidTimer_5000')
    assert page.property('timer100ns') == 5000
    click('voidEmbeddedPowerControl')
    assert page.property('highPerformancePower') is True
    # Footer remains reachable while the content scrolls; screenshot wide/narrow.
    for width,height in [(1000,760),(650,540),(420,420)]:
        window.resize(width,height);page.setWidth(width);page.setHeight(height)
        QTest.qWait(250)
        button = find(page,'voidEmbeddedReview')
        point = button.mapToScene(QPointF(0,0))
        assert 0 <= point.y() and point.y()+button.height() <= height
        assert button.width()>0 and point.x()+button.width()<=width
        screenshot = root/'.cache/optimizer-repair'/('studio04-%sx%s.png'%(width,height))
        screenshot.parent.mkdir(parents=True,exist_ok=True)
        assert window.grabWindow().save(str(screenshot))
    window.resize(1000,760);page.setWidth(1000);page.setHeight(760)
    requests=[];page.reviewRequested.connect(lambda value: requests.append(value))
    assert not requests,'page creation must not launch a request'
    click('voidEmbeddedReview')
    assert len(requests)==1 and 'profile=performance' in requests[-1] and 'timer=5000' in requests[-1]
    requests.clear()
    for profile,timer,flags in itertools.product(('balanced','performance','eco'),(0,5000,10000),itertools.product((False,True),repeat=4)):
        page.setProperty('profile',profile);page.setProperty('timer100ns',timer)
        for name,value in zip(('highPerformancePower','disableCapture','keepAwake','autoRestore'),flags):page.setProperty(name,value)
        before=len(requests)
        assert QMetaObject.invokeMethod(page,'review')
        assert len(requests)==before+1
        parsed=urlsplit(requests[-1])
        assert parsed.scheme=='voidoptimizer-v3' and parsed.netloc=='review' and not parsed.fragment
        query=parse_qs(parsed.query,strict_parsing=True)
        expected={'profile':[profile],'timer':[str(timer)]}
        expected.update({key:[str(int(value))] for key,value in zip(('power','capture','awake','autorestore'),flags)})
        assert query==expected,(requests[-1],expected)
        assert str(page.property('feedback')).startswith('Preview only:')
    assert len(requests)==144
    assert len(set(requests))==144
    before=len(requests)
    for profile,timer in [('Realtime',0),('balanced',12345)]:
        page.setProperty('profile',profile);page.setProperty('timer100ns',timer)
        assert QMetaObject.invokeMethod(page,'review')
        assert len(requests)==before,'unsupported state must not launch a request'
        assert str(page.property('feedback')).startswith('Review stopped:')
    assert not errors,errors
    window.close()
print('PASS: all 144 real-QML review combinations use the matching v3 contract; invalid state refuses launch; no helper or tuning action executed.')
