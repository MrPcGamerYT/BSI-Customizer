#include "ExtrasPatcher.h"

#include "PeUtils.h"
#include "QtResources.h"
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

// Sidebar buttons whose icon name a removal row nulls. Blankening the glyph is
// only half of a removal: the button keeps its 32px slot and the sidebar shows
// an empty gap where it used to be (reported against the latest player). The
// QML side of the removal disables the owning PlrSidebarElement, so it drops
// out of pSidebarItems before the first layout pass and the column closes up.
const QMap<QString, QString> &sidebarButtonAssets()
{
    static const QMap<QString, QString> m = {
        { "Sidebar InstanceManager", "SidebarInstanceManager" },
        { "Sidebar Rotate", "SidebarRotate" },
        { "Sidebar Location", "SidebarLocation" },
        { "Sidebar Shake", "SidebarShake" },
        { "Sidebar Macro", "SidebarMacro" },
        { "Sidebar Screenshot", "SidebarScreenshot" },
        { "Sidebar MediaFolder", "SidebarMediaFolder" },
        { "Sidebar Fullscreen", "SidebarFullScreen" },
        { "Sidebar Airplane Mode", "AirplaneModeOff" },
        { "Sidebar Sync Mode", "SidebarSync" },
        { "Sidebar Lock Cursor", "SidebarLockCursor" },
        { "Sidebar Eco Mode", "SidebarEcoMode" },
        { "Sidebar Side Panel", "SidebarSidePanel" },
        { "Sidebar Screen Record", "SidebarScreenRecord" },
    };
    return m;
}

// String/comment contents are blanked so brace counting and member search
// never see the braces, colons or keywords inside a literal. BlueStacks' QML
// mixes double- and single-quoted strings with both line comment styles.
QString maskQml(const QString &qml)
{
    QString mask = qml;
    const int n = mask.size();
    int i = 0;
    while (i < n) {
        const QChar c = mask.at(i);
        if (c == QLatin1Char('/') && i + 1 < n && mask.at(i + 1) == QLatin1Char('/')) {
            while (i < n && mask.at(i) != QLatin1Char('\n')) mask[i++] = QLatin1Char(' ');
        } else if (c == QLatin1Char('/') && i + 1 < n && mask.at(i + 1) == QLatin1Char('*')) {
            mask[i++] = QLatin1Char(' ');
            mask[i++] = QLatin1Char(' ');
            while (i + 1 < n && !(mask.at(i) == QLatin1Char('*') && mask.at(i + 1) == QLatin1Char('/'))) mask[i++] = QLatin1Char(' ');
            if (i < n) mask[i++] = QLatin1Char(' ');
            if (i < n) mask[i++] = QLatin1Char(' ');
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar quote = c;
            mask[i++] = QLatin1Char(' ');
            while (i < n && mask.at(i) != quote) {
                if (mask.at(i) == QLatin1Char('\\') && i + 1 < n) mask[i++] = QLatin1Char(' ');
                mask[i++] = QLatin1Char(' ');
            }
            if (i < n) mask[i++] = QLatin1Char(' ');
        } else {
            ++i;
        }
    }
    return mask;
}

// Start offsets of `type {` object literals (the type may be qualified).
QList<int> objectStarts(const QString &mask, const QString &type)
{
    QList<int> starts;
    for (int at = mask.indexOf(type); at >= 0; at = mask.indexOf(type, at + 1)) {
        if (at > 0 && (mask.at(at - 1).isLetterOrNumber() || mask.at(at - 1) == QLatin1Char('_')))
            continue; // a longer identifier merely ends in `type`
        int open = at + type.size();
        while (open < mask.size() && mask.at(open).isSpace()) ++open;
        if (open < mask.size() && mask.at(open) == QLatin1Char('{')) starts << open;
    }
    return starts;
}

// Exclusive end offset of the brace that opens at `open`.
int blockEnd(const QString &mask, int open)
{
    int depth = 0;
    for (int i = open; i < mask.size(); ++i) {
        if (mask.at(i) == QLatin1Char('{')) ++depth;
        else if (mask.at(i) == QLatin1Char('}') && --depth == 0) return i + 1;
    }
    return -1;
}

// [start, end) of a member's value expression inside one object block. The
// value runs to the end of its statement: a line end at balanced (), [], {}
// depth, or a ';' - BlueStacks writes one member per line and wraps long
// calls across lines while the parentheses stay open.
bool bindingValue(const QString &mask, int blockBegin, int blockStop,
                  const QString &name, int *valueBegin, int *valueEnd)
{
    for (int at = blockBegin; at >= 0 && at < blockStop;) {
        const int found = mask.indexOf(name, at);
        if (found < 0 || found >= blockStop) return false;
        const bool boundary = found == blockBegin
            || !(mask.at(found - 1).isLetterOrNumber() || mask.at(found - 1) == QLatin1Char('_') || mask.at(found - 1) == QLatin1Char('.'));
        int colon = found + name.size();
        while (colon < blockStop && mask.at(colon).isSpace()) ++colon;
        if (boundary && colon < blockStop && mask.at(colon) == QLatin1Char(':')
            && !(colon + 1 < blockStop && mask.at(colon + 1) == QLatin1Char('='))) {
            int start = colon + 1;
            while (start < blockStop && mask.at(start).isSpace()) ++start;
            int depth = 0;
            int end = start;
            for (; end < blockStop; ++end) {
                const QChar c = mask.at(end);
                if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) ++depth;
                else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) {
                    if (depth == 0) break;
                    --depth;
                } else if (depth == 0 && (c == QLatin1Char('\n') || c == QLatin1Char(';'))) break;
            }
            // Never swallow the line ending itself: the replacement keeps the
            // file's CRLF layout byte-stable around the edited member.
            while (end > start && (mask.at(end - 1) == QLatin1Char(' ')
                                   || mask.at(end - 1) == QLatin1Char('\t')
                                   || mask.at(end - 1) == QLatin1Char('\r'))) --end;
            *valueBegin = start;
            *valueEnd = end;
            return true;
        }
        at = found + name.size();
    }
    return false;
}

// Disable every PlrSidebarElement whose pAsset literal is one of `assets`.
// Replacing the pIsFeatureEnable value with `false` is the same switch the
// sidebar's own customization uses, so the element leaves pSidebarItems, the
// Repeater and the kebab overflow together - no slot survives to show a gap.
int disableSidebarElements(QString &qml, const QStringList &assets)
{
    const QString mask = maskQml(qml);
    struct Edit { int begin, length; QString replacement; };
    QList<Edit> edits;
    QSet<int> used;
    int hidden = 0;
    for (const QString &asset : assets) {
        const QString literal = QLatin1Char('"') + asset + QLatin1Char('"');
        for (const int open : objectStarts(mask, QStringLiteral("PlrSidebarElement"))) {
            const int end = blockEnd(mask, open);
            if (end < 0) continue;
            const int literalAt = qml.indexOf(literal, open);
            if (literalAt < 0 || literalAt >= end) continue;
            int valueBegin = 0, valueEnd = 0;
            if (bindingValue(mask, open, end, QStringLiteral("pIsFeatureEnable"), &valueBegin, &valueEnd)) {
                if (used.contains(valueBegin)) break;
                used.insert(valueBegin);
                edits << Edit { valueBegin, valueEnd - valueBegin, QStringLiteral("false") };
            } else {
                if (used.contains(open + 1)) break;
                used.insert(open + 1);
                edits << Edit { open + 1, 0, QStringLiteral("\npIsFeatureEnable:false") };
            }
            ++hidden;
            break;
        }
    }
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.begin > b.begin; });
    int boundary = qml.size();
    for (const auto &edit : edits) {
        if (edit.begin + edit.length > boundary) continue;
        qml.replace(edit.begin, edit.length, edit.replacement);
        boundary = edit.begin;
    }
    return hidden;
}

// Locate the sidebar document in the image's Qt resources and disable the
// removed buttons inside it. Players without an embedded PlrSidebar.qml are
// not an error: their removals are string-only and there is no layout to
// close up.
bool collapseSidebarButtons(QByteArray &data, const QStringList &assets, int *hidden, QString *error)
{
    const auto refuse = [&](const QString &message) { if (error) *error = message; return false; };
    const auto theme = ThemeEngine::findThemeZlib(data);
    auto files = QtResources::registeredFiles(data);
    if (theme.hasQrcNode)
        for (const auto &file : QtResources::filesInPool(data, theme.resource)) {
            bool present = false;
            for (const auto &old : files) if (old.entryOff == file.entryOff && old.name == file.name) present = true;
            if (!present) files << file;
        }
    for (const auto &file : files) {
        if (file.name != QLatin1String("PlrSidebar.qml") && file.name != QLatin1String("qrc:/PlrSidebar.qml")) continue;
        const auto bytes = QtResources::readFile(data, file);
        if (bytes.isEmpty()) return refuse(QStringLiteral("The sidebar QML resource could not be read; no file was written."));
        const bool bom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
        QString qml = QString::fromUtf8(bom ? bytes.mid(3) : bytes);
        if (qml.toUtf8() != (bom ? bytes.mid(3) : bytes))
            return refuse(QStringLiteral("The sidebar QML is not UTF-8; no file was written."));
        const int count = disableSidebarElements(qml, assets);
        const auto link = QtResources::findLink(data, file.entryOff, file.name);
        if (!link.found || link.ambiguous)
            return refuse(QStringLiteral("The sidebar QML resource mapping is ambiguous; no file was written."));
        const QByteArray header = bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        const QByteArray source = header + qml.toUtf8();
        if (source != bytes && !QtResources::replaceResource(data, file.entryOff, link, source, error))
            return false;
        if (hidden) *hidden = count;
        return true;
    }
    return true; // No embedded sidebar: nothing to collapse.
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
                replacements << ThemeEngine::Replacement { idx, int(pattern.size()),
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
            replacements << ThemeEngine::Replacement { tpos, int(targetBytes.size()), rep };
        }
    }

    QStringList hiddenAssets;
    for (const QString &name : enabledNames) {
        const auto it = sidebarButtonAssets().constFind(name);
        if (it != sidebarButtonAssets().constEnd() && !hiddenAssets.contains(*it))
            hiddenAssets << *it;
    }

    if (replacements.isEmpty() && hiddenAssets.isEmpty())
        return { false, QStringLiteral("No matching strings found.") };

    std::sort(replacements.begin(), replacements.end(),
              [](const ThemeEngine::Replacement &a, const ThemeEngine::Replacement &b) {
                  return a.pos > b.pos;
              });
    for (const auto &r : replacements)
        data.replace(int(r.pos), r.len, r.rep);

    // The QML half of the sidebar removals: a nulled icon name blanks the
    // glyph but leaves the button's 32px slot behind, so the sidebar shows a
    // gap where the button used to be. Disabling the owning element removes
    // the whole row from the layout. Files that carry no Qt resources have
    // nothing to collapse and skip this stage.
    int hidden = 0;
    if (!hiddenAssets.isEmpty() && PeUtils::peHeaderOffset(data) >= 0) {
        QString error;
        if (!collapseSidebarButtons(data, hiddenAssets, &hidden, &error))
            return { false, error };
    }

    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?).") };
    out.write(data);
    out.close();
    QString msg = QStringLiteral("Removed %1 string(s).").arg(replacements.size());
    if (hidden > 0)
        msg += QStringLiteral(" Hid %1 removed sidebar button(s) in the player QML so the sidebar closes up without leaving a gap.")
                   .arg(hidden);
    return { true, msg };
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
