#pragma once
#include "ShaderBundle.h"
#include <QVariantMap>

// Data-only service shared by the authorized desktop facade and read-only
// diagnostics. Installation can select ONLY a compiled-in, hash-pinned entry.
namespace ShaderActions {
QVariantMap inspect(const QString &exePath, const ShaderBundle::Catalog &bundle = ShaderBundle::load());
QVariantMap apply(const QString &exePath, const QString &bundleId, const QString &exeSha256,
                  const QString &packageSha256, const QString &reviewSha256, bool acknowledged);
QVariantMap remove(const QString &exePath, const QString &exeSha256, const QString &stateSha256, bool acknowledged);
QVariantMap recover(const QString &exePath, const QString &journalSha256, bool acknowledged);
QVariantMap bundleInfo();
} // namespace ShaderActions
