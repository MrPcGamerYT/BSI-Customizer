#include "PatchFile.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace PatchFile {

bool writeVerified(const QString &path, const QByteArray &expected,
                   const QByteArray &replacement, QString *error)
{
    QFile current(path);
    if (!current.open(QIODevice::ReadOnly) || current.readAll() != expected) {
        *error = QStringLiteral("The file changed while preparing the patch. Reload and try again.");
        return false;
    }
    current.close();
    const auto oldTime = QFileInfo(path).lastModified();
    QSaveFile out(path);
    out.setDirectWriteFallback(false);
    if (!out.open(QIODevice::WriteOnly) || out.write(replacement) != replacement.size()
        || !out.commit()) {
        *error = QStringLiteral("Could not replace %1: %2. Close BlueStacks (HD-Player.exe) "
                                "and run this tool as administrator. The original was not truncated.")
                     .arg(QFileInfo(path).fileName(), out.errorString());
        return false;
    }
    // Qt resource format 1 has no per-file timestamps; Qt falls back to the
    // EXE's mtime. Ensure a rapid repatch cannot retain the previous timestamp.
    QFile written(path);
    if (!written.open(QIODevice::ReadWrite)) {
        *error = QStringLiteral("File written, but it could not be reopened for verification.");
        return false;
    }
    if (QFileInfo(path).lastModified() <= oldTime
        && !written.setFileTime(oldTime.addSecs(1), QFileDevice::FileModificationTime)) {
        *error = QStringLiteral("File written, but its cache timestamp could not be refreshed.");
        return false;
    }
    if (written.readAll() != replacement) {
        *error = QStringLiteral("The on-disk patch did not pass verification. Restore the .bak backup.");
        return false;
    }
    return true;
}

} // namespace PatchFile
