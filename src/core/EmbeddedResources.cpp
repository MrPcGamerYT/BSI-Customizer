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

Budget budget(const QByteArray &image, qint64 entryOff, const QString &leaf)
{
    Budget out;
    if (entryOff < 0 || !ByteUtils::inRange(image, entryOff, 4)) {
        out.detail = QStringLiteral("The entry is outside the file.");
        return out;
    }
    out.used = ByteUtils::be32(image, entryOff);
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

Result replaceBytes(const QString &exePath, const QString &path, const QByteArray &bytes)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
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

    const auto link = QtResources::findLink(original, entry.entryOff, entry.leaf);
    if (!link.found || link.ambiguous)
        return { false, QStringLiteral("The %1 resource mapping is ambiguous; no file was written.")
                            .arg(entry.leaf) };

    // HARD RULE 1 - the embedded file itself never grows. A replacement or an
    // edit that needs more bytes than the entry already decodes to is refused
    // outright: the player's resource layout is packed, so a bigger file is
    // what breaks it but there is no need to find out.
    if (entry.size > 0 && bytes.size() > entry.size) {
        return { false, QStringLiteral("%1 would grow from %2 to %3 bytes. An embedded file is never "
                                       "made bigger, so nothing was written - shorten it by at least "
                                       "%4 bytes and try again.")
                            .arg(entry.leaf)
                            .arg(entry.size)
                            .arg(bytes.size())
                            .arg(bytes.size() - entry.size) };
    }

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
