#pragma once
#include <QString>

namespace PhoneProfilesInstall {
struct Result { bool ok = false; QString msg; };
// Explicit install/remove on a closed player. Owns one immutable local JS helper
// and a feature-local original-source snapshot; never selects a device or FPS.
Result apply(const QString &exePath, bool enabled);
} // namespace PhoneProfilesInstall
