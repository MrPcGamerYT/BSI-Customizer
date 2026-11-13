#include "EmbeddedResources.h"

#include "BsDetector.h"
#include "ByteUtils.h"
#include "EmbeddedImages.h"
#include "PatchFile.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace EmbeddedResources {
namespace {

constexpr qint64 kMaxEntry = 32 * 1024 * 1024;

const QStringList &pictureFormats()
{
    static const QStringList formats {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("jpe"), QStringLiteral("bmp"), QStringLiteral("gif"),
        QStringLiteral("ico"), QStringLiteral("webp"), QStringLiteral("tga")
    };
    return formats;
}

// Everything the code editor can open: the player's QML is UTF-8 and so are
// the JS/SVG/JSON/XML files next to it. Compiled caches (.qmlc) are excluded on
// purpose - they are bytecode, not source.
const QStringList &textFormats()
{
    static const QStringList formats {
        QStringLiteral("qml"), QStringLiteral("js"), QStringLiteral("mjs"),
        QStringLiteral("qmltypes"), QStringLiteral("json"), QStringLiteral("xml"),
        QStringLiteral("svg"), QStringLiteral("html"), QStringLiteral("htm"),
        QStringLiteral("css"), QStringLiteral("txt"), QStringLiteral("conf"),
        QStringLiteral("ini"), QStringLiteral("cfg"), QStringLiteral("ts"),
        QStringLiteral("glsl"), QStringLiteral("frag"), QStringLiteral("vert"),
        QStringLiteral("shader"), QStringLiteral("csv")
    };
    return formats;
}

QString extensionOf(const QString &path)
{
    const QString name = path.section(QLatin1Char('/'), -1);
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot + 1 >= name.size()) return QString();
    return name.mid(dot + 1).toLower();
}

QString shortName(const QString &path)
{
    return path.section(QLatin1Char('/'), -1);
}

// Every resource the file registers, plus the main QML pool. Same enumeration
// the About page and the old Images tab use: not every pool is registered
// through qRegisterResourceData, and the UI files live in the same tree.
QList<QtResources::File> allFiles(const QByteArray &image)
{
    QList<QtResources::File> files = QtResources::registeredFiles(image);
    const auto theme = ThemeEngine::findThemeZlib(image);
    if (theme.hasQrcNode) {
        for (const auto &file : QtResources::filesInPool(image, theme.resource)) {
            bool present = false;
            for (const auto &known : files)
                if (known.entryOff == file.entryOff && known.name == file.name) present = true;
            if (!present) files << file;
        }
    }
    return files;
}

qint64 decodedSize(const QByteArray &image, qint64 entryOff, quint16 flags)
{
    if (entryOff < 0 || !ByteUtils::inRange(image, entryOff, 4)) return 0;
    if (flags == 1 && ByteUtils::inRange(image, entryOff, 8))
        return ByteUtils::be32(image, entryOff + 4);
    return ByteUtils::be32(image, entryOff);
}

bool looksLikeTga(const QByteArray &bytes)
{
    if (bytes.size() < 18) return false;
    const int mapType = quint8(bytes.at(1));
    const int imageType = quint8(bytes.at(2));
    const bool knownType = imageType == 1 || imageType == 2 || imageType == 3
        || imageType == 9 || imageType == 10 || imageType == 11;
    return mapType <= 1 && knownType;
}

QSize sniffSize(const QByteArray &bytes, const QString &format)
{
    if (format == QLatin1String("svg")) {
        const QString head = QString::fromUtf8(bytes.left(4096));
        static const QRegularExpression attr(
            QStringLiteral("\\b(width|height)\\s*=\\s*[\"']([0-9.]+)(?:px)?[\"']"),
            QRegularExpression::CaseInsensitiveOption);
        int width = 0, height = 0;
        auto iter = attr.globalMatch(head);
        while (iter.hasNext()) {
            const auto match = iter.next();
            const int value = qRound(match.captured(2).toDouble());
            if (match.captured(1).compare(QLatin1String("width"), Qt::CaseInsensitive) == 0)
                width = width == 0 ? value : width;
            else
                height = height == 0 ? value : height;
        }
        if (width > 0 && height > 0) return QSize(width, height);
        static const QRegularExpression viewBox(
            QStringLiteral("\\bviewBox\\s*=\\s*[\"']\\s*[-0-9.]+\\s+[-0-9.]+\\s+([0-9.]+)\\s+([0-9.]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto box = viewBox.match(head);
        if (box.hasMatch())
            return QSize(qRound(box.captured(1).toDouble()), qRound(box.captured(2).toDouble()));
        return {};
    }
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    return reader.size(); // header only: no full decode for a list entry
}

// ---------------------------------------------------------------------------
// Byte-level picture scan
//
// The old SVG tab has always found every icon in every build, including the
// ones whose resource pools this tool cannot map: it never asked the tree for
// permission, it looked for "<svg" in the bytes. The pool walk above needs a
// registration this build recognises (a qRegisterResourceData import, an x64
// EIP-relative call, a tree whose root is a directory). A build that imports
// the symbol under another name, that was linked statically, or whose .text is
// packed is invisible to it - and then the Resources tab lists nothing while
// the SVG tab happily lists 400 icons.
//
// So the bytes are scanned too, for every picture kind, with one extra check
// that keeps the result honest: a hit only counts when the four bytes in front
// of it are the size of the blob that follows. That is exactly the shape a raw
// Qt resource payload has ([be32 stored][payload]), so an accepted hit is a
// real record the player loads - and therefore one this tab can preview,
// extract and replace in place - while a stray "GIF8" inside a texture or an
// "<svg" inside a JavaScript string is rejected.
struct ScannedBlob
{
    qint64 record = -1;  // offset of the [size][payload] record
    qint64 stored = 0;   // payload bytes the record holds
    QString format;
};

bool recordBehind(const QByteArray &image, qint64 start, qint64 length, qint64 *stored)
{
    if (start < 4 || length <= 0 || length > kMaxEntry) return false;
    const qint64 size = ByteUtils::be32(image, start - 4);
    // A payload may end in the whitespace the source file had; anything more
    // than a few bytes of it means this is not the record that owns the blob.
    if (size < length || size > length + 16) return false;
    if (!ByteUtils::inRange(image, start - 4, size + 4)) return false;
    for (qint64 at = start + length; at < start + size; ++at) {
        const char c = image.at(int(at));
        if (c != '\0' && c != '\r' && c != '\n' && c != '\t' && c != ' ') return false;
    }
    *stored = size;
    return true;
}

void scanSvg(const QByteArray &image, QList<ScannedBlob> *out)
{
    for (qsizetype at = image.indexOf("<svg"); at >= 0; at = image.indexOf("<svg", at + 4)) {
        const qsizetype close = image.indexOf("</svg>", at);
        if (close < 0) break;
        const qint64 end = qint64(close) + 6;
        qint64 stored = 0;
        if (!recordBehind(image, at, end - at, &stored)) continue;
        out->append(ScannedBlob { qint64(at) - 4, stored, QStringLiteral("svg") });
        at = qsizetype(end) - 4;
    }
}

void scanPng(const QByteArray &image, QList<ScannedBlob> *out)
{
    const QByteArray magic = QByteArray::fromHex("89504e470d0a1a0a");
    for (qsizetype at = image.indexOf(magic); at >= 0; at = image.indexOf(magic, at + 8)) {
        qsizetype pos = at + 8;
        qint64 end = -1;
        while (pos + 8 <= image.size()) {
            const quint32 length = ByteUtils::be32(image, pos);
            if (length > quint32(kMaxEntry)) break;
            const QByteArray type = image.mid(pos + 4, 4);
            const qint64 next = qint64(pos) + 12 + qint64(length);
            if (next > image.size()) break;
            pos = qsizetype(next);
            if (type == QByteArray("IEND")) { end = next; break; }
        }
        if (end < 0) continue;
        qint64 stored = 0;
        if (!recordBehind(image, at, end - at, &stored)) continue;
        out->append(ScannedBlob { qint64(at) - 4, stored, QStringLiteral("png") });
        at = qsizetype(end) - 8;
    }
}

void scanJpeg(const QByteArray &image, QList<ScannedBlob> *out)
{
    const QByteArray soi = QByteArray::fromHex("ffd8ff");
    const QByteArray eoi = QByteArray::fromHex("ffd9");
    for (qsizetype at = image.indexOf(soi); at >= 0; at = image.indexOf(soi, at + 3)) {
        qsizetype search = at + 3;
        qint64 end = -1;
        qint64 stored = 0;
        while (true) {
            const qsizetype close = image.indexOf(eoi, search);
            if (close < 0) break;
            const qint64 candidate = qint64(close) + 2;
            if (candidate - at > kMaxEntry) break;
            if (recordBehind(image, at, candidate - at, &stored)) { end = candidate; break; }
            search = close + 2; // an FFD9 inside the scan data: keep looking
        }
        if (end < 0) continue;
        out->append(ScannedBlob { qint64(at) - 4, stored, QStringLiteral("jpg") });
        at = qsizetype(end) - 3;
    }
}

void scanGif(const QByteArray &image, QList<ScannedBlob> *out)
{
    for (qsizetype at = image.indexOf("GIF8"); at >= 0; at = image.indexOf("GIF8", at + 4)) {
        if (at + 13 > image.size()) continue;
        const QByteArray version = image.mid(at + 4, 2);
        if (version != QByteArray("7a") && version != QByteArray("9a")) continue;
        const qint64 packed = quint8(image.at(at + 10));
        qsizetype pos = at + 13;
        if (packed & 0x80) pos += 3 * (1 << ((packed & 0x07) + 1));
        bool ok = pos <= image.size();
        while (ok && pos < image.size()) {
            const qint64 block = quint8(image.at(pos));
            if (block == 0x3b) { ++pos; break; }             // trailer
            if (block == 0x21) {                             // extension
                pos += 2;
                while (pos < image.size()) {
                    const qint64 length = quint8(image.at(pos));
                    ++pos;
                    if (length == 0) break;
                    pos += length;
                }
                continue;
            }
            if (block == 0x2c) {                             // image descriptor
                if (pos + 10 > image.size()) { ok = false; break; }
                const qint64 flags = quint8(image.at(pos + 9));
                pos += 10;
                if (flags & 0x80) pos += 3 * (1 << ((flags & 0x07) + 1));
                ++pos;                                       // LZW minimum code size
                while (pos < image.size()) {
                    const qint64 length = quint8(image.at(pos));
                    ++pos;
                    if (length == 0) break;
                    pos += length;
                }
                continue;
            }
            ok = false;
            break;
        }
        if (!ok || pos > image.size()) continue;
        qint64 stored = 0;
        if (!recordBehind(image, at, pos - at, &stored)) continue;
        out->append(ScannedBlob { qint64(at) - 4, stored, QStringLiteral("gif") });
        at = pos - 4;
    }
}

void scanBmp(const QByteArray &image, QList<ScannedBlob> *out)
{
    for (qsizetype at = image.indexOf("BM"); at >= 0; at = image.indexOf("BM", at + 2)) {
        if (at + 26 > image.size()) continue;
        const qint64 size = ByteUtils::le32(image, at + 2);
        if (size < 26 || size > kMaxEntry || qint64(at) + size > image.size()) continue;
        qint64 stored = 0;
        if (!recordBehind(image, at, size, &stored)) continue;
        out->append(ScannedBlob { qint64(at) - 4, stored, QStringLiteral("bmp") });
        at = qsizetype(at + size) - 2;
    }
}

void scanWebp(const QByteArray &image, QList<ScannedBlob> *out)
{
    for (qsizetype at = image.indexOf("RIFF"); at >= 0; at = image.indexOf("RIFF", at + 4)) {
        if (at + 12 > image.size()) continue;
        if (image.mid(at + 8, 4) != QByteArray("WEBP")) continue;
        const qint64 total = qint64(ByteUtils::le32(image, at + 4)) + 8;
        if (total < 12 || total > kMaxEntry || qint64(at) + total > image.size()) continue;
        qint64 stored = 0;
        if (!recordBehind(image, at, total, &stored)) continue;
        out->append(ScannedBlob { qint64(at) - 4, stored, QStringLiteral("webp") });
        at = qsizetype(at + total) - 4;
    }
}

QList<ScannedBlob> scanPictureRecords(const QByteArray &image)
{
    QList<ScannedBlob> out;
    scanSvg(image, &out);
    scanPng(image, &out);
    scanJpeg(image, &out);
    scanGif(image, &out);
    scanBmp(image, &out);
    scanWebp(image, &out);
    return out;
}

// The entry the list row refers to, located again from the file on disk: the
// list on screen may be older than the EXE.
bool locate(const QByteArray &image, const QString &path, Entry *out, QString *error)
{
    int matches = 0;
    for (const auto &entry : list(image, true)) {
        if (entry.path != path) continue;
        *out = entry;
        ++matches;
    }
    if (matches == 1) return true;
    if (error) {
        *error = matches == 0
            ? QStringLiteral("%1 is not embedded in this EXE any more; scan again.").arg(shortName(path))
            : QStringLiteral("This EXE carries %1 entries named %2; refusing to guess.")
                  .arg(matches)
                  .arg(shortName(path));
    }
    return false;
}

bool samePictureFamily(const QString &entryFormat, const QByteArray &bytes, const QString &filePath,
                       QString *error)
{
    const QString want = entryFormat == QLatin1String("jpeg") || entryFormat == QLatin1String("jpe")
        ? QStringLiteral("jpg")
        : entryFormat;
    QString got = EmbeddedImages::sniffFormat(bytes);
    if (got.isEmpty() && want == QLatin1String("tga") && extensionOf(filePath) == QLatin1String("tga")
        && looksLikeTga(bytes))
        got = QStringLiteral("tga");
    if (got.isEmpty()) {
        *error = QStringLiteral("The chosen file is not a recognised picture; the EXE was not changed.");
        return false;
    }
    if (got != want) {
        *error = QStringLiteral("This entry is a %1, but the replacement is a %2. Pick a %1 file so the "
                                "player still loads it; the EXE was not changed.")
                     .arg(want.toUpper(), got.toUpper());
        return false;
    }
    // TGA, WEBP and SVG have no universal decoder here; their signature is the check.
    if (want != QLatin1String("tga") && want != QLatin1String("webp") && want != QLatin1String("svg")) {
        QImage probe;
        if (!probe.loadFromData(bytes)) {
            *error = QStringLiteral("The replacement picture cannot be decoded; the EXE was not changed.");
            return false;
        }
    }
    return true;
}

} // namespace

QList<Entry> list(const QByteArray &image, bool includeBinary)
{
    QList<Entry> out;
    if (image.isEmpty()) return out;
    QSet<qint64> seen;
    for (const auto &file : allFiles(image)) {
        if (file.entryOff < 0 || seen.contains(file.entryOff)) continue;
        seen.insert(file.entryOff);
        Entry entry;
        entry.path = file.path.isEmpty() ? file.name : file.path;
        entry.leaf = file.name.isEmpty() ? shortName(entry.path) : file.name;
        entry.folder = entry.path.section(QLatin1Char('/'), 0, -2);
        entry.format = extensionOf(entry.path);
        entry.entryOff = file.entryOff;
        entry.flags = file.flags;
        entry.size = decodedSize(image, entry.entryOff, file.flags);
        entry.stored = ByteUtils::be32(image, entry.entryOff);
        if (entry.format == QLatin1String("svg")) {
            entry.kind = Kind::Svg;
            entry.text = true;
        } else if (pictureFormats().contains(entry.format)) {
            entry.kind = Kind::Image;
        } else if (textFormats().contains(entry.format)) {
            entry.kind = Kind::Text;
            entry.text = true;
        } else {
            entry.kind = Kind::Binary;
        }
        // A build may carry a picture under a name the extension table does not
        // know (`.dat`, `.asset`, nothing at all). The bytes are the better
        // judge, and only a small header is read to decide.
        if (entry.kind == Kind::Binary && entry.size > 0 && entry.size <= kMaxEntry) {
            const QByteArray head = QtResources::readFile(image, file, 512);
            const QString sniffed = EmbeddedImages::sniffFormat(head);
            if (sniffed == QLatin1String("svg")) {
                entry.kind = Kind::Svg;
                entry.text = true;
                entry.format = sniffed;
            } else if (!sniffed.isEmpty()) {
                entry.kind = Kind::Image;
                entry.format = sniffed;
            }
        }
        if (!includeBinary && entry.kind == Kind::Binary) continue;
        if ((entry.kind == Kind::Image || entry.kind == Kind::Svg)
            && entry.size > 0 && entry.size <= kMaxEntry) {
            const QByteArray bytes = QtResources::readFile(image, file, kMaxEntry);
            if (bytes.size() == entry.size) {
                const QSize size = sniffSize(bytes, entry.format);
                entry.width = size.width();
                entry.height = size.height();
            }
        }
        out << entry;
    }

    // Whatever the pool walk found, the bytes are scanned as well. On a build
    // whose pools cannot be mapped this is what fills the tab (the same hits the
    // SVG tab has always shown); on a build where the walk works the scan adds
    // nothing, because a record that is already listed is skipped.
    QList<QPair<qint64, qint64>> owned;
    owned.reserve(out.size());
    for (const auto &entry : out)
        owned << qMakePair(entry.entryOff, entry.entryOff + 4 + entry.stored);
    for (const auto &blob : scanPictureRecords(image)) {
        if (blob.record < 0 || seen.contains(blob.record)) continue;
        bool covered = false;
        for (const auto &range : owned) {
            if (blob.record >= range.first && blob.record < range.second) { covered = true; break; }
        }
        if (covered) continue;
        seen.insert(blob.record);
        Entry entry;
        entry.scanned = true;
        entry.format = blob.format;
        entry.path = QStringLiteral("scanned/%1_%2.%3")
                         .arg(blob.format)
                         .arg(blob.record, 8, 16, QLatin1Char('0'))
                         .arg(blob.format);
        entry.leaf = shortName(entry.path);
        entry.folder = QStringLiteral("scanned");
        entry.entryOff = blob.record;
        entry.flags = 0;
        entry.size = blob.stored;
        entry.stored = blob.stored;
        entry.kind = blob.format == QLatin1String("svg") ? Kind::Svg : Kind::Image;
        entry.text = entry.kind == Kind::Svg;
        const QByteArray bytes = image.mid(int(blob.record) + 4, int(blob.stored));
        const QSize size = sniffSize(bytes, entry.format);
        entry.width = size.width();
        entry.height = size.height();
        out << entry;
    }

    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
        const int byName = QString::compare(a.path, b.path, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.entryOff < b.entryOff;
    });
    return out;
}

QByteArray readEntry(const QString &exePath, qint64 entryOff, quint16 flags, qint64 limit)
{
    return EmbeddedImages::readEntry(exePath, entryOff, flags, limit);
}

QString decodeText(const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > kMaxEntry) return {};
    QByteArray body = bytes;
    if (body.startsWith(QByteArray::fromHex("efbbbf"))) body = body.mid(3);
    if (body.contains('\0')) return {}; // UTF-16 or binary
    const QString text = QString::fromUtf8(body);
    if (text.toUtf8() != body) return {}; // not valid UTF-8
    return text;
}

QString previewDataUrl(const QByteArray &bytes, const QString &format, int maxSide)
{
    return EmbeddedImages::previewDataUrl(bytes, format, maxSide);
}

QString previewFileUrl(const QByteArray &bytes, const QString &format)
{
    return EmbeddedImages::previewFileUrl(bytes, format);
}

Budget budget(const QByteArray &image, qint64 entryOff, const QString &leaf, bool inPlace)
{
    Budget out;
    if (entryOff < 0 || !ByteUtils::inRange(image, entryOff, 4)) {
        out.detail = QStringLiteral("The entry is outside the file.");
        return out;
    }
    out.used = ByteUtils::be32(image, entryOff);
    if (inPlace) {
        // A scanned record has no tree node, so it is never moved: the room it
        // has is the room it already uses.
        if (out.used <= 0 || out.used > kMaxEntry || !ByteUtils::inRange(image, entryOff + 4, out.used)) {
            out.detail = QStringLiteral("The scanned record is not readable.");
            return out;
        }
        out.known = true;
        out.capacity = out.used;
        out.detail = QStringLiteral("%1 bytes, rewritten in place (never moved, never grown)").arg(out.used);
        return out;
    }
    QString error;
    const qint64 capacity = QtResources::inPlaceCapacity(image, entryOff, leaf, &error);
    if (capacity < 0) {
        out.detail = error;
        return out;
    }
    out.known = true;
    out.capacity = capacity;
    out.detail = QStringLiteral("%1 of %2 bytes used, %3 free")
                     .arg(out.used)
                     .arg(out.capacity)
                     .arg(capacity - out.used);
    return out;
}

qint64 payloadSizeFor(const QByteArray &bytes, quint16 flags)
{
    if (flags == 0) return bytes.size();
    if (bytes.isEmpty()) return 0;
    return qCompress(bytes, 9).size();
}

Result replaceBytes(const QString &exePath, const QString &path, const QByteArray &bytes, QByteArray *preparedImage)
{
    if (preparedImage) preparedImage->clear();
    if (!preparedImage && BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before changing an embedded file.") };
    if (bytes.isEmpty())
        return { false, QStringLiteral("An empty file would break the player; nothing was written.") };
    if (bytes.size() > kMaxEntry)
        return { false, QStringLiteral("That file is larger than 32 MB; nothing was written.") };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read the selected EXE.") };
    const QByteArray original = file.readAll();
    file.close();

    Entry entry;
    QString error;
    if (!locate(original, path, &entry, &error)) return { false, error };
    if (entry.format == QLatin1String("qmlc") || entry.format == QLatin1String("jsc"))
        return { false, QStringLiteral("Compiled caches are read-only. Edit their source; matching caches are invalidated by the verified writer.") };
    if ((entry.kind == Kind::Image || entry.kind == Kind::Svg)
        && !samePictureFamily(entry.format, bytes, entry.path, &error)) return { false, error };

    if (entry.scanned) {
        // No tree node owns this record, so it is only ever rewritten in place:
        // the file cannot grow and no other entry's offset moves. The record
        // keeps its slot and the rest of it is zero-filled, exactly like the
        // in-place branch of the pool writer.
        if (bytes.size() > entry.stored)
            return { false, QStringLiteral("%1 holds %2 bytes and the replacement is %3. A scanned "
                                           "record is never moved or grown, so it has to be the same "
                                           "size or smaller; nothing was written.")
                                .arg(entry.leaf)
                                .arg(entry.stored)
                                .arg(bytes.size()) };
        QByteArray changed = original;
        QByteArray record(4, '\0');
        ByteUtils::putBe32(record, 0, quint32(bytes.size()));
        record += bytes;
        record += QByteArray(int(4 + entry.stored) - int(record.size()), '\0');
        changed.replace(int(entry.entryOff), int(record.size()), record);
        if (changed.size() != original.size()
            || changed.mid(int(entry.entryOff) + 4, int(bytes.size())) != bytes)
            return { false, QStringLiteral("The rewrite did not read back correctly; the EXE was not changed.") };
        if (preparedImage) { *preparedImage = changed; return { true, QStringLiteral("Replacement verified in memory; no files changed.") }; }
        if (!ThemeEngine::backup(exePath))
            return { false, QStringLiteral("Could not preserve the original backup; no EXE was written.") };
        if (!PatchFile::writeVerified(exePath, original, changed, &error)) return { false, error };
        return { true, QStringLiteral("%1 replaced (%2 bytes, was %3). Restart BlueStacks to see it.")
                           .arg(entry.leaf)
                           .arg(bytes.size())
                           .arg(entry.size) };
    }

    const auto link = QtResources::findLink(original, entry.entryOff, entry.leaf);
    if (!link.found || link.ambiguous)
        return { false, QStringLiteral("The %1 resource mapping is ambiguous; no file was written.")
                            .arg(entry.leaf) };

    // Decoded size is not storage capacity. A larger decoded resource is
    // allowed when its payload fits verified storage; the EXE still cannot grow.
    // HARD RULE 2 - the EXE itself never grows. The record has to fit the space
    // this entry already owns (its record plus the zero padding behind it), or
    // be moved into free space the file already contains, which the writer
    // only does when it is handed a destination that is verified empty and
    // mapped. Nothing is ever appended and no section changes size.
    const Budget room = budget(original, entry.entryOff, entry.leaf);
    if (!room.known)
        return { false, room.detail.isEmpty()
                            ? QStringLiteral("The space this entry owns could not be measured, so nothing "
                                             "was written.")
                            : room.detail };

    const qint64 payload = payloadSizeFor(bytes, entry.flags);
    qint64 destination = -1;
    if (payload > room.capacity) {
        // The compressed payload no longer fits its own slot. Rebuilding it in
        // spare space inside the same file keeps both size rules, provided the
        // file itself is not bigger (checked above).
        destination = QtResources::findFreeSpace(original, link, 4 + payload, &error);
        if (destination < 0)
            return { false, QStringLiteral("%1 needs %2 bytes but its slot only holds %3 and there is no "
                                           "free space left in the EXE to move it to. Nothing was "
                                           "written - make the edit shorter.")
                                .arg(entry.leaf)
                                .arg(payload)
                                .arg(room.capacity) };
    }

    QByteArray changed = original;
    qint64 newEntry = entry.entryOff;
    if (!QtResources::replaceResource(changed, entry.entryOff, link, bytes, &error, &newEntry,
                                      /*allowGrowth=*/false, destination))
        return { false, error.isEmpty()
                            ? QStringLiteral("The resource could not be rewritten; no file was written.")
                            : error };
    if (changed.size() != original.size())
        return { false, QStringLiteral("The rewrite would have changed the EXE size; nothing was written.") };
    // Read the new record back the way the player will, before the disk is touched.
    const auto check = QtResources::findLink(changed, newEntry, entry.leaf);
    if (!check.found || check.ambiguous
        || QtResources::readFile(changed, { entry.leaf, newEntry, entry.flags }, kMaxEntry) != bytes)
        return { false, QStringLiteral("The rewritten resource did not read back correctly; the EXE was "
                                       "not changed.") };
    if (preparedImage) { *preparedImage = changed; return { true, QStringLiteral("Replacement verified in memory; no files changed.") }; }
    if (!ThemeEngine::backup(exePath))
        return { false, QStringLiteral("Could not preserve the original backup; no EXE was written.") };
    if (!PatchFile::writeVerified(exePath, original, changed, &error)) return { false, error };
    const int caches = QtResources::clearPlayerQmlCache(exePath);

    QString msg = newEntry == entry.entryOff
        ? QStringLiteral("%1 replaced (%2 bytes, was %3). Restart BlueStacks to see it.")
              .arg(entry.leaf)
              .arg(bytes.size())
              .arg(entry.size)
        : QStringLiteral("%1 replaced (%2 bytes, was %3) and moved into free space inside the EXE, so "
                         "the file itself is the same size. Restart BlueStacks to see it.")
              .arg(entry.leaf)
              .arg(bytes.size())
              .arg(entry.size);
    if (caches > 0)
        msg += QStringLiteral(" Cleared the player QML disk cache so the new source is loaded.");
    return { true, msg };
}

Result replaceText(const QString &exePath, const QString &path, const QString &text)
{
    if (text.trimmed().isEmpty())
        return { false, QStringLiteral("The editor is empty - that would break the page. Nothing was written.") };
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read the selected EXE.") };
    const QByteArray original = file.readAll();
    file.close();
    Entry entry;
    QString error;
    if (!locate(original, path, &entry, &error)) return { false, error };
    if (!entry.text)
        return { false, QStringLiteral("%1 is not a text resource.").arg(entry.leaf) };
    QByteArray bytes = text.toUtf8();
    const QByteArray existing = readEntry(exePath, entry.entryOff, entry.flags);
    if (existing.startsWith(QByteArray::fromHex("efbbbf"))) bytes.prepend(QByteArray::fromHex("efbbbf"));
    return replaceBytes(exePath, path, bytes);
}

Result replacePicture(const QString &exePath, const QString &path, const QString &newImagePath)
{
    QFile source(newImagePath);
    if (!source.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read the replacement picture.") };
    const QByteArray bytes = source.readAll();
    source.close();
    if (bytes.isEmpty())
        return { false, QStringLiteral("The replacement picture is empty; the EXE was not changed.") };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read the selected EXE.") };
    const QByteArray original = file.readAll();
    file.close();
    Entry entry;
    QString error;
    if (!locate(original, path, &entry, &error)) return { false, error };
    if (entry.kind != Kind::Image && entry.kind != Kind::Svg)
        return { false, QStringLiteral("%1 is not a picture resource.").arg(entry.leaf) };
    if (!samePictureFamily(entry.format, bytes, newImagePath, &error)) return { false, error };
    return replaceBytes(exePath, path, bytes);
}

} // namespace EmbeddedResources
