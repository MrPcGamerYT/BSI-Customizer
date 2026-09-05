#include "ThemeEngine.h"

#include "ByteUtils.h"
#include "BsDetector.h"
#include "PatchFile.h"
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
    if (t.startsWith(QLatin1Char('"')) || t.startsWith(QLatin1Char('\''))) {
        const QChar quote = t.at(0);
        // Only a complete literal is quoted. A ternary/expression beginning
        // with a string must be replaced as a whole, not just its first token.
        if (t.size() >= 2 && t.endsWith(quote)) {
            tk.val = t.mid(1, t.size() - 2);
            tk.len = t.size();
            tk.quoted = true;
            tk.ok = true;
            return tk;
        }
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

// Keep offsets identical while masking comments and strings for declaration
// matching. A commented-out palette must never become an editable property.
QString qmlCodeMask(const QString &qml)
{
    QString mask = qml;
    QChar quote;
    bool lineComment = false, blockComment = false, escape = false;
    for (int i = 0; i < qml.size(); ++i) {
        const QChar c = qml.at(i);
        const QChar next = i + 1 < qml.size() ? qml.at(i + 1) : QChar();
        if (lineComment) {
            if (c == QLatin1Char('\n'))
                lineComment = false;
            else
                mask[i] = QLatin1Char(' ');
        } else if (blockComment) {
            mask[i] = c == QLatin1Char('\n') ? c : QLatin1Char(' ');
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                mask[++i] = QLatin1Char(' ');
                blockComment = false;
            }
        } else if (!quote.isNull()) {
            mask[i] = c == QLatin1Char('\n') ? c : QLatin1Char(' ');
            if (escape)
                escape = false;
            else if (c == QLatin1Char('\\'))
                escape = true;
            else if (c == quote)
                quote = QChar();
        } else if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            mask[i] = QLatin1Char(' ');
            mask[++i] = QLatin1Char(' ');
            lineComment = true;
        } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            mask[i] = QLatin1Char(' ');
            mask[++i] = QLatin1Char(' ');
            blockComment = true;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
            mask[i] = QLatin1Char(' ');
        }
    }
    return mask;
}

int qmlExpressionEnd(const QString &qml, int start)
{
    QChar quote;
    bool escape = false;
    int depth = 0;
    for (int i = start; i < qml.size(); ++i) {
        const QChar c = qml.at(i);
        if (!quote.isNull()) {
            if (escape)
                escape = false;
            else if (c == QLatin1Char('\\'))
                escape = true;
            else if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            quote = c;
        } else if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) {
            if (depth == 0)
                return i;
            --depth;
        } else if (depth == 0 && (c == QLatin1Char(';') || c == QLatin1Char('\r')
                                 || c == QLatin1Char('\n')
                                 || (c == QLatin1Char('/') && i + 1 < qml.size()
                                     && qml.at(i + 1) == QLatin1Char('/')))) {
            return i;
        }
    }
    return qml.size();
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

// Over-read decompression: inflate the stream at zoff without knowing its
// exact compressed length.  A generous slice is handed to zlib, which stops at
// the stream end and leaves the trailing bytes unconsumed.  This is the
// strategy the reference tool uses (its trySize sweep from 1 KB to 200 KB), and
// it is what finds a theme whose outer length field is missing, padded or
// otherwise not exactly compLen / compLen+4.
//
// The slice grows in steps rather than starting at the maximum: embedded QML
// records are a few KB compressed, so the first step normally succeeds and the
// caller never pays for a 256 KB copy.  That matters because a multi-megabyte
// executable contains tens of thousands of coincidental 78 9C / 78 DA pairs.
bool inflateOverread(const QByteArray &data, qint64 zoff, quint32 usize,
                     qint64 maxLen, QByteArray *out)
{
    if (zoff < 0 || zoff + 2 > data.size())
        return false;
    const qint64 avail = data.size() - zoff;
    const qint64 cap = (maxLen > 0) ? qMin<qint64>(avail, maxLen) : avail;
    if (cap < 2)
        return false;

    static const qint64 steps[] = { 2048, 8192, 32768, 131072, 524288 };
    qint64 tried = 0;
    for (const qint64 step : steps) {
        const qint64 len = qMin<qint64>(step, cap);
        if (len <= tried)
            continue;
        tried = len;
        const QByteArray res = ZlibUtils::inflateLoose(data.mid(zoff, int(len)),
                                                       usize ? qint64(usize) : -1);
        if (!res.isEmpty() && (!usize || quint32(res.size()) == usize)) {
            if (out)
                *out = res;
            return true;
        }
        if (len >= cap)
            break;
    }
    return false;
}

// Exact compressed length of the stream at zoff, recovered by binary search.
//
// Detection can succeed without knowing the length (see inflateOverread), but
// patching cannot: the slot size decides whether the recompressed theme fits in
// place or has to be relocated.  "The first L bytes inflate to the full output"
// is monotonic in L, so ~17 probes pin the length exactly - far tighter than the
// reference tool's 1024-byte granularity, which would misreport the slot size
// and break the in-place/relocate decision.
//
// Returns 0 if the length could not be determined.
quint32 measureCompLen(const QByteArray &data, qint64 zoff, quint32 usize, qint64 maxLen)
{
    if (zoff < 0 || zoff + 2 > data.size())
        return 0;
    const qint64 avail = data.size() - zoff;
    qint64 hi = (maxLen > 0) ? qMin<qint64>(avail, maxLen) : avail;
    QByteArray probe;
    if (!inflateOverread(data, zoff, usize, hi, &probe))
        return 0;

    qint64 lo = 2;
    // Narrow to the smallest length that still yields the complete stream.
    while (lo < hi) {
        const qint64 mid = lo + (hi - lo) / 2;
        QByteArray tmp;
        if (inflateOverread(data, zoff, usize, mid, &tmp) && tmp == probe)
            hi = mid;
        else
            lo = mid + 1;
    }
    // Sanity: the recovered length must reproduce the payload exactly.
    QByteArray check;
    if (!inflateOverread(data, zoff, usize, hi, &check) || check != probe)
        return 0;
    return quint32(hi);
}

// BlueStacks' stock theme colors, recovered from BS_THEME_CHANGER's own data
// table (the DEFAULT_COLORS list it falls back on when parsing finds nothing).
// These are the values a never-modified HD-Player.exe actually contains, and
// they are distinctive enough - #080A1F, #1B1E38, #1A90FF - that a handful of
// them in one decompressed blob is strong evidence the blob is the theme.
//
// The generic end of the stock table (#000000, #FFFFFF, mid grays) is left out
// on purpose: those occur in unrelated SVG and QML payloads and would only add
// noise to the count.
static const char *const kStockPalette[] = {
    "#080a1f", "#0b0c0e", "#1b1e38", "#17181c", "#232642", "#21242c",
    "#2b2e4c", "#2e3038", "#131621", "#555e6b", "#454954", "#373a58",
    "#5c6170", "#434664", "#8f94a3", "#565977", "#abafba", "#7f82a0",
    "#c7c9d1", "#b6b8cd", "#f0f1f4", "#e3e4e8", "#f7faff",
    "#1a90ff", "#2ea4ff", "#057ceb",
    "#eb2c43", "#ed1c24", "#ff333b", "#c61017", "#eb553e", "#ff6952", "#d7412a",
    "#7ac143", "#89d94c", "#67bd24",
};

// Number of stock colors present in a decompressed blob.  Hex digits are
// case-insensitive in QML, so both sides are folded to lower case.
int stockPaletteHits(const QByteArray &qml)
{
    const QByteArray lower = qml.toLower();
    int hits = 0;
    for (const char *hex : kStockPalette) {
        if (lower.contains(QLatin1String(hex)))
            ++hits;
    }
    return hits;
}

// Offsets of the zlib streams that sit within 8192 bytes of a
// "UiThemeObject.qml" marker - the reference tool's primary detection strategy.
//
// The marker is searched in every encoding it can appear in: ASCII (a plain
// string constant or a QML source reference), UTF-16BE (how a Qt resource name
// record stores it) and UTF-16LE (a Qt QString literal in .rdata).  Anchoring on
// the name is what keeps the scan out of the tens of thousands of coincidental
// 78 9C / 78 DA byte pairs in a 2 MB executable.
//
// Both directions are scanned.  The reference tool only looks forward, but rcc
// normally emits qt_resource_name AFTER qt_resource_data, so in the common
// layout the payload sits BEFORE its own name record and a forward-only window
// misses it.  Covering both sides also happens to separate a live record from an
// orphan: only the one near the name array gets the anchoring bonus.
QList<qint64> markerAnchoredMagics(const QByteArray &data,
                                   const QString &name = QStringLiteral("UiThemeObject.qml"))
{
    const QList<QByteArray> needles = { name.toLatin1(), utf16be(name), utf16le(name) };

    QList<qint64> out;
    QSet<qint64> seen;
    auto collect = [&data, &out, &seen](qint64 from, qint64 to) {
        from = qMax<qint64>(0, from);
        to = qMin<qint64>(data.size() - 2, to);
        for (qint64 p = from; p < to; ++p) {
            if (seen.contains(p))
                continue;
            if (data[int(p)] != '\x78')
                continue;
            const char c = data[int(p) + 1];
            if (c == '\xDA' || c == char('\x9C') || c == '\x01') {
                seen.insert(p);
                out << p;
            }
        }
    };

    for (const QByteArray &needle : needles) {
        qsizetype at = 0;
        while (true) {
            at = data.indexOf(needle, at);
            if (at < 0)
                break;
            collect(at + needle.size(), at + needle.size() + 8192); // after
            collect(at - 8192, at);                                 // before
            at += 1;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

using QrcNode = QtResources::Link;

QrcNode findQrcNodeForEntry(const QByteArray &data, qint64 entryOff,
                            const QString &name = QStringLiteral("UiThemeObject.qml"))
{
    return QtResources::findLink(data, entryOff, name);
}

qint64 zeroSlackAfter(const QByteArray &data, const ThemeInfo &info)
{
    const qint64 end = info.zoff + info.compSize;
    qint64 limit = qMin<qint64>(data.size(), end + 1024 * 1024);
    const auto secs = PeUtils::sections(data);
    if (const auto *section = PeUtils::sectionForRaw(secs, info.entryOff))
        limit = qMin(limit, qint64(section->rawPtr) + section->rawSize);
    for (const auto &range : info.resource.reserved) {
        if (range.first >= end)
            limit = qMin(limit, range.first);
    }
    qint64 slack = 0;
    while (end + slack < limit && data[int(end + slack)] == '\0')
        ++slack;
    return slack;
}

qint64 findZeroCave(const QByteArray &data, const QList<PeSection> &secs,
                    qint64 oldEntryOff, qint64 dataBaseRva, qint64 needLen,
                    const QList<QPair<qint64, qint64>> &reserved)
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
                bool free = true;
                for (const auto &range : reserved) {
                    if (pos < range.second && pos + needLen > range.first) {
                        pos = qMax<qsizetype>(pos, range.second - 1);
                        free = false;
                    }
                }
                if (!free) {
                    pos = data.indexOf(probe, pos + 1);
                    continue;
                }
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

    // Marker-anchored hits first: the reference tool's primary strategy, and by
    // far the strongest signal in a multi-megabyte executable.  The global magic
    // scan follows so a build whose name record was stripped or renamed is still
    // found.
    QList<qint64> magics = markerAnchoredMagics(data);
    const int anchoredCount = magics.size();
    {
        QSet<qint64> have(magics.begin(), magics.end());
        for (const qint64 pos : ZlibUtils::findMagicOffsets(data)) {
            if (!have.contains(pos))
                magics << pos;
        }
    }
    const QSet<qint64> anchoredSet(magics.begin(), magics.begin() + anchoredCount);

    // Analyse the zlib stream that starts at `pos`; fills `info` and returns true
    // when it decompresses into something theme-like.
    //
    // Three header layouts are tried, most specific first:
    //   A: [be32 outerLen][be32 usize][zlib]   Qt resource record (8 bytes)
    //   B: [be32 usize][zlib]                  bare qCompress blob (4 bytes)
    //   C: [zlib]                              no header at all
    // Only A carries an outer length field that a patch may rewrite.  B and C
    // cannot validate a length up front, so they are used on marker-anchored
    // hits, or anywhere at all when `loose` is set - the second pass, which runs
    // only if the first one found nothing.
    auto analyse = [&data](qint64 pos, bool anchored, bool loose, ThemeInfo &info) -> bool {
        if (pos < 2)
            return false;

        const quint32 outer = (pos >= 8) ? be32(data, pos - 8) : 0;
        const quint32 u4 = (pos >= 4) ? be32(data, pos - 4) : 0;

        quint32 usize = 0, compLen = 0, storedSize = 0;
        int headerBytes = 0;
        bool includesUSize = true;
        QByteArray qbytes;
        bool ok = false;

        // ---- Layout A ----
        if (pos >= 8 && inRange(data, pos - 8, 8) && u4 >= 16 && u4 <= 50000000u) {
            // Qt qrc normally stores outerLen = compressed_len + 4; try both.
            if (decompressExactAt(data, pos, qint64(outer) - 4, u4, &qbytes)) {
                compLen = outer - 4;
                includesUSize = true;
                ok = true;
            } else if (decompressExactAt(data, pos, outer, u4, &qbytes)) {
                compLen = outer;
                includesUSize = false;
                ok = true;
            }
            if (ok) {
                usize = u4;
                storedSize = outer;
                headerBytes = 8;
            }
        }

        // Layout B is cheap to try anywhere: the 100..500000 window on the size
        // field rejects essentially every coincidental 78 xx pair before a single
        // byte is inflated.  Layout C has no such pre-filter - without a size
        // field there is nothing to validate against - so it is only tried where
        // the marker already says the theme lives.  Letting C run across every
        // magic offset in a multi-megabyte EXE would mean tens of thousands of
        // speculative inflates for a blob that is indistinguishable from noise.
        const bool tryBare = anchored || loose;

        // ---- Layout B: bare qCompress, no outer length field ----
        // The reference tool reads only this 4-byte size and never looks for an
        // outer length, which is why it finds records this scan used to skip.
        if (!ok && tryBare && pos >= 4 && u4 >= 100 && u4 <= 500000u
            && inflateOverread(data, pos, u4, 262144, &qbytes)) {
            const quint32 measured = measureCompLen(data, pos, u4, 262144);
            if (measured > 0) {
                usize = u4;
                compLen = measured;
                storedSize = measured + 4;
                includesUSize = true;
                headerBytes = 4;
                ok = true;
            }
        }

        // ---- Layout C: stream stands alone, size unknown ----
        if (!ok && anchored && inflateOverread(data, pos, 0, 262144, &qbytes)) {
            const quint32 measured = measureCompLen(data, pos, quint32(qbytes.size()), 262144);
            if (measured > 0) {
                usize = quint32(qbytes.size());
                compLen = measured;
                storedSize = 0;
                includesUSize = true;
                headerBytes = 0;
                ok = true;
            }
        }

        if (!ok || qbytes.size() < 100)
            return false;
        const QString qml = QString::fromLatin1(qbytes);

        // Three content tiers.  The strict one is what UiThemeObject.qml looks
        // like; the middle one is a compressed QML blob with several color
        // properties; the loosest accepts any blob that still yields at least
        // one parseable color entry, which is where the reference tool lands
        // when a build renames or reshapes the theme object.  Rejecting here is
        // what turns a working EXE into "Theme was not found in this file."
        const bool strictContent = qml.contains(QLatin1String("property QtObject colors"))
                                   && qml.contains(QLatin1String("primary100"));
        int colorProps = 0;
        for (qsizetype at = 0; (at = qml.indexOf(QLatin1String("property color"), at)) >= 0;)
            at += 14, ++colorProps;
        const int parsed = parseQmlEntries(qml).size();
        const bool looseContent = qml.contains(QLatin1String("property"))
                                  && qml.contains(QLatin1String("color"))
                                  && (colorProps >= 1 || parsed >= 1);
        // A blob carrying several of BlueStacks' own stock colors is the theme
        // even if its property names or object shape are unfamiliar - which is
        // exactly the case the name-based tiers above would otherwise reject.
        // Note looseContent insists on the literal words "property" AND "color",
        // so a reshaped table like  surfaceDeep: "#080A1F"  parses fine yet
        // fails that tier; the palette is what rescues it.
        //
        // Requiring at least one parsed entry matters: accepting an unparseable
        // blob would report "found" with nothing editable AND suppress the
        // fall-through to the plain/hex methods in detectTheme, which is a worse
        // outcome than not finding it here.
        const int stockHits = stockPaletteHits(qbytes);
        const bool stockContent = stockHits >= 3 && parsed >= 1;
        if (!strictContent && colorProps < 3 && !looseContent && !stockContent)
            return false;

        // Layout A took its length from the file, so trust it only as far as it
        // agrees with the stream it describes.  When those 4 bytes are padding or
        // unrelated data instead of a length field, demote the record - patching
        // must never rewrite bytes it does not own.
        if (headerBytes == 8) {
            const quint32 measured = measureCompLen(data, pos, usize, 262144);
            if (measured > 0 && measured != compLen) {
                if (storedSize == measured + 4 || storedSize == measured) {
                    includesUSize = (storedSize == measured + 4);
                } else {
                    headerBytes = (u4 >= 100 && u4 <= 500000u) ? 4 : 0;
                    storedSize = (headerBytes == 4) ? measured + 4 : 0;
                    includesUSize = true;
                }
                compLen = measured;
            }
        }

        info = ThemeInfo();
        info.found = true;
        info.mode = QStringLiteral("zlib");
        info.qml = qml;
        info.entryOff = pos - headerBytes;
        info.zoff = pos;
        info.storedSize = storedSize;
        info.compSize = compLen;
        info.uncompSize = usize;
        info.sizeIncludesUSize = includesUSize;
        info.headerBytes = headerBytes;
        info.markerAnchored = anchored;
        info.stockHits = stockHits;
        info.entryCount = parsed;
        info.contentQuality = strictContent ? 2 : 1;

        const QrcNode node = findQrcNodeForEntry(data, info.entryOff);
        info.resource = node;
        if (node.found && !node.ambiguous && headerBytes == 8) {
            // A record the qresource tree really links to UiThemeObject.qml IS
            // the theme, however odd its content looks.
            if (node.support > 0)
                info.contentQuality = 2;
            info.hasQrcNode = true;
            info.qrcStrict = node.strict;
            info.qrcSupport = node.support;
            info.qrcNodeOff = node.nodeOff;
            info.qrcNameRecordOff = node.nameRecordOff;
            info.qrcNameBaseRaw = node.nameBaseRaw;
            info.qrcDataBaseRaw = node.dataBaseRaw;
            info.qrcDataBaseRva = node.dataBaseRva;
            info.qrcDataOffset = node.dataOffset;
        }
        return true;
    };

    QSet<qint64> seen;
    for (const qint64 pos : magics) {
        if (seen.contains(pos))
            continue;
        seen.insert(pos);
        ThemeInfo info;
        if (analyse(pos, anchoredSet.contains(pos), false, info))
            candidates << info;
    }

    // Nothing yet: retry with the bare-qCompress layout allowed everywhere.  This
    // mirrors the reference tool's own fallback chain (marker first, then a raw
    // 78 9C scan over the whole file) for a build whose name marker is missing or
    // unusable.  It only runs when the first pass came up empty.
    if (candidates.isEmpty()) {
        seen.clear();
        for (const qint64 pos : magics) {
            if (seen.contains(pos))
                continue;
            seen.insert(pos);
            ThemeInfo info;
            if (analyse(pos, anchoredSet.contains(pos), true, info))
                candidates << info;
        }
    }

    if (candidates.isEmpty())
        return {};

    // More than one copy of the theme can live in a single EXE: an orphan left
    // behind by a relocation (ours or another tool's) still decompresses fine,
    // but only the record the qresource tree points at is what BlueStacks
    // actually loads.
    //
    // A validated resource tree takes priority over palette heuristics. Stock
    // colors in an orphan must not outrank a custom palette in the live tree.
    // Unresolved ties are marked ambiguous and are never patched by guessing.
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const ThemeInfo &a, const ThemeInfo &b) {
                         const auto rank = [](const ThemeInfo &t) -> qint64 {
                             return (t.hasQrcNode ? 1000000000000LL : 0)
                                    + qint64(t.qrcSupport) * 1000000000LL
                                    + qint64(t.contentQuality) * 10000000
                                    + (t.qrcStrict ? 200000 : (t.hasQrcNode ? 100000 : 0))
                                    // A hit inside the marker window is the same
                                    // evidence the reference tool relies on.
                                    + (t.markerAnchored ? 50000 : 0)
                                    // Stock colors are the reference tool's own
                                    // fallback signature; worth more than a
                                    // slightly longer entry list, less than the
                                    // marker window.
                                    + qint64(qMin(t.stockHits, 36)) * 500
                                    // A record with a real outer length field is
                                    // a genuine qrc payload; a measured headerless
                                    // blob is a fallback, so rank it lower.
                                    + (t.headerBytes == 8 ? 20000 : 0)
                                    + t.entryCount;
                         };
                         return rank(a) > rank(b);
                     });

    ThemeInfo best = candidates.first();
    // Only count records that are just as convincing: other compressed QML
    // blobs in the EXE are not competing copies of the theme.
    int twins = 0;
    for (const ThemeInfo &c : candidates) {
        if (c.contentQuality == best.contentQuality)
            ++twins;
    }
    best.candidateCount = twins;
    int linked = 0;
    for (const auto &candidate : candidates) {
        if (candidate.hasQrcNode && candidate.qrcSupport == best.qrcSupport)
            ++linked;
    }
    best.ambiguous = best.resource.ambiguous || linked > 1
                     || (!best.hasQrcNode && twins > 1);
    return best;
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

// Property names that are QML/graphics keywords rather than BlueStacks theme
// slots.  Only the bare "name: #hex" and "name #hex" forms need this filter -
// rx1 and rx3 require an explicit "property color|var" prefix, so they can never
// produce one.
//
// Without it, a multi-line gradient stop
//     GradientStop {
//         position: 0.0
//         color: "#1A90FF"
//     }
// yields an entry literally named "color".  Because buildQml maps edits by name,
// editing that one row would rewrite EVERY GradientStop in the file at once, and
// the UI would offer a slot that is not part of the theme.  BlueStacks' own
// UiThemeObject.qml carries gradient definitions (the reference tool has
// "GradientStop" and "root" in its string table), so this is not hypothetical.
//
// Deliberately minimal.  "background", "border", "text", "accent", "primary",
// "title", "selection" and friends are real BlueStacks slot names - recovered
// from BS_THEME_CHANGER's own table - and must NOT be filtered out.
bool isGenericSlotName(const QString &name)
{
    static const QSet<QString> generic = {
        QStringLiteral("color"),    QStringLiteral("position"), QStringLiteral("opacity"),
        QStringLiteral("id"),       QStringLiteral("stopcolor"),QStringLiteral("gradient"),
        QStringLiteral("fill"),     QStringLiteral("stroke"),   QStringLiteral("source"),
        QStringLiteral("null"),     QStringLiteral("width"),    QStringLiteral("height"),
        QStringLiteral("x"),        QStringLiteral("y"),        QStringLiteral("z"),
        QStringLiteral("visible"),  QStringLiteral("enabled"),
        QStringLiteral("minimum"),  QStringLiteral("maximum"),
    };
    return generic.contains(name.toLower());
}

QList<QmlEntry> ThemeEngine::parseQmlEntries(const QString &q)
{
    QList<QmlEntry> entries;
    QSet<int> used;

    const QString mask = qmlCodeMask(q);
    static const QRegularExpression rx1(
        QStringLiteral("\\b(?:readonly\\s+)?property\\s+(?:color|var)\\s+"
                       "([A-Za-z_][A-Za-z0-9_]*)[ \\t]*:"));
    static const QRegularExpression rx2(
        QStringLiteral("^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*:[ \t]*"
                       "(\"(?:#[0-9A-Fa-f]{6,8})\"|#[0-9A-Fa-f]{6,8})"),
        QRegularExpression::MultilineOption);

    // Colon-less spellings.  The reference tool's own regex is
    //   property\s+color\s+(\w+)\s+([#A-Fa-f0-9]+)
    // i.e. it does NOT require the ':' a normal QML binding has.  Accepting that
    // form too is what keeps a build which stores the table without colons from
    // parsing to zero entries (which shows up as "No colors loaded/matched").
    static const QRegularExpression rx3(
        QStringLiteral("^[ \\t]*(?:readonly[ \\t]+)?property[ \\t]+(?:color|var)[ \\t]+"
                       "([A-Za-z_][A-Za-z0-9_]*)[ \\t]+"
                       "(\"#[0-9A-Fa-f]{6,8}\"|#[0-9A-Fa-f]{6,8})"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression rx4(
        QStringLiteral("^[ \\t]*([A-Za-z_][A-Za-z0-9_]*)[ \\t]+"
                       "(\"#[0-9A-Fa-f]{6,8}\"|#[0-9A-Fa-f]{6,8})"),
        QRegularExpression::MultilineOption);

    auto it1 = rx1.globalMatch(mask);
    while (it1.hasNext()) {
        const auto m = it1.next();
        const QString name = m.captured(1);
        if (name.isEmpty())
            continue;
        int start = int(m.capturedEnd());
        while (start < q.size() && q.at(start).isSpace())
            ++start;
        const int end = qmlExpressionEnd(q, start);
        const ColorToken tk = splitColorToken(q.mid(start, end - start), start);
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
        if (name.isEmpty() || isGenericSlotName(name)
            || mask.mid(m.capturedStart(1), name.size()).trimmed().isEmpty())
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

    // Colon-less passes.  The captured token is already a bare hex literal, so
    // the splice range is exactly the match.  Run after the colon passes and
    // guarded by `used`, so the more specific spelling always wins.
    const QRegularExpression colonless[2] = { rx3, rx4 };
    for (const QRegularExpression &rx : colonless) {
        auto it = rx.globalMatch(q);
        while (it.hasNext()) {
            const auto m = it.next();
            const QString name = m.captured(1);
            // rx3 still requires "property color|var", so the filter only ever
            // bites on rx4 - but applying it to both keeps the two consistent.
            if (name.isEmpty() || isGenericSlotName(name)
                || mask.mid(m.capturedStart(1), name.size()).trimmed().isEmpty())
                continue;
            const QString tokstr = m.captured(2);
            const int start = int(m.capturedStart(2));
            if (used.contains(start))
                continue;
            used.insert(start);
            QmlEntry e;
            e.name = name;
            e.quoted = tokstr.startsWith(QLatin1Char('"'));
            QString val = tokstr;
            val.remove(QLatin1Char('"'));
            e.value = val;
            e.original = val;
            e.exprStart = start;
            e.exprLen = tokstr.size();
            entries << e;
        }
    }
    return entries;
}

QString ThemeEngine::buildQml(const QString &qml, const QList<QmlEntry> &entries,
                              const QMap<QString, QString> &edits, bool allowGrow,
                              QStringList *skipped)
{
    static const QRegularExpression re6(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    static const QRegularExpression re8(QStringLiteral("^#[0-9A-Fa-f]{8}$"));
    static const QRegularExpression reAlpha(QStringLiteral("^#([0-9A-Fa-f]{2})[0-9A-Fa-f]{6}$"));

    if (skipped) {
        QSet<QString> names;
        for (const auto &entry : entries)
            names.insert(entry.name);
        for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
            if (!names.contains(it.key()))
                *skipped << QStringLiteral("%1: this property is no longer in the live theme")
                                .arg(it.key());
        }
    }

    struct Change { int start; int len; QString val; };
    QList<Change> changes;

    for (const QmlEntry &e : entries) {
        if (!edits.contains(e.name))
            continue;
        const QString wanted = edits.value(e.name).trimmed();
        QString v = wanted;
        if (v.isEmpty() || v == e.value)
            continue;

        const bool hex6 = re6.match(v).hasMatch();
        const bool hex8 = re8.match(v).hasMatch();
        if (hex6) {
            const auto m = reAlpha.match(e.original);
            if (m.hasMatch())
                v = QStringLiteral("#") + m.captured(1) + v.mid(1); // keep 8-digit alpha
        }
        if (hex6 || hex8)
            v = QStringLiteral("\"") + v + QStringLiteral("\"");

        // A plain-text (uncompressed) theme is rewritten byte for byte in place,
        // so the replacement must fit the expression that is already there.  The
        // zlib record is recompressed - and relocated when it outgrows its slot
        // - so there a hex color may be longer than the old one.  Silently
        // dropping it is how "it says patched but the color never changed"
        // happens, so anything refused is reported back to the caller.
        if (v.size() > e.exprLen && !(allowGrow && (hex6 || hex8))) {
            if (skipped)
                *skipped << QStringLiteral("%1: \"%2\" does not fit the %3-character value "
                                           "currently in the EXE")
                                .arg(e.name, wanted, QString::number(e.exprLen));
            continue;
        }
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
        // Descending order, so a value that grows the text (zlib mode only)
        // cannot invalidate the offsets still to be applied.
        const QString padded =
            c.val + QString(qMax(0, c.len - int(c.val.size())), QLatin1Char(' '));
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
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances (HD-Player.exe) before "
                                        "patching. Then reopen BlueStacks to load the new colors.") };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read file: %1 (run as administrator?)").arg(exePath) };
    QByteArray data = file.readAll();
    file.close();
    const QByteArray original = data;

    // ---------------- patch what is on disk RIGHT NOW ----------------
    // The ThemeInfo handed to us is a snapshot taken when the user pressed
    // Load.  The EXE can change in between - another theme tool patched or
    // relocated the record, BlueStacks updated itself, a Restore happened.
    // Patching with the cached offsets and the cached QML text then writes our
    // colors into a record the app no longer reads, which is exactly the
    // "it says patched but nothing changed" report.  So re-detect first and
    // build the new QML from the CURRENT text.
    ThemeInfo live = info;
    QList<QmlEntry> liveEntries = entries;
    if (info.mode == QLatin1String("zlib") || info.mode == QLatin1String("plain")) {
        const ThemeInfo fresh = detectTheme(data, QStringLiteral("qml"));
        if (!fresh.found || fresh.mode != info.mode) {
            return { false, QStringLiteral("The %1 theme record is no longer in this EXE - the "
                                           "file changed since it was loaded. Press Reload and "
                                           "try again.").arg(info.mode) };
        }
        if (fresh.entryOff != info.entryOff || fresh.qml != info.qml) {
            log(logger,
                QStringLiteral("EXE changed since Load - re-detected the live theme record at "
                               "0x%1 (UI had 0x%2). Patching the live one.")
                    .arg(fresh.entryOff, 0, 16)
                    .arg(info.entryOff, 0, 16),
                QStringLiteral("orange"));
        }
        live = fresh;
        liveEntries = parseQmlEntries(fresh.qml);
    }

    if (live.ambiguous)
        return { false, QStringLiteral("More than one theme resource could be active. No file "
                                        "was written; use --theme-probe to inspect this EXE.") };

    if (live.mode == QLatin1String("zlib") && live.headerBytes == 8 && !live.hasQrcNode
        && data.contains(utf16be(QStringLiteral("UiThemeObject.qml"))))
        return { false, QStringLiteral("The Qt resource tree could not be validated. No file "
                                        "was written; run --theme-probe for this EXE.") };

    // The zlib record is recompressed (and relocated if it outgrows its slot),
    // so a longer hex color is fine there; a plain-text theme must fit in place.
    QStringList skipped;
    const QString q = buildQml(live.qml, liveEntries, edits,
                               live.mode == QLatin1String("zlib"), &skipped);
    for (const QString &why : skipped)
        log(logger, QStringLiteral("Not applied - %1").arg(why), QStringLiteral("orange"));

    if (!skipped.isEmpty())
        return { false, QStringLiteral("Some colors could not be applied. No file was written; "
                                        "see the log and correct those edits first.") };

    // Reapplying a palette also repairs old patches whose source colors are
    // correct but whose embedded/disk cache still contains the original theme.
    const bool refreshOnly = q == live.qml && !edits.isEmpty()
                             && live.mode == QLatin1String("zlib") && live.hasQrcNode;
    if (q == live.qml && !refreshOnly) {
        return { false, skipped.isEmpty()
                            ? QStringLiteral("Nothing to patch - the EXE already contains these "
                                             "colors.")
                            : QStringLiteral("Nothing to patch - none of the edits could be "
                                             "applied (see the log above).") };
    }
    const QString skippedNote =
        skipped.isEmpty()
            ? QString()
            : QStringLiteral(" %1 edit(s) could not be applied - see the log above.")
                  .arg(skipped.size());

    // ---------------- plain ----------------
    if (live.mode == QLatin1String("plain")) {
        const QString old = live.qml;
        const int mult = live.mult;
        const qint64 ws = live.winStart;
        const int n = int(qMin<qint64>(old.size(), q.size()));
        int diff = 0;
        for (int ci = 0; ci < n; ++ci) {
            if (old.at(ci) == q.at(ci))
                continue;
            const qint64 boff = ws + qint64(ci) * mult;
            if (boff < 0 || boff + mult > data.size())
                return { false, QStringLiteral("Plain theme range is invalid. File was not written.") };
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
        QString error;
        if (!PatchFile::writeVerified(exePath, original, data, &error))
            return { false, error };
        return { true, QStringLiteral("Theme colors written and verified. Restart BlueStacks "
                                       "to load them.") + skippedNote };
    }

    if (live.mode != QLatin1String("zlib"))
        return { false, QStringLiteral("Unsupported theme mode: %1").arg(live.mode) };

    // ---------------- zlib ----------------
    const QByteArray qbytes = q.toLatin1();
    const QByteArray comp = refreshOnly ? original.mid(live.zoff, live.compSize)
                                        : ZlibUtils::compress(qbytes);
    if (comp.isEmpty())
        return { false, QStringLiteral("Theme compression failed.") };

    const qint64 zoff = live.zoff;
    const int headerBytes = live.headerBytes;
    quint32 oldCompLen = live.compSize;
    if (oldCompLen == 0) {
        QByteArray tmp;
        if (decompressExactAt(data, zoff, qint64(live.storedSize) - 4, live.uncompSize, &tmp))
            oldCompLen = live.storedSize - 4;
        else if (decompressExactAt(data, zoff, live.storedSize, live.uncompSize, &tmp))
            oldCompLen = live.storedSize;
        else
            oldCompLen = measureCompLen(data, zoff, live.uncompSize, 262144);
    }
    if (oldCompLen == 0)
        return { false, QStringLiteral("The compressed theme length could not be determined - "
                                        "the record is not safe to rewrite.") };

    const qint64 oldEnd = zoff + oldCompLen;
    const qint64 compCapacity = oldCompLen + zeroSlackAfter(data, live);

    bool relocated = false;

    if (comp.size() <= compCapacity) {
        // ---- in place ----
        // Only an 8-byte record has an outer length field.  A bare qCompress
        // blob (4-byte) or a headerless stream (0-byte) must not have the bytes
        // in front of it rewritten - they belong to whatever precedes the theme.
        if (headerBytes >= 8) {
            const quint32 storedNew = live.sizeIncludesUSize ? quint32(comp.size()) + 4
                                                             : quint32(comp.size());
            putBe32(data, live.entryOff, storedNew);
            putBe32(data, live.entryOff + 4, quint32(qbytes.size()));
        } else if (headerBytes == 4) {
            putBe32(data, live.entryOff, quint32(qbytes.size()));
        }
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
        if (headerBytes != 8) {
            // Relocation re-points a qresource tree entry at a freshly built
            // [outerLen][usize][zlib] record.  Without an outer length field the
            // blob is not a qrc payload, so there is no pointer to update and
            // moving it would leave the app reading the old bytes.  The
            // reference tool blind-scans the whole file for 4-byte LE/BE copies
            // of the old offset and rewrites every hit; that corrupts unrelated
            // data whenever an address or constant happens to equal the offset,
            // so it is deliberately not done here.
            return { false, QStringLiteral("No room for this custom theme in this EXE. "
                                           "The theme is stored as a bare compressed blob "
                                           "(no resource length field), so it can only be "
                                           "patched in place. Try a palette whose colors are "
                                           "the same length as the originals.") };
        }
        if (live.hasQrcNode) {
            node = live.resource;
        } else {
            node = findQrcNodeForEntry(data, live.entryOff);
        }
        if (!node.found || node.ambiguous)
            return { false, QStringLiteral("No room, and a verified UiThemeObject.qml qrc node "
                                           "was not found for relocation.") };

        const QList<PeSection> secs = PeUtils::sections(data);
        if (secs.isEmpty())
            return { false, QStringLiteral("No room, and PE section parsing failed.") };

        const qint64 oldEntryRva = PeUtils::rawToRva(secs, live.entryOff);
        if (oldEntryRva < 0)
            return { false, QStringLiteral("No room, and old qrc data is not in a "
                                           "mapped PE section.") };

        qint64 dataBaseRva = node.dataBaseRva;
        if (dataBaseRva < 0)
            dataBaseRva = oldEntryRva - node.dataOffset;

        const qint64 newRecordLen = 8 + comp.size();
        const qint64 cave = findZeroCave(data, secs, live.entryOff, dataBaseRva, newRecordLen,
                                           live.resource.reserved);
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
        for (const qint64 alias : node.nodes)
            putBe32(data, alias + 10, quint32(newDataOff));

        // Invalidate the orphaned old record (header + zlib stream) so
        // re-detection and future patches follow the qrc link to the cave.
        for (qint64 z = live.entryOff; z < qMin<qint64>(oldEnd, data.size()); ++z)
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

    const bool refreshed = QtResources::refreshTimestamp(data, live.resource);
    const int units = QtResources::invalidateThemeUnits(data);
    QString cacheError;
    if (!QtResources::invalidateCompressedThemeCaches(data, live.resource, &cacheError))
        return { false, cacheError };
    if (refreshed)
        log(logger, QStringLiteral("Refreshed the QML resource timestamp: old disk-cached "
                                   "colors will be rejected on the next launch."), QStringLiteral("blue"));
    if (units)
        log(logger, QStringLiteral("Invalidated %1 built-in theme cache unit(s); Qt will "
                                   "compile the edited source.").arg(units), QStringLiteral("blue"));

    // A successful inflate at an arbitrary offset is not enough. Re-detect
    // through the validated tree, then verify the complete selected palette.
    const ThemeInfo vt = findThemeZlib(data);
    if (!vt.found || vt.ambiguous || vt.qml != q
        || (live.hasQrcNode && (!vt.hasQrcNode || vt.qrcNodeOff != live.qrcNodeOff)))
        return { false, QStringLiteral("Active theme verification failed. File was not written.") };

    if (!backup(exePath, logger))
        return { false, QStringLiteral("Could not create backup.") };
    QString error;
    if (!PatchFile::writeVerified(exePath, original, data, &error))
        return { false, error };
    return { true, refreshOnly
                       ? QStringLiteral("Theme already has these colors. Its cache was refreshed; "
                                        "restart BlueStacks to load them.")
                       : (relocated
                              ? QStringLiteral("Theme relocated, colors verified and cache refreshed. "
                                               "Restart BlueStacks to load the new theme.")
                              : QStringLiteral("Theme colors verified and cache refreshed. "
                                               "Restart BlueStacks to load the new theme.")) };

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
        out << ThemeEngine::Replacement { idx, int(ov.size()), rep };
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
                    repls << Replacement { sidePos, int(oldPat.size()), rep };
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
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances (HD-Player.exe) before patching.") };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Read error (run as administrator?)") };
    QByteArray data = file.readAll();
    file.close();
    const QByteArray original = data;

    const QList<Replacement> repls = collectThemeReplacements(data, profile, colors, qmlColors);
    if (repls.isEmpty())
        return { false, QStringLiteral("No changes selected") };

    for (const Replacement &r : repls) {
        if (r.pos >= 0 && r.pos + r.len <= data.size())
            data.replace(int(r.pos), r.len, r.rep);
    }

    if (!backup(exePath, logger))
        return { false, QStringLiteral("Could not create backup") };
    QString error;
    if (!PatchFile::writeVerified(exePath, original, data, &error))
        return { false, error };

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

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

QString ThemeEngine::probeReport(const QString &exePath)
{
    QString out;
    auto line = [&out](const QString &s = QString()) { out += s + QLatin1Char('\n'); };

    line(QStringLiteral("BSI Customizer theme probe"));
    line(QStringLiteral("=========================="));
    line(QStringLiteral("file: %1").arg(exePath));

    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly)) {
        line(QStringLiteral("CANNOT READ: %1").arg(f.errorString()));
        line(QStringLiteral("  -> close BlueStacks (HD-Player.exe) and run as administrator."));
        return out;
    }
    const QByteArray data = f.readAll();
    f.close();
    line(QStringLiteral("size: %1 bytes").arg(data.size()));
    if (data.size() < 64) {
        line(QStringLiteral("Too small to be HD-Player.exe."));
        return out;
    }
    line();

    auto offsToStr = [](const QList<qint64> &v, int maxN) {
        QStringList parts;
        for (int i = 0; i < v.size() && i < maxN; ++i)
            parts << QStringLiteral("0x%1").arg(v.at(i), 0, 16);
        if (v.size() > maxN)
            parts << QStringLiteral("... (+%1 more)").arg(v.size() - maxN);
        return parts.join(QLatin1String(", "));
    };
    auto hexDump = [&data](qint64 off, int n) {
        const qint64 start = qMax<qint64>(0, off);
        return QString::fromLatin1(data.mid(int(start), n).toHex(' ')).toUpper();
    };
    auto looksLikeTheme = [](const QString &q) {
        const bool strict = q.contains(QLatin1String("property QtObject colors"))
                            && q.contains(QLatin1String("primary100"));
        int colorProps = 0;
        for (qsizetype at = 0; (at = q.indexOf(QLatin1String("property color"), at)) >= 0;)
            at += 14, ++colorProps;
        return QStringLiteral("strict=%1  'property color' x%2  parsed=%3")
            .arg(strict ? QLatin1String("yes") : QLatin1String("no"))
            .arg(colorProps)
            .arg(ThemeEngine::parseQmlEntries(q).size());
    };

    // ---------------- 1. markers ----------------
    line(QStringLiteral("[1] 'UiThemeObject.qml' markers"));
    const QString name = QStringLiteral("UiThemeObject.qml");
    const QList<QPair<QString, QByteArray>> encs = {
        { QStringLiteral("ascii"), name.toLatin1() },
        { QStringLiteral("utf-16be"), utf16be(name) },
        { QStringLiteral("utf-16le"), utf16le(name) },
    };
    int markerHits = 0;
    for (const auto &e : encs) {
        QList<qint64> offs;
        qsizetype at = 0;
        while (true) {
            at = data.indexOf(e.second, at);
            if (at < 0)
                break;
            offs << at;
            ++at;
        }
        markerHits += offs.size();
        line(QStringLiteral("  %1: %2 hit(s)%3")
                 .arg(e.first, -10)
                 .arg(offs.size())
                 .arg(offs.isEmpty() ? QString()
                                     : QStringLiteral("  at ") + offsToStr(offs, 8)));
    }
    if (markerHits == 0)
        line(QStringLiteral("  -> NO marker anywhere: detection has to fall back to the"));
    if (markerHits == 0)
        line(QStringLiteral("     global zlib scan, which is much weaker evidence."));
    line();

    // ---------------- 2. zlib streams ----------------
    line(QStringLiteral("[2] zlib stream starts"));
    const QList<qint64> allMagics = ZlibUtils::findMagicOffsets(data);
    const QList<qint64> anchored = markerAnchoredMagics(data);
    line(QStringLiteral("  total 78 DA/9C/01 pairs in file: %1").arg(allMagics.size()));
    line(QStringLiteral("  within 8192 bytes of a marker (either side): %1").arg(anchored.size()));
    if (!anchored.isEmpty())
        line(QStringLiteral("    %1").arg(offsToStr(anchored, 16)));
    line();

    // ---------------- 3. per-stream layout analysis ----------------
    // With no marker there is nothing to anchor on, so fall back to the handful
    // of global magic offsets whose preceding 4 bytes could plausibly be a
    // qCompress size field - otherwise the probe would print nothing to analyse
    // in exactly the case where the user most needs it.
    QList<qint64> toAnalyse = anchored;
    line(QStringLiteral("[3] header layout of each candidate stream"));
    if (toAnalyse.isEmpty()) {
        line(QStringLiteral("  no marker-anchored stream; falling back to the global scan"));
        for (const qint64 pos : allMagics) {
            if (toAnalyse.size() >= 8)
                break;
            if (pos < 4)
                continue;
            const quint32 u4 = be32(data, pos - 4);
            if (u4 >= 100 && u4 <= 500000u)
                toAnalyse << pos;
        }
    }
    if (toAnalyse.isEmpty())
        line(QStringLiteral("  (no plausible stream found anywhere in the file)"));
    for (const qint64 pos : toAnalyse) {
        line(QStringLiteral("  stream at 0x%1  magic=%2")
                 .arg(pos, 0, 16)
                 .arg(hexDump(pos, 2)));
        line(QStringLiteral("    bytes at -8: %1").arg(hexDump(pos - 8, 8)));
        const quint32 outer = (pos >= 8) ? be32(data, pos - 8) : 0;
        const quint32 u4 = (pos >= 4) ? be32(data, pos - 4) : 0;
        line(QStringLiteral("    as [outerLen][usize]: outerLen=%1 usize=%2")
                 .arg(outer).arg(u4));

        // Layout A
        QByteArray qb;
        QString verdictA = QStringLiteral("no");
        if (pos >= 8 && u4 >= 16 && u4 <= 50000000u) {
            if (decompressExactAt(data, pos, qint64(outer) - 4, u4, &qb))
                verdictA = QStringLiteral("YES, outerLen = compLen+4, %1 bytes out").arg(qb.size());
            else if (decompressExactAt(data, pos, outer, u4, &qb))
                verdictA = QStringLiteral("YES, outerLen = compLen, %1 bytes out").arg(qb.size());
        }
        line(QStringLiteral("    layout A [be32 outerLen][be32 usize][zlib]: %1").arg(verdictA));
        if (verdictA.startsWith(QLatin1String("YES")))
            line(QStringLiteral("      content: %1").arg(looksLikeTheme(QString::fromLatin1(qb))));

        // Layout B
        qb.clear();
        QString verdictB = QStringLiteral("no");
        if (pos >= 4 && u4 >= 100 && u4 <= 500000u) {
            if (inflateOverread(data, pos, u4, 262144, &qb)) {
                const quint32 m = measureCompLen(data, pos, u4, 262144);
                verdictB = QStringLiteral("YES, usize=%1 -> %2 bytes out, compLen measured = %3")
                               .arg(u4).arg(qb.size()).arg(m);
            } else {
                verdictB = QStringLiteral("no (usize=%1 is plausible but the stream did not inflate)")
                               .arg(u4);
            }
        } else {
            verdictB = QStringLiteral("skipped (usize=%1 outside 100..500000)").arg(u4);
        }
        line(QStringLiteral("    layout B [be32 usize][zlib] (bare qCompress): %1").arg(verdictB));
        if (!qb.isEmpty())
            line(QStringLiteral("      content: %1").arg(looksLikeTheme(QString::fromLatin1(qb))));

        // Layout C
        qb.clear();
        QString verdictC = QStringLiteral("no");
        if (inflateOverread(data, pos, 0, 262144, &qb)) {
            const quint32 m = measureCompLen(data, pos, quint32(qb.size()), 262144);
            verdictC = QStringLiteral("YES -> %1 bytes out, compLen measured = %2")
                           .arg(qb.size()).arg(m);
        }
        line(QStringLiteral("    layout C [zlib] (no header, size unknown): %1").arg(verdictC));
        if (!qb.isEmpty()) {
            line(QStringLiteral("      content: %1").arg(looksLikeTheme(QString::fromLatin1(qb))));
            const QString preview = QString::fromLatin1(qb).left(160).replace(QLatin1Char('\n'),
                                                                              QLatin1Char(' '));
            line(QStringLiteral("      preview: %1").arg(preview));
        }
        line();
    }

    // ---------------- 4. what detection picks ----------------
    line(QStringLiteral("[4] ThemeEngine::detectTheme(data, \"qml\") result"));
    const ThemeInfo t = detectTheme(data, QStringLiteral("qml"));
    if (!t.found) {
        line(QStringLiteral("  *** NOT FOUND ***  - this is the failure."));
        line(QStringLiteral("  Sections 1-3 above show which assumption does not hold."));
        line();
        return out;
    }
    line(QStringLiteral("  found            : yes"));
    line(QStringLiteral("  mode             : %1").arg(t.mode));
    line(QStringLiteral("  entryOff         : 0x%1").arg(t.entryOff, 0, 16));
    line(QStringLiteral("  zoff             : 0x%1").arg(t.zoff, 0, 16));
    line(QStringLiteral("  headerBytes      : %1%2")
             .arg(t.headerBytes)
             .arg(t.headerBytes == 8 ? QStringLiteral("  (Qt resource record)")
                  : t.headerBytes == 4 ? QStringLiteral("  (bare qCompress blob)")
                                       : QStringLiteral("  (no header)")));
    line(QStringLiteral("  storedSize       : %1").arg(t.storedSize));
    line(QStringLiteral("  compSize         : %1").arg(t.compSize));
    line(QStringLiteral("  uncompSize       : %1").arg(t.uncompSize));
    line(QStringLiteral("  sizeInclUSize    : %1").arg(t.sizeIncludesUSize ? "yes" : "no"));
    line(QStringLiteral("  markerAnchored   : %1").arg(t.markerAnchored ? "yes" : "no"));
    line(QStringLiteral("  contentQuality   : %1").arg(t.contentQuality));
    line(QStringLiteral("  stock palette    : %1 of %2 BlueStacks default colors present")
             .arg(t.stockHits)
             .arg(int(sizeof(kStockPalette) / sizeof(kStockPalette[0]))));
    line(QStringLiteral("  entryCount       : %1").arg(t.entryCount));
    line(QStringLiteral("  candidateCount   : %1").arg(t.candidateCount));
    line(QStringLiteral("  hasQrcNode       : %1").arg(t.hasQrcNode ? "yes" : "no"));
    line(QStringLiteral("  qrcStrict        : %1").arg(t.qrcStrict ? "yes" : "no"));
    line(QStringLiteral("  qrcSupport       : %1").arg(t.qrcSupport));
    line(QStringLiteral("  qrc node bytes   : %1").arg(t.resource.nodeSize));
    line(QStringLiteral("  ambiguous        : %1").arg(t.ambiguous ? "yes - patch refused" : "no"));
    if (t.resource.nodeSize == 22)
        line(QStringLiteral("  resource mtime   : %1").arg(be64(data, t.qrcNodeOff + 14)));
    line();

    // ---------------- 5. parsed colors ----------------
    line(QStringLiteral("[5] color entries parsed from the decompressed QML"));
    const QList<QmlEntry> ents = parseQmlEntries(t.qml);
    line(QStringLiteral("  decompressed length: %1 chars").arg(t.qml.size()));
    line(QStringLiteral("  contains 'property QtObject colors': %1")
             .arg(t.qml.contains(QLatin1String("property QtObject colors")) ? "yes" : "no"));
    line(QStringLiteral("  contains 'primary100'            : %1")
             .arg(t.qml.contains(QLatin1String("primary100")) ? "yes" : "no"));
    line(QStringLiteral("  entries parsed                   : %1").arg(ents.size()));
    if (ents.isEmpty()) {
        line(QStringLiteral("  *** ZERO ENTRIES *** - the UI would show 'No colors loaded/matched'."));
        line(QStringLiteral("  First 400 chars of the decompressed text:"));
        line(QStringLiteral("  ----"));
        const QStringList ls = t.qml.left(400).split(QLatin1Char('\n'));
        for (const QString &l : ls)
            line(QStringLiteral("  | %1").arg(l));
        line(QStringLiteral("  ----"));
    } else {
        for (int i = 0; i < ents.size() && i < 25; ++i)
            line(QStringLiteral("    %1%2 = %3")
                     .arg(ents.at(i).name, -34)
                     .arg(ents.at(i).quoted ? QStringLiteral("(quoted)  ")
                                            : QStringLiteral("(unquoted)"))
                     .arg(ents.at(i).value));
        if (ents.size() > 25)
            line(QStringLiteral("    ... (+%1 more)").arg(ents.size() - 25));
    }
    line();

    // ---------------- 6. patch feasibility ----------------
    line(QStringLiteral("[6] patch feasibility"));
    if (t.mode != QLatin1String("zlib")) {
        line(QStringLiteral("  mode is '%1', not zlib - in-place text rewrite only.").arg(t.mode));
        return out;
    }
    const qint64 zeroSlack = zeroSlackAfter(data, t);
    line(QStringLiteral("  current compressed size : %1 bytes").arg(t.compSize));
    line(QStringLiteral("  zero slack after record : %1 bytes").arg(zeroSlack));
    line(QStringLiteral("  in-place capacity       : %1 bytes").arg(t.compSize + zeroSlack));
    line(QStringLiteral("  relocation possible     : %1")
             .arg(t.headerBytes == 8
                      ? (t.hasQrcNode ? QStringLiteral("yes (qrc node found)")
                                      : QStringLiteral("NO - qrc node not found"))
                      : QStringLiteral("NO - record has no outer length field")));
    if (t.headerBytes != 8)
        line(QStringLiteral("  -> a bigger theme can only be applied if it recompresses to"));
    if (t.headerBytes != 8)
        line(QStringLiteral("     at most the in-place capacity above."));
    line();
    line(QStringLiteral("Probe complete."));
    return out;
}
