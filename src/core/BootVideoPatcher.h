#pragma once

#include <QString>

// Boot video patcher — rewrites the compressed boot QML in HD-Player.exe so
// the boot screen loads a custom video (Assets\Promo.mp4) instead of the
// stock promotion. Port of boot_kind_of / boot_find_streams / boot_patch_exe.
namespace BootVideoPatcher {

struct Result { bool ok = false; QString msg; };

Result patch(const QString &exePath);

} // namespace BootVideoPatcher
