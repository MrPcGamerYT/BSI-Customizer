"""Build the animated drop-in Settings asset; never patch native pages/EXE."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def build():
 s=(ROOT/'assets/emulator-settings/ReferencePanel.qml').read_text()
 def change(a,b):
  nonlocal s
  assert a in s,a
  s=s.replace(a,b,1)
 change('// BSI Settings — screenshot reference v2. Visual-only; native pages and Save stay embedded.','// BSI Settings — animated SVG edition. Native pages, guards and Save stay embedded.')
 change('import QtQuick.Layouts 1.15','import QtQuick.Layouts 1.15\nimport "Icons.js" as Icons')
 change('    property var bsiTheme','    property var bsiTheme\n    // Set false for reduced motion. No infinite/polling animations.\n    property bool bsiAnimations: true')
 for a,b in [('"cube"','"graphics"'),('"person"','"gameSettings"'),('"gamepad"','"controller"'),('"phone"','"smartphone"'),('"theme"','"palette"'),('"gear"','"advanced"'),('"audio"','"headphones"'),('"storage"','"harddrive"'),('"sliders"','"preferences"')]:
  # Change only the icon resolver, never a native tab/model identifier.
  at=s.index('    component BsiOutlineIcon:');s=s[:at].replace('return '+a,'return '+b)+s[at:]
 start=s.index('    component BsiOutlineIcon:')
 end=s.index('\n    Rectangle {\n        anchors.fill: parent',start)
 s=s[:start]+(ROOT/'assets/emulator-settings/SvgIcon.qml.txt').read_text()+s[end:]
 change('contentItem: BsiOutlineIcon { kind: "close"; stroke: closeButton.hovered ? "#ffffff" : "#b7b7be" }', '''contentItem: BsiOutlineIcon {
                    kind: "close"; hot: closeButton.hovered; downState: closeButton.down
                    hoverTwist: 90
                }''')
 change('                    contentItem: Item {\n                        BsiOutlineIcon {', '''                    contentItem: Item {
                        transform: Translate {
                            x: tab.hovered ? 2 : 0
                            Behavior on x { enabled: shell.bsiAnimations; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                        }
                        BsiOutlineIcon {''')
 change('                            width: 16; height: 16\n                            kind:', '                            width: 18; height: 18\n                            kind:')
 change('                            stroke: tab.selected || tab.hovered ? "#f4f4f6" : "#b4b4bd"','''                            hot: tab.hovered; downState: tab.down; active: tab.selected
                            hoverTwist: kind === "advanced" ? 18 : kind === "palette" ? -10 : 0''')
 change('font.pixelSize: 12; font.weight: tab.selected ? Font.Medium : Font.Normal','''font.pixelSize: 12; font.weight: tab.selected ? Font.Medium : Font.Normal
                            Behavior on color { enabled: shell.bsiAnimations; ColorAnimation { duration: 140 } }''')
 change('                    background: Rectangle {\n                        radius: 11','''                    background: Rectangle {
                        scale: tab.down ? 0.97 : 1
                        Behavior on scale { enabled: shell.bsiAnimations; NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
                        radius: 11''')
 change('Behavior on color { ColorAnimation { duration: 110 } }','Behavior on color { enabled: shell.bsiAnimations; ColorAnimation { duration: 110 } }')
 change('Behavior on border.color { ColorAnimation { duration: 110 } }','Behavior on border.color { enabled: shell.bsiAnimations; ColorAnimation { duration: 150 } }')
 change('                            visible: tab.selected\n                        }\n                        Rectangle {', '''                            opacity: tab.selected ? 1 : 0
                            visible: opacity > 0
                            Behavior on opacity { enabled: shell.bsiAnimations; NumberAnimation { duration: 170 } }
                        }
                        Rectangle {''')
 change('                            width: 2; height: 17; radius: 1\n                            color: "#f3f3f7"; visible: tab.selected','''                            width: 2; height: tab.selected ? 18 : 6; radius: 1
                            color: "#f3f3f7"; opacity: tab.selected ? 1 : 0
                            visible: opacity > 0
                            Behavior on height { enabled: shell.bsiAnimations; NumberAnimation { duration: 220; easing.type: Easing.OutBack } }
                            Behavior on opacity { enabled: shell.bsiAnimations; NumberAnimation { duration: 150 } }''')
 change('''                        Rectangle {
                            width: 5; height: 5; radius: 2.5''','''                        // One short selection flash, only after native selection changes.
                        Rectangle {
                            id: selectionFlash
                            objectName: "bsiSettingsSelectionFlash"
                            anchors.fill: parent; anchors.margins: 1; radius: 10
                            color: "#ffffff"; opacity: 0
                            visible: shell.bsiAnimations && opacity > 0
                        }
                        SequentialAnimation {
                            running: shell.bsiAnimations && tab.selected
                            NumberAnimation { target: selectionFlash; property: "opacity"; from: 0; to: 0.10; duration: 100 }
                            NumberAnimation { target: selectionFlash; property: "opacity"; to: 0; duration: 260; easing.type: Easing.OutCubic }
                        }
                        Rectangle {
                            width: 5; height: 5; radius: 2.5''')
 return s
if __name__=='__main__':
 s=build();(ROOT/'assets/emulator-settings/AnimatedPanel.qml').write_text(s)
 folder=ROOT/'downloads/Settings-Animated-MSI5/Assets';folder.mkdir(parents=True,exist_ok=True)
 (folder/'BSISettingsOutline.qml').write_text(s)
 (folder/'Icons.js').write_bytes((ROOT/'assets/emulator-settings/Icons.js').read_bytes())
 print('Created full animated Settings asset + Icons.js; native bridge unchanged')
