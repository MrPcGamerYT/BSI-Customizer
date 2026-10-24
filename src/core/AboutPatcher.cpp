#include "AboutPatcher.h"

#include "BsDetector.h"
#include "ByteUtils.h"
#include "PatchFile.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace AboutPatcher {
namespace {

// The stock rows we hide while the custom content is shown. Every edit keeps
// the original text in a tagged comment, so undo is byte-exact even though the
// patch rewrites a live page instead of blanking it.
const QString kHiddenWrap = QStringLiteral(
    "false /* BSI CUSTOMIZER: about hidden */ && (");
const QString kHiddenLine = QStringLiteral(
    "visible: false // BSI CUSTOMIZER: about hidden");
const QString kHiddenInline = QStringLiteral(
    "visible: false /* BSI CUSTOMIZER: about hidden */");
// BlueStacks hides the whole "Important links" and "About and other info"
// blocks on MSI builds (`visible: uiBackend.oem !== "msi5"`). The custom page
// must show them there too, so their binding is switched to `true` while the
// patch is applied and restored from the tag afterwards.
const QString kShownTag = QStringLiteral(" /* BSI CUSTOMIZER: about shown:");
const QString kTextTag = QStringLiteral(" /* BSI CUSTOMIZER: about text:");
const QString kHandlerTag = QStringLiteral(" /* BSI CUSTOMIZER: about handler:");
const QString kHandlerMarker = QStringLiteral("// BSI CUSTOMIZER: about handler");
const QString kConfigBegin = QStringLiteral("// BSI CUSTOMIZER: about-config ");

struct Edit
{
    int begin = 0;
    int length = 0;
    QString replacement;
};

// Defined below with the other "what the page prints" helpers.
QString displayFor(const QString &url);

void addEdit(QList<Edit> &edits, int begin, int length, const QString &replacement)
{
    edits << Edit { begin, length, replacement };
}

QString lineEnding(const QString &qml)
{
    return qml.contains(QLatin1String("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
}

struct Doc
{
    QtResources::File file;
    QtResources::Link link;
    QString qml;
    bool bom = false;
    // Read straight out of the file by its own text because the resource tree
    // could not be walked. Readable and analyzable, but not writable: writing
    // needs the node the scan does not have.
    bool scanned = false;
};

bool findDocs(const QByteArray &image, const QString &name, QList<Doc> *out, QString *error)
{
    auto files = QtResources::registeredFiles(image);
    const bool registered = std::any_of(files.cbegin(), files.cend(),
                                        [&](const QtResources::File &file) { return file.name == name; });
    if (!registered) {
        const auto theme = ThemeEngine::findThemeZlib(image);
        if (theme.hasQrcNode)
            for (const auto &file : QtResources::filesInPool(image, theme.resource)) {
                bool present = false;
                for (const auto &old : files)
                    if (old.entryOff == file.entryOff && old.name == file.name) present = true;
                if (!present) files << file;
            }
    }
    for (const auto &file : files) {
        if (file.name != name) continue;
        const auto bytes = QtResources::readFile(image, file);
        if (bytes.isEmpty()) {
            if (error) *error = QStringLiteral("%1 could not be read; no file was written.").arg(name);
            return false;
        }
        const bool bom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
        const QByteArray body = bom ? bytes.mid(3) : bytes;
        QString qml = QString::fromUtf8(body);
        if (qml.toUtf8() != body) {
            if (error) *error = QStringLiteral("%1 is not UTF-8; no file was written.").arg(name);
            return false;
        }
        const auto link = QtResources::findLink(image, file.entryOff, file.name);
        if (!link.found || link.ambiguous) {
            if (error) *error = QStringLiteral("The %1 resource mapping is ambiguous; no file was written.")
                                     .arg(name);
            return false;
        }
        if (out) *out << Doc { file, link, qml, bom };
    }
    if (out && !out->isEmpty()) return true;
    if (error) *error = QStringLiteral("%1 is not embedded in this player.").arg(name);
    return false;
}

// Last-resort read: the page is found by the object name it always carries and
// the record that holds it is located by its own size field. The resource tree
// is not consulted at all, so a build whose pools this tool cannot map still
// gets a filled editor instead of an empty card - the same idea as the SVGs
// that have always been found by their markup.
bool findDocByBytes(const QByteArray &image, Doc *out)
{
    const auto isPage = [](const QString &qml) {
        return qml.contains(QLatin1String("objectName")) && qml.contains(QLatin1String("aboutSettings"));
    };
    const auto accept = [out](const QString &qml, bool bom, bool scanned) {
        Doc doc;
        doc.qml = qml;
        doc.bom = bom;
        doc.scanned = scanned;
        *out = doc;
        return true;
    };

    // 1) A page stored raw: found by the object name it always carries, then
    //    the record that holds it (the four bytes in front are its size).
    static const QByteArray needles[] { QByteArray("objectName: \"aboutSettings\""),
                                        QByteArray("objectName:\"aboutSettings\"") };
    const auto looksLikePageStart = [](const QByteArray &head) {
        const QByteArray trimmed = head.left(8).trimmed();
        return trimmed.startsWith("/*") || trimmed.startsWith("//")
               || trimmed.startsWith("import") || trimmed.startsWith("Item")
               || head.startsWith(QByteArray::fromHex("efbbbf"));
    };
    for (const auto &needle : needles) {
        for (qsizetype at = image.indexOf(needle); at >= 0; at = image.indexOf(needle, at + 1)) {
            const qsizetype lowest = qMax<qsizetype>(4, at - 1024 * 1024);
            for (qsizetype record = at - 4; record >= lowest; --record) {
                const quint32 size = ByteUtils::be32(image, record);
                if (size < 4096u || size > 4u * 1024u * 1024u) continue;
                if (!ByteUtils::inRange(image, record + 4, size)) continue;
                if (quint64(record) + 4 + size <= quint64(at)) continue;
                if (!looksLikePageStart(image.mid(int(record) + 4, 8))) continue;
                QByteArray body = image.mid(int(record) + 4, int(size));
                const QString qml = QString::fromUtf8(body);
                if (qml.toUtf8() != body || qml.contains(QChar(0))) continue;
                return accept(qml, body.startsWith(QByteArray::fromHex("efbbbf")), false);
            }
        }
    }

    // 2) The page as every current player stores it: zlib, inside a record.
    //    A qCompress payload opens with its decoded size and a zlib header
    //    (78 01/5E/9C/DA), so the eight bytes in front of that header already
    //    say "record of N bytes holding M bytes of text". Only those are
    //    decompressed, and only until the page turns up.
    int attempts = 0;
    for (qsizetype at = image.indexOf(QByteArray::fromHex("78")); at >= 0; at = image.indexOf(QByteArray::fromHex("78"), at + 1)) {
        if (attempts >= 4000) break;
        if (at < 8 || at + 2 > image.size()) continue;
        const int zlibFlag = quint8(image.at(int(at) + 1));
        if ((0x7800 + zlibFlag) % 31 != 0) continue;
        const qint64 record = qint64(at) - 8;
        const quint32 fieldA = ByteUtils::be32(image, record);
        const quint32 fieldB = ByteUtils::be32(image, record + 4);
        // A record says (decoded, stored) in this order in every player seen so
        // far, and the reverse in one older 5.x build: try both, and let the
        // decoded size of the zlib stream decide which reading is right.
        const quint32 orders[2][2] { { fieldA, fieldB }, { fieldB, fieldA } };
        for (const auto &pair : orders) {
            const quint32 decoded = pair[0];
            const quint32 stored = pair[1];
            if (stored < 16u || stored > 4u * 1024u * 1024u || decoded < 4096u
                || decoded > 4u * 1024u * 1024u)
                continue;
            if (!ByteUtils::inRange(image, record + 4, stored)) continue;
            ++attempts;
            const QByteArray body = qUncompress(image.mid(int(record) + 4, int(stored)));
            if (body.size() != int(decoded)) continue;
            const QString qml = QString::fromUtf8(body);
            if (qml.toUtf8() != body || !isPage(qml)) continue;
            return accept(qml, body.startsWith(QByteArray::fromHex("efbbbf")), true);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// The compiled copy of the page
// ---------------------------------------------------------------------------
// A BlueStacks player ships PlrAboutSetting.qml as text *and* a compiled copy of
// the same page, and the window runs the compiled one. Every word the page
// prints - the headings, each row's name and address, the copyright sentence,
// the update button's caption - is a UTF-16LE record in that unit's string
// table:
//
//     FF FF FF FF | le32 chars | le32 0 | le32 0x18 | le32 0x18 | le32 0 | text
//
// Those records are the page. The .qml next to them is a copy nothing displays,
// so the editors read *these* strings and the patch rewrites *these* strings.
// Nothing here is matched by paragraph: a record is identified by where it sits
// in the page's fixed structure, which is what makes a second apply, an edited
// text and a hidden row all read back exactly.
struct Slot
{
    int marker = 0;  // first byte of FF FF FF FF
    int textAt = 0;  // first byte of the UTF-16LE text
    int chars = 0;   // characters the record holds now
    int end = 0;     // first byte after the slot: the next record, or its padding
    QString text;

    // Characters that still fit. A record owns the space up to the next record,
    // not just the length of the text in it: writing a shorter text leaves the
    // padding behind, so the slot keeps its room and can be edited - or put back
    // from the backup - again later.
    int maxChars() const { return qMax(chars, (end - textAt - 2) / 2); }
};

const QByteArray &slotMarker()
{
    static const QByteArray marker = QByteArray::fromHex("ffffffff");
    return marker;
}

// A record is 24 bytes of header, then its UTF-16LE text:
//
//    FF FF FF FF | chars | 0 | 24 | 24 | 0
//
// Every number is little-endian, and the two 24s are the header size, not a
// size of the text: they are the same for a 12 character heading and a 105
// character copyright line.
bool parseSlot(const QByteArray &image, int marker, Slot *slot)
{
    if (!ByteUtils::inRange(image, marker, 24)) return false;
    const quint32 chars = ByteUtils::le32(image, marker + 4);
    if (chars == 0 || chars > 8192) return false;
    if (ByteUtils::le32(image, marker + 8) != 0) return false;
    const quint32 a = ByteUtils::le32(image, marker + 12);
    const quint32 b = ByteUtils::le32(image, marker + 16);
    if (a != b || a > 0x1000) return false;
    if (ByteUtils::le32(image, marker + 20) != 0) return false;
    const int at = marker + 24;
    const int bytes = int(chars) * 2;
    if (!ByteUtils::inRange(image, at, bytes)) return false;
    QString text;
    text.reserve(int(chars));
    for (quint32 i = 0; i < chars; ++i) {
        const int p = at + int(i) * 2;
        const ushort unit = ushort(quint8(image.at(p)) | (quint8(image.at(p + 1)) << 8));
        if (unit == 0) return false;
        if (unit < 0x20 && unit != 9 && unit != 10 && unit != 13) return false;
        if (unit >= 0xD800 && unit <= 0xDFFF) return false; // no UTF-16 surrogates
        if (unit == 0xFFFE || unit == 0xFFFF) return false;
        text.append(QChar(unit));
    }
    // The slot owns everything up to the next record's marker. Measuring with
    // the marker (and not only with the zeros) keeps a record recognizable
    // after it was rewritten shorter - otherwise the page could be edited once
    // and would lose its room. The last record of a table has no next marker:
    // its slot is the text and the padding right behind it, never more.
    int end = at + bytes;
    const qsizetype next = image.indexOf(slotMarker(), at + bytes);
    if (next > 0 && next - (at + bytes) <= 4096) {
        end = int(next);
    } else {
        while (end < image.size() && image.at(end) == '\0' && end - (at + bytes) < 64) ++end;
    }
    Slot parsed;
    parsed.marker = marker;
    parsed.textAt = at;
    parsed.chars = int(chars);
    parsed.end = end;
    parsed.text = text;
    *slot = parsed;
    return true;
}

QList<Slot> scanSlots(const QByteArray &image)
{
    QList<Slot> records;
    for (int at = int(image.indexOf(slotMarker())); at >= 0;
         at = int(image.indexOf(slotMarker(), at + 1))) {
        Slot slot;
        if (!parseSlot(image, at, &slot)) continue;
        records << slot;
        // Nothing inside a literal can be a marker, so the next search may
        // resume at the end of this one.
        at = slot.textAt + slot.chars * 2;
    }
    return records;
}

// One link row of the page: the words in front of it, the anchor that prints
// the address, and the address itself.
//
//   [name]  [nxt_cn]  " - <a …>label</a>"  " - <a …>label</a>"  [address]  [address]
//
// A row can carry two of everything: the page branches on the OEM
// (`oem === "nxt_cn" ? A : B`), so both copies are rewritten together and the
// row is never half changed.
struct PageRow
{
    int name = -1;
    QList<int> fragments;
    QList<int> addresses;
};

// What the page's string table says, parsed by structure. Every index is a
// position in `records`, and the structure is the same on every player seen so
// far, so an index means the same row before and after a patch.
struct Layout
{
    QList<Slot> records;
    int about = -1;  // "About and other info"
    int links = -1;  // "Important links"
    QList<PageRow> rows;

    bool valid() const { return about >= 0 || links >= 0; }
    // Every patch blanks the update button's caption and only a restore writes
    // it back, so its absence is the page's own way of saying "this was
    // written by the customizer".
    bool patched() const
    {
        if (about < 0) return false;
        for (const Slot &slot : records)
            if (slot.text == QLatin1String("Check for updates")
                || slot.text == QLatin1String("Download Update"))
                return false;
        return true;
    }
};

// A property name or id in the table ("nxt_cn", "openEmail", "bodySmall"). The
// China branch keeps one of these between a row's name and its anchor, so the
// name lookup has to step over it.
bool isIdentifier(const QString &text)
{
    if (text.isEmpty()) return false;
    if (!text.at(0).isLower()) return false;
    for (const QChar ch : text) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_')) return false;
        if (ch.unicode() > 0x7f) return false;
    }
    return true;
}

bool isFragment(const QString &text) { return text.startsWith(QLatin1String(" - <a ")); }

// Where a link points, as the page stores it: an address, never a name.
bool isAddress(const QString &text)
{
    if (text.contains(QLatin1Char('<'))) return false;
    if (text.contains(QLatin1Char('@'))) return true;
    if (text.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) return true;
    if (text.contains(QLatin1Char('/'))) return true;
    return text.contains(QLatin1Char('.'));
}

QString anchorLabel(const QString &fragment)
{
    const int close = fragment.lastIndexOf(QLatin1String("</a>"));
    if (close < 0) return QString();
    const int open = fragment.lastIndexOf(QLatin1Char('>'), close);
    if (open < 0 || open >= close) return QString();
    return fragment.mid(open + 1, close - open - 1);
}

// Finds the heading by its words, case-insensitively, so a table read back
// after a patch is found by the one line the patch never touches.
int findHeading(const QList<Slot> &records, const QString &words)
{
    for (int i = 0; i < records.size(); ++i)
        if (records.at(i).text.trimmed().compare(words, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

// Where a sentence sits in the file as UTF-16LE, whatever surrounds it. The
// records are found by their header, but the words are found by themselves: a
// build whose header differs still carries the page, and knowing that turns
// "not supported" into a precise report instead of a guess.
QList<int> wideFind(const QByteArray &image, const QString &text)
{
    QByteArray needle;
    needle.reserve(int(text.size()) * 2);
    for (const QChar ch : text) {
        needle.append(char(ch.unicode() & 0xff));
        needle.append(char((ch.unicode() >> 8) & 0xff));
    }
    QList<int> hits;
    for (int at = int(image.indexOf(needle)); at >= 0;
         at = int(image.indexOf(needle, at + 1))) {
        hits << at;
        if (hits.size() >= 16) break;
    }
    return hits;
}

Layout layoutOf(const QByteArray &image)
{
    Layout layout;
    const QList<Slot> all = scanSlots(image);
    if (all.isEmpty()) return layout;

    int about = findHeading(all, QStringLiteral("About and other info"));
    int links = findHeading(all, QStringLiteral("Important links"));
    if (about < 0 && links < 0) {
        // A translated page keeps the shape but not the words: the copyright
        // sign and the update button's caption still identify it.
        for (int i = 1; i < all.size(); ++i) {
            if (!all.at(i).text.startsWith(QStringLiteral("\u00A9"))) continue;
            // The heading is the record right in front of the sentence; the
            // sentence is always the one after the heading.
            about = i - 1;
            break;
        }
        if (about < 0) return layout;
    }

    // The records of one compiled unit sit next to each other; the rest of the
    // file is other units, other tables, other pages. Only this run is the page.
    const int anchor = about >= 0 ? about : links;
    int lo = anchor;
    int hi = anchor;
    while (lo > 0 && all.at(lo).marker - all.at(lo - 1).end <= 8) --lo;
    while (hi + 1 < all.size() && all.at(hi + 1).marker - all.at(hi).end <= 8) ++hi;
    layout.records = all.mid(lo, hi - lo + 1);
    layout.about = findHeading(layout.records, QStringLiteral("About and other info"));
    layout.links = findHeading(layout.records, QStringLiteral("Important links"));

    const int start = layout.links >= 0 ? layout.links + 1 : 0;
    const int stop = layout.about >= 0 ? layout.about : layout.records.size();
    for (int i = start; i < stop;) {
        if (!isFragment(layout.records.at(i).text)) {
            ++i;
            continue;
        }
        PageRow row;
        int j = i;
        while (j < stop && isFragment(layout.records.at(j).text)) row.fragments << j++;
        // The name is the slot in front of the anchors. Identifiers are stepped
        // over; a blank is not - that is the name slot of a row that was
        // hidden, and walking past it would hand this row the previous row's
        // address.
        int k = i - 1;
        while (k >= start && isIdentifier(layout.records.at(k).text)) --k;
        row.name = k >= start ? k : -1;
        int at = j;
        while (at < stop && row.addresses.size() < row.fragments.size()) {
            const QString text = layout.records.at(at).text;
            if (isIdentifier(text)) {
                ++at;
                continue;
            }
            if (!isAddress(text)) break;
            row.addresses << at++;
        }
        layout.rows << row;
        i = at;
    }
    return layout;
}

QString pageRowName(const Layout &layout, const PageRow &row)
{
    if (row.name < 0 || row.name >= layout.records.size()) return QString();
    return layout.records.at(row.name).text;
}

QString pageRowLabel(const Layout &layout, const PageRow &row)
{
    if (row.fragments.isEmpty()) return QString();
    return anchorLabel(layout.records.at(row.fragments.last()).text);
}

QString pageRowUrl(const Layout &layout, const PageRow &row)
{
    if (row.addresses.isEmpty()) return QString();
    const QString text = layout.records.at(row.addresses.last()).text;
    return text.trimmed() == QLatin1String(" ") ? QString() : text;
}

bool pageRowHidden(const Layout &layout, const PageRow &row)
{
    if (row.fragments.isEmpty()) return true;
    for (const int i : row.fragments)
        if (anchorLabel(layout.records.at(i).text).trimmed() != QLatin1String(" "))
            return false;
    return true;
}

// The sentence the page prints: the literal, plus the "Terms of use" link the
// page appends to it when that link is still enabled.
QString layoutText(const Layout &layout)
{
    if (layout.about < 0 || layout.about + 1 >= layout.records.size()) return QString();
    QString text = layout.records.at(layout.about + 1).text;
    const int anchor = layout.about + 2;
    const int label = layout.about + 3;
    if (anchor < layout.records.size() && label < layout.records.size()
        && layout.records.at(anchor).text.startsWith(QLatin1String("<a href="))
        && layout.records.at(label).text.trimmed() != QLatin1String(" "))
        text += layout.records.at(label).text;
    return text;
}

struct SlotEdit
{
    int index = 0;
    QString text;
};

// What a hidden caption becomes. One space, never an empty string: a zero
// length record cannot be read back, so it could not be restored either, and
// the row would stop being recognizable.
const QString kBlankSlot = QStringLiteral(" ");

// What a hidden anchor becomes: the tag stays (so the row is still a row on the
// next read), it points nowhere and prints one blank.
const QString kDeadAnchor = QStringLiteral("<a href=\"\">");

bool writeSlot(QByteArray &image, const Slot &slot, const QString &text, QString *error)
{
    const auto refuse = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (text.contains(QChar(0))) return refuse(QStringLiteral("A page text contains a NUL."));
    // A zero-length record cannot be read back (its own count says "no text"),
    // so an empty value is stored as the one blank every hidden slot uses.
    const QString value = text.isEmpty() ? kBlankSlot : text;
    if (value.size() > slot.maxChars())
        return refuse(QStringLiteral("%1 needs %2 characters but its slot on this player only "
                                     "holds %3; nothing was written.")
                          .arg(value.left(40))
                          .arg(value.size())
                          .arg(slot.maxChars()));
    QByteArray bytes;
    bytes.reserve(int(value.size()) * 2);
    for (int i = 0; i < value.size(); ++i) {
        const ushort unit = value.at(i).unicode();
        bytes.append(char(unit & 0xff));
        bytes.append(char((unit >> 8) & 0xff));
    }
    image.replace(slot.textAt, bytes.size(), bytes);
    const int tail = slot.end - (slot.textAt + bytes.size());
    if (tail > 0) {
        const int from = slot.textAt + bytes.size();
        if (!ByteUtils::inRange(image, from, tail)) return refuse(QStringLiteral("Slot out of range."));
        image.replace(from, tail, QByteArray(tail, '\0'));
    }
    // The record's own length: without it the reader keeps the stock count and
    // prints the padding as part of the sentence.
    if (!ByteUtils::inRange(image, slot.marker + 4, 4))
        return refuse(QStringLiteral("Slot header out of range."));
    ByteUtils::putLe32(image, slot.marker + 4, quint32(value.size()));
    return true;
}

// " - <a href="…">label</a>": the row's anchor with a new label, keeping the
// href it already has (or the stock one when it has none - an unhidden row must
// be clickable again).
QString fragmentWith(const QString &current, const QString &label, bool hidden)
{
    const int close = current.lastIndexOf(QLatin1String("</a>"));
    const int open = close < 0 ? -1 : current.indexOf(QLatin1Char('>'));
    QString head = open > 0 ? current.left(open + 1) : QString();
    if (head.isEmpty()) head = QStringLiteral(" - <a href=\"javascript:void(0)\">");
    if (hidden) return QStringLiteral(" - ") + kDeadAnchor + QLatin1Char(' ') + QStringLiteral("</a>");
    const int hrefAt = head.indexOf(QLatin1String("href=\""));
    if (hrefAt >= 0) {
        const int from = hrefAt + 6;
        const int to = head.indexOf(QLatin1Char('"'), from);
        if (to == from)
            head = head.left(from) + QStringLiteral("javascript:void(0)") + head.mid(to);
    }
    const int gt = head.lastIndexOf(QLatin1Char('>'));
    return (gt > 0 ? head.left(gt + 1) : head) + label + QStringLiteral("</a>");
}

// The edits that make the compiled page say what the config says.
//
// Rows are matched by the position they hold in the page (the config stores it
// as the link id), then - for a config written before that existed - by name,
// address or printed label. Position first is what makes this idempotent: a row
// that was renamed or hidden is still the same row, so a second apply finds
// nothing left to change.
void planTable(const Layout &layout, const Config &config, QList<SlotEdit> *edits,
               QStringList *missing)
{
    QSet<int> touched;
    QSet<int> used;
    const auto set = [&](int index, const QString &text) {
        if (index < 0 || index >= layout.records.size() || touched.contains(index)) return;
        if (layout.records.at(index).text == text) return;
        touched.insert(index);
        *edits << SlotEdit { index, text };
    };

    // The update button is switched off: its caption goes, so a stray click
    // opens an empty confirmation instead of a download.
    const int stop = layout.about >= 0 ? layout.about + 4 : layout.records.size();
    for (int i = 0; i < layout.records.size() && i < stop; ++i) {
        const QString text = layout.records.at(i).text;
        if (text == QLatin1String("Check for updates") || text == QLatin1String("Download Update"))
            set(i, kBlankSlot);
    }

    if (layout.about >= 0) {
        const int sentence = layout.about + 1;
        const int anchor = layout.about + 2;
        const int label = layout.about + 3;
        const QString labelText =
            label < layout.records.size() ? layout.records.at(label).text : QString();
        const bool keepsLink = labelText.trimmed() != QLatin1String(" ")
                               && !config.text.isEmpty()
                               && config.text.endsWith(labelText, Qt::CaseInsensitive);
        if (!config.text.isEmpty()) {
            if (keepsLink)
                set(sentence, config.text.left(config.text.size() - labelText.size()));
            else
                set(sentence, config.text);
        }
        if (!keepsLink && !config.text.isEmpty()) {
            // The edited text does not end with the link's words, so the line
            // must print exactly what was typed: the anchor stays (a row of the
            // page has to stay recognizable) with nothing in it.
            if (anchor < layout.records.size()
                && layout.records.at(anchor).text.startsWith(QLatin1String("<a href=")))
                set(anchor, kDeadAnchor);
            if (label < layout.records.size() && labelText.trimmed() != QLatin1String(" "))
                set(label, kBlankSlot);
        }
    }

    for (int n = 0; n < config.links.size(); ++n) {
        const Link &link = config.links.at(n);
        int pick = -1;
        // 1) The row this link was written for: its position in the page.
        if (link.id.startsWith(QLatin1String("row"))) {
            bool ok = false;
            const int index = link.id.mid(3).toInt(&ok);
            if (ok && index >= 0 && index < layout.rows.size() && !used.contains(index)) pick = index;
        }
        // 2) A config from before the ids were positions: the row that prints
        //    this name, then the one that opens this address.
        if (pick < 0 && !link.name.isEmpty()) {
            for (int r = 0; r < layout.rows.size() && pick < 0; ++r)
                if (!used.contains(r)
                    && pageRowName(layout, layout.rows.at(r)).compare(link.name, Qt::CaseInsensitive) == 0)
                    pick = r;
        }
        if (pick < 0 && !link.url.isEmpty()) {
            for (int r = 0; r < layout.rows.size() && pick < 0; ++r)
                if (!used.contains(r)
                    && pageRowUrl(layout, layout.rows.at(r)).compare(link.url, Qt::CaseInsensitive) == 0)
                    pick = r;
        }
        if (pick < 0 && !link.label.isEmpty()) {
            for (int r = 0; r < layout.rows.size() && pick < 0; ++r)
                if (!used.contains(r)
                    && pageRowLabel(layout, layout.rows.at(r)).compare(link.label, Qt::CaseInsensitive) == 0)
                    pick = r;
        }
        // 3) The rows and the links are in the same order on every build seen so
        //    far, which is the last resort.
        for (int r = 0; r < layout.rows.size() && pick < 0; ++r)
            if (!used.contains(r)) pick = r;
        if (pick < 0) {
            if (!link.hidden && missing) *missing << link.name;
            continue;
        }
        used.insert(pick);
        const PageRow &row = layout.rows.at(pick);
        const bool hidden = link.hidden;
        if (row.name >= 0)
            set(row.name, hidden ? kBlankSlot
                                 : (link.name.isEmpty() ? layout.records.at(row.name).text
                                                        : link.name));
        const QString shown = link.label.trimmed().isEmpty() ? displayFor(link.url) : link.label;
        for (int i = 0; i < row.fragments.size(); ++i) {
            const int fragment = row.fragments.at(i);
            const QString current = layout.records.at(fragment).text;
            const QString next = hidden ? fragmentWith(current, QString(), true)
                                        : fragmentWith(current, shown, false);
            set(fragment, next);
            if (i >= row.addresses.size()) continue;
            // The address slot prints the words when the anchor above it only
            // points at them (the e-mail row), and is hidden from the page when
            // the anchor already prints them. Both are written, each with the
            // value it is actually for.
            const int address = row.addresses.at(i);
            const bool prints = layout.records.at(address).text.compare(
                                    anchorLabel(current), Qt::CaseInsensitive) == 0;
            if (hidden) {
                // A hidden row prints nothing at all: the record the page shows
                // is blanked. The record it merely opens is left alone - it is
                // not displayed, the anchor above it is already dead, and
                // keeping it is what lets the row be switched back on with its
                // address intact.
                if (prints) set(address, kBlankSlot);
            } else if (prints) {
                set(address, shown.isEmpty() ? kBlankSlot : shown);
            } else if (!link.url.isEmpty()) {
                set(address, link.url);
            }
        }
        for (int i = row.fragments.size(); i < row.addresses.size(); ++i) {
            if (hidden) continue;
            const int address = row.addresses.at(i);
            if (!link.url.isEmpty()) set(address, link.url);
        }
    }
}

bool applyTable(QByteArray &image, const Layout &layout, const QList<SlotEdit> &edits,
                int *written, QString *error)
{
    int count = 0;
    for (const SlotEdit &edit : edits) {
        if (edit.index < 0 || edit.index >= layout.records.size()) continue;
        if (!writeSlot(image, layout.records.at(edit.index), edit.text, error)) return false;
        ++count;
    }
    if (written) *written = count;
    return true;
}

// The page the editors read. A player that embeds the page twice answers with
// the copy that carries our config line when there is one, so the editors and
// analyze() always describe the content that was applied.
bool findDoc(const QByteArray &image, const QString &name, Doc *out, QString *error)
{
    QList<Doc> docs;
    if (!findDocs(image, name, &docs, error)) {
        // Nothing came out of the tree: the page may still be there.
        if (findDocByBytes(image, out)) return true;
        return false;
    }
    if (docs.isEmpty()) {
        if (error) *error = QStringLiteral("%1 is not embedded in this player.").arg(name);
        return false;
    }
    for (const auto &doc : docs) {
        if (doc.qml.contains(kConfigBegin)) {
            *out = doc;
            return true;
        }
    }
    *out = docs.first();
    return true;
}

bool applyEdits(QString &qml, QList<Edit> edits)
{
    std::sort(edits.begin(), edits.end(),
              [](const Edit &a, const Edit &b) { return a.begin > b.begin; });
    int boundary = qml.size();
    for (const auto &edit : edits) {
        if (edit.begin < 0 || edit.begin + edit.length > boundary) return false;
        qml.replace(edit.begin, edit.length, edit.replacement);
        boundary = edit.begin;
    }
    return true;
}

bool directChild(const QString &masked, int parentOpen, int childBegin)
{
    int depth = 0;
    for (int i = parentOpen + 1; i < childBegin; ++i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}')) --depth;
    }
    return depth == 0;
}

bool directBinding(const QString &masked, int open, int end, const QString &name,
                   QmlText::Binding *out)
{
    int from = open;
    while (from < end) {
        QmlText::Binding binding;
        if (!QmlText::binding(masked, from, end, name, &binding)) return false;
        if (directChild(masked, open, binding.nameBegin)) {
            *out = binding;
            return true;
        }
        from = binding.nameBegin + name.size();
    }
    return false;
}

int matchingBrace(const QString &masked, int open)
{
    int depth = 0;
    for (int i = open; i < masked.size(); ++i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}') && --depth == 0) return i + 1;
    }
    return -1;
}

// The innermost object whose braces enclose `pos`.
bool enclosingObject(const QString &masked, int pos, QmlText::Object *object)
{
    // Nearest brace still open at `pos` (walking backwards). A forward scan
    // that remembers the last '{' returns an already-closed sibling, which
    // breaks parent lookup for objects declared after a nested child.
    int depth = 0;
    int open = -1;
    for (int i = qMin(pos, int(masked.size())) - 1; i >= 0; --i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('}')) ++depth;
        else if (c == QLatin1Char('{')) {
            if (depth == 0) { open = i; break; }
            --depth;
        }
    }
    if (open < 0) return false;
    if (matchingBrace(masked, open) <= pos) return false;
    int nameEnd = open;
    while (nameEnd > 0 && masked.at(nameEnd - 1).isSpace()) --nameEnd;
    int nameBegin = nameEnd;
    while (nameBegin > 0 && (masked.at(nameBegin - 1).isLetterOrNumber()
                             || masked.at(nameBegin - 1) == QLatin1Char('_')
                             || masked.at(nameBegin - 1) == QLatin1Char('.')))
        --nameBegin;
    if (nameBegin == nameEnd) return false;
    const int close = matchingBrace(masked, open);
    if (close <= open) return false;
    *object = QmlText::Object { nameBegin, open, close };
    return true;
}

bool findObjectById(const QString &qml, const QString &masked, const QString &id,
                    QmlText::Object *out)
{
    const QRegularExpression rx(QStringLiteral("\\bid\\s*:\\s*")
                                + QRegularExpression::escape(id) + QStringLiteral("\\b"));
    auto iter = rx.globalMatch(masked);
    while (iter.hasNext()) {
        const auto match = iter.next();
        QmlText::Object object;
        if (!enclosingObject(masked, int(match.capturedStart()), &object)) continue;
        QmlText::Binding binding;
        if (!directBinding(masked, object.open, object.end, QStringLiteral("id"), &binding))
            continue;
        if (qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin) != id) continue;
        *out = object;
        return true;
    }
    return false;
}

// The object that directly contains `object`.
bool parentObject(const QString &masked, const QmlText::Object &object, QmlText::Object *parent)
{
    return enclosingObject(masked, object.nameBegin, parent);
}

// Walk up from `object` until its parent is the About content column. Returns
// the direct child of that column which holds the object (the section block).
bool sectionOf(const QString &masked, const QmlText::Object &content,
               const QmlText::Object &object, QmlText::Object *section)
{
    QmlText::Object current = object;
    for (int guard = 0; guard < 64; ++guard) {
        QmlText::Object parent;
        if (!parentObject(masked, current, &parent)) return false;
        if (parent.open == content.open && parent.end == content.end) {
            *section = current;
            return true;
        }
        if (parent.open <= content.open || parent.end > content.end) return false;
        current = parent;
    }
    return false;
}

// The About page always has a content column; a build that renamed its id is
// still handled by taking the first column layout inside the page's scroll
// view.
bool findContent(const QString &qml, const QString &masked, QmlText::Object *content)
{
    if (findObjectById(qml, masked, QStringLiteral("iContentLayout"), content)) return true;
    for (const auto &view : QmlText::objects(masked, QStringLiteral("UiScrollView"))) {
        for (const auto &column : QmlText::objects(masked, QStringLiteral("UiColumnLayout"))) {
            if (column.nameBegin <= view.open || column.end > view.end) continue;
            if (!directChild(masked, view.open, column.nameBegin)) continue;
            *content = column;
            return true;
        }
    }
    return false;
}

// The "Check for updates" / "Download Update" button. It is located through
// its own text/onClicked body, not through a wrapper, because both known page
// generations place it in a different column.
bool findUpdateButton(const QString &qml, const QString &masked, const QmlText::Object &content,
                      QmlText::Object *out)
{
    for (const auto &button : QmlText::objects(masked, QStringLiteral("UiButton"))) {
        if (button.open <= content.open || button.end > content.end) continue;
        const QString body = qml.mid(button.open, button.end - button.open);
        if (body.contains(QLatin1String("checkForUpdate"))
            || body.contains(QLatin1String("Check for updates"))
            || body.contains(QLatin1String("Download Update"))
            || body.contains(QLatin1String("pUpdateAvailable"))) {
            *out = button;
            return true;
        }
    }
    return false;
}

// Stock rows that only exist to offer an update or a BlueStacks help page:
// the "available · More info" line and the incompatible-update warning. Both
// carry a stock link that must not survive next to the custom one.
QList<QmlText::Object> findUpdateInfoRows(const QString &qml, const QString &masked,
                                          const QmlText::Object &content,
                                          const QList<QmlText::Object> &keep)
{
    QList<QmlText::Object> out;
    for (const auto &row : QmlText::objects(masked, QStringLiteral("UiTextUrl"))) {
        if (row.open <= content.open || row.end > content.end) continue;
        const QString body = qml.mid(row.open, row.end - row.open);
        if (!body.contains(QLatin1String("href"))) continue;
        if (!body.contains(QLatin1String("pMoreInfoURL"))
            && !body.contains(QLatin1String("incompatible_update_version")))
            continue;
        bool skipped = false;
        for (const auto &object : keep)
            if (object.open == row.open && object.end == row.end) skipped = true;
        if (!skipped) out << row;
    }
    return out;
}

// The "联系我们 / QQ" row only exists in the Chinese build, but it carries a
// second stock link there and must go as well. Both the outer row and the
// inner button row are returned: hiding only the inner one would leave the
// "联系我们:" label behind.
QList<QmlText::Object> findQqRows(const QString &qml, const QString &masked,
                                  const QmlText::Object &links)
{
    QList<QmlText::Object> out;
    for (const auto &type : { QStringLiteral("UiRowLayout"), QStringLiteral("RowLayout"),
                              QStringLiteral("Row"), QStringLiteral("UiRow"),
                              QStringLiteral("Item") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.open <= links.open || object.end > links.end) continue;
            const QString body = qml.mid(object.open, object.end - object.open);
            if (!body.contains(QLatin1String("openQQSupportUrl"))
                && !body.contains(QLatin1String("jq.qq.com"))
                && !body.contains(QString::fromUtf8("联系我们")))
                continue;
            bool seen = false;
            for (const auto &old : out)
                if (old.open == object.open && old.end == object.end) seen = true;
            if (!seen) out << object;
        }
    }
    return out;
}

QString memberIndent(const QString &qml, const QmlText::Object &object)
{
    const int lineStart = qml.lastIndexOf(QLatin1Char('\n'), object.nameBegin - 1) + 1;
    QString indent;
    int i = lineStart;
    while (i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
        indent += qml.at(i++);
    return indent;
}

// Where a `visible: false` member can be inserted into `object`, and the exact
// text written there. Multi-line objects get a whole line at the first
// member's indentation (no extra blank line); an object written on one line
// gets the member inline with a `;` separator and a block comment, so the
// remaining members are neither split nor swallowed by a line comment.
bool hiddenMemberSite(const QString &qml, const QmlText::Object &object, int *at,
                      QString *text)
{
    const QString nl = lineEnding(qml);
    int probe = object.open + 1;
    while (probe < qml.size() && (qml.at(probe) == QLatin1Char(' ') || qml.at(probe) == QLatin1Char('\t')))
        ++probe;
    const bool firstMemberOnNewLine = probe < object.end
        && (qml.at(probe) == QLatin1Char('\n') || qml.at(probe) == QLatin1Char('\r'));
    if (firstMemberOnNewLine) {
        const int newline = qml.indexOf(QLatin1Char('\n'), probe);
        if (newline < 0 || newline >= object.end) return false;
        const int lineStart = newline + 1;
        QString indent;
        int i = lineStart;
        while (i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
            indent += qml.at(i++);
        *at = lineStart;
        *text = indent + kHiddenLine + nl;
        return true;
    }
    // The object shares its opening line with its members (`UiButton { id: …`),
    // so a newline insertion would split that line. Add the member inline with
    // a separator and a block comment instead; the inline form is removed by
    // the same byte-exact site match in showObject().
    *at = object.open + 1;
    *text = QStringLiteral(" ") + kHiddenInline + QStringLiteral(";");
    return true;
}

QString tagged(const QString &value, const QString &original, const QString &marker)
{
    return value + marker + QString::fromLatin1(original.toUtf8().toBase64())
        + QStringLiteral(" */");
}

// The trailing comment that follows a patched value on the same line; the
// binding scanner trims comments, so it must be read from the source text.
bool taggedAfter(const QString &qml, int valueBegin, const QString &marker,
                 QString *original, int *tagEnd)
{
    const int at = qml.indexOf(marker, valueBegin);
    if (at < 0) return false;
    if (qml.mid(valueBegin, at - valueBegin).contains(QLatin1Char('\n'))) return false;
    const int close = qml.indexOf(QLatin1String("*/"), at + marker.size());
    if (close < 0) return false;
    const QByteArray encoded = qml.mid(at + marker.size(), close - at - marker.size())
                                   .trimmed().toLatin1();
    const QByteArray bytes = QByteArray::fromBase64(encoded);
    if (bytes.isEmpty() || bytes.toBase64() != encoded) return false;
    if (QString::fromUtf8(bytes).toUtf8() != bytes) return false;
    *original = QString::fromUtf8(bytes);
    *tagEnd = close + 2;
    return true;
}

// Hide one object: wrap an existing `visible` binding, or add a plain false
// member when the stock object has none.
bool hideObject(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Binding visible;
    if (directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible)) {
        const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
        if (value.startsWith(kHiddenWrap)) return false;
        addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                kHiddenWrap + value + QStringLiteral(")"));
    } else {
        int at = 0;
        QString text;
        if (!hiddenMemberSite(qml, object, &at, &text)) return false;
        addEdit(edits, at, 0, text);
    }
    return true;
}

bool showObject(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    int at = 0;
    QString inserted;
    if (!hiddenMemberSite(qml, object, &at, &inserted)) return false;
    if (qml.mid(at, inserted.size()) == inserted) {
        addEdit(edits, at, inserted.size(), QString());
        return true;
    }
    QmlText::Binding visible;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible))
        return false;
    const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
    if (value.startsWith(kHiddenWrap) && value.endsWith(QLatin1Char(')'))) {
        addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                value.mid(kHiddenWrap.size(), value.size() - kHiddenWrap.size() - 1));
        return true;
    }
    return false;
}

// Force a section visible even on a build that hides it (`oem !== "msi5"`).
bool forceShown(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Binding visible;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible))
        return false; // no binding: the section is already visible
    const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
    if (value == QLatin1String("true")) return false;
    addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
            tagged(QStringLiteral("true"), value, kShownTag));
    return true;
}

bool restoreShown(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Binding visible;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible))
        return false;
    const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
    if (value != QLatin1String("true")) return false;
    QString original;
    int tagEnd = -1;
    if (!taggedAfter(qml, visible.valueBegin, kShownTag, &original, &tagEnd)) return false;
    addEdit(edits, visible.valueBegin, tagEnd - visible.valueBegin, original);
    return true;
}

QString escapeQml(const QString &value)
{
    QString out;
    out.reserve(value.size() + 16);
    for (const QChar c : value) {
        switch (c.unicode()) {
        case '\\': out += QLatin1String("\\\\"); break;
        case '"': out += QLatin1String("\\\""); break;
        case '\n': out += QLatin1String("\\n"); break;
        case '\r': break;
        case '\t': out += QLatin1String("\\t"); break;
        default: out += c;
        }
    }
    return out;
}

QString escapeHtml(const QString &value)
{
    QString out;
    out.reserve(value.size() + 16);
    for (const QChar c : value) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        case '\'': out += QLatin1String("&#39;"); break;
        case '\n': out += QLatin1String("<br/>"); break;
        case '\r': break;
        case '\t': out += QLatin1String("&nbsp;&nbsp;&nbsp;&nbsp;"); break;
        default: out += c;
        }
    }
    return out;
}

QString configLine(const Config &config)
{
    QJsonObject object;
    object.insert(QStringLiteral("text"), config.text);
    object.insert(QStringLiteral("show"), config.showSections);
    QJsonArray links;
    for (const auto &link : config.links) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), link.id);
        entry.insert(QStringLiteral("name"), link.name);
        entry.insert(QStringLiteral("url"), link.url);
        entry.insert(QStringLiteral("label"), link.label);
        entry.insert(QStringLiteral("hidden"), link.hidden);
        links.append(entry);
    }
    object.insert(QStringLiteral("links"), links);
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return kConfigBegin + QString::fromLatin1(json.toBase64());
}

bool readConfig(const QString &qml, Config *config, bool *hasLinksField)
{
    const int at = qml.indexOf(kConfigBegin);
    if (at < 0) return false;
    int end = qml.indexOf(QLatin1Char('\n'), at);
    if (end < 0) end = qml.size();
    QString encoded = qml.mid(at + kConfigBegin.size(), end - at - kConfigBegin.size());
    encoded.remove(QLatin1Char('\r'));
    encoded = encoded.trimmed();
    const QByteArray latin = encoded.toLatin1();
    const QByteArray bytes = QByteArray::fromBase64(latin);
    if (bytes.isEmpty() || bytes.toBase64() != latin) return false;
    QJsonParseError error {};
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object();
    if (hasLinksField) *hasLinksField = object.contains(QStringLiteral("links"));
    Config out;
    out.text = object.value(QStringLiteral("text")).toString();
    out.showSections = object.value(QStringLiteral("show")).toBool(true);
    for (const auto &value : object.value(QStringLiteral("links")).toArray()) {
        const auto entry = value.toObject();
        Link link;
        link.id = entry.value(QStringLiteral("id")).toString();
        link.name = entry.value(QStringLiteral("name")).toString();
        link.url = entry.value(QStringLiteral("url")).toString();
        // Configs written before the display text existed have no label: the
        // anchor printed the address, and that is what an empty label means.
        link.label = entry.value(QStringLiteral("label")).toString();
        link.hidden = entry.value(QStringLiteral("hidden")).toBool();
        if (!link.id.isEmpty()) out.links << link;
    }
    *config = out;
    return true;
}

// The address a row opens. A bare e-mail address keeps the stock "Support
// email" row working after it is edited, and a bare domain becomes a real URL
// instead of a dead Qt.openUrlExternally call.
QString targetUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.contains(QLatin1Char(':'))) return trimmed;
    if (trimmed.contains(QLatin1Char('@'))) return QStringLiteral("mailto:") + trimmed;
    if (trimmed.contains(QLatin1Char('.')) && !trimmed.contains(QLatin1Char(' ')))
        return QStringLiteral("https://") + trimmed;
    return trimmed;
}

bool sameLink(const Link &a, const Link &b)
{
    return a.id == b.id && a.name == b.name && a.url == b.url && a.label == b.label
        && a.hidden == b.hidden;
}

// The row a saved config holds for this page row: by id when the config names
// one, otherwise by the position the row had when it was written.
Link savedLink(const Config &saved, const QString &id, int index)
{
    if (!id.isEmpty())
        for (const Link &link : saved.links)
            if (link.id == id) return link;
    return index >= 0 && index < saved.links.size() ? saved.links.at(index) : Link();
}

bool sameConfig(const Config &a, const Config &b)
{
    if (a.text != b.text || a.showSections != b.showSections || a.links.size() != b.links.size())
        return false;
    for (int i = 0; i < a.links.size(); ++i)
        if (!sameLink(a.links.at(i), b.links.at(i))) return false;
    return true;
}

Config normalized(Config config)
{
    config.text = config.text.trimmed();
    for (auto &link : config.links) {
        link.name = link.name.trimmed();
        link.url = link.url.trimmed();
        // Store the form the page ends up with, so reading it back matches
        // what was applied.
        link.url = targetUrl(link.url);
        link.label = link.label.trimmed();
        // The anchor prints the edited text when there is one, otherwise the
        // address itself: one value, so a read-back always matches.
        if (link.label.isEmpty()) link.label = displayFor(link.url);
        // A row with nothing to point at and nothing to say is a hidden row.
        if (link.url.isEmpty() && link.name.isEmpty()) link.hidden = true;
        if (link.name.isEmpty()) link.name = link.url;
    }
    return config;
}

// ------------------------------------------------- what the page shows right now
// The About editors are prefilled with the values the page has at this moment,
// so the stock text/link can be edited instead of retyped. Reading them needs
// no QML engine: a text expression is flattened to the strings it can render
// (`qsTranslate("c", "x")` -> `x`, literals decoded), and the markup the stock
// page uses is turned back into plain text.

// The index just past the ')' that closes the '(' at `open`, quote-aware.
int closingParen(const QString &text, int open)
{
    int depth = 0;
    QChar quote;
    for (int i = open; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!quote.isNull()) {
            if (c == QLatin1Char('\\')) { ++i; continue; }
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char('(')) ++depth;
        else if (c == QLatin1Char(')') && --depth == 0) return i + 1;
    }
    return -1;
}

QString decodeQmlString(const QString &raw)
{
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c != QLatin1Char('\\')) {
            out += c;
            continue;
        }
        if (++i >= raw.size()) break;
        const QChar escaped = raw.at(i);
        switch (escaped.unicode()) {
        case 'n': out += QLatin1Char('\n'); break;
        case 'r': out += QLatin1Char('\r'); break;
        case 't': out += QLatin1Char('\t'); break;
        default: out += escaped; break;
        }
    }
    return out;
}

// Every string a QML text expression can contribute, in source order. Calls
// such as `qsTranslate("QObject", "Terms of use")` contribute their text
// argument only; other identifiers and operators are skipped.
QStringList renderedChunks(const QString &expression)
{
    QStringList out;
    const int n = expression.size();
    int i = 0;
    while (i < n) {
        const QChar c = expression.at(i);
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar quote = c;
            QString raw;
            int j = i + 1;
            while (j < n && expression.at(j) != quote) {
                if (expression.at(j) == QLatin1Char('\\') && j + 1 < n) raw += expression.at(j++);
                raw += expression.at(j++);
            }
            out << decodeQmlString(raw);
            i = j + 1;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (expression.at(j).isLetterOrNumber()
                             || expression.at(j) == QLatin1Char('_')))
                ++j;
            const QString name = expression.mid(i, j - i);
            int open = j;
            while (open < n && expression.at(open).isSpace()) ++open;
            const bool translated = name == QLatin1String("qsTranslate")
                || name == QLatin1String("qsTr") || name == QLatin1String("qsTrId");
            if (translated && open < n && expression.at(open) == QLatin1Char('(')) {
                const int close = closingParen(expression, open);
                if (close > 0) {
                    // `close` is one past the ')', so the arguments are the
                    // characters strictly between it and the '('.
                    const QString arguments = expression.mid(open + 1, close - open - 2);
                    // Quote-aware split, so a comma inside the text is kept.
                    QStringList parts;
                    QString part;
                    QChar active;
                    for (int k = 0; k < arguments.size(); ++k) {
                        const QChar d = arguments.at(k);
                        if (!active.isNull()) {
                            part += d;
                            if (d == QLatin1Char('\\') && k + 1 < arguments.size())
                                part += arguments.at(++k);
                            else if (d == active) active = QChar();
                            continue;
                        }
                        if (d == QLatin1Char('"') || d == QLatin1Char('\'')) active = d;
                        if (d == QLatin1Char(',')) { parts << part; part.clear(); continue; }
                        part += d;
                    }
                    parts << part;
                    // The words to show: `qsTranslate("ctx", "text")` keeps its
                    // text in the second argument, `qsTr("text")` and
                    // `qsTrId("id")` in the first. A trailing argument (a plural
                    // count, a disambiguation string) is not part of the text.
                    QStringList literals;
                    for (const auto &candidate : parts) {
                        const QString value = candidate.trimmed();
                        if (value.size() >= 2
                            && (value.front() == QLatin1Char('"')
                                || value.front() == QLatin1Char('\''))
                            && value.back() == value.front())
                            literals << value.mid(1, value.size() - 2);
                    }
                    const int messageIndex = name == QLatin1String("qsTranslate") ? 1 : 0;
                    if (literals.size() > messageIndex)
                        out << decodeQmlString(literals.at(messageIndex));
                    i = close;
                    continue;
                }
            }
            i = j;
            continue;
        }
        ++i;
    }
    return out;
}

QString renderedText(const QString &expression)
{
    return renderedChunks(expression).join(QString());
}

QString htmlToPlain(const QString &html)
{
    QString out;
    out.reserve(html.size());
    bool inTag = false;
    for (const QChar c : html) {
        if (inTag) {
            if (c == QLatin1Char('>')) inTag = false;
            continue;
        }
        if (c == QLatin1Char('<')) {
            inTag = true;
            continue;
        }
        out += c;
    }
    out.replace(QLatin1String("&nbsp;"), QStringLiteral(" "));
    out.replace(QLatin1String("&lt;"), QStringLiteral("<"));
    out.replace(QLatin1String("&gt;"), QStringLiteral(">"));
    out.replace(QLatin1String("&quot;"), QStringLiteral("\""));
    out.replace(QLatin1String("&#39;"), QStringLiteral("'"));
    out.replace(QLatin1String("&#039;"), QStringLiteral("'"));
    out.replace(QLatin1String("&amp;"), QStringLiteral("&"));
    return out;
}

// The text of the last `<a …>label</a>` a row can render. The stock rows read
// `cond ? " - <a…>cn</a>" : " - <a…>com</a>"`, and the second branch is the
// one this build shows outside China.
QString lastAnchorLabel(const QString &rendered)
{
    static const QRegularExpression rx(QStringLiteral(">([^<>]*)</a\\s*>"),
                                       QRegularExpression::CaseInsensitiveOption);
    QString label;
    auto iter = rx.globalMatch(rendered);
    while (iter.hasNext()) label = iter.next().captured(1);
    return htmlToPlain(label).trimmed();
}

// Where a stock link row sends the user: the URL of its own handler first
// (`Qt.openUrlExternally("…")`), otherwise the anchor's href when it is a real
// address instead of the stock `javascript:void(0)` placeholder.
QString shownLinkUrl(const QString &handler, const QString &rendered)
{
    static const QRegularExpression call(
        QStringLiteral("Qt\\s*\\.\\s*openUrlExternally\\s*\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    QString url;
    auto calls = call.globalMatch(handler);
    while (calls.hasNext()) url = decodeQmlString(calls.next().captured(1));
    if (!url.isEmpty()) return url;
    static const QRegularExpression href(QStringLiteral("href\\s*=\\s*\"([^\"]*)\""),
                                         QRegularExpression::CaseInsensitiveOption);
    auto links = href.globalMatch(rendered);
    while (links.hasNext()) {
        const QString candidate = decodeQmlString(links.next().captured(1)).trimmed();
        if (candidate.isEmpty() || candidate.startsWith(QLatin1String("javascript:"))
            || candidate == QLatin1String("#"))
            continue;
        url = candidate;
    }
    return url;
}

// One stock link row, located and read from the document.
struct Row
{
    QString id;
    QmlText::Object object;
    QmlText::Binding text;
    QmlText::Binding handler;
    bool hasHandler = false;
};

// The About columns and the rows inside them.
struct Anchors
{
    QmlText::Object content;
    QmlText::Object aboutBlock;
    QmlText::Object linksBlock;
    QmlText::Object aboutText;
    QList<QmlText::Object> updateInfoRows;
    QList<QmlText::Object> qqRows;
    QList<Row> links;
    QmlText::Object updateButton;
    QmlText::Binding aboutTextBinding;
    bool sectionsHidden = false;
    bool ok = false;
};

bool sameObject(const QmlText::Object &a, const QmlText::Object &b)
{
    return a.open == b.open && a.end == b.end;
}

// True when the build's own binding can keep the section switched off, i.e.
// the MSI shape `visible: uiBackend.oem !== "msi5"`. A plain `true` (or no
// binding) is already shown and needs no unhide.
bool hidesItself(const QString &masked, const QmlText::Object &object)
{
    QmlText::Binding visible;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible))
        return false;
    return masked.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin).trimmed()
        != QLatin1String("true");
}

// The stock link rows, in the order the page lays them out. A build may have
// fewer of them; the ones it has are the ones the editor shows.
const QStringList &stockLinkIds()
{
    static const QStringList ids {
        QStringLiteral("iSupportEmailText"), QStringLiteral("iSupportText"),
        QStringLiteral("iWebsiteText")
    };
    return ids;
}

// The About/copyright line by what it says, for builds that renamed the object
// (or dropped the id). Only a row whose *rendered* text carries the copyright
// marker counts, so the update banner and the link rows are never mistaken for
// the About text.
bool findAboutTextByContent(const QString &qml, const QString &masked, QmlText::Object *out)
{
    static const QRegularExpression marker(
        QStringLiteral("(©|\\bcopyright\\b|rights reserved)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const auto &object : QmlText::objects(masked, QStringLiteral("UiTextUrl"))) {
        QmlText::Binding text;
        if (!directBinding(masked, object.open, object.end, QStringLiteral("text"), &text))
            continue;
        const QString value = qml.mid(text.valueBegin, text.valueEnd - text.valueBegin);
        if (marker.match(renderedText(value)).hasMatch()) {
            *out = object;
            return true;
        }
    }
    return false;
}

Anchors findAnchors(const QString &qml, const QString &masked, QString *why)
{
    Anchors anchors;
    if (!findContent(qml, masked, &anchors.content)) {
        if (why) *why = QStringLiteral("This player's About page has no replaceable content column.");
        return anchors;
    }
    if (!findObjectById(qml, masked, QStringLiteral("iTermsOfUseText"), &anchors.aboutText)
        && !findAboutTextByContent(qml, masked, &anchors.aboutText)) {
        if (why) *why = QStringLiteral("This player's About page has no replaceable About text line.");
        return anchors;
    }
    if (!directBinding(masked, anchors.aboutText.open, anchors.aboutText.end,
                       QStringLiteral("text"), &anchors.aboutTextBinding)) {
        if (why) *why = QStringLiteral("The About text line is not a text field; nothing would be written.");
        return anchors;
    }
    if (!sectionOf(masked, anchors.content, anchors.aboutText, &anchors.aboutBlock)) {
        if (why) *why = QStringLiteral("The About text is not inside the About content column.");
        return anchors;
    }
    for (const auto &id : stockLinkIds()) {
        Row row;
        row.id = id;
        if (!findObjectById(qml, masked, id, &row.object)) continue;
        if (!directBinding(masked, row.object.open, row.object.end,
                           QStringLiteral("text"), &row.text))
            continue;
        row.hasHandler = directBinding(masked, row.object.open, row.object.end,
                                       QStringLiteral("onLinkActivated"), &row.handler);
        anchors.links << row;
    }
    // A build whose link rows are shaped differently (renamed ids, a Repeater,
    // a new section) must still get the working text editor: the rows are only
    // skipped, they never take the whole card down. The card says so itself
    // ("No stock link rows in this player") when the list comes back empty.
    if (!anchors.links.isEmpty()
        && sectionOf(masked, anchors.content, anchors.links.first().object, &anchors.linksBlock)) {
        anchors.qqRows = findQqRows(qml, masked, anchors.linksBlock);
    } else {
        anchors.links.clear();
        anchors.linksBlock = QmlText::Object();
    }
    findUpdateButton(qml, masked, anchors.content, &anchors.updateButton);
    QList<QmlText::Object> keep { anchors.aboutText };
    for (const auto &row : anchors.links) keep << row.object;
    anchors.updateInfoRows = findUpdateInfoRows(qml, masked, anchors.content, keep);
    anchors.sectionsHidden = hidesItself(masked, anchors.aboutBlock);
    if (anchors.linksBlock.open >= 0 && !sameObject(anchors.aboutBlock, anchors.linksBlock))
        anchors.sectionsHidden = anchors.sectionsHidden || hidesItself(masked, anchors.linksBlock);
    anchors.ok = true;
    return anchors;
}

// The name a row prints before its link ("Support email - <a…>"). That is the
// first string the text expression contributes; the branches after it only
// repeat the label with a different link.
QString rowName(const QString &expression)
{
    QString prefix = renderedChunks(expression).value(0);
    const int anchor = prefix.indexOf(QLatin1String("<a"), 0, Qt::CaseInsensitive);
    if (anchor >= 0) prefix = prefix.left(anchor);
    prefix = htmlToPlain(prefix).trimmed();
    while (prefix.endsWith(QLatin1Char('-')) || prefix.endsWith(QLatin1Char(':'))
           || prefix.endsWith(QLatin1Char(' ')))
        prefix.chop(1);
    return prefix.trimmed();
}

bool rowIsHidden(const QString &qml, const QmlText::Object &object)
{
    const QString body = qml.mid(object.open, object.end - object.open);
    // Both forms hideObject() can write: the member on its own line, the
    // wrapped existing binding, and the inline member used when the object's
    // opening brace shares its line with the members.
    return body.contains(kHiddenLine) || body.contains(kHiddenWrap)
        || body.contains(kHiddenInline);
}

// What the page says right now: the About text without its markup, and every
// stock link row with the name it prints and the URL it opens.
bool readShown(const QString &qml, const QString &masked, const Anchors &anchors, Config *config)
{
    Config out;
    QmlText::Binding text;
    if (anchors.aboutText.open >= 0
        && directBinding(masked, anchors.aboutText.open, anchors.aboutText.end,
                         QStringLiteral("text"), &text)) {
        const QString value = qml.mid(text.valueBegin, text.valueEnd - text.valueBegin);
        out.text = htmlToPlain(renderedText(value)).trimmed();
    }
    for (const auto &row : anchors.links) {
        Link link;
        link.id = row.id;
        const QString value = qml.mid(row.text.valueBegin, row.text.valueEnd - row.text.valueBegin);
        const QString rendered = renderedText(value);
        link.name = rowName(value);
        link.label = lastAnchorLabel(rendered);
        const QString handler = row.hasHandler
            ? qml.mid(row.handler.valueBegin, row.handler.valueEnd - row.handler.valueBegin)
            : QString();
        link.url = shownLinkUrl(handler, rendered);
        if (link.url.isEmpty()) {
            // Rows that open a mail client (`uiBackend.openEmail`) or a help
            // page carry no external URL: their printed address is the target.
            const QString label = lastAnchorLabel(rendered).trimmed();
            if (label.contains(QLatin1Char('@'))
                || (label.contains(QLatin1Char('.')) && !label.contains(QLatin1Char(' '))))
                link.url = label;
        }
        link.hidden = rowIsHidden(qml, row.object);
        if (link.name.isEmpty()) link.name = link.url;
        // The label is the text the row prints. When it is nothing but the
        // address - which is how the stock page prints every one of its links -
        // it is not a caption the user chose, so it is left empty: a replaced
        // URL must print its own address, not keep advertising the BlueStacks
        // one it just supplanted. `normalized()` fills it back from whatever
        // URL the editor ends up with.
        if (!link.url.isEmpty() && link.label == displayFor(link.url)) link.label.clear();
        out.links << link;
    }
    // The sections are always offered shown. `visible: uiBackend.oem !== "msi5"`
    // is hidden on MSI builds and already true on BlueStacks ones, so forcing
    // `visible: true` is the only value that is correct on every build - and
    // reading "a non-true binding exists" as "the user wants it left alone" is
    // what made the patch look like it did nothing on MSI installs.
    out.showSections = true;
    if (out.text.isEmpty() && out.links.isEmpty()) return false;
    *config = out;
    return true;
}

// The text a link row should print: the name, then the address the way the
// stock page prints it (no scheme, no "mailto:").
QString displayFor(const QString &url)
{
    QString shown = url.trimmed();
    if (shown.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive))
        shown = shown.mid(7);
    if (shown.startsWith(QLatin1String("http://"), Qt::CaseInsensitive))
        shown = shown.mid(7);
    else if (shown.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        shown = shown.mid(8);
    if (shown.endsWith(QLatin1Char('/'))) shown.chop(1);
    return shown;
}

QString linkLiteral(const Link &link)
{
    QString html;
    if (!link.name.isEmpty()) html += escapeHtml(link.name) + QStringLiteral(" - ");
    // The address and the words that print it are separate: `label` is what
    // the user typed for the anchor, the URL stays the target.
    const QString shown = link.label.trimmed().isEmpty() ? displayFor(link.url)
                                                         : link.label.trimmed();
    html += QStringLiteral("<a href=\"") + escapeHtml(targetUrl(link.url))
        + QStringLiteral("\">") + escapeHtml(shown) + QStringLiteral("</a>");
    return QLatin1Char('"') + escapeQml(html) + QLatin1Char('"');
}

// The config row that belongs to a .qml link object. A config written before
// the rows were named by position carries the object id (`iSupportText`); this
// version names them `row0`, `row1`, ... because that is what the compiled page
// can match on. The rows are in the same order in both copies of the page, so
// position is the fallback - and it is the one that keeps a renamed row in
// place instead of hiding every row on the page.
const Link *findLink(const Config &config, const QString &id, int index)
{
    for (const auto &link : config.links)
        if (link.id == id) return &link;
    return index >= 0 && index < config.links.size() ? &config.links.at(index) : nullptr;
}

// Rewrite a row's text and handler, or add the handler when the build has none.
void writeLink(const QString &qml, const Row &row, const Link &link, QList<Edit> &edits)
{
    const QString nl = lineEnding(qml);
    const QString original = qml.mid(row.text.valueBegin, row.text.valueEnd - row.text.valueBegin);
    addEdit(edits, row.text.valueBegin, row.text.valueEnd - row.text.valueBegin,
            tagged(linkLiteral(link), original, kTextTag));
    const QString call = QStringLiteral("Qt.openUrlExternally(\"")
        + escapeQml(targetUrl(link.url)) + QStringLiteral("\")");
    if (row.hasHandler) {
        const QString handlerOriginal =
            qml.mid(row.handler.valueBegin, row.handler.valueEnd - row.handler.valueBegin);
        addEdit(edits, row.handler.valueBegin,
                row.handler.valueEnd - row.handler.valueBegin,
                tagged(call, handlerOriginal, kHandlerTag));
        return;
    }
    // No handler in this build: add one above the text member, tagged so
    // restoring removes exactly these lines.
    const int lineStart = qml.lastIndexOf(QLatin1Char('\n'), row.text.nameBegin - 1) + 1;
    QString indent;
    int i = lineStart;
    while (i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
        indent += qml.at(i++);
    addEdit(edits, lineStart, 0,
            indent + kHandlerMarker + nl + indent
                + QStringLiteral("onLinkActivated: ") + call + nl);
}

bool planEnable(const QString &qml, const Config &config, QList<Edit> &edits, QString *error)
{
    const QString masked = QmlText::mask(qml);
    const Anchors anchors = findAnchors(qml, masked, error);
    if (!anchors.ok) return false;

    const bool hasText = !config.text.isEmpty();
    bool visibleLinks = false;
    for (const auto &link : config.links)
        if (!link.hidden && !link.url.isEmpty()) visibleLinks = true;
    if (!hasText && !visibleLinks && !config.showSections) {
        if (error) *error = QStringLiteral("Nothing to apply: the text is empty, every link is "
                                           "hidden and the sections stay as they are.");
        return false;
    }

    const bool haveLinksBlock = anchors.linksBlock.open >= 0;
    const bool sharedBlock = !haveLinksBlock || sameObject(anchors.aboutBlock, anchors.linksBlock);

    // ---- the two sections the MSI build hides ---------------------------
    if (config.showSections) {
        forceShown(qml, anchors.aboutBlock, edits);
        if (!sharedBlock) forceShown(qml, anchors.linksBlock, edits);
    } else {
        // Keep the build's own behaviour: undo a force this tool applied
        // earlier, and leave the stock binding alone.
        restoreShown(qml, anchors.aboutBlock, edits);
        if (!sharedBlock) restoreShown(qml, anchors.linksBlock, edits);
    }

    // ---- the About text --------------------------------------------------
    if (hasText) {
        const QString literal = QLatin1Char('"') + escapeQml(escapeHtml(config.text))
            + QLatin1Char('"');
        const QString original = qml.mid(anchors.aboutTextBinding.valueBegin,
                                         anchors.aboutTextBinding.valueEnd
                                             - anchors.aboutTextBinding.valueBegin);
        addEdit(edits, anchors.aboutTextBinding.valueBegin,
                anchors.aboutTextBinding.valueEnd - anchors.aboutTextBinding.valueBegin,
                tagged(literal, original, kTextTag));
    } else {
        hideObject(qml, anchors.aboutText, edits);
    }

    // ---- the stock links: rename, re-point or hide each one --------------
    for (int i = 0; i < anchors.links.size(); ++i) {
        const auto &row = anchors.links.at(i);
        const Link *link = findLink(config, row.id, i);
        const bool hidden = !link || link->hidden || link->url.isEmpty();
        if (hidden) {
            hideObject(qml, row.object, edits);
            continue;
        }
        showObject(qml, row.object, edits); // undo our own earlier hiding
        writeLink(qml, row, *link, edits);
    }
    for (const auto &row : anchors.qqRows) hideObject(qml, row, edits);

    // ---- the update button and its "More info" links: never offered again -
    if (anchors.updateButton.open >= 0) hideObject(qml, anchors.updateButton, edits);
    for (const auto &row : anchors.updateInfoRows) hideObject(qml, row, edits);

    addEdit(edits, qml.size(), 0, lineEnding(qml) + configLine(config));
    return true;
}

// Remove the lines this tool inserted for rows that had no handler.
void restoreAddedHandlers(const QString &qml, QList<Edit> &edits)
{
    for (int at = qml.indexOf(kHandlerMarker); at >= 0;
         at = qml.indexOf(kHandlerMarker, at + kHandlerMarker.size())) {
        const int start = qml.lastIndexOf(QLatin1Char('\n'), at - 1) + 1;
        const int firstEnd = qml.indexOf(QLatin1Char('\n'), at);
        int stop = firstEnd < 0 ? qml.size() : qml.indexOf(QLatin1Char('\n'), firstEnd + 1);
        if (stop < 0) stop = qml.size();
        else ++stop;
        addEdit(edits, start, stop - start, QString());
    }
}

bool planDisable(const QString &qml, QList<Edit> &edits, QString *error)
{
    const QString masked = QmlText::mask(qml);
    const Anchors anchors = findAnchors(qml, masked, error);
    if (!anchors.ok) return false;

    const bool haveLinksBlock = anchors.linksBlock.open >= 0;
    restoreShown(qml, anchors.aboutBlock, edits);
    if (haveLinksBlock && !sameObject(anchors.linksBlock, anchors.aboutBlock))
        restoreShown(qml, anchors.linksBlock, edits);
    // Rows/sections hidden when the text or every link was switched off.
    showObject(qml, anchors.aboutText, edits);
    showObject(qml, anchors.aboutBlock, edits);
    if (haveLinksBlock && !sameObject(anchors.linksBlock, anchors.aboutBlock))
        showObject(qml, anchors.linksBlock, edits);

    for (const auto &row : anchors.links) showObject(qml, row.object, edits);
    for (const auto &row : anchors.qqRows) showObject(qml, row, edits);
    if (anchors.updateButton.open >= 0) showObject(qml, anchors.updateButton, edits);
    for (const auto &row : anchors.updateInfoRows) showObject(qml, row, edits);

    // The About text.
    QmlText::Binding text;
    if (directBinding(masked, anchors.aboutText.open, anchors.aboutText.end,
                      QStringLiteral("text"), &text)) {
        QString original;
        int tagEnd = -1;
        if (taggedAfter(qml, text.valueBegin, kTextTag, &original, &tagEnd))
            addEdit(edits, text.valueBegin, tagEnd - text.valueBegin, original);
    }
    // Every link row's text and handler.
    for (const auto &row : anchors.links) {
        QmlText::Binding link;
        if (directBinding(masked, row.object.open, row.object.end,
                          QStringLiteral("text"), &link)) {
            QString original;
            int tagEnd = -1;
            if (taggedAfter(qml, link.valueBegin, kTextTag, &original, &tagEnd))
                addEdit(edits, link.valueBegin, tagEnd - link.valueBegin, original);
        }
        QmlText::Binding handler;
        if (directBinding(masked, row.object.open, row.object.end,
                          QStringLiteral("onLinkActivated"), &handler)) {
            QString original;
            int tagEnd = -1;
            if (taggedAfter(qml, handler.valueBegin, kHandlerTag, &original, &tagEnd))
                addEdit(edits, handler.valueBegin, tagEnd - handler.valueBegin, original);
        }
    }
    restoreAddedHandlers(qml, edits);

    const int at = qml.indexOf(kConfigBegin);
    if (at >= 0) {
        int begin = at;
        if (begin > 0 && qml.at(begin - 1) == QLatin1Char('\n')) {
            --begin;
            if (begin > 0 && qml.at(begin - 1) == QLatin1Char('\r')) --begin;
        }
        int end = qml.indexOf(QLatin1Char('\n'), at);
        if (end < 0) end = qml.size();
        addEdit(edits, begin, end - begin, QString());
    }
    return true;
}

Result refuse(const QString &message)
{
    return Result { false, message };
}

} // namespace

State analyze(const QByteArray &image)
{
    State state;
    if (image.isEmpty()) {
        state.detail = QStringLiteral("Select the HD-Player.exe path first.");
        return state;
    }
    // The words the window actually prints live in the compiled page, and that
    // page is parsed first: it is the copy the window loads, so it is also the
    // only honest answer to "what does the About page say now". The .qml is a
    // second copy, and a build whose .qml is missing or stale still gets a
    // filled editor from here.
    const Layout layout = layoutOf(image);
    state.compiledPage = layout.valid();
    // Only the page's own words are listed. The table also holds property
    // names, ids and binding text ("objectName", "expression for text"), which
    // are not part of the page and would bury it in noise.
    const auto isWord = [](const QString &text) {
        if (text.isEmpty()) return false;
        if (isIdentifier(text)) return false;
        if (isFragment(text)) return false;
        if (text.startsWith(QLatin1String("<a "))) return false;
        if (text.trimmed() == QLatin1String(" ")) return false;
        return true;
    };
    const int firstWanted = layout.links >= 0 ? layout.links : layout.about;
    for (int i = 0; i < layout.records.size(); ++i) {
        const Slot &slot = layout.records.at(i);
        const bool caption = slot.text == QLatin1String("Check for updates")
                             || slot.text == QLatin1String("Download Update");
        const bool inPage = layout.valid() && firstWanted >= 0 && i >= firstWanted
                            && i <= layout.about + 3;
        if ((caption || inPage) && isWord(slot.text))
            state.table << TableString { int(state.table.size()), slot.text, slot.maxChars() };
    }

    Config fromPage;
    if (layout.valid()) {
        fromPage.text = layoutText(layout);
        fromPage.links.clear();
        for (int r = 0; r < layout.rows.size(); ++r) {
            const PageRow &row = layout.rows.at(r);
            Link link;
            // The row's position is its identity: a row that was renamed or
            // hidden is still this row, so applying twice changes nothing.
            link.id = QStringLiteral("row%1").arg(r);
            link.name = pageRowName(layout, row);
            link.url = pageRowUrl(layout, row);
            link.label = pageRowLabel(layout, row);
            link.hidden = pageRowHidden(layout, row);
            // The record's own room, so the editor can stop at it instead of
            // letting a value be typed that the apply then has to refuse.
            if (row.name >= 0) link.nameMax = layout.records.at(row.name).maxChars();
            if (!row.fragments.isEmpty()) {
                // The record holds " - <a href=\"…\">label</a>", so only what
                // is left after the tag around it is room for the label.
                const Slot &fragment = layout.records.at(row.fragments.last());
                const QString printed = link.label;
                const QString whole = fragmentWith(fragment.text, printed, false);
                link.labelMax = qMax(0, fragment.maxChars() - (whole.size() - printed.size()));
            }
            if (!row.addresses.isEmpty())
                link.urlMax = layout.records.at(row.addresses.last()).maxChars();
            fromPage.links << link;
        }
    }

    Doc doc;
    QString error;
    const auto wideNote = [&image]() {
        // "About and other info" is the heading the page never loses, so it is
        // the sentence the whole file is searched for: if it is there but no
        // record holds it, the build stores the page in a shape this version
        // does not rewrite - and that is worth saying instead of "not found".
        const QList<int> hits = wideFind(image, QStringLiteral("About and other info"));
        if (hits.isEmpty()) return QString();
        QStringList where;
        for (const int at : hits) where << QStringLiteral("0x%1").arg(at, 0, 16);
        return QStringLiteral(" The page's own words \"About and other info\" are in this file at "
                              "%1, but not inside a string record this version can rewrite.")
            .arg(where.join(QStringLiteral(", ")));
    };
    if (!findDoc(image, QStringLiteral("PlrAboutSetting.qml"), &doc, &error)) {
        if (!state.compiledPage) {
            state.detail = error + wideNote();
            return state;
        }
        // Compiled page only: there is no .qml to rewrite, but the page itself
        // carries everything the editors need - the sentence and every row.
        state.supported = true;
        state.applied = layout.patched();
        state.config = fromPage;
        state.detail = layout.rows.isEmpty()
            ? QStringLiteral("This player keeps the About page only as compiled code. The text "
                             "below is what the window prints and is rewritten in place; the link "
                             "rows cannot be listed on this build.")
            : QStringLiteral("This player keeps the About page only as compiled code. Everything "
                             "below - the text and every link row - is what the window prints, and "
                             "it is rewritten in place without growing the file.");
        return state;
    }
    const QString masked = QmlText::mask(doc.qml);
    const Anchors anchors = findAnchors(doc.qml, masked, &error);
    if (!anchors.ok) {
        if (!state.compiledPage) {
            state.detail = error;
            return state;
        }
        // The .qml is not the shape the editor knows, but the compiled copy is
        // there: the text can still be rewritten where the window reads it.
        state.supported = true;
        state.source = doc.qml;
        state.config = fromPage;
        state.detail = error + QStringLiteral(" The compiled copy of the page was found, so the "
                                              "text and the link rows below can still be rewritten "
                                              "where the window reads them.");
        return state;
    }
    state.supported = true;
    state.qmlEditable = !doc.scanned;
    state.sectionsHidden = anchors.sectionsHidden;
    state.source = doc.qml;
    state.scanned = doc.scanned;
    bool hasLinksField = false;
    Config tag;
    const bool tagged = readConfig(doc.qml, &tag, &hasLinksField);
    if (layout.valid()) {
        // The compiled page is what the window shows, so it is what the fields
        // show. The tag written into the .qml only supplies the values of a row
        // that is hidden on the page (a hidden row prints nothing, so its own
        // fields would otherwise come back empty and could not be switched back
        // on), plus the flag saying the page was patched.
        state.config = fromPage;
        if (tagged) {
            state.applied = true;
            for (int i = 0; i < state.config.links.size(); ++i) {
                const Link saved = savedLink(tag, state.config.links.at(i).id, i);
                Link &shown = state.config.links[i];
                if (shown.hidden && !saved.name.trimmed().isEmpty()) {
                    shown.name = saved.name;
                    shown.url = saved.url;
                    shown.label = saved.label;
                } else if (shown.name.trimmed().isEmpty()) {
                    shown.name = saved.name;
                }
            }
        }
        const bool patched = layout.patched();
        state.applied = state.applied || patched;
        state.detail = patched
            ? QStringLiteral("Applied: the text and the link rows below are what the window "
                             "prints now. The update button is disabled.")
            : QStringLiteral("Stock About page. The fields show the text and every link the page "
                             "prints now - edit them, untick a row to hide it and apply.");
    } else if (tagged) {
        state.applied = true;
        // A tag written by an older version holds one custom link only; the
        // rows are then read back from the page itself. A current tag always
        // lists its rows, so an empty list really means "no row was named".
        state.config = tag;
        if (!hasLinksField) {
            Config shown;
            if (readShown(doc.qml, masked, anchors, &shown)) state.config.links = shown.links;
        }
        // A tag written before the display text existed has no label for a row:
        // the anchor printed the address, which is exactly what an empty label
        // means, so both forms read back the same.
        state.config = normalized(state.config);
        state.detail = QStringLiteral("Applied: the About text and the link rows below are the "
                                      "patched ones, and the update button is disabled.");
    } else {
        // Not patched: hand the fields what the page says now, so the text and
        // each link can be edited in place instead of retyped.
        Config shown;
        if (readShown(doc.qml, masked, anchors, &shown)) state.config = shown;
        state.detail = QStringLiteral("Stock About page. The fields show the text and links the "
                                      "page has now; edit them, untick a row to hide it and "
                                      "apply. The update button is disabled by the patch.");
    }
    if (state.compiledPage) {
        state.detail += QStringLiteral(" These are the %1 compiled strings of the page itself - the "
                                       "copy the window loads - and each one is rewritten in its own "
                                       "slot, so the file never grows and nothing moves.")
                            .arg(layout.records.size());
    }
    if (state.scanned && state.supported)
        state.detail += QStringLiteral(" The page was read by scanning the file because the "
                                       "resource tree could not be walked, so it can be viewed "
                                       "but not rewritten on this build.");
    return state;
}

namespace {

// Rewrites every copy of the page the file carries, not just the first one: a
// player that embeds the same QML twice (a second pool, an alias) would
// otherwise keep loading the untouched copy and the patch would look like it
// did nothing. Copies are re-located after each write, so a copy that had to be
// moved into free space is followed instead of being patched at a stale offset.
bool rewritePage(QByteArray &changed, bool enabled, const Config &config,
                 int *patched, QString *error)
{
    *patched = 0;
    for (int pass = 0; pass < 8; ++pass) {
        QList<Doc> docs;
        if (!findDocs(changed, QStringLiteral("PlrAboutSetting.qml"), &docs, error))
            return false;
        const Doc *next = nullptr;
        for (const auto &doc : docs) {
            // An applied copy carries our config line; a stock copy does not.
            const bool applied = doc.qml.contains(kConfigBegin);
            bool needsWork = applied != enabled;
            // Updating a page that is already patched has to be recognised as
            // work too: the tag is there, but it holds other content.
            if (enabled && applied) {
                Config current;
                const bool tagged = readConfig(doc.qml, &current, nullptr);
                needsWork = !tagged || !sameConfig(normalized(current), config);
            }
            if (needsWork) {
                next = &doc;
                break;
            }
        }
        if (!next) return true;
        Doc doc = *next;
        QList<Edit> edits;
        if (enabled) {
            if (doc.qml.contains(kConfigBegin)) {
                // Re-applying with different content: back to the stock page
                // first, then plan the new content against it.
                if (!planDisable(doc.qml, edits, error) || !applyEdits(doc.qml, edits)) {
                    if (error && error->isEmpty())
                        *error = QStringLiteral("Cannot safely update the previous About page edit; "
                                                "no file was written.");
                    return false;
                }
                edits.clear();
            }
            if (!planEnable(doc.qml, config, edits, error)) {
                if (error && error->isEmpty())
                    *error = QStringLiteral("The About page is not a supported shape; no file was written.");
                return false;
            }
            if (!applyEdits(doc.qml, edits)) {
                if (error)
                    *error = QStringLiteral("About page edits overlap in this player; "
                                            "no file was written.");
                return false;
            }
        } else {
            if (!planDisable(doc.qml, edits, error) || !applyEdits(doc.qml, edits)) {
                if (error && error->isEmpty())
                    *error = QStringLiteral("Cannot safely restore the stock About page; "
                                            "no file was written.");
                return false;
            }
        }
        const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link,
                                          header + doc.qml.toUtf8(), error))
            return false;
        ++*patched;
    }
    if (error)
        *error = QStringLiteral("This player carries more copies of the About page than can be "
                                "rewritten safely; no file was written.");
    return false;
}

} // namespace

Result apply(const QString &exePath, bool enabled, const Config &rawConfig)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return refuse(QStringLiteral("Close all BlueStacks instances before changing the About page."));
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("Cannot read the selected EXE."));
    const QByteArray original = file.readAll();
    file.close();

    const Config config = normalized(rawConfig);
    bool anyLink = false;
    for (const auto &link : config.links)
        if (!link.hidden && !link.url.isEmpty()) anyLink = true;
    if (enabled && config.text.isEmpty() && !anyLink && !config.showSections)
        return refuse(QStringLiteral("Enter the About text, keep at least one link or switch the "
                                     "sections on first."));

    const State before = analyze(original);
    if (!before.supported) return refuse(before.detail);

    // The compiled copy of the page is the one the window shows, so it is
    // planned before anything is written: "already applied" has to mean both
    // copies agree, not just the .qml nothing displays.
    const Layout layout = layoutOf(original);
    QList<SlotEdit> tableEdits;
    QStringList unlisted;
    if (layout.valid() && enabled) planTable(layout, config, &tableEdits, &unlisted);

    // Restoring needs the words that were there before, and the only copy of
    // them is the .bak this tool writes before its first edit. Slots are fixed
    // offsets, so a per-slot copy from the backup puts every one of them back
    // byte for byte.
    QByteArray backup;
    {
        QFile bak(exePath + QStringLiteral(".bak"));
        if (bak.open(QIODevice::ReadOnly)) backup = bak.readAll();
    }
    const Layout backupLayout = backup.isEmpty() ? Layout() : layoutOf(backup);
    // A backup is only usable when it holds the same records at the same
    // offsets: anything else is a different build, and copying one page's words
    // into another page's slots would corrupt it.
    bool canRestore = backupLayout.valid() && backupLayout.records.size() == layout.records.size();
    if (canRestore)
        for (int i = 0; i < layout.records.size(); ++i)
            if (layout.records.at(i).marker != backupLayout.records.at(i).marker) {
                canRestore = false;
                break;
            }
    bool tablePatched = false;
    if (canRestore)
        for (int i = 0; i < layout.records.size(); ++i)
            if (layout.records.at(i).text != backupLayout.records.at(i).text) { tablePatched = true; break; }

    if (enabled && before.applied && tableEdits.isEmpty()
        && (layout.valid() || sameConfig(before.config, config)))
        return { true, QStringLiteral("This About page content is already applied.") };
    if (!enabled && !before.applied && !tablePatched)
        return { true, QStringLiteral("The stock About page is already in place.") };

    QByteArray changed = original;
    QString error;
    int patched = 0;
    // A build that keeps the page only as compiled code has no .qml to rewrite,
    // and one whose .qml is a shape the editor cannot parse is left alone on
    // apply (its compiled copy is still rewritten below).
    QList<Doc> qmlDocs;
    QString qmlError;
    const bool hasQmlPage = findDocs(original, QStringLiteral("PlrAboutSetting.qml"), &qmlDocs, &qmlError);
    const bool qmlWritable = hasQmlPage && (!enabled || before.qmlEditable);
    if (enabled && !qmlWritable && !layout.valid())
        return refuse(QStringLiteral("The About page on this build was found by scanning and cannot be "
                                     "rewritten; no file was written."));
    if (qmlWritable
        && !rewritePage(changed, enabled, config, &patched, &error))
        return refuse(error.isEmpty() ? QStringLiteral("The About page could not be rewritten; "
                                                       "no file was written.") : error);

    int strings = 0;
    if (layout.valid()) {
        if (enabled) {
            if (!applyTable(changed, layout, tableEdits, &strings, &error)) return refuse(error);
        } else if (canRestore) {
            // Each slot is restored by writing its own words back, not by
            // copying a byte span: a slot is as long as the text it holds, so
            // the span of a record that was shortened is not the span it had.
            // The padding behind every record is zeros on both sides, so the
            // result is the original bytes.
            for (int i = 0; i < layout.records.size(); ++i) {
                if (layout.records.at(i).text == backupLayout.records.at(i).text) continue;
                if (!writeSlot(changed, layout.records.at(i), backupLayout.records.at(i).text, &error))
                    return refuse(QStringLiteral("The compiled About text cannot be put back from the "
                                                 "backup: %1").arg(error));
                ++strings;
            }
        } else if (tablePatched) {
            return refuse(QStringLiteral("The compiled About page has no matching backup to restore it "
                                         "from; no file was written. Restore the .bak file instead."));
        }
    }

    const State after = analyze(changed);
    if (!after.supported)
        return refuse(QStringLiteral("The written About page did not pass read-back verification; "
                                     "no file was written."));
    if (qmlWritable) {
        if (enabled && !after.applied)
            return refuse(QStringLiteral("The written About page did not read back with the new content; "
                                         "no file was written."));
        if (enabled && !layout.valid() && !sameConfig(after.config, config))
            return refuse(QStringLiteral("The written About page did not read back with the new content; "
                                         "no file was written."));
        if (!enabled && after.applied)
            return refuse(QStringLiteral("The written About page is still patched; no file was written."));
    }
    // The compiled page is read back too: the words the window will print are
    // checked against the config, not only the .qml that was written next to
    // them.
    if (layout.valid()) {
        const Layout written = layoutOf(changed);
        if (enabled) {
            QList<SlotEdit> left;
            QStringList ignored;
            planTable(written, config, &left, &ignored);
            if (!left.isEmpty())
                return refuse(QStringLiteral("The compiled About page did not read back with the new "
                                             "words; no file was written."));
        } else if (canRestore) {
            if (written.records.size() != backupLayout.records.size())
                return refuse(QStringLiteral("The compiled About page did not read back from the "
                                             "backup; no file was written."));
            for (int i = 0; i < written.records.size(); ++i)
                if (written.records.at(i).text != backupLayout.records.at(i).text)
                    return refuse(QStringLiteral("The compiled About page is still patched; no file "
                                                 "was written."));
        }
    }

    if (!ThemeEngine::backup(exePath))
        return refuse(QStringLiteral("Could not preserve the original backup; no EXE was written."));
    if (!PatchFile::writeVerified(exePath, original, changed, &error)) return refuse(error);

    // What the player will read: the file on disk, not the buffer that was
    // planned. This is the check that catches a write which "succeeded" but
    // left the page it loads untouched.
    QFile written(exePath);
    if (!written.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("The EXE was written but cannot be read back; restore the .bak."));
    const QByteArray onDisk = written.readAll();
    written.close();
    const State disk = analyze(onDisk);
    bool diskOk = disk.supported;
    if (diskOk && qmlWritable) {
        diskOk = disk.applied == enabled;
        if (diskOk && enabled && !layout.valid()) diskOk = sameConfig(disk.config, config);
    }
    if (diskOk && enabled && layout.valid()) {
        QList<SlotEdit> left;
        QStringList ignored;
        planTable(layoutOf(onDisk), config, &left, &ignored);
        diskOk = left.isEmpty();
    }
    if (!diskOk) {
        // Put the original back: a page that does not read back from disk is a
        // patch the player would not show either.
        PatchFile::writeVerified(exePath, onDisk, original, &error);
        return refuse(QStringLiteral("The About page does not read back from the file on disk; "
                                     "the original EXE was restored."));
    }

    const int caches = QtResources::clearPlayerQmlCache(exePath);
    const QString pageCount = patched > 1
        ? QStringLiteral(" (%1 copies of the page)").arg(patched) : QString();
    const QString stringCount = strings > 0
        ? QStringLiteral(" %1 compiled page string(s) - what the window actually prints - were "
                         "rewritten in place, without growing the file.").arg(strings)
        : QString();
    QString msg = enabled
        ? QStringLiteral("About page updated%1: the text and every link row are written as edited "
                         "and the update button is disabled.%2 Verified in the file on disk. ")
              .arg(pageCount, stringCount)
        : QStringLiteral("Stock About page restored%1 (text, every link row, section visibility, "
                         "the compiled page strings and the update button). Verified in the file on "
                         "disk. ").arg(pageCount);
    // Closing the player window does not stop BlueStacks - the page is compiled
    // when the Settings window is built, so an instance that is still running
    // keeps showing the old one no matter what the file says.
    msg += QStringLiteral("Quit BlueStacks completely (tray icon → Quit BlueStacks), then start it "
                          "again: closing the window leaves the player running with the old page.");
    if (!unlisted.isEmpty())
        msg += QStringLiteral(" The compiled page has no row named %1, so that row was left as it is.")
                   .arg(unlisted.join(QStringLiteral(", ")));
    if (caches > 0)
        msg += QStringLiteral(" Cleared the player QML cache so the edited page is compiled again.");
    return { true, msg };
}

// ---------------------------------------------------------------- diagnostics

namespace {

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString oneLine(const QString &text, int limit = 160)
{
    QString out = text;
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out = out.simplified();
    if (out.size() > limit)
        out = out.left(limit) + QStringLiteral(" …");
    return out;
}

QString span(const QmlText::Object &object)
{
    return object.open < 0 ? QStringLiteral("not found")
                           : QStringLiteral("%1..%2").arg(object.open).arg(object.end);
}

} // namespace

// A few lines the card can show when the editor still refuses or the page
// does not change: what was found, what it reads now, and the exact reason.
QString summary(const QString &exePath)
{
    QStringList lines;
    QFile file(exePath);
    const QFileInfo info(exePath);
    lines << QStringLiteral("File: %1 (%2 MB)")
                 .arg(info.fileName())
                 .arg(double(info.size()) / 1048576.0, 0, 'f', 1);
    if (!file.open(QIODevice::ReadOnly)) {
        lines << QStringLiteral("Cannot read the selected EXE.");
        return lines.join(QLatin1Char('\n'));
    }
    const QByteArray image = file.readAll();
    file.close();
    const State state = analyze(image);
    lines << QStringLiteral("Supported: %1   applied: %2   sections hidden: %3")
                 .arg(state.supported ? QStringLiteral("yes") : QStringLiteral("no"),
                      state.applied ? QStringLiteral("yes") : QStringLiteral("no"),
                      state.sectionsHidden ? QStringLiteral("yes") : QStringLiteral("no"));
    if (!state.supported) {
        lines << QStringLiteral("Reason: %1").arg(state.detail);
        return lines.join(QLatin1Char('\n'));
    }
    lines << QStringLiteral("Compiled page strings: %1%2")
                 .arg(state.table.size())
                 .arg(state.compiledPage ? QStringLiteral(" (the page the window shows)")
                                         : QStringLiteral(" (plain QML page)"));
    lines << QStringLiteral("Text now: \"%1\"").arg(oneLine(state.config.text, 120));
    lines << QStringLiteral("Link rows: %1").arg(state.config.links.size());
    for (const auto &link : state.config.links) {
        lines << QStringLiteral("  %1  %2 -> %3  prints \"%4\"%5")
                     .arg(link.id.isEmpty() ? QStringLiteral("(no id)") : link.id,
                          link.name, link.url, oneLine(link.label, 60),
                          link.hidden ? QStringLiteral("  [hidden]") : QString());
    }
    // How many copies of the page the file carries: a player that embeds it
    // twice is why a single-copy patch could look like it did nothing.
    QList<Doc> copies;
    QString ignored;
    const int count = findDocs(image, QStringLiteral("PlrAboutSetting.qml"), &copies, &ignored)
        ? copies.size() : 0;
    lines << QStringLiteral("Copies of the page in this EXE: %1").arg(count);
    return lines.join(QLatin1Char('\n'));
}

QString probeReport(const QString &exePath)
{
    QStringList lines;
    const auto add = [&lines](const QString &text) { lines << text; };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) {
        add(QStringLiteral("*** CANNOT READ %1 ***").arg(exePath));
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }
    const QByteArray image = file.readAll();
    file.close();
    add(QStringLiteral("file: %1 (%2 bytes)").arg(exePath).arg(image.size()));
    // The compiled copy of the page: the records the window prints, and the
    // room each one has. This is what the About card reads and rewrites, so a
    // build that "does nothing" can be told apart from one whose page was never
    // located - and a row that was mapped to the wrong record is visible here.
    const Layout layout = layoutOf(image);
    add(QStringLiteral("compiled page records: %1   table found: %2")
            .arg(layout.records.size())
            .arg(layout.valid() ? QStringLiteral("yes") : QStringLiteral("NO")));
    // The words searched for on their own: a build whose records cannot be
    // parsed still has the page's text, and this says where.
    {
        QStringList where;
        for (const int at : wideFind(image, QStringLiteral("About and other info")))
            where << QStringLiteral("0x%1").arg(at, 0, 16);
        add(QStringLiteral("\"About and other info\" as UTF-16LE: %1")
                .arg(where.isEmpty() ? QStringLiteral("not in this file")
                                     : where.join(QStringLiteral(", "))));
    }
    if (layout.valid()) {
        add(QStringLiteral("  headings: links=%1 about=%2   patched=%3")
                .arg(layout.links)
                .arg(layout.about)
                .arg(layout.patched() ? QStringLiteral("yes") : QStringLiteral("no")));
        if (layout.about >= 0 && layout.about + 1 < layout.records.size()) {
            const int label = layout.about + 3;
            add(QStringLiteral("  sentence @%1 room=%2  %3")
                    .arg(layout.records.at(layout.about + 1).marker)
                    .arg(layout.records.at(layout.about + 1).maxChars())
                    .arg(oneLine(layout.records.at(layout.about + 1).text, 80)));
            if (label < layout.records.size())
                add(QStringLiteral("  terms label: \"%1\"")
                        .arg(oneLine(layout.records.at(label).text, 40)));
        }
        for (int r = 0; r < layout.rows.size(); ++r) {
            const PageRow &row = layout.rows.at(r);
            add(QStringLiteral("  row %1: name=%2 -> %3  prints \"%4\"%5")
                    .arg(r)
                    .arg(oneLine(pageRowName(layout, row), 40), pageRowUrl(layout, row),
                         oneLine(pageRowLabel(layout, row), 60),
                         pageRowHidden(layout, row) ? QStringLiteral("  [hidden]") : QString()));
            add(QStringLiteral("       records: name=%1 fragments=%2 addresses=%3")
                    .arg(row.name)
                    .arg([&row]() {
                        QStringList list;
                        for (const int i : row.fragments) list << QString::number(i);
                        return list.join(QLatin1Char(','));
                    }(),
                         [&row]() {
                             QStringList list;
                             for (const int i : row.addresses) list << QString::number(i);
                             return list.join(QLatin1Char(','));
                         }()));
        }
    }
    for (const auto &slot : layout.records)
        add(QStringLiteral("    @%1 chars=%2 room=%3  %4")
                .arg(slot.marker)
                .arg(slot.chars)
                .arg(slot.maxChars())
                .arg(oneLine(slot.text, 70)));

    const auto files = QtResources::registeredFiles(image);
    add(QStringLiteral("registered resource files: %1").arg(files.size()));
    int matches = 0;
    for (const auto &entry : files) {
        if (entry.name == QLatin1String("PlrAboutSetting.qml")) {
            ++matches;
            const auto bytes = QtResources::readFile(image, entry);
            add(QStringLiteral("  PlrAboutSetting.qml: entryOff=%1 flags=%2 stored=%3 decoded=%4")
                    .arg(entry.entryOff)
                    .arg(entry.flags)
                    .arg(QtResources::readFile(image, entry).isEmpty()
                             ? QStringLiteral("unreadable")
                             : QString::number(ByteUtils::be32(image, entry.entryOff)))
                    .arg(bytes.size()));
        }
    }
    add(QStringLiteral("PlrAboutSetting.qml entries: %1").arg(matches));

    const ThemeInfo theme = ThemeEngine::findThemeZlib(image);
    add(QStringLiteral("theme pool: hasQrcNode=%1 ambiguous=%2")
            .arg(theme.hasQrcNode ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(theme.ambiguous ? QStringLiteral("yes") : QStringLiteral("no")));
    if (theme.hasQrcNode) {
        int named = 0;
        for (const auto &entry : QtResources::filesInPool(image, theme.resource))
            if (entry.name == QLatin1String("PlrAboutSetting.qml")) ++named;
        add(QStringLiteral("  PlrAboutSetting.qml in that pool: %1").arg(named));
    }

    QString error;
    Doc doc;
    if (!findDoc(image, QStringLiteral("PlrAboutSetting.qml"), &doc, &error)) {
        add(QStringLiteral("*** DOC NOT FOUND: %1 ***").arg(error));
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }
    add(QStringLiteral("document: %1 chars, bom=%2, crlf=%3")
            .arg(doc.qml.size())
            .arg(doc.bom ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(doc.qml.contains(QLatin1String("\r\n")) ? QStringLiteral("yes")
                                                         : QStringLiteral("no")));

    const QString masked = QmlText::mask(doc.qml);
    const Anchors anchors = findAnchors(doc.qml, masked, &error);
    if (!anchors.ok) {
        add(QStringLiteral("*** ANCHORS FAILED: %1 ***").arg(error));
        // Say which half of the shape is missing, so the next build can be
        // compared instead of re-guessed.
        QmlText::Object content;
        add(QStringLiteral("  iContentLayout: %1")
                .arg(findObjectById(doc.qml, masked, QStringLiteral("iContentLayout"), &content)
                         ? span(content)
                         : QStringLiteral("missing")));
        for (const auto &type : { QStringLiteral("UiScrollView"), QStringLiteral("ScrollView"),
                                  QStringLiteral("Flickable") }) {
            add(QStringLiteral("  %1 objects: %2").arg(type)
                    .arg(QmlText::objects(masked, type).size()));
        }
        for (const auto &id : stockLinkIds()) {
            QmlText::Object object;
            add(QStringLiteral("  %1: %2").arg(id,
                    findObjectById(doc.qml, masked, id, &object) ? span(object)
                                                                 : QStringLiteral("missing")));
        }
        QmlText::Object terms;
        add(QStringLiteral("  iTermsOfUseText: %1")
                .arg(findObjectById(doc.qml, masked, QStringLiteral("iTermsOfUseText"), &terms)
                         ? span(terms)
                         : QStringLiteral("missing")));
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    add(QStringLiteral("content column: %1").arg(span(anchors.content)));
    add(QStringLiteral("about text object: %1  (binding %2..%3)")
            .arg(span(anchors.aboutText))
            .arg(anchors.aboutTextBinding.valueBegin)
            .arg(anchors.aboutTextBinding.valueEnd));
    add(QStringLiteral("  raw: %1")
            .arg(oneLine(doc.qml.mid(anchors.aboutTextBinding.valueBegin,
                                     anchors.aboutTextBinding.valueEnd
                                         - anchors.aboutTextBinding.valueBegin))));
    add(QStringLiteral("  chunks: %1")
            .arg(oneLine(renderedChunks(doc.qml.mid(anchors.aboutTextBinding.valueBegin,
                                                    anchors.aboutTextBinding.valueEnd
                                                        - anchors.aboutTextBinding.valueBegin))
                             .join(QStringLiteral(" | ")))));
    add(QStringLiteral("about block: %1   links block: %2   hidden=%3/%4")
            .arg(span(anchors.aboutBlock), span(anchors.linksBlock))
            .arg(hidesItself(masked, anchors.aboutBlock) ? QStringLiteral("yes")
                                                         : QStringLiteral("no"))
            .arg(hidesItself(masked, anchors.linksBlock) ? QStringLiteral("yes")
                                                         : QStringLiteral("no")));
    add(QStringLiteral("update button: %1   update rows: %2   qq rows: %3")
            .arg(anchors.updateButton.open >= 0 ? span(anchors.updateButton)
                                                : QStringLiteral("not found"))
            .arg(anchors.updateInfoRows.size())
            .arg(anchors.qqRows.size()));
    for (const auto &row : anchors.links) {
        const QString text = doc.qml.mid(row.text.valueBegin,
                                         row.text.valueEnd - row.text.valueBegin);
        add(QStringLiteral("row %1: %2 handler=%3")
                .arg(row.id, span(row.object),
                     row.hasHandler ? QStringLiteral("yes") : QStringLiteral("no")));
        add(QStringLiteral("   text: %1").arg(oneLine(text)));
        add(QStringLiteral("   chunks: %1")
                .arg(oneLine(renderedChunks(text).join(QStringLiteral(" | ")))));
        if (row.hasHandler) {
            add(QStringLiteral("   handler: %1")
                    .arg(oneLine(doc.qml.mid(row.handler.valueBegin,
                                             row.handler.valueEnd - row.handler.valueBegin))));
        }
    }

    Config config;
    if (readShown(doc.qml, masked, anchors, &config)) {
        add(QStringLiteral("readShown text: \"%1\"").arg(oneLine(config.text, 240)));
        for (const auto &link : config.links) {
            add(QStringLiteral("readShown link %1: name=\"%2\" prints=\"%3\" url=\"%4\" hidden=%5")
                    .arg(link.id, oneLine(link.name, 60), oneLine(link.label, 60),
                         oneLine(link.url, 80))
                    .arg(link.hidden ? QStringLiteral("yes") : QStringLiteral("no")));
        }
    } else {
        add(QStringLiteral("*** readShown FAILED (empty text and links) ***"));
    }

    // End-to-end proof on a throw-away copy: plan the edit against the values
    // the page has now, write it, read it back, then restore and compare the
    // bytes with the original. This is what the UI's Apply button does, so a
    // failure here is a failure there, named step by step.
    {
        QTemporaryDir scratch;
        if (!scratch.isValid()) {
            add(QStringLiteral("round trip: no temporary directory"));
        } else {
            const QString copy = scratch.filePath(QStringLiteral("about-probe.exe"));
            if (!QFile::copy(exePath, copy)) {
                add(QStringLiteral("round trip: cannot copy the EXE next to the probe"));
            } else {
                const State stock = analyze(image);
                Config wanted = stock.config;
                wanted.text = QStringLiteral("BSI Customizer probe");
                wanted.showSections = true;
                for (int i = 0; i < wanted.links.size(); ++i)
                    wanted.links[i].hidden = false;
                const Result wrote = apply(copy, true, wanted);
                add(QStringLiteral("round trip apply: ok=%1 %2")
                        .arg(wrote.ok ? QStringLiteral("yes") : QStringLiteral("no"), wrote.msg));
                if (wrote.ok) {
                    const State after = analyze(readAll(copy));
                    add(QStringLiteral("   read back: supported=%1 applied=%2 text=\"%3\" links=%4")
                            .arg(after.supported ? QStringLiteral("yes") : QStringLiteral("no"),
                                 after.applied ? QStringLiteral("yes") : QStringLiteral("no"),
                                 oneLine(after.config.text, 60))
                            .arg(after.config.links.size()));
                }
                const Result undone = apply(copy, false, Config {});
                add(QStringLiteral("round trip restore: ok=%1 %2")
                        .arg(undone.ok ? QStringLiteral("yes") : QStringLiteral("no"), undone.msg));
                const QByteArray back = readAll(copy);
                add(QStringLiteral("   bytes identical to the original: %1")
                        .arg(back == image ? QStringLiteral("yes") : QStringLiteral("no")));
            }
        }
    }

    const State state = analyze(image);
    add(QStringLiteral("analyze: supported=%1 applied=%2 sectionsHidden=%3")
            .arg(state.supported ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(state.applied ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(state.sectionsHidden ? QStringLiteral("yes") : QStringLiteral("no")));
    add(QStringLiteral("  detail: %1").arg(oneLine(state.detail, 300)));
    add(QStringLiteral("  config.text: \"%1\"").arg(oneLine(state.config.text, 240)));
    for (const auto &link : state.config.links) {
        add(QStringLiteral("  config link %1: name=\"%2\" url=\"%3\" hidden=%4")
                .arg(link.id, oneLine(link.name, 60), oneLine(link.url, 80))
                .arg(link.hidden ? QStringLiteral("yes") : QStringLiteral("no")));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace AboutPatcher
