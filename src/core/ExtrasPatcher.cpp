#include "ExtrasPatcher.h"

#include "BsDetector.h"
#include "PatchFile.h"
#include "PeUtils.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QTextStream>

#include <algorithm>

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
        { "Sidebar Airplane Mode", "AirplaneModeOff.svg" },
        { "Sidebar Sync Mode", "SidebarSync.svg" },
        { "Sidebar Lock Cursor", "SidebarLockCursor.svg" },
        { "Sidebar Eco Mode", "SidebarEcoMode.svg" },
        { "Sidebar Side Panel", "SidebarSidePanel" },
        { "Sidebar Screen Record", "SidebarScreenRecord.svg" },
        { "Titlebar Menu", "TitlebarMenu.svg" },
    };
    return m;
}

// The title bar prints the player version and the engine label next to it, both
// as their own Text element in PlrTopbar.qml:
//
//     Text { id: iVersionTxt; text: plrBackend.version ... }
//     Text { id: iImageTxt;   text: "N32" ... }
//
// "Titlebar Version" used to be an entry of the string table above, with the
// needle "version". That nulls *every* UTF-16 "version" in the file - property
// names in compiled QML caches and translator tables included, not just the
// one label - and the player then fails to load its own UI (reported as the
// 0x…05 start error against the latest build). Both rows are QML edits now:
// the element is hidden, the string table is not touched.
const QMap<QString, QString> &titlebarHides()
{
    static const QMap<QString, QString> m = {
        { "Titlebar Version", "iVersionTxt" },
        { "Titlebar Instance Label", "iImageTxt" },
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
        { "Sidebar Airplane Mode", "AirplaneModeOff" },
        { "Sidebar Sync Mode", "SidebarSync" },
        { "Sidebar Lock Cursor", "SidebarLockCursor" },
        { "Sidebar Eco Mode", "SidebarEcoMode" },
        { "Sidebar Side Panel", "SidebarSidePanel" },
        { "Sidebar Screen Record", "SidebarScreenRecord" },
    };
    return m;
}

// Disable every PlrSidebarElement whose pAsset literal is one of `assets`.
// Replacing the pIsFeatureEnable value with `false` is the same switch the
// sidebar's own customization uses, so the element leaves pSidebarItems, the
// Repeater and the kebab overflow together - no slot survives to show a gap.
int disableSidebarElements(QString &qml, const QStringList &assets)
{
    const QString masked = QmlText::mask(qml);
    struct Edit { int begin, length; QString replacement; };
    QList<Edit> edits;
    QSet<int> used;
    int hidden = 0;
    for (const QString &asset : assets) {
        const QString literal = QLatin1Char('"') + asset + QLatin1Char('"');
        for (const auto &object : QmlText::objects(masked, QStringLiteral("PlrSidebarElement"))) {
            const int literalAt = qml.indexOf(literal, object.open);
            if (literalAt < 0 || literalAt >= object.end) continue;
            QmlText::Binding feature;
            if (QmlText::binding(masked, object.open, object.end, QStringLiteral("pIsFeatureEnable"), &feature)) {
                if (used.contains(feature.valueBegin)) break;
                used.insert(feature.valueBegin);
                edits << Edit { feature.valueBegin, feature.valueEnd - feature.valueBegin,
                                QStringLiteral("false") };
            } else {
                if (used.contains(object.open + 1)) break;
                used.insert(object.open + 1);
                edits << Edit { object.open + 1, 0, QStringLiteral("\npIsFeatureEnable:false") };
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

// ---------------------------------------------------------------- title bar
// The player's title bar prints `plrBackend.version` and the engine label
// (`"N32"`, `"P64"`, ...) as two Text elements with stable ids in
// PlrTopbar.qml - the same ids in the 5.9, 5.10 and current builds:
//
//     Text { id: iVersionTxt; text: plrBackend.version; ... }
//     Text { id: iImageTxt;   text: "N32"; ... }
//
// Hiding them keeps every binding the player relies on. An existing `visible:`
// member is ANDed with false (so the expression is kept, just never true);
// an element without one gets a tagged `visible: false` member added.
const QString kTitlebarHideTag = QStringLiteral("BSI CUSTOMIZER: titlebar hide");
const QString kTitlebarHidePrefix = QStringLiteral("false /* BSI CUSTOMIZER: titlebar hide */ && (");

struct TitlebarEdit { int begin = 0, length = 0; QString text; };

bool titlebarHiddenSite(const QString &qml, const QmlText::Object &object, int *at, QString *text)
{
    const QString nl = qml.contains(QLatin1String("\r\n")) ? QStringLiteral("\r\n")
                                                          : QStringLiteral("\n");
    int probe = object.open + 1;
    while (probe < qml.size() && (qml.at(probe) == QLatin1Char(' ') || qml.at(probe) == QLatin1Char('\t')))
        ++probe;
    const bool membersOnNewLine = probe < object.end
        && (qml.at(probe) == QLatin1Char('\n') || qml.at(probe) == QLatin1Char('\r'));
    if (membersOnNewLine) {
        // Insert a tagged member on its own line, indented like the first one
        // already inside the element.
        const int newline = qml.indexOf(QLatin1Char('\n'), probe);
        if (newline < 0 || newline >= object.end) return false;
        const int lineStart = newline + 1;
        QString indent;
        int i = lineStart;
        while (i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
            indent += qml.at(i++);
        *at = lineStart;
        *text = indent + QStringLiteral("visible: false // ") + kTitlebarHideTag + nl;
        return true;
    }
    // A one-liner (`Text { id: iVersionTxt; text: ... }`) cannot take a line
    // comment without swallowing the members that follow it.
    *at = object.open + 1;
    *text = QStringLiteral(" visible: false /* ") + kTitlebarHideTag + QStringLiteral(" */;");
    return true;
}

// Hide the object whose `id:` is one of `ids`. Returns how many were hidden.
//
// Every edit below is measured so that it cannot grow the document. A player
// record has a fixed size, and growing it means finding free space in a 24 MB
// EXE that has none - which is the "needs N bytes but its record only holds M"
// error these two rows used to end in on a real build. The stock elements have
// no `visible:` member, so the label is emptied instead:
//
//     text: plrBackend.version   ->   text: ""
//
// The element stays, every binding the layout reads stays, and nothing is
// added to the file.
int hideTitlebarElements(QString &qml, const QStringList &ids)
{
    const QString masked = QmlText::mask(qml);
    const QList<QmlText::Object> all = QmlText::objects(masked, QStringLiteral("Text"));
    QList<TitlebarEdit> edits;
    int hidden = 0;
    for (const QString &id : ids) {
        for (const QmlText::Object &object : all) {
            QmlText::Binding idBinding;
            if (!QmlText::binding(masked, object.open, object.end, QStringLiteral("id"), &idBinding))
                continue;
            if (qml.mid(idBinding.valueBegin, idBinding.valueEnd - idBinding.valueBegin) != id)
                continue;
            // The tag is only ever written by us, so finding it inside the
            // element means an earlier removal already hid this label.
            if (qml.mid(object.open, object.end - object.open).contains(kTitlebarHideTag))
                break;
            // 1) A member the player already reads is switched off in place.
            //    `false` fits wherever the expression was at least five
            //    characters long; anything shorter takes `!1`, also false.
            QmlText::Binding visible;
            if (QmlText::binding(masked, object.open, object.end, QStringLiteral("visible"), &visible)) {
                const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
                const QString replacement = value.size() >= 5 ? QStringLiteral("false")
                                           : value.size() >= 2 ? QStringLiteral("!1")
                                                               : QStringLiteral("0");
                edits << TitlebarEdit { visible.valueBegin, int(value.size()), replacement };
                ++hidden;
                break;
            }
            // 2) The label prints nothing: its text binding is emptied. No
            //    byte is added, so the record never has to move.
            QmlText::Binding text;
            if (QmlText::binding(masked, object.open, object.end, QStringLiteral("text"), &text)) {
                const QString value = qml.mid(text.valueBegin, text.valueEnd - text.valueBegin);
                // Already empty (the quotes may or may not be part of the
                // value): nothing to do, and nothing to grow.
                if (value.trimmed().isEmpty() || value == QLatin1String("\"\"")) break;
                // A one-character expression cannot be replaced by an empty
                // string without adding a byte, so it is left to the fallback.
                if (value.size() < 2) break;
                edits << TitlebarEdit { text.valueBegin, int(value.size()), QStringLiteral("\"\"") };
                // An empty label still reserves its left margin; zero it in
                // the same pass so the row really closes up.
                QmlText::Binding margin;
                if (QmlText::binding(masked, object.open, object.end,
                                     QStringLiteral("Layout.leftMargin"), &margin)) {
                    const QString spacing =
                        qml.mid(margin.valueBegin, margin.valueEnd - margin.valueBegin);
                    if (spacing.trimmed() != QLatin1String("0") && spacing.size() >= 1)
                        edits << TitlebarEdit { margin.valueBegin, int(spacing.size()),
                                                QStringLiteral("0") };
                }
                ++hidden;
                break;
            }
            {
                int at = 0;
                QString text;
                if (!titlebarHiddenSite(qml, object, &at, &text)) break;
                edits << TitlebarEdit { at, 0, text };
            }
            ++hidden;
            break;
        }
    }
    std::sort(edits.begin(), edits.end(), [](const TitlebarEdit &a, const TitlebarEdit &b) {
        return a.begin > b.begin;
    });
    int boundary = qml.size();
    for (const auto &edit : edits) {
        if (edit.begin + edit.length > boundary) continue;
        qml.replace(edit.begin, edit.length, edit.text);
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

// Find PlrTopbar.qml in the image's Qt resource pools and hide the title-bar
// labels named by `ids` inside it. The entry is compressed in every build seen
// so far, so the rewrite goes through the same capacity rule as the resource
// editor: the record has to fit the slot it already owns, or move into verified
// free space inside the same file. The EXE never grows.
bool hideTitlebarLabels(QByteArray &data, const QStringList &ids, int *hidden, QString *error)
{
    const auto refuse = [&](const QString &message) { if (error) *error = message; return false; };
    auto files = QtResources::registeredFiles(data);
    const auto theme = ThemeEngine::findThemeZlib(data);
    if (theme.hasQrcNode)
        for (const auto &file : QtResources::filesInPool(data, theme.resource)) {
            bool present = false;
            for (const auto &old : files)
                if (old.entryOff == file.entryOff && old.name == file.name) present = true;
            if (!present) files << file;
        }
    for (const auto &file : files) {
        if (file.name != QLatin1String("PlrTopbar.qml")) continue;
        const auto bytes = QtResources::readFile(data, file);
        if (bytes.isEmpty())
            return refuse(QStringLiteral("The title bar QML resource could not be read; no file was written."));
        const bool bom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
        QString qml = QString::fromUtf8(bom ? bytes.mid(3) : bytes);
        if (qml.toUtf8() != (bom ? bytes.mid(3) : bytes))
            return refuse(QStringLiteral("The title bar QML is not UTF-8; no file was written."));
        const int count = hideTitlebarElements(qml, ids);
        if (hidden) *hidden = count;
        const QByteArray header = bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        const QByteArray source = header + qml.toUtf8();
        if (source == bytes) return true;
        const auto link = QtResources::findLink(data, file.entryOff, file.name);
        if (!link.found || link.ambiguous)
            return refuse(QStringLiteral("The title bar QML resource mapping is ambiguous; no file was written."));
        const qint64 payload = file.flags == 1 ? qCompress(source, 9).size() : source.size();
        qint64 destination = -1;
        const qint64 capacity = QtResources::inPlaceCapacity(data, file.entryOff, file.name);
        if (capacity >= 0 && payload > capacity) {
            destination = QtResources::findFreeSpace(data, link, 4 + payload, error);
            if (destination < 0)
                return refuse(QStringLiteral("Hiding the title-bar label needs %1 bytes but its record only "
                                             "holds %2 and the file has no free space left; nothing was written.")
                                  .arg(payload)
                                  .arg(capacity));
        }
        qint64 newEntry = file.entryOff;
        if (!QtResources::replaceResource(data, file.entryOff, link, source, error, &newEntry,
                                          /*allowGrowth=*/false, destination))
            return false;
        return true;
    }
    // A player without an embedded title bar (older launchers keep it in a
    // separate QML file) has nothing to hide here; that is not an error.
    if (hidden) *hidden = 0;
    return true;
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

QStringList removalNames()
{
    QStringList names = removeStrings().keys();
    for (const ContextualRemoval &cr : contextualRemovals())
        names << cr.name;
    for (const QString &name : titlebarHides().keys())
        names << name;
    return names;
}

Result applyRemovals(const QString &exePath, const QStringList &enabledNames)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before removing UI strings.") };
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read EXE.") };
    QByteArray data = f.readAll();
    f.close();
    const QByteArray original = data;
    const qsizetype originalSize = data.size();

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
    QStringList titlebarIds;
    for (const QString &name : enabledNames) {
        const auto it = titlebarHides().constFind(name);
        if (it != titlebarHides().constEnd() && !titlebarIds.contains(*it))
            titlebarIds << *it;
    }

    if (replacements.isEmpty() && hiddenAssets.isEmpty() && titlebarIds.isEmpty())
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
    bool hasResources = PeUtils::peHeaderOffset(data) >= 0;
    if (!hiddenAssets.isEmpty() && hasResources) {
        QString error;
        if (!collapseSidebarButtons(data, hiddenAssets, &hidden, &error))
            return { false, error };
    }
    // The title-bar version and engine label are hidden in the QML, not removed
    // from the file: nulling the string "version" also hit the compiled QML
    // caches and the translator tables, and the player then refused to start.
    int hiddenLabels = 0;
    if (!titlebarIds.isEmpty() && hasResources) {
        QString error;
        if (!hideTitlebarLabels(data, titlebarIds, &hiddenLabels, &error))
            return { false, error };
    }

    if (data.size() != originalSize)
        return { false, QStringLiteral("The edit would have changed the EXE size; nothing was written.") };
    // Same verified write every other patcher uses: a backup first, an
    // in-memory read-back of the record, and only then the disk.
    if (!ThemeEngine::backup(exePath))
        return { false, QStringLiteral("Could not preserve the original backup; no EXE was written.") };
    QString writeError;
    if (!PatchFile::writeVerified(exePath, original, data, &writeError))
        return { false, writeError };
    QString msg = QStringLiteral("Removed %1 string(s).").arg(replacements.size());
    if (hidden > 0)
        msg += QStringLiteral(" Hid %1 removed sidebar button(s) in the player QML so the sidebar closes up without leaving a gap.")
                   .arg(hidden);
    if (hiddenLabels > 0)
        msg += QStringLiteral(" Hid %1 title-bar label(s) in the player QML; the strings themselves are "
                              "untouched, so nothing the player reads was changed.")
                   .arg(hiddenLabels);
    else if (!titlebarIds.isEmpty() && hasResources)
        msg += QStringLiteral(" The title-bar label was already hidden in this player.");
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
