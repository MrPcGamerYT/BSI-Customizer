"""Real Qt review-URI matrix. Preview mode suppresses external helper launch."""
from pathlib import Path
import itertools
import os
import shutil
import tempfile
from urllib.parse import urlsplit, parse_qs
os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
os.environ.setdefault('QSG_RHI_BACKEND','software')
from PySide6.QtCore import QUrl, QMetaObject
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlEngine, QQmlComponent
from PySide6.QtQuick import QQuickWindow, QQuickItem
from PySide6.QtTest import QTest

root=Path(__file__).resolve().parents[1]
app=QGuiApplication([])
with tempfile.TemporaryDirectory() as directory:
    base=Path(directory)
    shutil.copyfile(root/'assets/optimizer-repair/Assets/OptimizerPage.qml',base/'OptimizerPage.qml')
    # Palette fixture only: preserve runtime page code and the actual shared icon API.
    shutil.copyfile(root/'assets/settings-icons-repair/Shared-Recovery/Icons.js',base/'Icons.js')
    (base/'qmldir').write_text('singleton Theme 1.0 Theme.qml\n')
    (base/'Theme.qml').write_text('''pragma Singleton
import QtQuick 2.15
QtObject {
 readonly property color background: "#080809"
 readonly property color surface: "#111114"
 readonly property color raised: "#26262e"
 readonly property color border: "#45454f"
 readonly property color muted: "#94949f"
 readonly property color accent: "#f4f4f7"
 readonly property color text: "#f4f4f7"
}''')
    engine=QQmlEngine();errors=[]
    engine.warnings.connect(lambda values: errors.extend(v.toString() for v in values))
    component=QQmlComponent(engine,QUrl.fromLocalFile(str(base/'OptimizerPage.qml')))
    page=component.createWithInitialProperties({'previewOnly':True})
    assert page is not None,[e.toString() for e in component.errors()]
    window=QQuickWindow();window.setGeometry(0,0,1000,760)
    page.setParentItem(window.contentItem());page.setWidth(1000);page.setHeight(760)
    window.show();QTest.qWait(100)
    requests=[];page.reviewRequested.connect(lambda value: requests.append(value))
    assert not requests,'page creation must not launch a request'
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
