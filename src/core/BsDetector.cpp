#include "BsDetector.h"
#include "FpsPatcher.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QRegularExpression>
#include <QVariantMap>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace BsDetector {

namespace {

QString profileKey(const QString &name)
{
    return name.contains(QLatin1String("msi"), Qt::CaseInsensitive) ? QStringLiteral("MSI5") : QStringLiteral("NXT");
}
QString playerIn(const QString &directory)
{
    const QDir dir(directory);
    for (const auto &name : dir.entryList(QDir::Files))
        if (name.compare(QLatin1String("HD-Player.exe"), Qt::CaseInsensitive) == 0) return dir.absoluteFilePath(name);
    return {};
}
QString cleanLocation(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && value.front() == QLatin1Char('"') && value.back() == QLatin1Char('"'))
        value = value.mid(1, value.size() - 2);
    return value.isEmpty() ? QString() : QDir::cleanPath(value);
}
} // namespace

QVariantList findBlueStacks(const QStringList &programFolders, bool includeRegistry)
{
    struct Install { QString profile, root, exe, data; };
    QList<Install> roots;
    const auto env = QProcessEnvironment::systemEnvironment();
    auto add = [&](const QString &profile, const QString &directory, const QString &dataDirectory = QString()) {
        const QString clean = cleanLocation(directory);
        if (clean.isEmpty()) return;
        const auto exe = playerIn(clean);
        if (exe.isEmpty()) return;
        QString canonical = QFileInfo(clean).canonicalFilePath();
        if (canonical.isEmpty()) canonical = QFileInfo(clean).absoluteFilePath();
        for (auto &root : roots) {
            if (root.root.compare(canonical, Qt::CaseInsensitive) != 0) continue;
            if (!dataDirectory.isEmpty()) root.data = cleanLocation(dataDirectory);
            return;
        }
        roots << Install { profile, canonical, exe, cleanLocation(dataDirectory) };
    };
    QStringList folders = programFolders;
    if (folders.isEmpty()) {
        for (const QString &key : { QStringLiteral("ProgramW6432"), QStringLiteral("ProgramFiles"), QStringLiteral("ProgramFiles(x86)") }) {
            const QString path = cleanLocation(env.value(key));
            if (!path.isEmpty() && !folders.contains(path, Qt::CaseInsensitive)) folders << path;
        }
#ifdef Q_OS_WIN
        if (folders.isEmpty()) folders << QStringLiteral("C:/Program Files") << QStringLiteral("C:/Program Files (x86)");
#endif
    }
    // Bounded search under the real Program Files locations, including MSI's
    // branded folder names. Never scan every drive or select another install's DLL.
    for (const auto &folder : folders) {
        const QDir base(folder);
        for (const auto &name : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString compact = QString(name).toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('_')).remove(QLatin1Char('-'));
            if (!compact.contains(QLatin1String("bluestacks")) && !compact.contains(QLatin1String("msiappplayer"))) continue;
            for (const auto &sub : { QString(), QStringLiteral("Client"), QStringLiteral("bin"), QStringLiteral("Client/bin"), QStringLiteral("Engine") }) {
                add(profileKey(name), QDir(base.filePath(name)).filePath(sub));
            }
        }
    }
#ifdef Q_OS_WIN
    if (includeRegistry) {
        for (const auto &hive : { QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\"),
                                  QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\"),
                                  QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\") }) {
            for (const auto &product : { QStringLiteral("BlueStacks_nxt"), QStringLiteral("BlueStacks_msi5"), QStringLiteral("BlueStacks") }) {
                QSettings key(hive + product, QSettings::NativeFormat);
                for (const auto &value : { QStringLiteral("InstallDir"), QStringLiteral("InstallLocation"), QStringLiteral("InstallPath") })
                    add(profileKey(product), key.value(value).toString(), key.value(QStringLiteral("DataDir")).toString());
            }
        }
        for (const auto &sub : { QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
                                 QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
                                 QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall") }) {
            QSettings key(sub, QSettings::NativeFormat);
            for (const auto &group : key.childGroups()) {
                QSettings item(sub + QLatin1Char('\\') + group, QSettings::NativeFormat);
                const QString display = item.value(QStringLiteral("DisplayName")).toString();
                const QString location = item.value(QStringLiteral("InstallLocation")).toString();
                const QString identity = (display + QLatin1Char(' ') + location).toLower();
                if (!identity.contains(QLatin1String("bluestacks")) && !identity.contains(QLatin1String("msi app player"))) continue;
                add(profileKey(identity), location);
                QString icon = item.value(QStringLiteral("DisplayIcon")).toString().trimmed();
                if (icon.startsWith(QLatin1Char('"'))) icon = icon.section(QLatin1Char('"'), 1, 1);
                else icon = icon.section(QLatin1Char(','), 0, 0).trimmed();
                if (!icon.isEmpty()) add(profileKey(identity), QFileInfo(icon).absolutePath());
            }
        }
    }
#else
    Q_UNUSED(includeRegistry);
#endif
    QString programData = env.value(QStringLiteral("ProgramData"), QStringLiteral("C:/ProgramData"));
    QVariantList result;
    for (const auto &root : roots) {
        const QString data = root.data.isEmpty() ? QDir(programData).filePath(root.profile == QLatin1String("MSI5")
                            ? QStringLiteral("BlueStacks_msi5") : QStringLiteral("BlueStacks_nxt")) : root.data;
        const auto fps = FpsPatcher::detectDll(root.exe);
        result << QVariantMap {
            { "profile", root.profile }, { "root", QDir::toNativeSeparators(root.root) },
            { "exe", QDir::toNativeSeparators(root.exe) },
            { "conf", QDir::toNativeSeparators(QDir(data).filePath(QStringLiteral("bluestacks.conf"))) },
            { "fps_dll", fps.path }, { "qt_major", fps.qtMajor }, { "fps_detection", fps.msg },
            { "native_dll", QDir::toNativeSeparators(QDir(root.root).filePath(QStringLiteral("HD-Opengl-Native.dll"))) }
        };
    }
    return result;
}

bool isProcessRunning(const QString &exeName)
{
#ifdef Q_OS_WIN
    if (exeName.isEmpty())
        return false;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (exeName.compare(QString::fromWCharArray(pe.szExeFile), Qt::CaseInsensitive) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
#else
    Q_UNUSED(exeName)
    return false;
#endif
}

} // namespace BsDetector
