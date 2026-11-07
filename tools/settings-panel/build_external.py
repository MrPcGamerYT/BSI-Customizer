#!/usr/bin/env python3
"""Build a tiny embedded bridge plus a local visual asset; never write an EXE."""
import argparse
import json
from pathlib import Path
import re
import zlib
from restyle import ROOT, TEMPLATE, closing_brace, restyle_qml


def external_style(template):
    start=template.index('            StackLayout {')
    end=closing_brace(template,template.index('{',start))
    template=template[:start]+template[end:]
    template=template.replace('UiShadowContainer {','Item {',1)
    template=template.replace('    objectName: "bsiSettingsShell"', '''    objectName: "bsiSettingsShell"
    property var bsiHost
    property var bsiTheme
    readonly property var pSettingsItems: bsiHost.pSettingsItems
    readonly property int pCurrentTabIndex: bsiHost.pCurrentTabIndex
    readonly property var iSettingsWindow: bsiHost
    property alias pageContainer: bsiPageBody
    function fChangeSelectedSetting(index) { bsiHost.fChangeSelectedSetting(index) }''',1)
    template=template.replace('UiTheme.','bsiTheme.')
    marker='        Rectangle {\n            anchors.left: bsiRail.right; anchors.right: parent.right; anchors.top: bsiPageHeading.bottom;'
    assert marker in template
    template=template.replace(marker,'        Rectangle {\n            id: bsiPageBody\n            anchors.left: bsiRail.right; anchors.right: parent.right; anchors.top: bsiPageHeading.bottom;',1)
    return 'import QtQuick 2.15\nimport QtQuick.Controls 2.15\nimport QtQuick.Layouts 1.15\n\n'+template


def bridge(url):
    return '''UiShadowContainer {
id:iSettingsWindowShadowContainer;anchors.fill:parent
Loader{id:bsiExternal;anchors.fill:parent
Component.onCompleted:setSource(URL,{bsiHost:iSettingsWindow,bsiTheme:UiTheme})
onLoaded:iSelectedSettingControl.parent=item.pageContainer
}
Text{anchors.centerIn:parent;visible:bsiExternal.status===Loader.Error;text:"Settings style missing. Close with Alt+F4.";color:UiTheme.colors.primary10}
StackLayout{id:iSelectedSettingControl;anchors.fill:parent;anchors.margins:8;visible:bsiExternal.status===Loader.Ready;currentIndex:pCurrentTabIndex
Repeater{model:pSettingsItems.pSettingElements;delegate:Loader{Component.onCompleted:{var a=model.modelData.pComponentArgs;a?setSource(model.modelData.pQmlSource,a):setSource(model.modelData.pQmlSource)}onLoaded:{model.modelData.pComponentObj=item;if(item.fOnQmlLoaded)item.fOnQmlLoaded()}}}
}
}'''.replace('URL',json.dumps(url))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input',type=Path,help='Original extracted UiSettingsWindow.qml')
    parser.add_argument('output',type=Path,help='NEW folder; no files are overwritten')
    parser.add_argument('--asset-url',required=True,help='Actual file:/// URL of Assets/BSISettingsOutline.qml on the emulator PC')
    parser.add_argument('--capacity',type=int,default=2701)
    args=parser.parse_args()
    if not args.asset_url.startswith('file:///') or not args.asset_url.endswith('/BSISettingsOutline.qml'):
        parser.error('Use an absolute local file:/// URL ending in /BSISettingsOutline.qml.')
    source=args.input.read_bytes().decode('utf-8')
    result=restyle_qml(source,bridge(args.asset_url))
    # qCompress consists of a four-byte uncompressed-size prefix plus zlib.
    used=4+len(zlib.compress(result.encode('utf-8'),9))
    if used>args.capacity:
        parser.error(f'Compressed bridge needs {used} bytes, above {args.capacity}; nothing was written.')
    args.output.mkdir(parents=True,exist_ok=False)
    (args.output/'UiSettingsWindow.qml').write_text(result,encoding='utf-8')
    (args.output/'BSISettingsOutline.qml').write_text(external_style(TEMPLATE.read_text(encoding='utf-8')),encoding='utf-8')
    (args.output/'bridge.qml.txt').write_text(bridge(args.asset_url),encoding='utf-8')
    print(f'Created files in {args.output}; compressed complete resource estimate {used}/{args.capacity} bytes.')
    print('Native resource writer remains authoritative. Install the asset at the exact URL BEFORE changing the embedded resource.')


if __name__=='__main__':main()
