#include "BsDetector.h"

#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QVariantMap>

namespace BsDetector {

namespace {

QString profileKey(const QString &dirName)
{
    return dirName.toLower().contains(QLatin1String("nxt")) ? QStringLiteral("NXT")
                                                            : QStringLiteral("MSI5");
}

} // namespace

QVariantList findBlueStacks()
{
    QList<QPair<QString, QString>> roots; // (profile, install dir)

    auto add = [&roots](const QString &profile, const QString &d) {
        if (d.isEmpty())
            return;
        const QString clean = QDir::cleanPath(d);
        if (!QFileInfo::exists(clean + QStringLiteral("/HD-Player.exe")))
            return;
        for (const auto &r : roots) {
            if (r.second == clean)
                return;
        }
        roots << qMakePair(profile, clean);
    };

    // 1. known defaults
    add(QStringLiteral("MSI5"), QStringLiteral("C:/Program Files/BlueStacks_msi5"));
    add(QStringLiteral("NXT"), QStringLiteral("C:/Program Files/BlueStacks_nxt"));

    // 2. Program Files scan
    const auto env = QProcessEnvironment::systemEnvironment();
    for (const QString &key : { QStringLiteral("ProgramFiles"),
                                QStringLiteral("ProgramFiles(x86)") }) {
        const QString base = env.value(key);
        if (base.isEmpty() || !QDir(base).exists())
            continue;
        const auto entries = QDir(base).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : entries) {
            if (name.toLower().startsWith(QLatin1String("bluestacks")))
                add(profileKey(name), base + QLatin1Char('/') + name);
        }
    }

    // 3. registry uninstall keys (Windows)
#ifdef Q_OS_WIN
    for (const QString &sub : { QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft"
                                               "\\Windows\\CurrentVersion\\Uninstall"),
                                QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node"
                                               "\\Microsoft\\Windows\\CurrentVersion\\Uninstall") }) {
        QSettings key(sub, QSettings::NativeFormat);
        const auto groups = key.childGroups();
        for (const QString &g : groups) {
            QSettings k(sub + QLatin1Char('\\') + g, QSettings::NativeFormat);
            const QString disp = k.value(QStringLiteral("DisplayName")).toString();
            const QString loc = k.value(QStringLiteral("InstallLocation")).toString();
            if ((disp + QLatin1Char(' ') + loc).toLower().contains(QLatin1String("bluestacks"))) {
                QString clean = loc;
                while (clean.endsWith(QLatin1Char('\\')) || clean.endsWith(QLatin1Char('/')))
                    clean.chop(1);
                add(profileKey(QFileInfo(clean).fileName()), clean);
            }
        }
    }
#endif

    QString programData = env.value(QStringLiteral("ProgramData"));
    if (programData.isEmpty())
        programData = QStringLiteral("C:/ProgramData");

    QVariantList result;
    for (const auto &r : roots) {
        const QString confDir =
            (r.first == QLatin1String("NXT")) ? QStringLiteral("BlueStacks_nxt")
                                              : QStringLiteral("BlueStacks_msi5");
        result << QVariantMap {
            { "profile", r.first },
            { "root", QDir::toNativeSeparators(r.second) },
            { "exe", QDir::toNativeSeparators(r.second + QStringLiteral("/HD-Player.exe")) },
            { "conf", QDir::toNativeSeparators(programData + QLatin1Char('/') + confDir
                                               + QStringLiteral("/bluestacks.conf")) },
            { "opengl_dll",
              QDir::toNativeSeparators(r.second + QStringLiteral("/libOpenglRender.dll")) },
            { "native_dll",
              QDir::toNativeSeparators(r.second + QStringLiteral("/HD-Opengl-Native.dll")) },
        };
    }
    return result;
}

} // namespace BsDetector
