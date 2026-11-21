"""Exercise the explicit nonpainting tooltip adapter without OEM QML/plugin.

A bound attached property used to crash independent PyQt5 hosts when its
Python wrapper was collected before the QML binding wrote to the QObject.
Do not fix that by editing native controls or weakening actual-native tests.
"""
import gc
import json

from shaders_plugin_ui_test import (
    configure_host_qt, QGuiApplication, QQmlEngine, QQmlComponent,
    QQmlExpression, QTest, QUrl, qVersion,
)
from shaders import native_ui_fixture as native


def main():
    configure_host_qt()
    app = QGuiApplication.instance() or QGuiApplication([])
    engine = QQmlEngine()
    native.register_tooltip_service()
    native.register_tooltip_service(shortcut=True)
    warnings = []
    engine.warnings.connect(lambda errors: warnings.extend(e.toString() for e in errors))
    for cycle in range(3):
        component = QQmlComponent(engine)
        component.setData(b'''import QtQuick 2.15
import UiToolTipControl 1.0
import UiShortcutToolTip 1.0
Item {
 id: root
 property bool showFirst: true
 property bool showSecond: false
 property string note: "first"
 Item {
  id:first; UiToolTip.visible:root.showFirst; UiToolTip.text:root.note
  UiShortcutToolTip.visible:root.showSecond; UiShortcutToolTip.text:"shortcut-first"
 }
 Item {
  id:second; UiToolTip.visible:root.showSecond; UiToolTip.text:"second"
  UiShortcutToolTip.visible:root.showFirst; UiShortcutToolTip.text:root.note
 }
 function values() { return JSON.stringify([first.UiToolTip.visible,first.UiToolTip.text,
                                         second.UiToolTip.visible,second.UiToolTip.text,
                                         first.UiShortcutToolTip.visible,first.UiShortcutToolTip.text,
                                         second.UiShortcutToolTip.visible,second.UiShortcutToolTip.text]) }
}
''', QUrl("TooltipServiceContract.qml"))
        assert component.status() == QQmlComponent.Status.Ready, [e.toString() for e in component.errors()]
        owner = component.create()
        assert owner is not None, [e.toString() for e in component.errors()]
        assert len(native._service_objects) == 4, "both services must have distinct, retained per-owner wrappers"
        assert all(obj.parent() is not None and not obj.inherits("QQuickItem")
                   for obj in native._service_objects.values()), "tooltip service doubles must not paint"

        def values():
            expression = QQmlExpression(QQmlEngine.contextForObject(owner), owner, "values()")
            result = expression.evaluate()
            assert not expression.hasError(), expression.error().toString()
            return json.loads(result[0] if isinstance(result, tuple) else result)

        gc.collect()
        QTest.qWait(20)
        assert values() == [True, "first", False, "second", False, "shortcut-first", True, "first"]
        owner.setProperty("showFirst", False)
        owner.setProperty("showSecond", True)
        owner.setProperty("note", "changed")
        gc.collect()
        QTest.qWait(20)
        assert values() == [False, "changed", True, "second", True, "shortcut-first", False, "changed"], "live per-owner/service bindings were lost"
        owner.deleteLater()
        QTest.qWait(20)
        gc.collect()
        assert not native._service_objects, "destroyed QML owners leaked retained tooltip wrappers"
    assert not warnings, "\n".join(warnings)
    print("PASS: Qt", qVersion(), "nonpainting normal/shortcut tooltip services, independent per-owner bool/text bindings survive GC and all wrappers release on owner destruction in three cycles; no OEM assets, shader plugin or player", flush=True)


if __name__ == "__main__":
    main()
