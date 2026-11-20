import QtQuick 2.15
import QtQuick.Templates 2.15 as T
import QtQuick.Layouts 1.15

T.Button {
    id: control
    property var shaderStyle
    property bool primary: false
    property bool quiet: false
    readonly property var nativeState: shaderStyle.buttonState(primary, down, hovered)
    readonly property color fill: quiet ? "transparent" : shaderStyle.value(nativeState, "background", down ? shaderStyle.panel : shaderStyle.field)
    readonly property color outline: quiet ? "transparent" : activeFocus ? shaderStyle.accent : shaderStyle.value(nativeState, "borderColor", primary ? shaderStyle.accent : shaderStyle.border)
    implicitHeight: quiet ? 24 : shaderStyle.buttonHeight
    // Measure the full text, not an already elided, layout-constrained item.
    // Explicit paddings also avoid the host style adding its own extra inset.
    implicitWidth: Math.max(quiet ? 52 : 100, Math.ceil(measure.advanceWidth) + leftPadding + rightPadding)
    Layout.minimumWidth: implicitWidth
    leftPadding: quiet ? 8 : shaderStyle.buttonPadding
    rightPadding: leftPadding
    topPadding: 0; bottomPadding: 0
    leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0
    font: quiet ? shaderStyle.smallFont : shaderStyle.buttonFont
    opacity: enabled ? 1 : .4
    hoverEnabled: true
    TextMetrics { id: measure; text: control.text; font: control.font }
    contentItem: Text {
        text: control.text; font: control.font
        color: control.quiet && control.hovered && control.enabled ? control.shaderStyle.accent : control.shaderStyle.value(control.nativeState, "textColor", control.shaderStyle.text)
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        elide: Text.ElideNone
    }
    background: Rectangle {
        radius: control.shaderStyle.buttonRadius
        color: control.outline
        gradient: control.primary ? control.shaderStyle.value(control.nativeState, "borderGradient", control.shaderStyle.buttonGradient) : null
        ShaderSurface {
            anchors.fill: parent; anchors.margins: control.quiet ? 0 : 1
            radius: Math.max(0, parent.radius - 1)
            base: control.fill
        }
    }
}
