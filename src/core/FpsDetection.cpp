#include "FpsPatcher.h"
#include "PeUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QMap>

namespace FpsPatcher {
namespace {
QString namedFile(const QDir &dir, const QString &name)
{
    for (const auto &file : dir.entryList(QDir::Files))
        if (file.compare(name, Qt::CaseInsensitive) == 0) return dir.absoluteFilePath(file);
    return {};
}
QByteArray readImage(const QString &path, qint64 *budget = nullptr)
{
    const qint64 size = QFileInfo(path).size();
    if (size <= 0 || size > 128 * 1024 * 1024 || (budget && size > *budget)) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (budget) *budget -= size;
    return file.readAll();
}
QSet<int> majors(const QStringList &modules, const QString &suffix = {})
{
    QSet<int> result;
    for (const auto &module : modules) {
        for (int major : { 5, 6 }) {
            const auto prefix = QStringLiteral("qt%1").arg(major);
            if ((!suffix.isEmpty() && module == prefix + suffix)
                || (suffix.isEmpty() && module.startsWith(prefix) && module.endsWith(QLatin1String(".dll"))))
                result.insert(major);
        }
    }
    return result;
}
}

DllDetection detectDll(const QString &playerExe)
{
    DllDetection out;
    const auto image = readImage(playerExe);
    // Older BlueStacks generations ship a 32-bit HD-Player.exe with Qt5QuickTemplates2.dll.
    // Recognising those installations is useful on its own (theme, gradient, config and
    // backup features all work there), so the bitness is recorded rather than used to end
    // the detection; the 64-bit requirement is enforced on the slider DLL further below.
    const quint16 machine = PeUtils::machine(image);
    out.playerBits = PeUtils::isPe32Plus(image) && machine == 0x8664 ? 64
                   : !PeUtils::isPe32Plus(image) && machine == 0x14c && !image.isEmpty() ? 32 : 0;
    if (!out.playerBits) {
        out.msg = QStringLiteral("Select a readable Windows HD-Player.exe (x64, or the x86 build of an older installation) from the installation you want to patch.");
        return out;
    }
    const QDir install(QFileInfo(playerExe).absolutePath());
    const auto imports = PeUtils::importedModules(image);
    if (!imports.ok) {
        out.msg = QStringLiteral("Cannot safely detect Qt from HD-Player imports: %1. Manual DLL selection is still available.").arg(imports.error);
        return out;
    }
    QString evidence = QStringLiteral("HD-Player import table");
    auto versions = majors(imports.modules, QStringLiteral("quicktemplates2.dll"));
    if (versions.isEmpty()) versions = majors(imports.modules, QStringLiteral("core.dll"));
    if (versions.isEmpty()) versions = majors(imports.modules);

    // Some launchers import a local UI DLL instead of Qt directly. Follow only
    // named dependencies beside this player, with a strict I/O/count budget.
    if (versions.isEmpty()) {
        QStringList pending = imports.modules;
        QSet<QString> visited;
        qint64 budget = 128 * 1024 * 1024;
        while (!pending.isEmpty() && visited.size() < 32 && budget > 0) {
            const QString name = pending.takeFirst().toLower();
            if (visited.contains(name)) continue;
            visited.insert(name);
            const auto path = namedFile(install, name);
            if (path.isEmpty()) continue;
            const auto dependency = PeUtils::importedModules(readImage(path, &budget));
            if (!dependency.ok) continue;
            versions.unite(majors(dependency.modules));
            pending.append(dependency.modules);
        }
        evidence = QStringLiteral("local UI dependency imports");
    }
    if (versions.size() > 1) {
        out.msg = QStringLiteral("This installation references both Qt 5 and Qt 6; automatic selection would be ambiguous. Select its slider DLL manually.");
        return out;
    }
    if (!versions.isEmpty()) out.qtMajor = *versions.cbegin();

    // Every location the Windows loader would consult for a Qt DLL of this generation.
    // Qt5-era installations keep them beside the player or under Qt/Qt5/Qt6 (with or
    // without a bin/ subdirectory), so all of those have to be searched.
    const QStringList locations { install.absolutePath(), install.filePath(QStringLiteral("bin")),
                                  install.filePath(QStringLiteral("Qt/bin")), install.filePath(QStringLiteral("Qt5/bin")),
                                  install.filePath(QStringLiteral("Qt6/bin")), install.filePath(QStringLiteral("Qt")),
                                  install.filePath(QStringLiteral("Qt5")), install.filePath(QStringLiteral("Qt6")) };
    QMap<int, QStringList> candidates;
    for (int major : { 5, 6 }) {
        const auto name = QStringLiteral("Qt%1QuickTemplates2.dll").arg(major);
        for (const auto &location : locations) {
            const auto path = namedFile(QDir(location), name);
            if (!path.isEmpty()) { candidates[major] << path; break; } // the loader's first hit wins
        }
    }
    if (!out.qtMajor) {
        const bool five = !candidates.value(5).isEmpty(), six = !candidates.value(6).isEmpty();
        if (five == six) {
            out.msg = five ? QStringLiteral("Both Qt slider DLL versions are present, but the player's Qt version could not be established. Select the DLL manually.")
                           : QStringLiteral("No Qt5QuickTemplates2.dll or Qt6QuickTemplates2.dll was found in this installation. Searched: %1")
                                 .arg(QDir::toNativeSeparators(locations.join(QStringLiteral("; "))));
            return out;
        }
        out.qtMajor = five ? 5 : 6;
        evidence = QStringLiteral("only slider DLL version present in this installation");
    }
    const auto matches = candidates.value(out.qtMajor);
    if (matches.size() != 1) {
        out.msg = matches.isEmpty()
            ? QStringLiteral("Detected Qt %1, but Qt%1QuickTemplates2.dll is missing from this installation. A DLL from another installation will not be substituted. Searched: %2")
                  .arg(out.qtMajor).arg(QDir::toNativeSeparators(locations.join(QStringLiteral("; "))))
            : QStringLiteral("Detected Qt %1, but multiple matching slider DLLs exist in this installation. Select the correct one manually.").arg(out.qtMajor);
        return out;
    }
    const auto dll = readImage(matches.first());
    if (!PeUtils::isPe32Plus(dll) || PeUtils::machine(dll) != 0x8664) {
        // Reached by every 32-bit (x86) BlueStacks installation: the Qt DLL there is
        // x86, while the slider patch decodes 64-bit code. Naming both the reason and
        // the features that still work keeps this from looking like a detection bug.
        out.msg = QStringLiteral("Detected Qt %1 in a %2-bit installation, but its Qt%1QuickTemplates2.dll is not a 64-bit PE, which the FPS slider patch requires: %3. Themes, gradients, config edits and backups still work for this installation.")
                      .arg(out.qtMajor).arg(out.playerBits ? out.playerBits : 32).arg(matches.first());
        return out;
    }
    out.ok = true;
    out.path = QDir::toNativeSeparators(matches.first());
    out.msg = QStringLiteral("Auto-detected Qt %1 (%2): %3").arg(out.qtMajor).arg(evidence, out.path);
    return out;
}

QString findDll(const QString &installDir)
{
    if (installDir.isEmpty()) return {};
    const auto player = namedFile(QDir(installDir), QStringLiteral("HD-Player.exe"));
    return player.isEmpty() ? QString() : detectDll(player).path;
}
} // namespace FpsPatcher
