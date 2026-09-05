#pragma once

#include <QByteArray>
#include <QString>

namespace PatchFile {

// Replace the current file, never a stale .bak. Fail without truncating the
// original on locks/short writes; report success only after reading it back.
bool writeVerified(const QString &path, const QByteArray &expected,
                   const QByteArray &replacement, QString *error);

} // namespace PatchFile
