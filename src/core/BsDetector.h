#pragma once

#include <QVariantList>

// BlueStacks install auto-detection (port of find_bluestacks):
// default paths, Program Files scan and (Windows) registry uninstall keys.
namespace BsDetector {

// [{profile, root, exe, conf, opengl_dll, native_dll}]
QVariantList findBlueStacks();

} // namespace BsDetector
