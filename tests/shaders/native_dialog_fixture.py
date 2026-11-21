"""Authored doubles for the native dialog's public C++ model/window services.

No player implementation is copied. Real-resource tests use the unmodified
qrc:/UiMessageDialog.qml and UiDialog.qml with these nonpainting services;
standalone tests explicitly use the marked, generic-Qt resource below instead.
Neither mode executes an OEM EXE, restarts a guest or certifies a real player.
"""
import os
from pathlib import Path

_registered = []


def register(directory):
    if os.environ.get("BSI_TEST_QT_API") == "PyQt5":
        from PyQt5.QtCore import QUrl
        from PyQt5.QtQml import qmlRegisterType
    else:
        from PySide6.QtCore import QUrl
        from PySide6.QtQml import qmlRegisterType
    assert not _registered, "run each native-resource family in its own process"
    root = Path(directory) / "native-dialog-services"
    root.mkdir()
    sources = {
        "DialogButtonModel": '''import QtQml 2.15
QtObject {
 property string pText: ""
 property string pImage: ""
 property string pExtn: ".svg"
 property string pSubText: ""
 property bool pImageAfterText: false
 property QtObject pColor: null
 property QtObject pSize: null
 signal clicked()
}
''',
        "DialogButtonModelList": '''import QtQml 2.15
QtObject { property list<QtObject> pButtons }
''',
        "UiDialogWindow": '''import QtQuick 2.15
import QtQuick.Window 2.15
Window {
 enum Kind { Simple, SimpleNarrow, Descriptive, DescriptiveNarrow, Tall, TallAlert, Graphic, GraphicSimple }
 property int pDialogType: 0
 property int pOwnership: 1
 property QtObject pButtonsList: null
 property string pSubtitle: ""
 property string pDescription: ""
 property string pNotice: ""
 property string pDialogImageSource: ""
 property string pDialogImageExtn: ".svg"
 property string pDialogHeaderImageSource: ""
 property string pCheckBoxText: ""
 property string pHyperlink: ""
 property bool pCheckBoxState: false
 property bool pCheckBoxInitialState: false
 property bool pCloseBtnVisible: true
 signal closed()
 signal closeClicked()
 signal hyperlinkClicked()
 onClosing: closed()
}
''',
    }
    for name, source in sources.items():
        path = root / (name + ".qml")
        path.write_text(source, encoding="utf-8")
        # URL registration works on both public Qt 5.15 and Qt 6 APIs, including
        # typed list properties and QML enums. No custom painted Window class.
        registered = qmlRegisterType(QUrl.fromLocalFile(str(path)), name, 1, 0, name)
        assert registered >= 0, name
        _registered.append((registered, path))


def message_source():
    return '''import QtQuick 2.15
import QtQuick.Window 2.15
import UiDialogWindow 1.0
UiDialogWindow {
 id: dialog
 readonly property string bsiFixtureKind: "UiMessageDialog"
 width: 440
 height: body.implicitHeight + 48
 color: UiTheme.colors.primary80
 flags: Qt.Dialog | Qt.FramelessWindowHint
 modality: Qt.ApplicationModal
 onVisibleChanged: if (visible && transientParent) {
  x = transientParent.x + (transientParent.width - width) / 2
  y = transientParent.y + (transientParent.height - height) / 2
 }
 Component.onCompleted: if (typeof uiBackend !== "undefined") uiBackend.pIsShortcutsEnabled = false
 onClosed: {
  if (typeof uiBackend !== "undefined") uiBackend.pIsShortcutsEnabled = true
  destroy()
 }
 Column {
  id: body
  anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24 }
  spacing: 16
  Row {
   width: parent.width
   Text { width: parent.width - 24; text: dialog.title; wrapMode: Text.Wrap; color: UiTheme.colors.primary10; font: UiTheme.fonts.bodyLarge }
   UiButton {
    objectName: "nativeDialogClose"
    pSize: UiTheme.buttons.sizes.small; pColor: UiTheme.buttons.colors.transparent
    text: "×"; onClicked: { dialog.close(); dialog.closeClicked() }
   }
  }
  Text { width: parent.width; text: dialog.pDescription; wrapMode: Text.Wrap; color: UiTheme.colors.primary20; font: UiTheme.fonts.bodySmall }
  Text { visible: !!text; width: parent.width; text: dialog.pNotice; wrapMode: Text.Wrap; color: UiTheme.colors.primary30; font: UiTheme.fonts.bodySmall }
  Row {
   width: parent.width; spacing: 12; layoutDirection: Qt.RightToLeft
   Repeater {
    model: dialog.pButtonsList ? dialog.pButtonsList.pButtons : []
    delegate: UiButton {
     objectName: "nativeDialogButton" + index
     pSize: UiTheme.buttons.sizes.medium; pColor: modelData.pColor
     text: modelData.pText; onClicked: modelData.clicked()
    }
   }
  }
 }
}
'''
