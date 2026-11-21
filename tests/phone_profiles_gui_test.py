"""Real Qt controls + production Phone extension, with authored native services.

This is not an emulator/game or an FPS whitelist test. No real config, network,
Android framework, profile, game or root/FPS/renderer setting is accessed.
"""
import json
import os
import shutil
import xml.etree.ElementTree as ET
from pathlib import Path
import subprocess
import tempfile

from shaders_plugin_ui_test import (
    QGuiApplication, QObject, QPointF, QQmlComponent, QQmlEngine, QQmlExpression,
    Qt, QTest, QUrl, configure_host_qt,
)
if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
    from PyQt5.QtCore import QResource, pyqtSlot as Slot
else:
    from PySide6.QtCore import QResource, Slot


class Config(QObject):
    def __init__(self):
        super().__init__()
        self.saved = {"device_profile_code": "stock-a", "device_carrier_code": "carrier-a",
                      "device_custom_manufacturer": "", "device_custom_brand": "", "device_custom_model": "",
                      "enable_root_access": False, "enable_high_fps": False, "fps": 60,
                      "graphics_renderer": "gl"}
        self.draft = {}
        self.commits = 0

    @Slot(str, result=str)
    def confGetInstanceString(self, key):
        return str(self.draft.get(key, self.saved.get(key, "")))

    @Slot(str, str)
    def confSetInstanceString(self, key, value):
        assert key in {"device_profile_code", "device_carrier_code", "device_custom_manufacturer",
                       "device_custom_brand", "device_custom_model"}, (key, value)
        if value == self.saved.get(key, ""):
            self.draft.pop(key, None)
        else:
            self.draft[key] = value

    @Slot(str, result=bool)
    def confIsInstancePropModified(self, key):
        return key in self.draft and self.draft[key] != self.saved.get(key, "")

    @Slot(result=bool)
    def confIsModified(self):
        return bool(self.draft)

    @Slot()
    def confCommit(self):
        self.saved.update(self.draft)
        self.draft.clear()
        self.commits += 1

    @Slot()
    def confDiscard(self):
        self.draft.clear()


class Player(QObject):
    def __init__(self):
        super().__init__()
        self.requests = 0
        self.calls = []

    @Slot()
    def getDeviceProfileList(self):
        self.requests += 1

    @Slot(str, str)
    def setDeviceProfile(self, profile, carrier):
        self.calls.append(("native", profile, carrier))

    @Slot(str, str, str, str)
    def setCustomDeviceProfile(self, manufacturer, brand, model, carrier):
        self.calls.append(("custom", manufacturer, brand, model, carrier))


class Toast(QObject):
    def __init__(self):
        super().__init__()
        self.messages = []

    @Slot(str)
    def fShowToast(self, message):
        self.messages.append(message)


def main():
    planner = Path(os.environ["BSI_PHONE_PROFILES_TEST"]).resolve()
    assert planner.is_file(), planner
    runtime_qml = configure_host_qt()
    app = QGuiApplication.instance() or QGuiApplication([])
    config, player, toast = Config(), Player(), Toast()
    engine = QQmlEngine()
    engine.setImportPathList([runtime_qml, "qrc:/qt-project.org/imports", "qrc:/qt/qml"])
    engine.rootContext().setContextProperty("uiBackend", config)
    engine.rootContext().setContextProperty("plrBackend", player)
    engine.rootContext().setContextProperty("iSettingsWindow", toast)
    warnings = []
    engine.warnings.connect(lambda errors: warnings.extend(e.toString() for e in errors))
    cloud = {"device_profile_list": [{"display_name": "Stock phone A", "pcode": "stock-a"},
                                     {"display_name": "Stock phone B", "pcode": "stock-b"}],
             "ca_selector_list": [{"display_name": "Carrier A", "ca_selector": "carrier-a"},
                                  {"display_name": "Carrier B", "ca_selector": "carrier-b"}]}

    with tempfile.TemporaryDirectory(prefix="bsi-phone-contract-") as temporary:
        directory = Path(temporary) / "local data é"
        directory.mkdir()
        subprocess.run([str(planner), "--export", str(directory)], check=True)
        catalog = json.loads((directory / "catalog.json").read_text())
        expected_names = [p["name"] for p in catalog]
        assert len(catalog) == 35 and catalog[0]["model"] == "ASUSAI2501A"
        count = len(catalog)
        component = QQmlComponent(engine)
        component.setData(b'''import QtQuick 2.15
import QtQuick.Window 2.15
Window { width:568;height:470;visible:true; property alias page:loader.item
Loader{id:loader;anchors.fill:parent}
function load(name){loader.source=name}
}''', QUrl.fromLocalFile(str(directory / "Host.qml")))
        window = component.create()
        assert window, [e.toString() for e in component.errors()]
        window.requestActivate()

        def evaluate(context, expression):
            expr = QQmlExpression(QQmlEngine.contextForObject(context) or engine.rootContext(), context, expression)
            value = expr.evaluate()
            assert not expr.hasError(), expr.error().toString()
            return value[0] if isinstance(value, tuple) else value

        def named(page, name):
            pending = list(page.childItems())
            while pending:
                obj = pending.pop()
                if obj.objectName() == name:
                    return obj
                pending.extend(obj.childItems())
            raise AssertionError(name)

        def receive(page, payload=None):
            evaluate(page, "fDeviceProfilesReceivedFromCloud(" + json.dumps(json.dumps(payload or cloud)) + ")")
            QTest.qWait(10)

        def load(filename="Phone.qml", payload=None):
            # Reset the Loader first: assigning its current URL only refreshes
            # data and is not evidence of a genuine close/reopen lifecycle.
            evaluate(window, 'load("")')
            QTest.qWait(10)
            evaluate(window, "load(" + json.dumps(filename) + ")")
            window.requestActivate()
            QTest.qWait(25)
            page = window.property("page")
            assert page, warnings
            evaluate(page, "fOnQmlLoaded()")
            receive(page, payload)
            return page

        def names(page):
            return json.loads(evaluate(page, "JSON.stringify((function(){var a=[];for(var i=0;i<pDeviceProfileModel.count;i++)a.push(pDeviceProfileModel.get(i).name);return a;})())"))

        def click(control):
            assert control.isVisible() and control.isEnabled(), control.objectName()
            point = control.mapToScene(QPointF(control.width() / 2, control.height() / 2)).toPoint()
            QTest.mouseClick(window, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point)
            QTest.qWait(15)

        def choose(page, index):
            combo = named(page, "phoneDropdown")
            click(combo)
            QTest.keyClick(window, Qt.Key.Key_Home)
            for _ in range(index):
                QTest.keyClick(window, Qt.Key.Key_Down)
            QTest.keyClick(window, Qt.Key.Key_Return)
            QTest.qWait(15)
            assert combo.property("currentIndex") == index, (index, combo.property("currentIndex"))

        def metrics(page):
            out = {}
            for name in ("predefined", "phoneDropdown", "custom", "carrierDropdown", "save"):
                obj = named(page, name)
                out[name] = (obj.x(), obj.y(), obj.width(), obj.height(), obj.property("font").toString())
            return out

        stock = load("StockPhone.qml")
        stock_metrics = metrics(stock)
        assert names(stock) == ["Stock phone A", "Stock phone B"]
        page = load()
        assert names(page) == expected_names + ["Stock phone A", "Stock phone B"]
        assert metrics(page) == stock_metrics, "the stock controls/font/geometry changed"
        assert named(page, "phoneDropdown").property("currentIndex") == count
        assert not config.draft and not player.calls and not named(page, "save").isEnabled(), "loading must not select/write/apply a phone"
        receive(page)
        assert names(page) == expected_names + ["Stock phone A", "Stock phone B"], "cloud refresh duplicated or reordered stock profiles"

        for index, profile in enumerate(catalog):
            before = dict(config.saved)
            choose(page, index)
            assert named(page, "predefined").property("checked") and named(page, "phoneDropdown").isVisible()
            assert not named(page, "custom").property("checked"), "local preset must still look like a predefined choice"
            assert config.saved == before and config.confIsModified(), "selection must be a native unsaved draft only"
            assert config.confGetInstanceString("device_custom_model") == profile["model"]
            click(named(page, "save"))
            assert player.calls[-1] == ("custom", profile["manufacturer"], profile["brand"], profile["model"], "carrier-a")
            assert config.saved == before and not named(page, "save").isEnabled(), "no commit before native success callback"
            call_count = len(player.calls)
            evaluate(page, "fHandleConfChange()")
            assert len(player.calls) == call_count, "a second Save cannot issue duplicate native calls"
            if index == 0:
                # Changing the visible selection while a native response is
                # pending must not alter the already captured request/commit.
                choose(page, 1)
                evaluate(page, "fHandleConfChange()")
                assert len(player.calls) == call_count
            evaluate(page, "fDeviceProfileSettingChangedClbk(0)")
            assert config.saved["device_profile_code"] == "custom" and config.saved["device_custom_model"] == profile["model"]
            assert not config.draft and not named(page, "save").isEnabled()
            assert named(page, "phoneDropdown").property("currentIndex") == index
            assert all(config.saved[k] == before[k] for k in ("enable_root_access", "enable_high_fps", "fps", "graphics_renderer", "device_carrier_code"))
            evaluate(page, "fHandleConfChange()")
            assert len(player.calls) == call_count, "a clean programmatic Save must be a no-op, not lock the next edit"
            page = load()
            assert named(page, "phoneDropdown").property("currentIndex") == index and not config.draft, "saved custom identity must reopen as the same named choice"

        # Native carrier-only changes on a saved preset use the same custom
        # identity, not the cloud endpoint or an empty model.
        carrier = named(page, "carrierDropdown")
        click(carrier)
        QTest.keyClick(window, Qt.Key.Key_End)
        QTest.keyClick(window, Qt.Key.Key_Return)
        QTest.qWait(10)
        click(named(page, "save"))
        last_profile = catalog[-1]
        assert player.calls[-1] == ("custom", last_profile["manufacturer"], last_profile["brand"], last_profile["model"], "carrier-b")
        evaluate(page, "fDeviceProfileSettingChangedClbk(0)")
        assert config.saved["device_carrier_code"] == "carrier-b"
        click(carrier)
        QTest.keyClick(window, Qt.Key.Key_Home)
        QTest.keyClick(window, Qt.Key.Key_Return)
        QTest.qWait(10)
        click(named(page, "save"))
        evaluate(page, "fDeviceProfileSettingChangedClbk(0)")
        assert config.saved["device_carrier_code"] == "carrier-a"

        # Returning to the saved choice cancels only the selector's own draft.
        last = dict(config.saved)
        choose(page, 0)
        choose(page, count - 1)
        assert config.saved == last and not config.draft and not named(page, "save").isEnabled()
        choose(page, 0)
        before_calls = len(player.calls)
        evaluate(page, "fDiscardChanges()")
        receive(page)
        assert config.saved == last and not config.draft and len(player.calls) == before_calls
        assert named(page, "phoneDropdown").property("currentIndex") == count - 1

        # Native failure keeps the old identity; callbacks are not forged.
        choose(page, 0)
        click(named(page, "save"))
        evaluate(page, "fDeviceProfileSettingChangedClbk(1)")
        assert config.saved == last and not config.draft and "Failed" in toast.messages[-1]
        assert named(page, "phoneDropdown").property("currentIndex") == count - 1

        # Existing server profiles keep the real server API/code, below extras.
        choose(page, count + 1)
        click(named(page, "save"))
        assert player.calls[-1] == ("native", "stock-b", "carrier-a")
        evaluate(page, "fDeviceProfileSettingChangedClbk(0)")
        assert config.saved["device_profile_code"] == "stock-b" and config.saved["device_custom_model"] == ""
        assert named(page, "phoneDropdown").property("currentIndex") == count + 1
        choose(page, 0)
        choose(page, count + 1)
        assert not config.draft, "returning to the original cloud phone must clear stale local custom-field drafts"

        # Do not discard an unrelated staged carrier/setting on selection.
        config.draft["device_carrier_code"] = "carrier-b"
        config.draft["unrelated"] = "other-page-draft"
        choose(page, 1)
        assert config.draft["device_carrier_code"] == "carrier-b" and config.draft["unrelated"] == "other-page-draft"
        config.confDiscard()
        page = load()

        # Genuine cloud support for one of the added names takes precedence,
        # remains first, and is not duplicated or converted to a made-up code.
        duplicate = json.loads(json.dumps(cloud))
        duplicate["device_profile_list"].append({"display_name": expected_names[0], "pcode": "real-rog9-cloud"})
        duplicate["device_profile_list"].append({"display_name": expected_names[0], "pcode": "duplicate-rog9-cloud"})
        page = load(payload=duplicate)
        assert names(page).count(expected_names[0]) == 1 and names(page)[:count] == expected_names
        choose(page, 0)
        click(named(page, "save"))
        assert player.calls[-1] == ("native", "real-rog9-cloud", "carrier-a")
        evaluate(page, "fDeviceProfileSettingChangedClbk(0)")
        assert config.saved["device_profile_code"] == "real-rog9-cloud"

        # Unknown and custom identities are preserved, never auto-selected to ROG.
        config.saved.update(device_profile_code="unknown-cloud")
        page = load()
        assert named(page, "phoneDropdown").property("currentIndex") == -1 and not config.draft
        config.saved.update(device_profile_code="custom", device_custom_manufacturer="Other", device_custom_brand="Other", device_custom_model="Unlisted")
        page = load()
        assert named(page, "custom").property("checked") and config.saved["device_custom_model"] == "Unlisted"
        # Manual custom controls remain functional and unknown identities stay
        # manual, rather than being mislabeled as a catalogued phone.
        for name, text in (("manufacturer", "manualmaker"), ("brand", "manualbrand"), ("model", "manualmodel")):
            field = named(page, name)
            click(field)
            QTest.keyClick(window, Qt.Key.Key_A, Qt.KeyboardModifier.ControlModifier)
            for character in text:
                modifier = Qt.KeyboardModifier.ShiftModifier if character.isupper() else Qt.KeyboardModifier.NoModifier
                QTest.keyClick(window, Qt.Key(ord(character.upper())), modifier)
            QTest.qWait(5)
        assert config.saved["device_custom_model"] == "Unlisted"
        click(named(page, "save"))
        assert player.calls[-1] == ("custom", "manualmaker", "manualbrand", "manualmodel", "carrier-a"), player.calls[-1]
        evaluate(page, "fDeviceProfileSettingChangedClbk(0)")
        page = load()
        assert named(page, "custom").property("checked") and config.saved["device_custom_model"] == "manualmodel" and not config.draft

        # A local preset with no known carrier must refuse before a callback
        # count is incremented, not become stuck waiting for a call never sent.
        config.saved.update(device_profile_code="stock-a", device_carrier_code="", device_custom_manufacturer="", device_custom_brand="", device_custom_model="")
        page = load()
        choose(page, 0)
        count = len(player.calls)
        click(named(page, "save"))
        assert len(player.calls) == count and page.property("pCallbackExpectedCount") == 0 and config.draft
        assert "network provider" in toast.messages[-1]

        # Actual installed QML originates in qrc, while the helper is a local
        # file URL. Exercise that precise Qt trust/import boundary on both Qt
        # majors, not just file-to-file loading. Format 1 has no Qt 6 zstd-only
        # feature; source bytes are deliberately uncompressed.
        rcc = os.environ.get("BSI_PHONE_RCC") or shutil.which("rcc")
        if not rcc:
            candidates = [Path(__import__("sys").executable).parent.parent / "lib/python3.11/site-packages/PySide6/Qt/libexec/rcc",
                          Path("/usr/lib/qt5/bin/rcc"),
                          Path(__file__).resolve().parents[1] / ".venv/lib/python3.11/site-packages/PySide6/Qt/libexec/rcc"]
            rcc = next((str(path) for path in candidates if path.is_file()), None)
        assert rcc, "rcc is required to test installed qrc-to-local-script loading"
        qrc = ET.Element("RCC")
        resource = ET.SubElement(qrc, "qresource", prefix="/bsi-phone-contract")
        for name in ("Phone.qml", "UiComboBox.qml"):
            ET.SubElement(resource, "file", alias=name).text = str(directory / name)
        qrc_path, binary = directory / "Phone.qrc", directory / "Phone.rcc"
        ET.ElementTree(qrc).write(qrc_path, encoding="utf-8")
        subprocess.run([str(rcc), "--binary", "--format-version", "1", "--no-compress", str(qrc_path), "-o", str(binary)], check=True)
        assert QResource.registerResource(str(binary)), "register independently compiled binary QRC"
        config.confDiscard()
        config.saved.update(device_profile_code="stock-a", device_carrier_code="carrier-a", device_custom_manufacturer="", device_custom_brand="", device_custom_model="")
        page = load("qrc:/bsi-phone-contract/Phone.qml")
        assert names(page) == expected_names + ["Stock phone A", "Stock phone B"] and not config.draft
        choose(page, 0)
        click(named(page, "save"))
        assert player.calls[-1] == ("custom", catalog[0]["manufacturer"], catalog[0]["brand"], catalog[0]["model"], "carrier-a")
        evaluate(page, "fDeviceProfileSettingChangedClbk(0)")
        page = load("qrc:/bsi-phone-contract/Phone.qml")
        assert named(page, "phoneDropdown").property("currentIndex") == 0 and not config.draft
        assert not warnings, "\n".join(warnings)
        evaluate(window, 'load("")')
        QTest.qWait(10)
        engine.clearComponentCache()
        assert QResource.unregisterResource(str(binary))
        window.hide()
        window.deleteLater()
        QTest.qWait(10)
    print("PASS: stock dropdown controls/fonts/geometry retained; all 35 phones first; physical selection/Save, catalogued-model native custom API arguments, draft-only selection, success/failure/duplicate-Save, persistence/reopen, Discard, cloud-code priority/no duplicates, native/manual/carrier flows, qrc-to-local-script import, missing-carrier refusal and untouched FPS/root/renderer settings")
    print("LIMIT: authored native-service contract with production QML/JS on a real Qt host, not an installed player, guest build properties, game whitelist or measured FPS")


if __name__ == "__main__":
    main()
