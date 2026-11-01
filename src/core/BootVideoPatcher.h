#pragma once

#include <QString>

// Boot video patcher — rewrites the compressed boot QML in HD-Player.exe so
// the boot screen uses Assets\Promo.mp4 from the selected installation.
// Reuses an existing media player where available, preserves QML loaders,
// grows mapped resources safely and invalidates their compiled caches.
namespace BootVideoPatcher {

struct Result { bool ok = false; QString msg; };

Result patch(const QString &exePath, int holdMs = 3000);

} // namespace BootVideoPatcher
