#pragma once

#include <QString>
#include <QVariantMap>

namespace SevenZipArchive {

// Opens a plain 7z archive or a 7-Zip SFX/engine EXE without an installed
// archive utility, extracts its Root.vhd into BSI Customizer's private cache,
// and returns {ok, path, msg}. The source package is never modified.
QVariantMap extractRootVhd(const QString &archivePath, const QString &cacheBase = QString());

} // namespace SevenZipArchive
