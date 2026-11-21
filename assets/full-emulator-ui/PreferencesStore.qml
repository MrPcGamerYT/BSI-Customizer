// Optional persistent backend, loaded dynamically so stripped OEM Qt builds
// keep a working session theme when Qt.labs.settings is not shipped.
import QtQuick 2.15
import Qt.labs.settings 1.1

Settings {
    category: "NativeAppearance"
    property string selectedPreset: "oled"
    property string customJson: ""
    property string chosenFont: "Segoe UI"
    property int radiusOverride: -1
    property int durationOverride: -1
    property bool motion: true
}
