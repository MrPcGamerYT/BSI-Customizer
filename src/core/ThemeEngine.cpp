#include "ThemeEngine.h"

#include "ByteUtils.h"
#include "PeUtils.h"
#include "ZlibUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace ByteUtils;

namespace {

// ---- tables (exact values from the Python tool) ----

const QMap<QString, QString> &themePurpose()
{
    static const QMap<QString, QString> m = {
        { "primary100", "Border shadows + selected color" }, { "primary90", "2nd UI color" },
        { "primary80", "Main UI color" }, { "primary70", "Sidebar color" },
        { "primary60", "Hover color" }, { "primary50", "Box borders + some hovers" },
        { "primary40", "More hovers, etc." }, { "primary30", "Light UI tint" },
        { "primary20", "Light UI tint" }, { "primary10", "Text color" },
        { "accentnormal", "Accent / highlight" }, { "accenthover", "Accent hover" },
        { "accentclick", "Accent click" }, { "accentclicktransparent", "Accent click (transparent)" },
        { "secondarynormal", "Secondary buttons" }, { "secondaryhover", "Secondary hover" },
        { "secondaryclick", "Secondary click" },
        { "tertiarynormal", "Tertiary buttons" }, { "tertiaryhover", "Tertiary hover" },
        { "tertiaryclick", "Tertiary click" },
        { "alternatenormal", "Alternate accent" }, { "alternatehover", "Alternate hover" },
        { "alternateclick", "Alternate click" },
        { "dangernormal", "Danger buttons" }, { "dangerhover", "Danger hover" },
        { "dangerclick", "Danger click" }, { "dangerdark", "Danger dark bg" },
        { "dangertranslucent", "Danger translucent" }, { "dangertransparent", "Danger transparent" },
        { "notice", "Notice / info" }, { "success", "Success" },
        { "warning", "Warning" }, { "error", "Error" },
        { "overlayprimary", "Overlay / dim" }, { "overlayblack", "Overlay black" },
        { "overlaygray", "Overlay gray" }, { "overlaygrey", "Overlay grey" },
        { "overlaybright", "Overlay bright" }, { "overlaytint", "Overlay tint" },
        { "black", "Black" }, { "white", "White" }, { "transparent", "Transparent" },
        { "cloudgradientprimary", "Cloud gradient 1" }, { "cloudgradientsecondary", "Cloud gradient 2" },
        { "notificationgradientprimary", "Notification gradient 1" },
        { "notificationgradientsecondary", "Notification gradient 2" },
        { "subwindowheader", "Sub-window header" }, { "subwindowheaderclick", "Sub-window header (clicked)" },
        { "subwindowbg", "Sub-window background" }, { "separator", "Separators / dividers" },
    };
    return m;
}

QMap<QString, QPair<QString, QString>> msi5Targets()
{
    return {
        { "Sidebar (#000000)",    { "#000000", "utf16" } },
        { "Settings Window",      { "#21242C", "utf16" } },
        { "Toggle Button",        { "#ED1C24", "ascii" } },
        { "Toggle Hover",         { "#FF333B", "ascii" } },
        { "Cursor/Slider/Border", { "#EB2C43", "both" } },
        { "Hover Accent",         { "#454954", "both" } },
        { "Click Color",          { "#17181C", "both" } },
        { "Shadow Color",         { "#0B0C0E", "both" } },
        { "Keymap Item Hover",    { "#5C6170", "both" } },
        { "Close Button Click",   { "#C61017", "both" } },
        { "Close Button Hover",   { "#131621", "utf16" } },
    };
}

QMap<QString, QPair<QString, QString>> nxtTargets()
{
    return {
        { "Sidebar/Topbar",      { "#232642", "utf16" } },
        { "Slider",              { "#1a90ff", "utf16" } },
        { "Toggle Button",       { "#1a90ff", "ascii" } },
        { "Toggle Button Hover", { "#2ea4ff", "ascii" } },
        { "Combobox",            { "#1b1e38", "utf16" } },
    };
}

QByteArray utf16le(const QString &s)
{
    QByteArray out;
    for (const QChar c : s) {
        out.append(char(c.unicode() & 0xFF));
        out.append(char((c.unicode() >> 8) & 0xFF));
    }
    return out;
}

QByteArray utf16be(const QString &s)
{
    QByteArray out;
    for (const QChar c : s) {
        out.append(char((c.unicode() >> 8) & 0xFF));
        out.append(char(c.unicode() & 0xFF));
    }
    return out;
}

void log(const ThemeEngine::Logger &lg, const QString &msg, const QString &color)
{
    if (lg)
        lg(msg, color);
}

// ---- split_color_token port ----
struct ColorToken
{
    bool ok = false;
    QString val;
    bool quoted = false;
    int start = 0;
    int len = 0;
};

ColorToken splitColorToken(const QString &raw, int start)
{
    QString t = raw;
    while (t.endsWith(QLatin1Char(' ')) || t.endsWith(QLatin1Char('\t')))
        t.chop(1);
    if (t.isEmpty())
        return {};

    ColorToken tk;
    tk.start = start;
    if (t.startsWith(QLatin1Char('"'))) {
        const int q2 = t.indexOf(QLatin1Char('"'), 1);
        if (q2 >= 0) {
            tk.val = t.mid(1, q2 - 1);
            tk.len = q2 + 1;
            tk.quoted = true;
        } else {
            tk.val = t;
            tk.val.remove(QLatin1Char('"'));
            tk.len = t.size();
            tk.quoted = true;
        }
        tk.ok = true;
        return tk;
    }
    int cut = t.indexOf(QStringLiteral("//"));
    if (cut < 0)
        cut = t.size();
    QString tok = t.left(cut);
    while (tok.endsWith(QLatin1Char(' ')) || tok.endsWith(QLatin1Char('\t')))
        tok.chop(1);
    if (tok.isEmpty())
        return {};
    tk.val = tok;
    tk.len = tok.size();
    tk.quoted = false;
    tk.ok = true;
    return tk;
}

// ---- qrc relocation helpers (port of the "no room" fix block) ----

bool decompressExactAt(const QByteArray &data, qint64 zoff, qint64 compLen,
                       quint32 usize, QByteArray *out)
{
    if (compLen <= 0 || zoff < 0 || zoff + compLen > data.size())
        return false;
    const QByteArray res = ZlibUtils::tryDecompress(data.mid(zoff, compLen), qint64(usize));
    if (res.isEmpty())
        return false;
    if (out)
        *out = res;
    return true;
}

struct QrcNode
{
    bool found = false;
    qint64 nodeOff = -1;
    qint64 nameRecordOff = -1;
    qint64 nameBaseRaw = -1;
    qint64 dataBaseRaw = -1;
    qint64 dataBaseRva = -1;
    quint32 dataOffset = 0;
};

QList<qint64> qrcNameRecords(const QByteArray &data, const QString &name)
{
    // Qt qrc name record: [be16 length][be32 hash][UTF-16BE name]
    QList<qint64> out;
    const QByteArray pat = utf16be(name);
    qsizetype pos = 0;
    while (true) {
        pos = data.indexOf(pat, pos);
        if (pos < 0)
            break;
        const qint64 rec = pos - 6;
        if (rec >= 0 && inRange(data, rec, 2) && be16(data, rec) == quint16(name.size()))
            out << rec;
        pos += 1;
    }
    return out;
}

QrcNode findQrcNodeForEntry(const QByteArray &data, qint64 entryOff,
                            const QString &name = QStringLiteral("UiThemeObject.qml"))
{
    QrcNode best;
    const QList<qint64> nameRecords = qrcNameRecords(data, name);
    if (nameRecords.isEmpty())
        return best;

    const QList<PeSection> secs = PeUtils::sections(data);
    const qint64 entryRva = secs.isEmpty() ? -1 : PeUtils::rawToRva(secs, entryOff);

    qint64 bestScore = 0;

    auto scanRange = [&](qint64 nameRec, qint64 start, qint64 end, bool strict) {
        start = qMax<qint64>(0, start);
        end = qMin<qint64>(data.size() - 14, end);
        if (start >= end)
            return;
        for (qint64 node = start; node < end; ++node) {
            if (!inRange(data, node, 14))
                continue;
            const quint16 flags = be16(data, node + 4);
            if ((flags & 0x0001) == 0 || (flags & 0x0002))
                continue; // must be a compressed file node, not a directory
            const quint32 nameOff = be32(data, node);
            const quint32 dataOff = be32(data, node + 10);

            const qint64 nameBaseRaw = nameRec - nameOff;
            const qint64 dataBaseRaw = entryOff - dataOff;
            if (nameBaseRaw < 0 || nameBaseRaw >= data.size())
                continue;
            if (dataBaseRaw < 0 || dataBaseRaw >= data.size())
                continue;
            if (strict && !(dataBaseRaw < nameBaseRaw && nameBaseRaw < node))
                continue;

            qint64 dataBaseRva = -1;
            if (entryRva >= 0)
                dataBaseRva = entryRva - dataOff;

            qint64 score = std::llabs(node - nameRec);
            if (dataBaseRaw < nameBaseRaw && nameBaseRaw < node)
                score -= 0x10000000; // strongly prefer normal qrc array order
            if (node >= nameRec)
                score -= 0x1000;

            if (!best.found || score < bestScore) {
                bestScore = score;
                best.found = true;
                best.nodeOff = node;
                best.nameRecordOff = nameRec;
                best.nameBaseRaw = nameBaseRaw;
                best.dataBaseRaw = dataBaseRaw;
                best.dataBaseRva = dataBaseRva;
                best.dataOffset = dataOff;
            }
        }
    };

    for (const qint64 nr : nameRecords)
        scanRange(nr, nr, nr + 0x200000, true);
    if (best.found)
        return best;
    for (const qint64 nr : nameRecords)
        scanRange(nr, qMax<qint64>(0, nr - 0x40000), nr + 0x200000, false);
    return best;
}

qint64 findZeroCave(const QByteArray &data, const QList<PeSection> &secs,
                    qint64 oldEntryOff, qint64 dataBaseRva, qint64 needLen)
{
    if (needLen <= 0)
        return -1;
    const QByteArray probe(int(qMin<qint64>(needLen, 64)), '\0');

    QList<const PeSection *> preferred;
    const PeSection *oldSec = PeUtils::sectionForRaw(secs, oldEntryOff);
    if (oldSec)
        preferred << oldSec;
    for (const PeSection &s : secs) {
        if ((s.name == QLatin1String(".rdata") || s.name == QLatin1String(".data")
             || s.name == QLatin1String(".rsrc"))
            && !preferred.contains(&s))
            preferred << &s;
    }
    for (const PeSection &s : secs) {
        if (s.name != QLatin1String(".text") && !preferred.contains(&s))
            preferred << &s;
    }

    for (const PeSection *s : preferred) {
        qint64 start = s->rawPtr;
        const qint64 end = s->rawPtr + s->rawSize;
        if (oldSec == s)
            start = qMax(start, oldEntryOff);
        if (end - start < needLen)
            continue;

        qsizetype pos = data.indexOf(probe, int(start));
        while (pos >= 0 && pos < end) {
            pos = (pos + 3) & ~qsizetype(3); // qrc data alignment
            if (pos + needLen <= end
                && data.mid(pos, int(needLen)) == QByteArray(int(needLen), '\0')) {
                const qint64 rva = PeUtils::rawToRva(secs, pos);
                if (rva >= 0) {
                    if (dataBaseRva >= 0) {
                        const qint64 newDataOff = rva - dataBaseRva;
                        if (newDataOff >= 0 && newDataOff <= 0xFFFFFFFF)
                            return pos;
                    } else {
                        return pos;
                    }
                }
            }
            pos = data.indexOf(probe, pos + 1);
        }
    }
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

QStringList ThemeEngine::qmlColorNames()
{
    static const QStringList names = {
        "primary100", "primary90", "primary80", "primary70", "primary60",
        "primary50", "primary40", "primary30", "primary20", "primary10",
        "subWindowHeader", "subWindowHeaderClick", "subWindowBg", "separator",
        "accentNormal", "accentHover", "accentClick", "accentClickTransparent",
        "secondaryNormal", "secondaryHover", "secondaryClick",
        "tertiaryNormal", "tertiaryHover", "tertiaryClick",
        "alternateNormal", "alternateHover", "alternateClick",
        "dangerNormal", "dangerHover", "dangerClick", "dangerDark",
        "dangerTranslucent", "dangerTransparent",
        "notice", "success", "warning", "error",
        "overlayPrimary", "overlayBlack", "overlayGray", "overlayGrey",
        "overlayBright", "overlayTint",
        "black", "transparent", "white",
        "cloudGradientPrimary", "cloudGradientSecondary",
        "notificationGradientPrimary", "notificationGradientSecondary",
    };
    return names;
}

QString ThemeEngine::purpose(const QString &name)
{
    return themePurpose().value(name.toLower());
}

QMap<QString, QPair<QString, QString>> ThemeEngine::hexTargets(const QString &profile)
{
    return profile == QLatin1String("MSI5") ? msi5Targets() : nxtTargets();
}

QString ThemeEngine::detectProfileFromPath(const QString &exePath)
{
    const QString d = QDir::fromNativeSeparators(exePath).toLower();
    if (d.contains(QLatin1String("nxt")))
        return QStringLiteral("NXT");
    if (d.contains(QLatin1String("msi5")) || d.contains(QLatin1String("msi 5")))
        return QStringLiteral("MSI5");
    return {};
}

// ---------------------------------------------------------------------------
// Detection
// ---------------------------------------------------------------------------

ThemeInfo ThemeEngine::findThemeZlib(const QByteArray &data)
{
    QList<ThemeInfo> candidates;
    QSet<qint64> seen;

    const QList<qint64> magics = ZlibUtils::findMagicOffsets(data);
    for (const qint64 pos : magics) {
        if (pos < 8 || seen.contains(pos))
            continue;
        seen.insert(pos);

        if (!inRange(data, pos - 8, 8))
            continue;
        const quint32 storedSize = be32(data, pos - 8);
        const quint32 usize = be32(data, pos - 4);
        if (usize < 16 || usize > 50000000u)
            continue;

        // Qt qrc normally stores size = compressed_len + 4; try both.
        quint32 compLen = 0;
        bool includesUSize = true;
        QByteArray qbytes;
        if (decompressExactAt(data, pos, qint64(storedSize) - 4, usize, &qbytes)) {
            compLen = storedSize - 4;
            includesUSize = true;
        } else if (decompressExactAt(data, pos, storedSize, usize, &qbytes)) {
            compLen = storedSize;
            includesUSize = false;
        } else {
            continue;
        }

        if (qbytes.size() < 1000)
            continue;
        const QString qml = QString::fromLatin1(qbytes);
        if (!qml.contains(QLatin1String("property QtObject colors")))
            continue;
        if (!qml.contains(QLatin1String("property color primary100"))
            && !qml.contains(QLatin1String("primary100")))
            continue;

        ThemeInfo info;
        info.found = true;
        info.mode = QStringLiteral("zlib");
        info.qml = qml;
        info.entryOff = pos - 8;
        info.zoff = pos;
        info.storedSize = storedSize;
        info.compSize = compLen;
        info.uncompSize = usize;
        info.sizeIncludesUSize = includesUSize;
        info.entryCount = parseQmlEntries(qml).size();

        const QrcNode node = findQrcNodeForEntry(data, info.entryOff);
        if (node.found) {
            info.hasQrcNode = true;
            info.qrcNodeOff = node.nodeOff;
            info.qrcNameRecordOff = node.nameRecordOff;
            info.qrcNameBaseRaw = node.nameBaseRaw;
            info.qrcDataBaseRaw = node.dataBaseRaw;
            info.qrcDataBaseRva = node.dataBaseRva;
            info.qrcDataOffset = node.dataOffset;
        }
        candidates << info;
    }

    if (candidates.isEmpty())
        return {};

    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const ThemeInfo &a, const ThemeInfo &b) {
                         const int ka = (a.hasQrcNode ? 1 : 0) * 100000 + a.entryCount;
                         const int kb = (b.hasQrcNode ? 1 : 0) * 100000 + b.entryCount;
                         return ka > kb;
                     });
    return candidates.first();
}

ThemeInfo ThemeEngine::findThemePlain(const QByteArray &data)
{
    static const QStringList anchors = {
        QStringLiteral("property color primary100"), QStringLiteral("primary100"),
        QStringLiteral("property color primary90"), QStringLiteral("primary90"),
    };
    static const QRegularExpression colorRe(
        QStringLiteral("primary100[ \t]*:[ \t]*\"?#[0-9A-Fa-f]{6,8}"),
        QRegularExpression::MultilineOption);

    for (const QString &anchor : anchors) {
        QByteArray needle = utf16le(anchor);
        int mult = 2;
        QString enc = QStringLiteral("utf-16-le");
        qint64 p = data.indexOf(needle);
        if (p < 0) {
            needle = anchor.toLatin1();
            p = data.indexOf(needle);
            if (p < 0)
                continue;
            mult = 1;
            enc = QStringLiteral("utf-8");
        }

        qint64 winStart = p - 4096 * mult;
        if (winStart < 0)
            winStart = 0;
        if ((p - winStart) % mult != 0)
            winStart += 1;
        qint64 winLen = qMin<qint64>(200000, data.size() - winStart);
        if (mult == 2)
            winLen -= winLen % 2;
        if (winLen < 2000)
            continue;

        const QByteArray win = data.mid(winStart, winLen);
        QString qml;
        if (mult == 2) {
            qml = QString::fromUtf16(reinterpret_cast<const char16_t *>(win.constData()),
                                     int(win.size() / 2));
        } else {
            qml = QString::fromLatin1(win); // lossless byte<->char mapping
        }

        if (colorRe.match(qml).hasMatch()) {
            ThemeInfo info;
            info.found = true;
            info.mode = QStringLiteral("plain");
            info.enc = enc;
            info.mult = mult;
            info.winStart = winStart;
            info.winLen = int(winLen);
            info.qml = qml;
            return info;
        }
    }
    return {};
}

ThemeInfo ThemeEngine::findThemeHex(const QByteArray &data, const QString &profile)
{
    auto countMatches = [&data](const QString &prof) {
        int n = 0;
        const auto targets = hexTargets(prof);
        for (auto it = targets.constBegin(); it != targets.constEnd(); ++it) {
            const auto pats = oldPatterns(it.value().first, it.value().second);
            for (const auto &pat : pats) {
                if (data.contains(pat.first)) {
                    ++n;
                    break;
                }
            }
        }
        return n;
    };

    QString prof = profile;
    int count = 0;
    if (prof.isEmpty()) {
        const int m = countMatches(QStringLiteral("MSI5"));
        const int n = countMatches(QStringLiteral("NXT"));
        prof = (m >= n) ? QStringLiteral("MSI5") : QStringLiteral("NXT");
        count = qMax(m, n);
    } else {
        count = countMatches(prof);
    }

    ThemeInfo info;
    info.found = true; // like the Python tool, hex always "finds" a profile
    info.mode = QStringLiteral("hex");
    info.profile = prof;
    info.hexMatchCount = count;
    return info;
}

ThemeInfo ThemeEngine::detectTheme(const QByteArray &data, const QString &method,
                                   const QString &hexProfile)
{
    if (method == QLatin1String("auto") || method == QLatin1String("qml")) {
        ThemeInfo t = findThemeZlib(data);
        if (!t.found) {
            t = findThemePlain(data);
            if (t.found && parseQmlEntries(t.qml).isEmpty())
                t = {};
        }
        if (t.found)
            return t;
    }
    if (method == QLatin1String("auto") || method == QLatin1String("hex")) {
        ThemeInfo t = findThemeHex(data, hexProfile);
        if (t.found)
            return t;
    }
    return {};
}

// ---------------------------------------------------------------------------
// QML parse / build
// ---------------------------------------------------------------------------

QList<QmlEntry> ThemeEngine::parseQmlEntries(const QString &q)
{
    QList<QmlEntry> entries;
    QSet<int> used;

    static const QRegularExpression rx1(
        QStringLiteral("^[ \t]*(?:readonly[ \t]+)?property[ \t]+(?:color|var)[ \t]+"
                       "([A-Za-z_][A-Za-z0-9_]*)[ \t]*:[ \t]*([^\r\n]*)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression rx2(
        QStringLiteral("^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*:[ \t]*"
                       "(\"(?:#[0-9A-Fa-f]{6,8})\"|#[0-9A-Fa-f]{6,8})"),
        QRegularExpression::MultilineOption);

    auto it1 = rx1.globalMatch(q);
    while (it1.hasNext()) {
        const auto m = it1.next();
        const QString name = m.captured(1);
        if (name.isEmpty())
            continue;
        const ColorToken tk = splitColorToken(m.captured(2), int(m.capturedStart(2)));
        if (!tk.ok || used.contains(tk.start))
            continue;
        used.insert(tk.start);
        QmlEntry e;
        e.name = name;
        e.value = tk.val;
        e.original = tk.val;
        e.quoted = tk.quoted;
        e.exprStart = tk.start;
        e.exprLen = tk.len;
        entries << e;
    }

    auto it2 = rx2.globalMatch(q);
    while (it2.hasNext()) {
        const auto m = it2.next();
        const QString name = m.captured(1);
        if (name.isEmpty())
            continue;
        const QString tokstr = m.captured(2);
        const int start = int(m.capturedStart(2));
        if (used.contains(start))
            continue;
        used.insert(start);
        QmlEntry e;
        e.name = name;
        QString val = tokstr;
        e.quoted = tokstr.startsWith(QLatin1Char('"'));
        val.remove(QLatin1Char('"'));
        e.value = val;
        e.original = val;
        e.exprStart = start;
        e.exprLen = tokstr.size();
        entries << e;
    }
    return entries;
}

QString ThemeEngine::buildQml(const QString &qml, const QList<QmlEntry> &entries,
                              const QMap<QString, QString> &edits)
{
    static const QRegularExpression re6(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    static const QRegularExpression re8(QStringLiteral("^#[0-9A-Fa-f]{8}$"));
    static const QRegularExpression reAlpha(QStringLiteral("^#([0-9A-Fa-f]{2})[0-9A-Fa-f]{6}$"));

    struct Change { int start; int len; QString val; };
    QList<Change> changes;

    for (const QmlEntry &e : entries) {
        if (!edits.contains(e.name))
            continue;
        QString v = edits.value(e.name).trimmed();
        if (v.isEmpty() || v == e.value)
            continue;

        if (re6.match(v).hasMatch()) {
            const auto m = reAlpha.match(e.original);
            if (m.hasMatch())
                v = QLatin1Char('#') + m.captured(1) + v.mid(1); // keep 8-digit alpha
            if (e.quoted)
                v = QLatin1Char('"') + v + QLatin1Char('"');
        } else if (re8.match(v).hasMatch()) {
            if (e.quoted)
                v = QLatin1Char('"') + v + QLatin1Char('"');
        }
        if (v.size() > e.exprLen)
            continue; // never grow the slot
        changes << Change { e.exprStart, e.exprLen, v };
    }

    if (changes.isEmpty())
        return qml;

    std::sort(changes.begin(), changes.end(),
              [](const Change &a, const Change &b) { return a.start > b.start; });

    QString out = qml;
    for (const Change &c : changes) {
        if (c.start < 0 || c.start + c.len > out.size())
            continue;
        QString padded = c.val + QString(c.len - c.val.size(), QLatin1Char(' '));
        out.replace(c.start, c.len, padded);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Patching — QML (zlib / plain)
// ---------------------------------------------------------------------------

ThemeEngine::PatchResult ThemeEngine::patchTheme(const QString &exePath, const ThemeInfo &info,
                                                 const QList<QmlEntry> &entries,
                                                 const QMap<QString, QString> &edits,
                                                 Logger logger)
{
    if (!info.found)
        return { false, QStringLiteral("Load the theme first.") };

    const QString q = buildQml(info.qml, entries, edits);
    if (q == info.qml)
        return { false, QStringLiteral("Nothing to patch - set some colors first.") };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot open %1 (run as administrator?)").arg(exePath) };
    QByteArray data = file.readAll();
    file.close();

    // ---------------- plain ----------------
    if (info.mode == QLatin1String("plain")) {
        const QString old = info.qml;
        const int mult = info.mult;
        const qint64 ws = info.winStart;
        const int n = int(qMin<qint64>(old.size(), q.size()));
        int diff = 0;
        for (int ci = 0; ci < n; ++ci) {
            if (old.at(ci) == q.at(ci))
                continue;
            const qint64 boff = ws + qint64(ci) * mult;
            if (boff < 0 || boff + mult > data.size())
                continue;
            const char16_t ch = q.at(ci).unicode();
            data[int(boff)] = char(ch & 0xFF);
            if (mult == 2)
                data[int(boff) + 1] = char((ch >> 8) & 0xFF);
            ++diff;
        }
        if (diff == 0)
            return { false, QStringLiteral("Nothing to patch - set some colors first.") };

        if (!backup(exePath, logger))
            return { false, QStringLiteral("Could not create backup.") };
        QFile out(exePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return { false, QStringLiteral("Write error (run as administrator?).") };
        out.write(data);
        out.close();
        return { true, QStringLiteral("Patch complete. Full theme applied.") };
    }

    if (info.mode != QLatin1String("zlib"))
        return { false, QStringLiteral("Unsupported theme mode: %1").arg(info.mode) };

    // ---------------- zlib ----------------
    const QByteArray qbytes = q.toLatin1();
    const QByteArray comp = ZlibUtils::compress(qbytes);
    if (comp.isEmpty())
        return { false, QStringLiteral("Compression failed.") };

    const qint64 zoff = info.zoff;
    quint32 oldCompLen = info.compSize;
    if (oldCompLen == 0) {
        QByteArray tmp;
        if (decompressExactAt(data, zoff, qint64(info.storedSize) - 4, info.uncompSize, &tmp))
            oldCompLen = info.storedSize - 4;
        else
            oldCompLen = info.storedSize;
    }

    const qint64 oldEnd = zoff + oldCompLen;
    qint64 zeroSlack = 0;
    while (oldEnd + zeroSlack < data.size() && data[int(oldEnd + zeroSlack)] == '\0'
           && zeroSlack < 1024 * 1024)
        ++zeroSlack;
    const qint64 compCapacity = oldCompLen + zeroSlack;

    bool relocated = false;

    if (comp.size() <= compCapacity) {
        // ---- in place ----
        const quint32 storedNew = info.sizeIncludesUSize ? quint32(comp.size()) + 4
                                                         : quint32(comp.size());
        putBe32(data, info.entryOff, storedNew);
        putBe32(data, info.entryOff + 4, quint32(qbytes.size()));
        data.replace(int(zoff), comp.size(), comp);
        const qint64 padStart = zoff + comp.size();
        const qint64 padEnd = zoff + compCapacity;
        if (padEnd > padStart)
            data.replace(int(padStart), int(padEnd - padStart),
                         QByteArray(int(padEnd - padStart), '\0'));

        QByteArray test;
        if (!decompressExactAt(data, zoff, comp.size(), quint32(qbytes.size()), &test)
            || test != qbytes)
            return { false, QStringLiteral("Patched theme verify failed before write.") };
    } else {
        // ---- relocation into a zero cave ----
        log(logger,
            QStringLiteral("Compressed QML is %1 bytes; old slot capacity is %2 bytes. "
                           "Trying relocation...")
                .arg(comp.size())
                .arg(compCapacity),
            QStringLiteral("orange"));

        QrcNode node;
        if (info.hasQrcNode) {
            node.found = true;
            node.nodeOff = info.qrcNodeOff;
            node.dataBaseRva = info.qrcDataBaseRva;
            node.dataOffset = info.qrcDataOffset;
        } else {
            node = findQrcNodeForEntry(data, info.entryOff);
        }
        if (!node.found)
            return { false, QStringLiteral("No room, and UiThemeObject.qml qrc node "
                                           "was not found for relocation.") };

        const QList<PeSection> secs = PeUtils::sections(data);
        if (secs.isEmpty())
            return { false, QStringLiteral("No room, and PE section parsing failed.") };

        const qint64 oldEntryRva = PeUtils::rawToRva(secs, info.entryOff);
        if (oldEntryRva < 0)
            return { false, QStringLiteral("No room, and old qrc data is not in a "
                                           "mapped PE section.") };

        qint64 dataBaseRva = node.dataBaseRva;
        if (dataBaseRva < 0)
            dataBaseRva = oldEntryRva - node.dataOffset;

        const qint64 newRecordLen = 8 + comp.size();
        const qint64 cave = findZeroCave(data, secs, info.entryOff, dataBaseRva, newRecordLen);
        if (cave < 0)
            return { false, QStringLiteral("No room for this custom theme in this EXE "
                                           "(no mapped zero cave large enough).") };

        const qint64 newEntryRva = PeUtils::rawToRva(secs, cave);
        if (newEntryRva < 0)
            return { false, QStringLiteral("Relocation failed: chosen cave is not mapped.") };
        const qint64 newDataOff = newEntryRva - dataBaseRva;
        if (newDataOff < 0 || newDataOff > 0xFFFFFFFF)
            return { false, QStringLiteral("Relocation failed: new qrc data offset "
                                           "is out of range.") };

        putBe32(data, cave, quint32(comp.size()) + 4);
        putBe32(data, cave + 4, quint32(qbytes.size()));
        data.replace(int(cave + 8), comp.size(), comp);
        putBe32(data, node.nodeOff + 10, quint32(newDataOff));

        // Invalidate the orphaned old record (header + zlib stream) so
        // re-detection and future patches follow the qrc link to the cave.
        for (qint64 z = info.entryOff; z < qMin<qint64>(oldEnd, data.size()); ++z)
            data[int(z)] = '\0';

        QByteArray test;
        if (!decompressExactAt(data, cave + 8, comp.size(), quint32(qbytes.size()), &test)
            || test != qbytes)
            return { false, QStringLiteral("Relocation verify failed before write.") };

        log(logger,
            QStringLiteral("Relocated QML theme to file offset 0x%1 (%2 bytes compressed).")
                .arg(cave, 0, 16)
                .arg(comp.size()),
            QStringLiteral("blue"));
        relocated = true;
    }

    // Verify the active qrc-linked UiThemeObject.qml now yields the new theme.
    const ThemeInfo vt = findThemeZlib(data);
    if (vt.found && vt.hasQrcNode && vt.qml != q)
        return { false, QStringLiteral("Patched theme verify failed. File was not written.") };

    if (!backup(exePath, logger))
        return { false, QStringLiteral("Could not create backup.") };
    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?).") };
    out.write(data);
    out.close();

    return relocated ? PatchResult { true, QStringLiteral("Patch complete. Full theme relocated and applied.") }
                     : PatchResult { true, QStringLiteral("Patch complete. Full theme applied.") };
}

// ---------------------------------------------------------------------------
// Patching — hex (old builds + QML color names in EXE strings)
// ---------------------------------------------------------------------------

QList<QPair<QByteArray, QString>> ThemeEngine::oldPatterns(const QString &hexValue,
                                                           const QString &mode)
{
    QList<QPair<QByteArray, QString>> patterns;
    QSet<QByteArray> seen;

    auto add = [&](const QString &v, const QByteArray &p, const QString &label) {
        Q_UNUSED(v)
        if (!seen.contains(p)) {
            seen.insert(p);
            patterns << qMakePair(p, label);
        }
    };

    const QStringList variants = { hexValue, hexValue.toLower(), hexValue.toUpper() };
    if (mode == QLatin1String("ascii") || mode == QLatin1String("both")) {
        for (const QString &v : variants)
            add(v, v.toLatin1(), QStringLiteral("ascii"));
    }
    if (mode == QLatin1String("utf16") || mode == QLatin1String("both")) {
        for (const QString &v : variants)
            add(v, utf16le(v), QStringLiteral("utf-16le"));
    }
    return patterns;
}

namespace {

void collectOccurrences(const QByteArray &data, const QByteArray &ov, const QString &encMode,
                        const QString &newHex, QList<ThemeEngine::Replacement> &out)
{
    const QByteArray newEnc =
        (encMode == QLatin1String("ascii")) ? newHex.toLatin1() : utf16le(newHex);
    qsizetype idx = 0;
    while (true) {
        idx = data.indexOf(ov, idx);
        if (idx < 0)
            break;
        QByteArray rep = newEnc;
        if (rep.size() < ov.size())
            rep += QByteArray(ov.size() - rep.size(), '\0');
        else if (rep.size() > ov.size())
            rep = rep.left(ov.size());
        out << ThemeEngine::Replacement { idx, ov.size(), rep };
        idx += ov.size();
    }
}

QStringList nameVariants(const QString &name)
{
    const QString lo = name.toLower();
    return (name == lo) ? QStringList { name } : QStringList { name, lo };
}

bool qmlNameEndOk(const QByteArray &data, qint64 end, const QString &enc)
{
    QChar ch;
    if (enc == QLatin1String("utf-16-le")) {
        if (end + 2 > data.size())
            return true;
        ch = QChar(uchar(data[int(end)]) | uchar(data[int(end) + 1]) << 8);
    } else {
        if (end >= data.size())
            return true;
        ch = QChar(uchar(data[int(end)]));
    }
    return !(ch.isLetterOrNumber() || ch == QLatin1Char('_'));
}

QList<QPair<qint64, QString>> iterQmlNames(const QByteArray &data, const QString &name)
{
    QList<QPair<qint64, QString>> out;
    for (const QString &enc : { QStringLiteral("utf-16-le"), QStringLiteral("ascii") }) {
        for (const QString &n : nameVariants(name)) {
            const QByteArray pat = (enc == QLatin1String("utf-16-le")) ? utf16le(n) : n.toLatin1();
            qsizetype pos = 0;
            while (true) {
                pos = data.indexOf(pat, pos);
                if (pos < 0)
                    break;
                if (qmlNameEndOk(data, pos + pat.size(), enc))
                    out << qMakePair(qint64(pos), enc);
                pos += pat.size();
            }
        }
    }
    return out;
}

// "#RRGGBB"/"#AARRGGBB" right after a color name (ascii or utf-16le).
bool findHexAfter(const QByteArray &data, qint64 namePos, qint64 nameLen, const QString &enc,
                  qint64 *hexOff, int *hexLen, QByteArray *hexRaw)
{
    const qint64 tailStart = namePos + nameLen;
    const QByteArray tail = data.mid(tailStart, qMin<qint64>(56, data.size() - tailStart));

    if (enc == QLatin1String("ascii")) {
        static const QRegularExpression re(QStringLiteral("#[0-9A-Fa-f]{6,8}"));
        const auto m = re.match(QString::fromLatin1(tail));
        if (!m.hasMatch())
            return false;
        *hexOff = tailStart + m.capturedStart();
        *hexLen = m.capturedLength();
        *hexRaw = m.captured().toLatin1();
        return true;
    }

    // utf-16le: "#\0" then 6-8 hex chars each followed by \0
    for (int need = 8; need >= 6; --need) {
        const int byteLen = 2 + need * 2;
        if (tail.size() < byteLen)
            continue;
        for (int i = 0; i + byteLen <= tail.size(); ++i) {
            if (tail[i] != '#' || tail[i + 1] != '\0')
                continue;
            bool ok = true;
            for (int k = 0; k < need; ++k) {
                const char c = tail[i + 2 + k * 2];
                const char z = tail[i + 3 + k * 2];
                const bool hexChar = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
                                     || (c >= 'A' && c <= 'F');
                if (!hexChar || z != '\0') {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                *hexOff = tailStart + i;
                *hexLen = byteLen;
                *hexRaw = tail.mid(i, byteLen);
                return true;
            }
        }
    }
    return false;
}

} // namespace

QString ThemeEngine::findQmlColor(const QByteArray &data, const QString &name)
{
    const QByteArray nameU16 = utf16le(name);
    const QByteArray nameAsc = name.toLatin1();
    for (const auto &occ : iterQmlNames(data, name)) {
        const qint64 nameLen = (occ.second == QLatin1String("utf-16-le")) ? nameU16.size()
                                                                          : nameAsc.size();
        qint64 hexOff;
        int hexLen;
        QByteArray raw;
        if (!findHexAfter(data, occ.first, nameLen, occ.second, &hexOff, &hexLen, &raw))
            continue;
        if (occ.second == QLatin1String("ascii"))
            return QString::fromLatin1(raw).toUpper();
        QByteArray compact;
        for (int i = 0; i + 1 < raw.size(); i += 2)
            compact += raw[i];
        return QString::fromLatin1(compact).toUpper();
    }
    return {};
}

QList<ThemeEngine::Replacement> ThemeEngine::collectThemeReplacements(
    const QByteArray &data, const QString &profile, const QList<HexColorItem> &colors,
    const QList<HexColorItem> &qmlColors)
{
    static const QRegularExpression re6(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    static const QRegularExpression re68(QStringLiteral("^#[0-9A-Fa-f]{6,8}$"));

    QList<Replacement> repls;
    const auto targets = hexTargets(profile);

    // ---- hex targets ----
    for (const HexColorItem &item : colors) {
        if (!item.enabled)
            continue;
        const QString oldHex = item.currentHex;
        const QString newHex = item.color;
        if (newHex.isEmpty() || !re6.match(newHex).hasMatch())
            continue;
        if (oldHex.compare(newHex, Qt::CaseInsensitive) == 0)
            continue;

        if (profile == QLatin1String("MSI5") && item.name == QLatin1String("Sidebar (#000000)")) {
            // Locate the sidebar #000000 RELATIVE to the Settings Window color.
            const QString settingsHex = targets.value(QStringLiteral("Settings Window")).first;
            bool done = false;
            for (const QString &enc : { QStringLiteral("utf-16-le"), QStringLiteral("ascii") }) {
                const QByteArray settingsPat =
                    (enc == QLatin1String("utf-16-le")) ? utf16le(settingsHex)
                                                        : settingsHex.toLatin1();
                const qsizetype swPos = data.indexOf(settingsPat);
                if (swPos < 0)
                    continue;
                const QByteArray oldPat =
                    (enc == QLatin1String("utf-16-le")) ? utf16le(oldHex) : oldHex.toLatin1();
                const qsizetype sidePos = data.indexOf(oldPat, swPos + settingsPat.size());
                if (sidePos >= 0) {
                    QByteArray rep =
                        (enc == QLatin1String("utf-16-le")) ? utf16le(newHex) : newHex.toLatin1();
                    if (rep.size() < oldPat.size())
                        rep += QByteArray(oldPat.size() - rep.size(), '\0');
                    else if (rep.size() > oldPat.size())
                        rep = rep.left(oldPat.size());
                    repls << Replacement { sidePos, oldPat.size(), rep };
                    done = true;
                    break;
                }
            }
            Q_UNUSED(done)
            continue;
        }

        for (const auto &pat : oldPatterns(oldHex, QStringLiteral("both")))
            collectOccurrences(data, pat.first, pat.second, newHex, repls);
    }

    // ---- QML color names stored as EXE strings ----
    for (const HexColorItem &item : qmlColors) {
        if (!item.enabled)
            continue;
        const QString newHex = item.color.trimmed().toUpper();
        if (!re68.match(newHex).hasMatch())
            continue;

        const QString newRgb6 = newHex.right(6);
        QString newAlpha; // "" unless caller supplied 8 digits
        if (newHex.size() == 9)
            newAlpha = newHex.mid(1, 2);

        const QByteArray nameU16 = utf16le(item.name);
        const QByteArray nameAsc = item.name.toLatin1();

        for (const auto &occ : iterQmlNames(data, item.name)) {
            const bool u16 = (occ.second == QLatin1String("utf-16-le"));
            const qint64 nameLen = u16 ? nameU16.size() : nameAsc.size();
            qint64 hexOff;
            int hexLen;
            QByteArray raw;
            if (!findHexAfter(data, occ.first, nameLen, occ.second, &hexOff, &hexLen, &raw))
                continue;

            QByteArray rep;
            if (!u16) {
                if (hexLen == 9) { // "#AARRGGBB" slot
                    QByteArray alpha = newAlpha.isEmpty() ? raw.left(3)
                                                          : ("#" + newAlpha).toLatin1();
                    rep = alpha + newRgb6.toLatin1();
                } else {
                    rep = ("#" + newRgb6).toLatin1();
                }
            } else {
                if (hexLen == 18) { // utf-16 "#AARRGGBB"
                    QByteArray alpha = newAlpha.isEmpty() ? raw.left(6)
                                                          : utf16le("#" + newAlpha);
                    rep = alpha + utf16le(newRgb6);
                } else {
                    rep = utf16le("#" + newRgb6);
                }
            }
            repls << Replacement { hexOff, hexLen, rep };
        }
    }

    std::sort(repls.begin(), repls.end(),
              [](const Replacement &a, const Replacement &b) { return a.pos > b.pos; });
    return repls;
}

ThemeEngine::PatchResult ThemeEngine::applyHexTheme(const QString &exePath, const QString &profile,
                                                    const QList<HexColorItem> &colors,
                                                    const QList<HexColorItem> &qmlColors,
                                                    Logger logger)
{
    if (!QFileInfo::exists(exePath))
        return { false, QStringLiteral("EXE not found") };
    if (!backup(exePath, logger))
        return { false, QStringLiteral("Could not create backup") };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Read error (run as administrator?)") };
    QByteArray data = file.readAll();
    file.close();

    const QList<Replacement> repls = collectThemeReplacements(data, profile, colors, qmlColors);
    if (repls.isEmpty())
        return { false, QStringLiteral("No changes selected") };

    for (const Replacement &r : repls) {
        if (r.pos >= 0 && r.pos + r.len <= data.size())
            data.replace(int(r.pos), r.len, r.rep);
    }

    QFile out(exePath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?)") };
    out.write(data);
    out.close();

    return { true, QStringLiteral("All selected colors applied (%1 replacements)")
                       .arg(repls.size()) };
}

// ---------------------------------------------------------------------------
// Backup
// ---------------------------------------------------------------------------

bool ThemeEngine::backup(const QString &path, Logger logger)
{
    const QString bak = path + QStringLiteral(".bak");
    if (QFileInfo::exists(bak)) {
        log(logger, QStringLiteral("Backup already exists, reusing it."), QStringLiteral("gray"));
        return true;
    }
    if (QFile::copy(path, bak)) {
        log(logger, QStringLiteral("Backup created: %1").arg(bak), QStringLiteral("green"));
        return true;
    }
    log(logger, QStringLiteral("Backup failed: %1").arg(bak), QStringLiteral("red"));
    return false;
}
