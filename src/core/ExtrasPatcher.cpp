#include "ExtrasPatcher.h"

#include "ThemeEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

namespace ExtrasPatcher {

namespace {

QByteArray utf16le(const QString &s)
{
    QByteArray out;
    for (const QChar c : s) {
        out.append(char(c.unicode() & 0xFF));
        out.append(char((c.unicode() >> 8) & 0xFF));
    }
    return out;
}

// Ad / string removal table (SVG file names, matched as UTF-16LE).
const QMap<QString, QString> &removeStrings()
{
    static const QMap<QString, QString> m = {
        { "Titlebar Help", "TitlebarHelp" },
        { "Sidebar InstanceManager", "SidebarInstanceManager.svg" },
        { "Sidebar Rotate", "SidebarRotate.svg" },
        { "Sidebar Location", "SidebarLocation.svg" },
        { "Sidebar Shake", "SidebarShake.svg" },
        { "Sidebar Macro", "SidebarMacro.svg" },
        { "Sidebar Screenshot", "SidebarScreenshot.svg" },
        { "Sidebar MediaFolder", "SidebarMediaFolder.svg" },
        { "Sidebar Fullscreen", "SidebarFullScreen.svg" },
        { "Sidebar Airplane Mode", "AirplaneModeOff.svg" },
        { "Sidebar Sync Mode", "SidebarSync.svg" },
        { "Sidebar Lock Cursor", "SidebarLockCursor.svg" },
        { "Sidebar Eco Mode", "SidebarEcoMode.svg" },
        { "Titlebar Logo", "BlueStacksLogo.svg" },
        { "Sidebar Side Panel", "SidebarSidePanel" },
        { "Sidebar Screen Record", "SidebarScreenRecord.svg" },
        { "Titlebar Menu", "TitlebarMenu.svg" },
        { "Titlebar Version", "version" },
    };
    return m;
}

struct ContextualRemoval
{
    QString name, search, target, replaceWith; // "spaces" | "null"
};

const QList<ContextualRemoval> &contextualRemovals()
{
    static const QList<ContextualRemoval> list = {
        { "Clean Boot Screen",
          "Zoom In/Out in-game, press CTRL+Mousewheel Up/Down", "qsTranslate", "spaces" },
        { "Titlebar Home", "iHomeBtn", "SidebarHome", "null" },
        { "Titlebar Recents", "iRecentBtn", "SidebarRecents", "null" },
        { "Titlebar Back", "iBackBtn", "SidebarBack", "null" },
    };
    return list;
}

// Instance detection patterns from bluestacks.conf.
struct InstancePattern { QString key, pattern, display, confKey; };
const QList<InstancePattern> &instancePatterns()
{
    static const QList<InstancePattern> list = {
        { "Nougat32", "N32", "N32", "Nougat32" },
        { "Pie64", "P64", "P64", "Pie64" },
        { "Nougat64", "N64", "N64", "Nougat64" },
        { "Rvc64", "Android 11", "Android 11", "Rvc64" },
        { "Tiramisu64", "Tiramisu64", "Tiramisu64", "Tiramisu64" },
    };
    return list;
}

} // namespace

QList<QVariantMap> findVersions(const QByteArray &data)
{
    QList<QVariantMap> found;
    QSet<QString> seen;
    const QString text = QString::fromLatin1(data); // 1:1 byte mapping
    for (const QString &pat : { QStringLiteral("\\d+\\.\\d+\\.\\d+\\.\\d+"),
                                QStringLiteral("\\d+\\.\\d+\\.\\d+") }) {
        const QRegularExpression re(pat);
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString v = m.captured();
            if (seen.contains(v))
                continue;
            seen.insert(v);
            found << QVariantMap { { "version", v }, { "offset", m.capturedStart() } };
        }
    }
    return found;
}

Result applyVersion(const QString &exePath, const QString &oldV, const QString &newV)
{
    if (newV.size() > oldV.size())
        return { false, QStringLiteral("New version string longer than original.") };

    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read EXE.") };
    QByteArray data = f.readAll();
    f.close();

    const QByteArray oldB = oldV.toLatin1();
    QByteArray rep = newV.toLatin1();
    rep += QByteArray(oldB.size() - rep.size(), '\0');

    int changed = 0;
    qsizetype pos = 0;
    while (true) {
        pos = data.indexOf(oldB, pos);
        if (pos < 0)
            break;
        data.replace(int(pos), oldB.size(), rep);
        ++changed;
        pos += oldB.size();
    }
    if (changed == 0)
        return { false, QStringLiteral("Version string not found.") };

    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?).") };
    out.write(data);
    out.close();
    return { true, QStringLiteral("Version spoofed (%1 -> %2) at %3 location(s).")
                       .arg(oldV, newV)
                       .arg(changed) };
}

QStringList removalNames()
{
    QStringList names = removeStrings().keys();
    for (const ContextualRemoval &cr : contextualRemovals())
        names << cr.name;
    return names;
}

Result applyRemovals(const QString &exePath, const QStringList &enabledNames)
{
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read EXE.") };
    QByteArray data = f.readAll();
    f.close();

    QList<ThemeEngine::Replacement> replacements;

    for (const QString &name : enabledNames) {
        if (removeStrings().contains(name)) {
            const QByteArray pattern = utf16le(removeStrings().value(name));
            qsizetype idx = 0;
            while (true) {
                idx = data.indexOf(pattern, idx);
                if (idx < 0)
                    break;
                replacements << ThemeEngine::Replacement { idx, pattern.size(),
                                                           QByteArray(pattern.size(), '\0') };
                idx += pattern.size();
            }
            continue;
        }
        for (const ContextualRemoval &cr : contextualRemovals()) {
            if (cr.name != name)
                continue;
            const QByteArray searchBytes = utf16le(cr.search);
            const QByteArray targetBytes = utf16le(cr.target);
            const qsizetype idx = data.indexOf(searchBytes);
            if (idx < 0)
                continue;
            const qsizetype tpos = data.indexOf(targetBytes, idx + searchBytes.size());
            if (tpos < 0)
                continue;
            const QByteArray rep = (cr.replaceWith == QLatin1String("spaces"))
                                       ? QByteArray(targetBytes.size(), ' ')
                                       : QByteArray(targetBytes.size(), '\0');
            replacements << ThemeEngine::Replacement { tpos, targetBytes.size(), rep };
        }
    }

    if (replacements.isEmpty())
        return { false, QStringLiteral("No matching strings found.") };

    std::sort(replacements.begin(), replacements.end(),
              [](const ThemeEngine::Replacement &a, const ThemeEngine::Replacement &b) {
                  return a.pos > b.pos;
              });
    for (const auto &r : replacements)
        data.replace(int(r.pos), r.len, r.rep);

    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?).") };
    out.write(data);
    out.close();
    return { true, QStringLiteral("Removed %1 string(s).").arg(replacements.size()) };
}

QVariantList detectInstances(const QString &confPath)
{
    QVariantList out;
    QFile f(confPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    for (const InstancePattern &p : instancePatterns()) {
        if (text.contains(p.pattern))
            out << QVariantMap { { "name", p.key }, { "display", p.display },
                                 { "conf_key", p.confKey } };
    }
    return out;
}

} // namespace ExtrasPatcher
