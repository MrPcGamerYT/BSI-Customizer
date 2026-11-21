#include "PhoneProfilesInstall.h"
#include "PhoneProfiles.h"
#include "PatchFile.h"
#include "QtResources.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace PhoneProfilesInstall {
namespace {
constexpr qint64 imageLimit = 256LL * 1024 * 1024;
bool playerClosed(const QString &exeName)
{
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false, ok = Process32FirstW(snapshot, &entry) != FALSE;
    if (ok) {
        do {
            const QString name = QString::fromWCharArray(entry.szExeFile);
            if (name.compare(QLatin1String("HD-Player.exe"), Qt::CaseInsensitive) == 0
                || name.compare(exeName, Qt::CaseInsensitive) == 0) { found = true; break; }
        } while (Process32NextW(snapshot, &entry));
        if (!found) ok = GetLastError() == ERROR_NO_MORE_FILES;
    }
    CloseHandle(snapshot);
    return ok && !found;
#else
    Q_UNUSED(exeName)
    return true;
#endif
}
QByteArray hash(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}
bool safePath(const QString &path)
{
    QFileInfo file(QFileInfo(path).absoluteFilePath());
    for (;;) {
        if (file.isSymLink() || file.isJunction()) return false;
        const QFileInfo parent(file.absolutePath());
        if (parent.absoluteFilePath() == file.absoluteFilePath()) return true;
        file = parent;
    }
}
QByteArray read(const QString &path, qint64 limit)
{
    const QFileInfo info(path);
    if (!safePath(path) || !info.isFile() || info.size() < 0 || info.size() > limit) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.read(limit + 1);
    return bytes.size() == info.size() && file.atEnd() && file.error() == QFileDevice::NoError
        ? bytes : QByteArray{};
}
bool save(const QString &path, const QByteArray &bytes)
{
    if (!safePath(path)) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
        && file.commit() && read(path, bytes.size()) == bytes;
}
bool backup(const QString &path, const QByteArray &image)
{
    const QString target = path + QStringLiteral(".bak");
    if (!safePath(target)) return false;
    // Preserve an existing tool/user backup, just like other patchers. Exact
    // Phone-source undo uses the separately verified recovery.json, NOT .bak.
    if (QFileInfo::exists(target)) return QFileInfo(target).isFile();
    // QFile::copy is exclusive: never replace a backup created by another tool.
    return QFile::copy(path, target) && read(target, imageLimit) == image;
}
} // namespace

Result apply(const QString &exePath, bool enabled)
{
    const auto refuse = [](const QString &why) { return Result{false, why}; };
    if (!playerClosed(QFileInfo(exePath).fileName()))
        return refuse(QStringLiteral("Close every HD-Player instance before changing Phone choices; the process list must be verified closed."));
    if (!safePath(exePath)) return refuse(QStringLiteral("Linked/reparse player paths are refused. Select the actual player file."));
    const QString canonical = QFileInfo(exePath).canonicalFilePath();
    if (canonical.isEmpty() || QFileInfo(canonical).suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) != 0)
        return refuse(QStringLiteral("Select an existing HD-Player.exe first."));
    const QString lockPath = canonical + QStringLiteral(".bsi-phone-profiles.lock");
    if (!safePath(lockPath)) return refuse(QStringLiteral("The Phone writer lock path is linked; nothing was changed."));
    QLockFile lock(lockPath);
    if (!lock.tryLock(0)) return refuse(QStringLiteral("Another Phone-profile writer is active; retry after it finishes."));
    const QByteArray image = read(canonical, imageLimit);
    if (image.isEmpty()) return refuse(QStringLiteral("The selected player is unreadable or exceeds the safe size limit."));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (appData.isEmpty() || !QDir::isAbsolutePath(appData))
        return refuse(QStringLiteral("The Customizer's local application-data directory is unavailable; nothing was changed."));
    const QString root = appData + QStringLiteral("/phone-profiles/") + QString::fromLatin1(hash(canonical.toUtf8()));
    if (!safePath(root)) return refuse(QStringLiteral("Linked/reparse Phone helper/recovery directories are refused."));
    const QByteArray script = PhoneProfiles::runtimeJavascript();
    const QString helper = QDir(root).filePath(QStringLiteral("PhoneProfiles-%1.js").arg(QString::fromLatin1(hash(script))));
    const QString snapshot = QDir(root).filePath(QStringLiteral("recovery.json"));
    const QString url = QUrl::fromLocalFile(helper).toString(QUrl::FullyEncoded);
    const auto previousSnapshot = read(snapshot, 2LL * 1024 * 1024);
    QJsonParseError parse;
    const auto record = QJsonDocument::fromJson(previousSnapshot, &parse).object();
    QByteArray original;
    if (parse.error == QJsonParseError::NoError
        && record.value(QStringLiteral("schema")).toString() == QLatin1String("bsi.phone-profiles.v1")
        && record.value(QStringLiteral("target")).toString() == canonical) {
        const auto encoded = record.value(QStringLiteral("original")).toString().toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (decoded.decodingStatus == QByteArray::Base64DecodingStatus::Ok && decoded.decoded.size() <= 1024 * 1024
            && decoded.decoded.toBase64() == encoded
            && hash(decoded.decoded) == record.value(QStringLiteral("originalSha256")).toString().toLatin1()) original = decoded.decoded;
    }
    const auto validHash = [](const QByteArray &value) {
        if (value.size() != 64) return false;
        for (const char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    };
    auto plan = PhoneProfiles::prepare(image, enabled, url, original);
    QByteArray installedHelper, installedSource;
    // Catalog updates change the immutable helper's hash/URL. First prove the
    // exact OLD source from the saved native bytes and its recorded local URL;
    // prepare removal + the new import IN MEMORY, never on the live EXE in two
    // writes. No downloaded/old helper is executed or used as source evidence.
    for (const auto &prefix : {QString(), QStringLiteral("previous")}) {
        const QString helperKey = prefix.isEmpty() ? QStringLiteral("helperSha256") : prefix + QStringLiteral("HelperSha256");
        const QString sourceKey = prefix.isEmpty() ? QStringLiteral("patchedSha256") : prefix + QStringLiteral("PatchedSha256");
        const auto oldHelper = record.value(helperKey).toString().toLatin1();
        const auto oldSource = record.value(sourceKey).toString().toLatin1();
        if (!validHash(oldHelper) || !validHash(oldSource) || original.isEmpty()) continue;
        const QString oldPath = QDir(root).filePath(QStringLiteral("PhoneProfiles-%1.js").arg(QString::fromLatin1(oldHelper)));
        const QString oldUrl = QUrl::fromLocalFile(oldPath).toString(QUrl::FullyEncoded);
        const auto removed = PhoneProfiles::prepare(image, false, oldUrl, original);
        if (!removed.ok || !removed.beforeSource.contains("BSI-PHONE") || hash(removed.beforeSource) != oldSource) continue;
        if (enabled && (QFileInfo::exists(oldPath) || QFileInfo(oldPath).isSymLink())
            && hash(read(oldPath, 128 * 1024)) != oldHelper)
            return refuse(QStringLiteral("The installed Phone helper was modified or linked; update was refused. Exact-source removal remains available."));
        installedHelper = oldHelper; installedSource = oldSource;
        if (!plan.ok) {
            plan = enabled ? PhoneProfiles::prepare(removed.output, true, url) : removed;
            if (plan.ok) {
                plan.beforeSource = removed.beforeSource;
                plan.changed = plan.output != image;
            }
        }
        break;
    }
    if (!plan.ok) return refuse(plan.error);
    const bool wasInstalled = plan.beforeSource.contains("BSI-PHONE");
    if (wasInstalled && (installedHelper.isEmpty() || installedSource != hash(plan.beforeSource)))
        return refuse(QStringLiteral("The Phone recovery snapshot's installed-source/helper hash does not match. No source was overwritten."));
    if (enabled) {
        if (!QDir().mkpath(root)) return refuse(QStringLiteral("The local Phone helper directory could not be created."));
        if (QFileInfo(helper).exists() || QFileInfo(helper).isSymLink()) {
            if (read(helper, 128 * 1024) != script)
                return refuse(QStringLiteral("The checksum-named Phone helper was modified; no unknown local script was adopted or overwritten."));
        } else if (!save(helper, script)) {
            return refuse(QStringLiteral("The immutable local Phone helper could not be saved and verified. No EXE was changed."));
        }
        if (!wasInstalled) original = plan.beforeSource;
        QJsonObject next{{QStringLiteral("schema"), QStringLiteral("bsi.phone-profiles.v1")},
            {QStringLiteral("target"), canonical}, {QStringLiteral("original"), QString::fromLatin1(original.toBase64())},
            {QStringLiteral("originalSha256"), QString::fromLatin1(hash(original))},
            {QStringLiteral("patchedSha256"), QString::fromLatin1(hash(plan.afterSource))},
            {QStringLiteral("helperSha256"), QString::fromLatin1(hash(script))}};
        // Stage BOTH accepted source identities before an update. If backup /
        // atomic EXE replacement fails (or the process exits between writes),
        // the old on-disk source is still exactly recoverable on the next run.
        // One previous generation is sufficient: each attempt proves the actual
        // current source first, and never rolls back an unrecognized edit.
        if (wasInstalled && installedHelper != hash(script)) {
            next.insert(QStringLiteral("previousHelperSha256"), QString::fromLatin1(installedHelper));
            next.insert(QStringLiteral("previousPatchedSha256"), QString::fromLatin1(installedSource));
        }
        const auto bytes = QJsonDocument(next).toJson(QJsonDocument::Compact);
        if (previousSnapshot != bytes && !save(snapshot, bytes))
            return refuse(QStringLiteral("The exact native Phone recovery source could not be saved and verified. No EXE was changed."));
    }
    if (plan.changed) {
        // Recheck after preparation; PatchFile also checks the exact current
        // image immediately before its atomic replacement. No in-place write.
        if (!playerClosed(QFileInfo(canonical).fileName()))
            return refuse(QStringLiteral("Close every HD-Player instance before changing Phone choices; the process list must be verified closed. Recovery data was retained."));
        QString error;
        if (!backup(canonical, image) || !PatchFile::writeVerified(canonical, image, plan.output, &error))
            return refuse(QStringLiteral("The verified Phone-source write failed. Recovery data was retained. ") + error);
        QtResources::clearSourceQmlCache(canonical, {PhoneProfiles::resourceName()},
            {QStringLiteral("fDeviceProfilesReceivedFromCloud"), QStringLiteral("iDeviceProfileCombobox"), QStringLiteral("fDeviceProfileSettingChangedClbk")});
    }
    if (!enabled) {
        if (wasInstalled) QFile::remove(snapshot);
        // Immutable helpers are deliberately retained: another disposable copy
        // of this patched EXE may still reference the same content-addressed URL.
        return {true, QStringLiteral("The original Phone source is restored (or was already stock). Restart the player. Your saved device identity was not changed; choose and Save a stock profile in the player if you want to change it.")};
    }
    return {true, QStringLiteral("%1 phones are first in the stock Phone dropdown. Restart the player, choose one and use native Save changes. Apply did not select a phone or change FPS/root settings. Keep the Customizer's local phone-profiles data folder. Native profile switching retains its Android-data warning; game high-FPS options are game/version dependent.").arg(PhoneProfiles::catalog().size())};
}
} // namespace PhoneProfilesInstall
