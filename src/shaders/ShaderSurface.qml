// Flat, owned public-API paint for controls, including moving slider handles.
// Do not replace native materials or add dynamic gradients/layers/masks here.
// Keep the conservative surface path separate from the shell's glass styling.
import QtQuick 2.15
Rectangle {
    property color base: "transparent"
    color: base
}
