#pragma once
#include "ShaderPackage.h"
#include <QList>
#include <functional>

// The only installable packages exposed by the Customizer come from its own
// compiled resource bundle, not CWD/PATH/a downloaded or user-picked DLL. Hashes
// pin those build inputs; they do not replace publisher signature verification.
namespace ShaderBundle {
struct Entry {
    QString id;
    QString qtVersion;
    QString packageSha256;
    ShaderPackage::Payload payload;
};
struct Catalog {
    bool ok = false;
    QString error;
    QList<Entry> entries;
};
Catalog load();
// Shared bounded decoder for resource data and synthetic/corruption tests.
// Reader receives only fixed relative paths derived from validated numeric IDs.
Catalog decode(const QByteArray &index, const std::function<QByteArray(const QString &)> &read);
} // namespace ShaderBundle
