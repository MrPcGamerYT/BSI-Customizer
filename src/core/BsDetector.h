#pragma once

#include <QString>
#include <QVariantList>

// BlueStacks install auto-detection (port of find_bluestacks):
// default paths, Program Files scan and (Windows) registry uninstall keys.
namespace BsDetector {

// [{profile, root, exe, conf, fps_dll, native_dll}]
QVariantList findBlueStacks();

// True when the given executable (e.g. "HD-Player.exe") is running right now.
// Patching BlueStacks while it runs does not stick: the process keeps the theme
// it loaded at startup in memory, and Windows locks a running image file.
// Always false on non-Windows builds.
bool isProcessRunning(const QString &exeName);

} // namespace BsDetector
