#include "EmbeddedImages.h"

#include "BsDetector.h"
#include "ByteUtils.h"
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
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>

#include <algorithm>

namespace EmbeddedImages {
namespace {

// A single embedded picture larger than this is not previewed or replaced: the
// player's own images are all far smaller, and the resource rewrite has to hold
// both versions in memory.
constexpr qint64 kMaxImage = 32 * 1024 * 1024;

const QStringList &imageFamilies()
{
    static const QStringList families {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"),
        QStringLiteral("gif"), QStringLiteral("ico"), QStringLiteral("webp"),
        QStringLiteral("tga"), QStringLiteral("svg")
    };
    return families;
}

QString extensionOf(const QString &path)
{
    const QString name = path.section(QLatin1Char('/'), -1);
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot + 1 >= name.size()) return QString();
    return name.mid(dot + 1).toLower();
}

// jpg and jpeg are the same format; the pools name both.
QString familyOf(const QString &path)
{
    const QString ext = extensionOf(path);
    if (ext == QLatin1String("jpeg") || ext == QLatin1String("jpe")) return QStringLiteral("jpg");
    if (ext == QLatin1String("svgz")) return QString();
    return ext;
}

QString displayFormat(const QString &family)
{
    if (family == QLatin1String("jpg")) return QStringLiteral("JPEG");
    return family.toUpper();
}

QString shortName(const QString &path)
{
    return path.section(QLatin1Char('/'), -1);
}

// Every resource the file registers, plus the main QML pool. Mirrors how the
// About page locates PlrAboutSetting.qml: not every pool is registered through
// qRegisterResourceData, and the images live in the same tree as the QML.
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

// The decoded length of a record, without decompressing it. The record's own
// 4-byte prefix is the length as stored (the compressed length for zlib
// entries); a compressed payload carries the uncompressed length in its own
// qCompress header, right behind it.
qint64 decodedSize(const QByteArray &image, qint64 entryOff, quint16 flags)
{
    if (entryOff < 0 || !ByteUtils::inRange(image, entryOff, 4)) return 0;
    if (flags == 1 && ByteUtils::inRange(image, entryOff, 8))
        return ByteUtils::be32(image, entryOff + 4);
    return ByteUtils::be32(image, entryOff);
}

QSize svgSize(const QByteArray &bytes)
{
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
    if (box.hasMatch()) return QSize(qRound(box.captured(1).toDouble()), qRound(box.captured(2).toDouble()));
    return {};
}

QSize rasterSize(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    return reader.size(); // header only: no full decode for a list entry
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

bool plausibleBmp(const QByteArray &bytes)
{
    if (bytes.size() < 26 || !bytes.startsWith(QByteArrayLiteral("BM"))) return false;
    const quint32 declared = ByteUtils::le32(bytes, 2);
    const quint32 pixelOffset = ByteUtils::le32(bytes, 10);
    return (declared == quint32(bytes.size()) || (declared > 26 && declared <= quint32(bytes.size())))
        && pixelOffset >= 26 && pixelOffset <= quint32(bytes.size());
}

} // namespace

QString sniffFormat(const QByteArray &bytes)
{
    if (bytes.size() < 8) return {};
    if (bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) return QStringLiteral("png");
    if (bytes.startsWith(QByteArray::fromHex("ffd8ff"))) return QStringLiteral("jpg");
    if (bytes.startsWith(QByteArrayLiteral("GIF87a")) || bytes.startsWith(QByteArrayLiteral("GIF89a")))
        return QStringLiteral("gif");
    if (plausibleBmp(bytes)) return QStringLiteral("bmp");
    if (bytes.size() >= 12 && bytes.startsWith(QByteArrayLiteral("RIFF"))
        && bytes.mid(8, 4) == QByteArrayLiteral("WEBP"))
        return QStringLiteral("webp");
    if (bytes.startsWith(QByteArray::fromHex("00000100"))) return QStringLiteral("ico");
    const QByteArray head = bytes.left(4096);
    if (head.contains("<svg")) {
        QByteArray trimmed = head;
        if (trimmed.startsWith(QByteArray::fromHex("efbbbf"))) trimmed = trimmed.mid(3);
        trimmed = trimmed.trimmed();
        if (trimmed.startsWith(QByteArrayLiteral("<svg")) || trimmed.startsWith(QByteArrayLiteral("<?xml")))
            return QStringLiteral("svg");
    }
    return {};
}

QList<Asset> list(const QByteArray &image, bool includeAll)
{
    QList<Asset> out;
    if (image.isEmpty()) return out;
    QSet<qint64> seen;
    for (const auto &file : allFiles(image)) {
        if (file.entryOff < 0 || seen.contains(file.entryOff)) continue;
        Asset asset;
        // filesInPool rebuilds the rcc path ("images/logo.png") from the tree's
        // directory nodes; fall back to the leaf for flat pools.
        asset.path = file.path.isEmpty() ? file.name : file.path;
        asset.family = familyOf(asset.path);
        asset.image = imageFamilies().contains(asset.family);
        if (!asset.image && !includeAll) continue;
        seen.insert(file.entryOff);
        asset.entryOff = file.entryOff;
        asset.flags = file.flags;
        asset.size = decodedSize(image, file.entryOff, file.flags);
        if (asset.image && asset.size > 0 && asset.size <= kMaxImage) {
            const QByteArray bytes = QtResources::readFile(image, file, kMaxImage);
            if (bytes.size() == asset.size) {
                const QSize size = asset.family == QLatin1String("svg") ? svgSize(bytes)
                                                                       : rasterSize(bytes);
                if (!size.isEmpty()) {
                    asset.width = size.width();
                    asset.height = size.height();
                }
                // A picture the player can load but this build cannot decode is
                // still replaceable; only the preview gives up, and the entry
                // simply reports no dimensions.
            }
        }
        out << asset;
    }
    std::sort(out.begin(), out.end(), [](const Asset &a, const Asset &b) {
        const int byName = QString::compare(a.path, b.path, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.entryOff < b.entryOff;
    });
    return out;
}

QByteArray readEntry(const QString &exePath, qint64 entryOff, quint16 flags, qint64 limit)
{
    if (entryOff < 0 || limit <= 0) return {};
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (!file.seek(entryOff)) return {};
    const QByteArray head = file.read(4);
    if (head.size() != 4) return {};
    const quint32 stored = ByteUtils::be32(head, 0);
    if (stored == 0 || qint64(stored) > limit + 65536) return {};
    const QByteArray payload = file.read(stored);
    if (payload.size() != int(stored)) return {};
    if (flags == 0) return stored <= limit ? payload : QByteArray();
    if (flags != 1) return {};
    const QByteArray bytes = qUncompress(payload);
    return bytes.size() <= limit ? bytes : QByteArray();
}

namespace {

// SVG arrives as plain markup in every build seen so far, but a pool may name
// the same markup differently (`.dat`, no extension at all). The bytes decide,
// never the file name: an entry the list called "binary" still gets a preview.
bool looksLikeSvg(const QByteArray &bytes)
{
    return bytes.left(4096).toLower().contains("<svg");
}

bool looksLikeGzip(const QByteArray &bytes)
{
    return bytes.size() > 18 && bytes.startsWith(QByteArray::fromHex("1f8b"));
}

} // namespace

QString previewDataUrl(const QByteArray &bytes, const QString &family, int maxSide, bool rasterSvg)
{
    if (bytes.isEmpty() || bytes.size() > kMaxImage || looksLikeGzip(bytes)) return {};
    QString format = family.toLower();
    // The extension is a hint, not the truth: a build may name the same markup
    // differently, and an entry with no extension must still preview.
    if (format != QLatin1String("svg") && looksLikeSvg(bytes))
        format = QStringLiteral("svg");
    if (format == QLatin1String("svg") && !rasterSvg) {
        // QML renders SVG data URLs itself; base64 keeps the markup intact
        // whatever quotes or whitespace it contains.
        if (!bytes.contains("<svg"))
            return {}; // a fragment without a root element cannot be drawn
        return QStringLiteral("data:image/svg+xml;base64,")
            + QString::fromLatin1(bytes.toBase64());
    }
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize original = reader.size();
    if (maxSide > 0 && original.isValid()
        && ((rasterSvg && format == QLatin1String("svg")) || original.width() > maxSide || original.height() > maxSide))
        reader.setScaledSize(original.scaled(maxSide, maxSide, Qt::KeepAspectRatio));
    const QImage picture = reader.read();
    if (picture.isNull()) return {};
    QByteArray png;
    QBuffer encoded(&png);
    if (!encoded.open(QIODevice::WriteOnly) || !picture.save(&encoded, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
}

QString previewFileUrl(const QByteArray &bytes, const QString &family)
{
    if (bytes.isEmpty() || bytes.size() > kMaxImage) return {};
    // A data: URL carries no extension, and picture plugins decide what they
    // can read from the suffix as much as from the bytes. Writing the entry
    // into the session's temp folder under its own extension gives QML the
    // same thing the player gets from its resource: a file with a name.
    static QTemporaryDir directory;
    if (!directory.isValid()) return {};
    QString suffix = family.toLower();
    if (suffix == QLatin1String("jpeg") || suffix == QLatin1String("jpe"))
        suffix = QStringLiteral("jpg");
    if (suffix != QLatin1String("svg") && looksLikeSvg(bytes))
        suffix = QStringLiteral("svg");
    if (suffix.isEmpty()) suffix = QStringLiteral("bin");
    // Enough of a key to keep two entries apart without hashing 32 MB: the
    // length plus the first page of bytes.
    quint32 key = quint32(bytes.size());
    const int sample = int(qMin<qsizetype>(bytes.size(), 4096));
    for (int i = 0; i < sample; ++i)
        key = key * 16777619u ^ quint8(bytes.at(i));
    const QString name = QStringLiteral("bsi-%1.%2")
                             .arg(QString::number(key, 16))
                             .arg(suffix);
    const QString path = directory.filePath(name);
    if (!QFileInfo::exists(path) || QFileInfo(path).size() != bytes.size()) {
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) return {};
        out.close();
    }
    return QUrl::fromLocalFile(path).toString();
}

Result replace(const QString &exePath, const QString &path, const QString &newImagePath)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before replacing an embedded image.") };

    QFile source(newImagePath);
    if (!source.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read the replacement image.") };
    const QByteArray replacement = source.readAll();
    source.close();
    if (replacement.isEmpty())
        return { false, QStringLiteral("The replacement image is empty; the EXE was not changed.") };
    if (replacement.size() > kMaxImage)
        return { false, QStringLiteral("The replacement image is larger than 32 MB; the EXE was not changed.") };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read the selected EXE.") };
    const QByteArray original = file.readAll();
    file.close();

    // The entry has to be located again: the list the UI holds may be older
    // than the file on disk. Copy it out of the scan - the list itself is a
    // temporary.
    Asset asset;
    bool found = false;
    int matches = 0;
    for (const auto &candidate : list(original, true)) {
        if (candidate.path != path) continue;
        asset = candidate;
        found = true;
        ++matches;
    }
    if (!found)
        return { false, QStringLiteral("%1 is not embedded in this EXE any more; scan again.").arg(shortName(path)) };
    if (matches > 1)
        return { false, QStringLiteral("This EXE carries more than one entry named %1; refusing to guess.").arg(shortName(path)) };
    if (!asset.image)
        return { false, QStringLiteral("Only image resources can be replaced here.") };

    const QString want = asset.family;
    QString got = sniffFormat(replacement);
    if (got.isEmpty() && want == QLatin1String("tga") && familyOf(newImagePath) == QLatin1String("tga")
        && looksLikeTga(replacement))
        got = QStringLiteral("tga");
    if (got.isEmpty())
        return { false, QStringLiteral("The chosen file is not a recognised image (PNG, JPEG, GIF, BMP, ICO, "
                                       "WEBP, SVG or TGA); the EXE was not changed.") };
    if (got != want) {
        return { false, QStringLiteral("%1 is a %2 image, but the replacement is a %3. Pick a %2 file so the "
                                       "player still loads it; the EXE was not changed.")
                            .arg(shortName(path), displayFormat(want), displayFormat(got)) };
    }
    if (want != QLatin1String("svg") && want != QLatin1String("tga")
        && want != QLatin1String("webp")) {
        // Decoded with the handlers Qt always ships. WEBP needs an optional
        // plugin, so it is accepted on its RIFF signature alone.
        QImage probe;
        if (!probe.loadFromData(replacement))
            return { false, QStringLiteral("The replacement image cannot be decoded; the EXE was not changed.") };
    }

    // The tree node stores only the leaf; the path is the list's label.
    const auto link = QtResources::findLink(original, asset.entryOff, shortName(path));
    if (!link.found || link.ambiguous)
        return { false, QStringLiteral("The %1 resource mapping is ambiguous; no file was written.")
                            .arg(shortName(path)) };

    QByteArray changed = original;
    QString error;
    if (!QtResources::replaceResource(changed, asset.entryOff, link, replacement, &error))
        return { false, error.isEmpty()
                            ? QStringLiteral("The resource could not be rewritten; no file was written.")
                            : error };
    if (!ThemeEngine::backup(exePath))
        return { false, QStringLiteral("Could not preserve the original backup; no EXE was written.") };
    if (!PatchFile::writeVerified(exePath, original, changed, &error))
        return { false, error };

    return { true, QStringLiteral("Replaced %1 (%2 -> %3 bytes). Restart BlueStacks to see it.")
                       .arg(shortName(path))
                       .arg(asset.size)
                       .arg(replacement.size()) };
}

} // namespace EmbeddedImages
