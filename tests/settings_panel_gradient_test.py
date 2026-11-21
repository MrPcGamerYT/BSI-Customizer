"""The production selected-tab Gradient must not read Qt's uninitialized default.

Older Qt 5/6 leave an omitted GradientStop.position uninitialized (QTBUG-140364).
Exercise the EXACT generated factory after QML-owned stop allocation/destruction,
with explicit endpoints, live colors and actual pixels. A poisoned endpoint must
fail the same strict reflection assertion. No player, OEM assets or shader plugin.
"""
import json
from pathlib import Path
import re

from shaders_plugin_ui_test import (
    configure_host_qt, QGuiApplication, QQmlEngine, QQmlComponent,
    QQmlExpression, QUrl, QTest, qVersion,
)
from shaders.qt_capture import capture_window

REPO = Path(__file__).resolve().parents[1]


def main():
    cpp = (REPO / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
    factory = re.search(r"(Component\{id:g;Gradient\{[^\n]+\}\})\nfunction fGlassTab", cpp)[1]
    stops = re.findall(r"GradientStop\{([^{}]+)\}", factory)
    assert len(stops) == 2 and all(f"position:{n};" in stop for n, stop in enumerate(stops)), (
        "generated reflection endpoints must be explicit, not Qt's uninitialized default", stops)
    qml = configure_host_qt()
    app = QGuiApplication.instance() or QGuiApplication([])
    app.setQuitOnLastWindowClosed(False)
    engine = QQmlEngine()
    engine.setImportPathList([qml])
    warnings = []
    engine.warnings.connect(lambda errors: warnings.extend(e.toString() for e in errors))
    component = QQmlComponent(engine)
    component.setData(('''import QtQuick 2.15
import QtQuick.Window 2.15
Window {
 id:root; width:160; height:100; visible:false
 property color accent:"#54c8ff"
 property var objects:[]
 Rectangle { anchors.fill:parent; color:"#101a27" }
 Rectangle { id:fill; x:32; y:24; width:105; height:44; radius:6; opacity:.2; color:root.accent }
 Component { id:seed; GradientStop { position:.73; color:"red" } }
 ''' + factory + '''
 function allocate(seeded) {
  for (var n=0;n<64;++n) objects.push(seeded?seed.createObject(root):g.createObject(fill,{s:fill}))
  if (!seeded) fill.gradient=objects[0]
 }
 function endpoints() {
  return objects.map(function(v){return [v.stops[0].position,v.stops[1].position]})
 }
 function clear() {
  fill.gradient=null
  for (var n=0;n<objects.length;++n) objects[n].destroy()
  objects=[]
 }
 function poison() { fill.gradient.stops[0].position=1 }
}
''').encode(), QUrl("ProductionTabGradient.qml"))
    assert component.status() == QQmlComponent.Status.Ready, [e.toString() for e in component.errors()]
    window = component.create()
    assert window is not None, [e.toString() for e in component.errors()]

    def evaluate(text):
        expression = QQmlExpression(engine.rootContext(), window, text)
        result = expression.evaluate()
        assert not expression.hasError(), expression.error().toString()
        return result[0] if isinstance(result, tuple) else result

    def reflection(image):
        # Same local points and strict >6 delta as the complete Settings test.
        def pixel(x, y):
            return image.pixelColor(int(x * image.width() / window.width()),
                                    int(y * image.height() / window.height()))
        top, bottom = pixel(32 + 5, 24 + 8), pixel(32 + 5, 24 + 44 - 8)
        assert top.lightness() > bottom.lightness() + 6, (
            "the selected-tab reflection is not present in the rendered pixels",
            top.name(), bottom.name(), top.lightness(), bottom.lightness())

    try:
        window.create()
        app.processEvents()
        window.show()
        # Only the tiny independent fixture needs Qt 5 offscreen realization;
        # production Settings geometry and the full DPI suite stay unchanged.
        window.resize(160, 100)
        QTest.qWait(20)
        for cycle in range(3):
            evaluate("allocate(true); clear()")
            QTest.qWait(10)  # public QML destroy() is deferred; drain it once
            evaluate("allocate(false)")
            endpoints = json.loads(evaluate("JSON.stringify(endpoints())"))
            assert endpoints == [[0, 1]] * 64, ("a generated endpoint read recycled memory", cycle, endpoints)
            for accent in ("#54c8ff", "#f59e0b"):
                window.setProperty("accent", accent)
                reflection(capture_window(window))
            evaluate("poison()")
            try:
                reflection(capture_window(window))
            except AssertionError as error:
                assert error.args[0][0] == "the selected-tab reflection is not present in the rendered pixels"
            else:
                raise AssertionError("a poisoned first endpoint passed the unchanged pixel assertion")
            evaluate("clear()")
            QTest.qWait(10)
        assert not warnings, "\n".join(warnings)
        print(f"PASS Qt {qVersion()}: exact production gradient factory, 192 explicit endpoint pairs after QML heap reuse, live colors and strict painted reflection/poisoned-endpoint negatives; no OEM assets or shader plugin", flush=True)
    finally:
        window.hide()
        window.deleteLater()
        QTest.qWait(20)
        engine.deleteLater()
        QTest.qWait(20)


if __name__ == "__main__":
    main()
