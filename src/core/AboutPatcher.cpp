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
        const QPair<quint32, quint32> orders[] { qMakePair(fieldA, fieldB),
                                                 qMakePair(fieldB, fieldA) };
        for (const auto &pair : orders) {
            const quint32 decoded = pair.first;
            const quint32 stored = pair.second;
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
// The compiled copy of the page
// ---------------------------------------------------------------------------
// A BlueStacks player ships PlrAboutSetting.qml as text *and* a compiled copy of
// the same page, and the window runs the compiled one. Every word the page
// prints - the headings, each row's name and address, the copyright sentence,
// the update button's caption - is a UTF-16LE record in that unit's string
// table:
//
//     FF FF FF FF | be32 chars | be32 0 | be32 0x18 | be32 0x18 | text | pad
//
// Rewriting the .qml therefore reaches a copy nothing displays, which is why the
// About card could report "applied" while the window never changed. The records
// below are what the window actually reads. There is no byte count in a record,
// so its room is measured up to the following marker: a literal is replaced
// inside its own slot and the rest of the slot is padded with U+0000. The file
// never changes size, nothing moves, and a shorter text is as safe as the stock
// one - only growth is refused.
struct Slot
{
    int marker = 0;  // first byte of FF FF FF FF
    int textAt = 0;  // first byte of the UTF-16LE text
    int chars = 0;   // characters the record holds now
    int end = 0;     // first byte after the slot, padding included
    QString text;

    // Characters that still fit. The stock text always fits; the padding after
    // it is room for a few more, and a text written shorter leaves that padding
    // behind, so a record can be rewritten again later.
    int maxChars() const
    {
        const int room = end - textAt - 2; // keep at least one terminating NUL
        return qMax(chars, room / 2);
    }
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
    int end = at + bytes;
    while (end < image.size() && image.at(end) == '\0' && end - (at + bytes) < 16) ++end;
    // The slot owns everything up to the next record's marker. Measuring with
    // the marker (and not only with the zeros) keeps a record recognizable
    // after it was rewritten shorter - otherwise the page could be edited once
    // and never read again.
    const qsizetype next = image.indexOf(slotMarker(), at + bytes);
    if (next > 0 && next - (at + bytes) <= 4096) end = int(next);
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
    QList<Slot> slots;
    for (int at = int(image.indexOf(slotMarker())); at >= 0;
         at = int(image.indexOf(slotMarker(), at + 1))) {
        Slot slot;
        if (!parseSlot(image, at, &slot)) continue;
        slots << slot;
        // Nothing inside a literal can be a marker, so the next search may
        // resume at the end of this one.
        at = slot.textAt + slot.chars * 2;
    }
    return slots;
}

// The run of records that holds the About page. Records of one compiled unit
// sit next to each other with nothing but their padding between them; the page
// is the run that carries both of its structural headings.
QList<Slot> aboutTable(const QByteArray &image)
{
    const QList<Slot> all = scanSlots(image);
    QList<QList<Slot>> runs;
    for (const Slot &slot : all) {
        if (!runs.isEmpty() && slot.marker - runs.last().last().end <= 8)
            runs.last() << slot;
        else
            runs << QList<Slot> { slot };
    }
    const auto holds = [](const QList<Slot> &run, const QString &text) {
        for (const Slot &slot : run)
            if (slot.text == text) return true;
        return false;
    };
    const auto isCopyright = [](const QList<Slot> &run) {
        for (const Slot &slot : run)
            if (slot.text.startsWith(QStringLiteral("\u00A9"))) return true;
        return false;
    };
    const auto hasAnchor = [](const QList<Slot> &run) {
        for (const Slot &slot : run)
            if (slot.text.startsWith(QLatin1String("<a href="))) return true;
        return false;
    };
    QList<Slot> best;
    for (const auto &run : runs) {
        // "Terms of use" is one of the things the editor rewrites, so it cannot
        // be the only way to recognize the page: after a patch the heading and
        // the anchor are what is left of the shape. Both are never touched.
        const bool page = holds(run, QStringLiteral("About and other info"))
                          && (holds(run, QStringLiteral("Terms of use"))
                              || holds(run, QStringLiteral("Important links")) || hasAnchor(run));
        // A translated page keeps the shape but not the words: the copyright
        // sign and the update button are enough to recognize it.
        const bool translated = isCopyright(run)
                                && (holds(run, QStringLiteral("Check for updates"))
                                    || holds(run, QStringLiteral("Download Update")));
        if (!page && !translated) continue;
        if (run.size() > best.size()) best = run;
    }
    return best;
}

int aboutTextSlot(const QList<Slot> &slots)
{
    // The sentence is the literal the page puts in front of its "Terms of use"
    // anchor, which is the only slot in the table that *starts* with an <a> tag
    // (a row's own fragment starts with " - "). Finding it by shape keeps
    // working after the sentence has been replaced - the copyright sign is no
    // longer there to look for.
    for (int i = 0; i + 1 < slots.size(); ++i) {
        if (!slots[i + 1].text.startsWith(QLatin1String("<a href="))) continue;
        if (slots[i].text.contains(QLatin1Char('<'))) continue;
        return i;
    }
    for (int i = 0; i < slots.size(); ++i) {
        const QString text = slots[i].text;
        if (text.startsWith(QStringLiteral("\u00A9"))
            || text.contains(QLatin1String("rights reserved"), Qt::CaseInsensitive))
            return i;
    }
    return -1;
}

// One "name - <a …>label</a>" row of the compiled page.
struct TableRow
{
    int name = -1;     // the slot before the row's text ("Support")
    int fragment = -1; // the slot printing " - <a …>support.bluestacks.com</a>"
    int label = -1;    // the slot printing the address on its own, if any
    int url = -1;      // the slot holding the http:// address it opens
};

QString anchorLabel(const QString &fragment)
{
    const int close = fragment.lastIndexOf(QLatin1String("</a>"));
    if (close < 0) return QString();
    const int open = fragment.lastIndexOf(QLatin1Char('>'), close);
    if (open < 0 || open >= close) return QString();
    return fragment.mid(open + 1, close - open - 1);
}

QList<TableRow> tableRows(const QList<Slot> &slots)
{
    QList<TableRow> rows;
    for (int i = 0; i < slots.size(); ++i) {
        const QString text = slots[i].text;
        if (!text.contains(QLatin1String("<a "), Qt::CaseInsensitive)) continue;
        if (anchorLabel(text).isEmpty()) continue;
        TableRow row;
        row.fragment = i;
        for (int j = i - 1; j >= 0; --j) {
            if (slots[j].text.contains(QLatin1String("<a "), Qt::CaseInsensitive)) continue;
            row.name = j;
            break;
        }
        const QString label = anchorLabel(text);
        for (int j = 0; j < slots.size(); ++j) {
            if (j == i || j == row.name) continue;
            if (row.label < 0 && slots[j].text == label) row.label = j;
            if (row.url < 0 && slots[j].text.startsWith(QLatin1String("http"), Qt::CaseInsensitive)
                && slots[j].text.contains(label, Qt::CaseInsensitive))
                row.url = j;
        }
        rows << row;
    }
    return rows;
}

// A player draws one row twice when it has a China branch
// (`oem === "nxt_cn" ? A : B`), so the two fragments that belong to one row are
// edited together and never one without the other.
struct RowGroup
{
    int name = -1;
    QList<TableRow> rows;
};

QList<RowGroup> tableGroups(const QList<Slot> &slots)
{
    QList<RowGroup> groups;
    for (const TableRow &row : tableRows(slots)) {
        if (row.name < 0) continue;
        bool placed = false;
        for (RowGroup &group : groups) {
            if (group.name != row.name) continue;
            group.rows << row;
            placed = true;
            break;
        }
        if (!placed) groups << RowGroup { row.name, QList<TableRow> { row } };
    }
    return groups;
}

struct SlotEdit
{
    int index = 0;
    QString text;
};

// What a hidden caption becomes. One space, never an empty string: a zero
// length record cannot be read back, so it could not be restored either.
const QString kBlankSlot = QStringLiteral(" ");

bool writeSlot(QByteArray &image, const Slot &slot, const QString &text, QString *error)
{
    const auto refuse = [&](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (text.contains(QChar(0))) return refuse(QStringLiteral("A page text contains a NUL."));
    if (text.size() > slot.maxChars())
        return refuse(QStringLiteral("%1 needs %2 characters but its slot on this player only "
                                     "holds %3; nothing was written.")
                          .arg(text.left(40))
                          .arg(text.size())
                          .arg(slot.maxChars()));
    QByteArray bytes;
    bytes.reserve(int(text.size()) * 2);
    for (int i = 0; i < text.size(); ++i) {
        const ushort unit = text.at(i).unicode();
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
    ByteUtils::putLe32(image, slot.marker + 4, quint32(text.size()));
    return true;
}

// The edits that make the compiled page say what the config says. A link row is
// found by the name it prints, then by the text or the address it shows, so a
// row that was already edited is still found on the next apply. Anything the
// table does not carry is skipped and reported by the caller.
void planTable(const QList<Slot> &slots, const Config &config, QList<SlotEdit> *edits,
               QStringList *missing)
{
    QSet<int> touched;
    const auto set = [&](int index, const QString &text) {
        if (index < 0 || index >= slots.size() || touched.contains(index)) return;
        if (slots[index].text == text) return;
        touched.insert(index);
        *edits << SlotEdit { index, text };
    };
    // An exact match anywhere in the page: this is how a row's own words are
    // found when the record next to the fragment is not the row's name (the
    // China branch keeps its token in between).
    const auto setText = [&](const QString &from, const QString &to) {
        if (from.isEmpty() || from == to) return;
        for (int i = 0; i < slots.size(); ++i)
            if (slots[i].text.compare(from, Qt::CaseInsensitive) == 0) {
                set(i, to);
                return;
            }
    };

    // The update button is disabled: its caption goes, so a stray click opens an
    // empty confirmation instead of a download.
    for (int i = 0; i < slots.size(); ++i) {
        const QString text = slots[i].text;
        if (text == QLatin1String("Check for updates") || text == QLatin1String("Download Update"))
            set(i, kBlankSlot);
    }
    const int about = aboutTextSlot(slots);
    if (about >= 0 && !config.text.isEmpty()) {
        // The stock sentence is one literal plus the "Terms of use" link the
        // page appends to it, and the editor shows both together. Writing the
        // whole sentence into the literal would paste the link on a second time
        // - and the literal is a fixed size, so it would not fit either. When
        // the edited text still ends with the link's own words, only the part
        // before it is written and the link stays; when it does not, the link is
        // cleared so the line prints exactly what was typed.
        int label = -1;
        QString labelText;
        for (int i = about + 1; i < slots.size() && i <= about + 3; ++i) {
            if (!slots[i].text.startsWith(QLatin1String("<a href="))) continue;
            for (int j = i + 1; j < slots.size() && j <= i + 2; ++j) {
                if (slots[j].text.contains(QLatin1Char('<'))) continue;
                if (slots[j].text.compare(QLatin1String("Terms of use"), Qt::CaseInsensitive) != 0)
                    continue;
                label = j;
                labelText = slots[j].text;
            }
        }
        QString prefix = config.text;
        const bool keepsLink = label >= 0 && !labelText.isEmpty()
                               && prefix.endsWith(labelText, Qt::CaseInsensitive);
        if (keepsLink) prefix.chop(labelText.size());
        set(about, prefix);
        if (!keepsLink) {
            // The anchor tag itself stays (with nothing to open and no words in
            // it), so the line is still recognizable as the About line on the
            // next read; the label it used to print is cleared.
            const QString emptyAnchor = QStringLiteral("<a href=") + QLatin1Char('"')
                                        + QLatin1Char('"') + QLatin1Char('>');
            for (int i = about + 1; i < slots.size() && i <= about + 3; ++i) {
                const QString text = slots[i].text;
                if (text.startsWith(QLatin1String("<a href=")))
                    set(i, emptyAnchor);
                else if (i == label || text == QLatin1String("Terms of use"))
                    set(i, kBlankSlot);
            }
        }
    }

    const QList<RowGroup> groups = tableGroups(slots);
    QSet<int> assigned;
    for (const Link &link : config.links) {
        int pick = -1;
        for (int g = 0; g < groups.size() && pick < 0; ++g) {
            if (assigned.contains(g)) continue;
            if (slots[groups[g].name].text.compare(link.name, Qt::CaseInsensitive) == 0) pick = g;
        }
        for (int g = 0; g < groups.size() && pick < 0; ++g) {
            if (assigned.contains(g)) continue;
            for (const TableRow &row : groups[g].rows) {
                if (row.label >= 0 && !link.label.isEmpty()
                    && slots[row.label].text.compare(link.label, Qt::CaseInsensitive) == 0) {
                    pick = g;
                    break;
                }
                if (row.url >= 0 && !link.url.isEmpty()
                    && slots[row.url].text.compare(link.url, Qt::CaseInsensitive) == 0) {
                    pick = g;
                    break;
                }
            }
        }
        // The rows and the links are in the same order on every build seen so
        // far, which is the last resort when a row was already renamed.
        for (int g = 0; g < groups.size() && pick < 0; ++g)
            if (!assigned.contains(g)) pick = g;
        if (pick < 0) {
            if (!link.hidden && missing) *missing << link.name;
            continue;
        }
        assigned.insert(pick);
        if (link.hidden) {
            setText(link.name, kBlankSlot);
            for (const TableRow &row : groups[pick].rows) set(row.fragment, kBlankSlot);
            continue;
        }
        setText(link.name, link.name); // no-op unless the name is being kept
        const QString label = link.label.trimmed().isEmpty() ? displayFor(link.url) : link.label;
        for (const TableRow &row : groups[pick].rows) {
            if (!label.isEmpty() && row.fragment >= 0) {
                QString fragment = slots[row.fragment].text;
                const int close = fragment.lastIndexOf(QLatin1String("</a>"));
                const int open = close < 0 ? -1 : fragment.lastIndexOf(QLatin1Char('>'), close);
                if (open >= 0 && close > open)
                    fragment = fragment.left(open + 1) + label + fragment.mid(close);
                set(row.fragment, fragment);
                if (row.label >= 0) set(row.label, label);
            }
            if (!link.url.isEmpty() && row.url >= 0) set(row.url, link.url);
        }
    }
}

bool applyTable(QByteArray &image, const QList<Slot> &slots, const QList<SlotEdit> &edits,
                int *written, QString *error)
{
    int count = 0;
    for (const SlotEdit &edit : edits) {
        if (edit.index < 0 || edit.index >= slots.size()) continue;
        if (!writeSlot(image, slots[edit.index], edit.text, error)) return false;
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

const Link *findLink(const Config &config, const QString &id)
{
    for (const auto &link : config.links)
        if (link.id == id) return &link;
    return nullptr;
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
    for (const auto &row : anchors.links) {
        const Link *link = findLink(config, row.id);
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
    // The words the window actually prints live in the compiled page. They are
    // read first, so a build whose .qml is missing (or is only a stale copy)
    // still gets a filled editor.
    const QList<Slot> table = aboutTable(image);
    state.compiledPage = !table.isEmpty();
    // Only the words are listed. The same table also holds property names,
    // ids and binding text ("objectName", "expression for text"), which are not
    // part of the page and would bury it in noise.
    const auto isWord = [](const QString &text) {
        if (text.startsWith(QLatin1String("expression for "))) return false;
        if (text.startsWith(QLatin1String("QtQuick"))) return false;
        if (text.contains(QLatin1Char(' '))) return true;
        if (text.contains(QLatin1Char('/')) || text.contains(QLatin1Char('@'))) return true;
        if (text.startsWith(QStringLiteral("\u00A9"))) return true;
        if (text.startsWith(QLatin1String("http"), Qt::CaseInsensitive)) return true;
        return text.compare(QLatin1String("Support"), Qt::CaseInsensitive) == 0
               || text.compare(QLatin1String("Website"), Qt::CaseInsensitive) == 0;
    };
    for (const Slot &slot : table)
        if (isWord(slot.text))
            state.table << TableString { int(state.table.size()), slot.text, slot.maxChars() };
    const int aboutSlot = aboutTextSlot(table);

    Doc doc;
    QString error;
    if (!findDoc(image, QStringLiteral("PlrAboutSetting.qml"), &doc, &error)) {
        if (!state.compiledPage) {
            state.detail = error;
            return state;
        }
        // Compiled page only: the text can be edited, the row list cannot be
        // named, because the names come from the QML this build does not carry.
        state.supported = true;
        state.detail = QStringLiteral("This player keeps the About page only as compiled code. The "
                                      "text below is what the window prints and is rewritten in "
                                      "place; the link rows cannot be listed on this build.");
        if (aboutSlot >= 0) state.config.text = table[aboutSlot].text;
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
        state.detail = error + QStringLiteral(" The compiled copy of the page was found, so the text "
                                              "below can still be rewritten where the window reads "
                                              "it; the link rows cannot be listed on this build.");
        if (aboutSlot >= 0) state.config.text = table[aboutSlot].text;
        return state;
    }
    state.supported = true;
    state.qmlEditable = !doc.scanned;
    state.sectionsHidden = anchors.sectionsHidden;
    state.source = doc.qml;
    state.scanned = doc.scanned;
    bool hasLinksField = false;
    if (readConfig(doc.qml, &state.config, &hasLinksField)) {
        state.applied = true;
        // A tag written by an older version holds one custom link only; the
        // rows are then read back from the page itself. A current tag always
        // lists its rows, so an empty list really means "no row was named".
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
        state.detail += QStringLiteral(" This player also carries a compiled copy of the page, and "
                                       "that is the one the window shows: its %1 strings are "
                                       "rewritten in place as well, so the text and links above "
                                       "take effect on screen.")
                            .arg(table.size());
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
    const QList<Slot> slots = aboutTable(original);
    QList<SlotEdit> tableEdits;
    QStringList unlisted;
    if (!slots.isEmpty() && enabled) planTable(slots, config, &tableEdits, &unlisted);

    // Restoring needs the words that were there before, and the only copy of
    // them is the .bak this tool writes before its first edit. Slots are fixed
    // offsets, so a per-slot copy from the backup puts every one of them back
    // byte for byte.
    QByteArray backup;
    {
        QFile bak(exePath + QStringLiteral(".bak"));
        if (bak.open(QIODevice::ReadOnly)) backup = bak.readAll();
    }
    const QList<Slot> backupSlots = backup.isEmpty() ? QList<Slot>() : aboutTable(backup);
    const bool canRestore = !backupSlots.isEmpty() && backupSlots.size() == slots.size();
    bool tablePatched = false;
    if (canRestore)
        for (int i = 0; i < slots.size(); ++i)
            if (slots[i].text != backupSlots[i].text) { tablePatched = true; break; }

    if (enabled && before.applied && sameConfig(before.config, config) && tableEdits.isEmpty())
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
    if (enabled && !qmlWritable && slots.isEmpty())
        return refuse(QStringLiteral("The About page on this build was found by scanning and cannot be "
                                     "rewritten; no file was written."));
    if (qmlWritable
        && !rewritePage(changed, enabled, config, &patched, &error))
        return refuse(error.isEmpty() ? QStringLiteral("The About page could not be rewritten; "
                                                       "no file was written.") : error);

    int strings = 0;
    if (!slots.isEmpty()) {
        if (enabled) {
            if (!applyTable(changed, slots, tableEdits, &strings, &error)) return refuse(error);
        } else if (canRestore) {
            for (int i = 0; i < slots.size(); ++i) {
                if (slots[i].text == backupSlots[i].text) continue;
                const int size = slots[i].end - slots[i].marker;
                if (backupSlots[i].end - backupSlots[i].marker != size
                    || !ByteUtils::inRange(backup, backupSlots[i].marker, size)
                    || !ByteUtils::inRange(changed, slots[i].marker, size))
                    return refuse(QStringLiteral("The backup does not line up with this EXE, so the "
                                                 "compiled About text cannot be put back; nothing was "
                                                 "written. Restore the .bak file instead."));
                changed.replace(slots[i].marker, size, backup.mid(backupSlots[i].marker, size));
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
        if (enabled && (!after.applied || !sameConfig(after.config, config)))
            return refuse(QStringLiteral("The written About page did not read back with the new content; "
                                         "no file was written."));
        if (!enabled && after.applied)
            return refuse(QStringLiteral("The written About page is still patched; no file was written."));
    }
    // The compiled page is read back too: the words the window will print are
    // checked against the config, not only the .qml that was written next to
    // them.
    if (!slots.isEmpty()) {
        const QList<Slot> written = aboutTable(changed);
        if (enabled) {
            QList<SlotEdit> left;
            QStringList ignored;
            planTable(written, config, &left, &ignored);
            if (!left.isEmpty())
                return refuse(QStringLiteral("The compiled About page did not read back with the new "
                                             "words; no file was written."));
        } else if (canRestore) {
            for (int i = 0; i < written.size() && i < backupSlots.size(); ++i)
                if (written[i].text != backupSlots[i].text)
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
    if (diskOk && qmlWritable)
        diskOk = disk.applied == enabled && (!enabled || sameConfig(disk.config, config));
    if (diskOk && enabled && !slots.isEmpty()) {
        QList<SlotEdit> left;
        QStringList ignored;
        planTable(aboutTable(onDisk), config, &left, &ignored);
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
    // The compiled copy of the page: the strings the window prints, and the
    // room each one has. This is what the About card rewrites, so a build that
    // "does nothing" can be told apart from one whose page was never located.
    const QList<Slot> table = aboutTable(image);
    add(QStringLiteral("compiled page strings: %1").arg(table.size()));
    for (const auto &slot : table)
        add(QStringLiteral("  @%1 chars=%2 room=%3  %4")
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
