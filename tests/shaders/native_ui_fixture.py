"""Authored public native-control contracts, not copies of player QML/artwork.

The production page imports qrc:/Ui*.qml from the PLAYER. An independent Qt
host has none, so explicitly register these visibly marked test-only controls
at that same boundary. Tests prove delegation/theme identity and callbacks;
they do not certify proprietary control pixels or an installed emulator.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET


def compile_resources(qrc_path, binary):
    """rcc v1/no-compression is readable by both independent host generations."""
    rcc = os.environ.get("BSI_PHONE_RCC") or shutil.which("rcc")
    if not rcc:
        import importlib.util
        binding = importlib.util.find_spec("PySide6")
        candidates = ([Path(binding.origin).parent / "Qt/libexec/rcc",
                       Path(binding.origin).parent / "rcc.exe"] if binding else [])
        candidates += [Path("/usr/lib/qt5/bin/rcc")]
        rcc = next((str(path) for path in candidates if path.is_file()), None)
    assert rcc, "A build-host rcc is required for the explicit native UI resource test"
    subprocess.run([str(rcc), "--binary", "--format-version", "1", "--no-compress", str(qrc_path), "-o", str(binary)], check=True)


def write_shell_material(directory):
    """The exact owned About/material, including its real Loader boundary."""
    source = (Path(__file__).resolve().parents[2] / "src/core/SettingsPanelPatcher.cpp").read_text(encoding="utf-8")
    qml = re.search(r'QString generated = QStringLiteral\(R"QML\((.*?)\)QML"\);', source, re.S)[1]
    for key, value in {"DEVELOPER": '"Native control resource test"', "TAGLINE": '"Not an installed player"',
                       "TAB_ICONS": "({})", "LINKS": "", "LOGO_SOURCE": '""'}.items():
        qml = qml.replace("@@BSI_" + key + "@@", value)
    assert "@@BSI_" not in qml
    (Path(directory) / "ShellMaterial.qml").write_text(qml, encoding="utf-8")


def assert_component(item, kind):
    if os.environ.get("BSI_SHADERS_NATIVE_UI_DIR"):
        assert item.metaObject().className().startswith(kind + "_QMLTYPE_"), (kind, item.metaObject().className())
        assert item.metaObject().indexOfProperty("bsiFixtureKind") < 0, "authored controls substituted for the requested real resources"
    else:
        assert item.property("bsiFixtureKind") == kind, ("native component boundary bypassed", kind)


_service_types = []
_shortcut_service_types = []
_service_objects = {}


def register_tooltip_service(shortcut=False):
    """Nonpainting attached-property double, independent of OEM/player code."""
    if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
        from PyQt5.QtCore import QObject, pyqtProperty as Property
        from PyQt5.QtQml import qmlRegisterType
    else:
        from PySide6.QtCore import QObject, Property
        from PySide6.QtQml import QmlAttached, qmlRegisterType
    types = _shortcut_service_types if shortcut else _service_types
    uri, name = ("UiShortcutToolTip", "UiShortcutToolTip") if shortcut else ("UiToolTipControl", "UiToolTip")
    assert not types, "run each native resource family in its own process"

    class TipProperties(QObject):
        def __init__(self, parent=None):
            super().__init__(parent)
            self._visible, self._text = False, ""
            # PyQt's attached-object factory does not keep its Python wrapper
            # alive for later QML bindings, despite the QObject parent. A bound
            # bool write then targets a freed object. Retain exactly until the
            # native owner destroys it, not for the life of the test process.
            identity = id(self)
            _service_objects[identity] = self
            self.destroyed.connect(lambda _=None, key=identity: _service_objects.pop(key, None))

        @Property(bool)
        def visible(self): return self._visible

        @visible.setter
        def visible(self, value): self._visible = value

        @Property(str)
        def text(self): return self._text

        @text.setter
        def text(self, value): self._text = value

    class Tip(QObject):
        @staticmethod
        def qmlAttachedProperties(_parent, obj):
            return TipProperties(obj)

    if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
        qmlRegisterType(Tip, uri, 1, 0, name, attachedProperties=TipProperties)
    else:
        Tip = QmlAttached(TipProperties)(Tip)
        qmlRegisterType(Tip, uri, 1, 0, name)
    types.extend((TipProperties, Tip))


def install_player_resources(engine, directory, reference):
    """Use unmodified, hash-checked extracted QML/assets, never OEM binaries.

    Tooltip attachments and dialog-window/button-model C++ services are authored
    nonpainting doubles. Actual button/checkbox/combo/textfield/slider/layout/
    scroll/image/dialog/theme QML is native, NOT rewritten to make it load.
    This is still not a running player.
    """
    if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
        from PyQt5.QtCore import QResource, QUrl
        from PyQt5.QtQml import QQmlComponent
    else:
        from PySide6.QtCore import QResource, QUrl
        from PySide6.QtQml import QQmlComponent
    metadata = json.loads((reference / "inventory.json").read_text(encoding="utf-8"))
    # Checking every payload here also prevents a hand-edited extracted control
    # from being reported as original resource coverage.
    for entry in metadata["resources"]:
        payload = (reference / entry["path"]).read_bytes()
        assert len(payload) == entry["bytes"] and hashlib.sha256(payload).hexdigest() == entry["sha256"], entry["path"]
    assert "UiThemeObject.qml" in {e["path"] for e in metadata["resources"]}
    register_tooltip_service()
    if b"import UiShortcutToolTip 1.0" in (reference / "UiImageButton.qml").read_bytes():
        # Newer native close buttons import this separate attached service even
        # when their default pUseShortcutTooltip is false. No tooltip is painted
        # or shortcut registry imitated by either explicit service double.
        register_tooltip_service(shortcut=True)
    binary = Path(directory) / "player-native.rcc"
    compile_resources(reference / "native.qrc", binary)
    assert QResource.registerResource(str(binary))
    for entry in metadata["resources"]:
        actual = bytes(QResource(":/" + entry["path"]).uncompressedData())
        assert hashlib.sha256(actual).hexdigest() == entry["sha256"], ("QRC path/hash mismatch", entry["path"])
    component = QQmlComponent(engine, QUrl("qrc:/UiThemeObject.qml"))
    assert component.status() == QQmlComponent.Status.Ready, [e.toString() for e in component.errors()]
    theme = component.create()
    assert theme, [e.toString() for e in component.errors()]
    theme.setParent(engine)
    engine.rootContext().setContextProperty("UiTheme", theme)
    print("UNMODIFIED native QRC/theme:", metadata["reference"], metadata["referenceSha256"], flush=True)
    return theme, lambda: QResource.unregisterResource(str(binary))


def theme_source(singleton=False):
    return ('pragma Singleton\n' if singleton else '') + '''import QtQuick 2.15
QtObject {
 id: theme
 property QtObject colors: QtObject {
  property color primary10: "#f4f7fb"
  property color primary20: "#d2dae4"
  property color primary30: "#a9adba"
  property color primary40: "#8f94a3"
  property color primary50: "#5c6170"
  property color primary60: "#35465a"
  property color primary70: "#090c11"
  property color primary80: "#101a27"
  property color primary90: "#080d15"
  property color accentNormal: "#54c8ff"
  property color subWindowHeaderClick: accentNormal
  property color dangerNormal: "#ee3344"
  property color warning: "#edc35a"
  property color seperator: primary60
 }
 property QtObject fonts: QtObject {
  property font bodyLarge: Qt.font({family: "Segoe UI", pixelSize: 14})
  property font bodyMedium: Qt.font({family: "Segoe UI", pixelSize: 13})
  property font bodySmall: Qt.font({family: "Segoe UI", pixelSize: 12})
  property font buttonMedium: Qt.font({family: "Segoe UI", pixelSize: 15})
  property font buttonSmall: Qt.font({family: "Segoe UI", pixelSize: 11})
 }
 property Gradient buttonBorder: Gradient {
  orientation: Gradient.Horizontal
  GradientStop { color: theme.colors.accentNormal }
  GradientStop { position: 1; color: theme.colors.dangerNormal }
 }
 property QtObject buttons: QtObject {
  property QtObject sizes: QtObject {
   property QtObject medium: QtObject {
    property real height: 32
    property real radius: 0
    property real horizontalPadding: 16
    property real spacing: 8
    property font textFont: theme.fonts.buttonMedium
   }
   property QtObject small: QtObject {
    property real height: 23
    property real radius: 0
    property real horizontalPadding: 10
    property real spacing: 6
    property font textFont: theme.fonts.buttonSmall
   }
  }
  property QtObject colors: QtObject {
   property QtObject accent: QtObject {
    property QtObject normal: QtObject {
     property color background: theme.colors.primary80
     property color textColor: theme.colors.primary10
     property color borderColor: theme.colors.accentNormal
     property Gradient borderGradient: theme.buttonBorder
    }
    property QtObject hover: QtObject {
     property color background: theme.colors.primary90
     property color textColor: theme.colors.primary10
     property color borderColor: theme.colors.accentNormal
     property Gradient borderGradient: theme.buttonBorder
    }
    property QtObject click: QtObject {
     property color background: theme.colors.primary70
     property color textColor: theme.colors.primary10
     property color borderColor: theme.colors.accentNormal
     property Gradient borderGradient: theme.buttonBorder
    }
   }
   property QtObject secondary: QtObject {
    property QtObject normal: QtObject {
     property color background: theme.colors.primary80
     property color textColor: theme.colors.primary10
     property color borderColor: theme.colors.primary50
     property Gradient borderGradient: null
    }
    property QtObject hover: normal
    property QtObject click: normal
   }
   property QtObject transparent: QtObject {
    property QtObject normal: QtObject {
     property color background: "transparent"
     property color textColor: theme.colors.primary20
     property color borderColor: "transparent"
     property Gradient borderGradient: null
    }
    property QtObject hover: normal
    property QtObject click: normal
   }
   property QtObject danger: accent
  }
 }
 property QtObject checkBoxes: QtObject {
  property QtObject sizes: QtObject {
   property QtObject medium: QtObject {
    property real spacing: 8
    property real iconWidth: 14
    property real iconHeight: 14
    property font textFont: theme.fonts.bodyMedium
   }
  }
 }
 property QtObject comboBoxes: QtObject {
  property QtObject sizes: QtObject {
   property QtObject medium: QtObject { property real height: 32 }
  }
 }
 property QtObject sliders: QtObject {
  property QtObject sizes: QtObject {
   property QtObject medium: QtObject { property real handleWidth: 20; property real handleHeight: 20 }
  }
 }
}
'''


def install(engine, directory, theme=None):
    """Register root native QML contracts; caller retains engine and cleanup."""
    from .native_dialog_fixture import register as register_dialog_services, message_source
    register_dialog_services(directory)
    if os.environ.get("BSI_SHADERS_NATIVE_UI_DIR"):
        assert theme is None, "real-resource mode must use the player's real theme, not the authored singleton"
        return install_player_resources(engine, directory, Path(os.environ["BSI_SHADERS_NATIVE_UI_DIR"]))
    if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
        from PyQt5.QtCore import QResource, QUrl
        from PyQt5.QtQml import QQmlComponent
    else:
        from PySide6.QtCore import QResource, QUrl
        from PySide6.QtQml import QQmlComponent
    root = Path(directory) / "native-ui-contracts"
    root.mkdir()
    sources = {"NativeTheme.qml": theme_source(), "UiMessageDialog.qml": message_source()}
    # Generic Qt Controls with native public property names; distinctive kind
    # markers make any accidental use of the old Shader* lookalikes fail.
    sources["UiButton.qml"] = '''import QtQuick 2.15
import QtQuick.Controls 2.15
Button {
 readonly property string bsiFixtureKind: "UiButton"
 property QtObject pSize
 property QtObject pColor
 readonly property QtObject pColorState: down ? pColor.click : hovered ? pColor.hover : pColor.normal
 implicitHeight: pSize.height
 implicitWidth: label.implicitWidth + 2 * pSize.horizontalPadding
 leftPadding: pSize.horizontalPadding; rightPadding: leftPadding
 topPadding: 0; bottomPadding: 0
 font: pSize.textFont
 hoverEnabled: true
 contentItem: Text { id: label; text: parent.text; font: parent.font; color: parent.pColorState.textColor; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
 background: Rectangle { color: parent.pColorState.background; border.color: parent.pColorState.borderColor; radius: parent.pSize.radius }
}
'''
    sources["UiCheckBox.qml"] = '''import QtQuick 2.15
import QtQuick.Controls 2.15
CheckBox {
 readonly property string bsiFixtureKind: "UiCheckBox"
 property QtObject pSize
 font: pSize.textFont; spacing: pSize.spacing
 // The standalone contract must remain readable on the dark sample host.
 // These are public Qt palette roles, not a copy of the native indicator.
 palette.windowText: UiTheme.colors.primary10
 palette.text: UiTheme.colors.primary10
 palette.button: UiTheme.colors.primary70
 palette.light: UiTheme.colors.primary60
 palette.dark: UiTheme.colors.primary60
 palette.highlight: UiTheme.colors.accentNormal
 opacity: enabled ? 1 : 0.4
}
'''
    sources["UiComboBox.qml"] = '''import QtQuick 2.15
import QtQuick.Controls 2.15
ComboBox {
 readonly property string bsiFixtureKind: "UiComboBox"
 property QtObject pSize
 implicitHeight: pSize.height
 font: UiTheme.fonts.bodyMedium
 palette.button: UiTheme.colors.primary70
 palette.buttonText: UiTheme.colors.primary10
 palette.base: UiTheme.colors.primary80
 palette.text: UiTheme.colors.primary10
 palette.windowText: UiTheme.colors.primary10
 palette.highlight: UiTheme.colors.accentNormal
 palette.highlightedText: UiTheme.colors.primary80
}
'''
    sources["UiSlider.qml"] = '''import QtQuick 2.15
import QtQuick.Controls 2.15
Slider {
 readonly property string bsiFixtureKind: "UiSlider"
 property QtObject pSize
 implicitHeight: pSize.handleHeight
 padding: 0
}
'''
    sources["UiScrollView.qml"] = '''import QtQuick.Controls 2.15
ScrollView { readonly property string bsiFixtureKind: "UiScrollView" }
'''
    sources["UiImage.qml"] = '''import QtQuick 2.15
Item {
 readonly property string bsiFixtureKind: "UiImage"
 property string asset: ""
 property url source: ""
 property real pImageWidth: 0
 property real pImageHeight: 0
 implicitWidth: pImageWidth; implicitHeight: pImageHeight
}
'''
    for direction in ("Column", "Row"):
        sources[f"Ui{direction}Layout.qml"] = '''import QtQuick 2.15
import QtQuick.Layouts 1.15
Item {
 readonly property string bsiFixtureKind: "UiDIRECTIONLayout"
 default property alias content: layout.data
 property real spacing: 0
 implicitWidth: layout.implicitWidth
 implicitHeight: layout.implicitHeight
 DIRECTIONLayout { id: layout; anchors.fill: parent; spacing: parent.spacing }
}
'''.replace("DIRECTION", direction)
    for name, text in sources.items():
        (root / name).write_text(text, encoding="utf-8")
    qrc = ET.Element("RCC")
    resources = ET.SubElement(qrc, "qresource", prefix="/")
    for name in sources:
        ET.SubElement(resources, "file", alias=name).text = str(root / name)
    qrc_path, binary = root / "native.qrc", root / "native.rcc"
    ET.ElementTree(qrc).write(qrc_path, encoding="utf-8")
    compile_resources(qrc_path, binary)
    assert QResource.registerResource(str(binary))
    if theme is None:
        component = QQmlComponent(engine, QUrl("qrc:/NativeTheme.qml"))
        assert component.status() == QQmlComponent.Status.Ready, [e.toString() for e in component.errors()]
        theme = component.create()
        assert theme, [e.toString() for e in component.errors()]
        theme.setParent(engine)
    engine.rootContext().setContextProperty("UiTheme", theme)
    return theme, lambda: QResource.unregisterResource(str(binary))
