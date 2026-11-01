#include "PlayerCache.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <functional>

namespace PlayerCache {
namespace {
bool linked(const QFileInfo &info) { return info.isSymLink() || info.isJunction(); }
bool safeAncestors(QString path)
{
    for (;;) {
        const QFileInfo info(path);
        if (linked(info)) return false;
        const QString parent = info.absolutePath();
        if (parent == path) return true;
        path = parent;
    }
}
// Never traverse a symlink/junction, even when it lives inside a cache. Report
// the skipped entry instead of calling removeRecursively on an untrusted tree.
bool removeCache(const QString &path, int depth = 0)
{
    if (depth > 64 || linked(QFileInfo(path))) return false;
    QDir dir(path);
    bool ok = true;
    for (const auto &entry : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (linked(entry)) { ok = false; continue; }
        if (entry.isDir()) { if (!removeCache(entry.absoluteFilePath(), depth + 1)) ok = false; }
        else if (!QFile::remove(entry.absoluteFilePath())) ok = false;
    }
    return ok && QDir().rmdir(path);
}
}
Result purge(const QStringList &appDataRoots)
{
    Result result;
    // No broad AppData wipe, ProgramData walk, Engine directory, virtual disk,
    // cookies or settings. Only cache leaves beneath an exact HD-Player root.
    const QStringList owners = { QString(), QStringLiteral("BlueStacks"), QStringLiteral("BlueStacks_nxt"),
        QStringLiteral("BlueStacks_msi5"), QStringLiteral("MSI"), QStringLiteral("MSI App Player"), QStringLiteral("now.gg") };
    const QSet<QString> names = { QStringLiteral("cache"), QStringLiteral("qmlcache"), QStringLiteral("gpucache"),
        QStringLiteral("shadercache"), QStringLiteral("code cache"), QStringLiteral("dawncache"),
        QStringLiteral("grshadercache"), QStringLiteral("d3dscache") };
    QSet<QString> visited;
    int budget = 1024;
    std::function<void(const QString &, int)> visit = [&](const QString &path, int depth) {
        if (depth > 5 || --budget < 0) { ++result.failed; return; }
        const QFileInfo info(path);
        if (!info.exists()) return;
        if (linked(info)) { ++result.failed; return; }
        const QString key = info.absoluteFilePath().toLower();
        if (visited.contains(key)) return;
        visited.insert(key);
        if (names.contains(info.fileName().toLower())) {
            if (removeCache(path)) ++result.removed; else ++result.failed;
            return;
        }
        for (const auto &child : QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
            const auto name = child.fileName().toLower();
            if (name == QLatin1String("engine") || name == QLatin1String("logs") || name == QLatin1String("sharedfolder")) continue;
            visit(child.absoluteFilePath(), depth + 1);
        }
    };
    for (const auto &base : appDataRoots) {
        if (base.trimmed().isEmpty() || !QDir::isAbsolutePath(base)) continue;
        for (const auto &owner : owners) {
            const auto root = QDir(base).filePath(owner.isEmpty() ? QStringLiteral("HD-Player") : owner + QStringLiteral("/HD-Player"));
            if (!QFileInfo::exists(root)) continue;
            if (!safeAncestors(root)) { ++result.failed; continue; }
            visit(root, 0);
        }
    }
    result.ok = result.failed == 0;
    result.msg = result.ok
        ? QStringLiteral("Purged %1 HD-Player cache folder(s) from AppData. Settings and emulator data were preserved.").arg(result.removed)
        : QStringLiteral("Purged %1 cache folder(s); %2 locked, linked or unsupported location(s) were skipped. Close HD-Player and retry. No emulator data was targeted.").arg(result.removed).arg(result.failed);
    return result;
}
}
