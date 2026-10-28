#include "SevenZipArchive.h"

#include <QCryptographicHash>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

extern "C" {
#include "../third_party/7zip/C/7z.h"
#include "../third_party/7zip/C/7zAlloc.h"
#include "../third_party/7zip/C/7zCrc.h"
}

namespace SevenZipArchive {
namespace {

constexpr char kSignature[] = { char(0x37), char(0x7a), char(0xbc), char(0xaf),
                                char(0x27), char(0x1c) };
constexpr qint64 kScanLimit = 128ll * 1024 * 1024;
constexpr qint64 kScanChunk = 1024ll * 1024;
constexpr std::size_t kLookLimit = 1024u * 1024u;
constexpr UInt64 kMaxRootVhdAndSolidFolder = UInt64(8) * 1024 * 1024 * 1024;

struct Input
{
    ILookInStream vtable {};
    QFile *file = nullptr;
    QByteArray lookBuffer;
};

Input *input(ILookInStreamPtr stream)
{
    return const_cast<Input *>(reinterpret_cast<const Input *>(stream));
}

SRes look(ILookInStreamPtr stream, const void **buffer, size_t *size)
{
    Input *in = input(stream);
    const qint64 want = qint64(std::min(*size, kLookLimit));
    in->lookBuffer = in->file->peek(want);
    *size = size_t(in->lookBuffer.size());
    *buffer = in->lookBuffer.constData();
    return SZ_OK;
}

SRes skip(ILookInStreamPtr stream, size_t amount)
{
    Input *in = input(stream);
    const qint64 current = in->file->pos();
    if (amount > size_t(std::numeric_limits<qint64>::max())
        || current < 0 || qint64(amount) > in->file->size() - current
        || !in->file->seek(current + qint64(amount)))
        return SZ_ERROR_READ;
    return SZ_OK;
}

SRes read(ILookInStreamPtr stream, void *buffer, size_t *size)
{
    Input *in = input(stream);
    const qint64 requested = qint64(std::min(*size,
        size_t(std::numeric_limits<qint64>::max())));
    const QByteArray bytes = in->file->read(requested);
    if (bytes.size() < 0) return SZ_ERROR_READ;
    if (!bytes.isEmpty())
        std::memcpy(buffer, bytes.constData(), size_t(bytes.size()));
    *size = size_t(bytes.size());
    return in->file->error() == QFileDevice::NoError || in->file->atEnd()
        ? SZ_OK : SZ_ERROR_READ;
}

SRes seek(ILookInStreamPtr stream, Int64 *position, ESzSeek origin)
{
    Input *in = input(stream);
    qint64 base = 0;
    if (origin == SZ_SEEK_CUR) base = in->file->pos();
    else if (origin == SZ_SEEK_END) base = in->file->size();
    else if (origin != SZ_SEEK_SET) return SZ_ERROR_PARAM;
    if (base < 0 || *position < -base || *position > in->file->size() - base)
        return SZ_ERROR_READ;
    const qint64 absolute = base + *position;
    if (!in->file->seek(absolute)) return SZ_ERROR_READ;
    // 7-Zip's seek contract returns the resulting absolute position through
    // the same pointer; the archive parser relies on this for SFX offsets.
    *position = absolute;
    return SZ_OK;
}

void initInput(Input &in, QFile &file)
{
    in.file = &file;
    in.vtable.Look = look;
    in.vtable.Skip = skip;
    in.vtable.Read = read;
    in.vtable.Seek = seek;
}

std::vector<qint64> signatures(QFile &file)
{
    std::vector<qint64> found;
    const qint64 limit = std::min(file.size(), kScanLimit);
    QByteArray tail;
    qint64 position = 0;
    while (position < limit) {
        if (!file.seek(position)) break;
        const QByteArray chunk = file.read(std::min(kScanChunk, limit - position));
        if (chunk.isEmpty()) break;
        const QByteArray data = tail + chunk;
        qint64 at = 0;
        while ((at = data.indexOf(QByteArray(kSignature, sizeof(kSignature)), at)) >= 0) {
            const qint64 absolute = position - tail.size() + at;
            if (found.empty() || found.back() != absolute)
                found.push_back(absolute);
            ++at;
        }
        tail = data.right(int(sizeof(kSignature) - 1));
        position += chunk.size();
    }
    if (file.size() >= qint64(sizeof(kSignature)) && file.seek(0)) {
        const QByteArray first = file.peek(sizeof(kSignature));
        if (first == QByteArray(kSignature, sizeof(kSignature))
            && (found.empty() || found.front() != 0))
            found.insert(found.begin(), 0);
    }
    // A self-extracting module can contain coincidental signature bytes. Don't
    // let a crafted executable force an unbounded number of archive parses.
    if (found.size() > 128) found.resize(128);
    return found;
}

QString archiveFileName(const CSzArEx &db, UInt32 index)
{
    const size_t count = SzArEx_GetFileNameUtf16(&db, index, nullptr);
    if (count == 0 || count > 32768) return {};
    std::vector<UInt16> name(count, 0);
    SzArEx_GetFileNameUtf16(&db, index, name.data());
    QString result = QString::fromUtf16(reinterpret_cast<const char16_t *>(name.data()),
                                        qsizetype(count - 1));
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return result;
}

QString unpackError(SRes result)
{
    switch (result) {
    case SZ_ERROR_UNSUPPORTED:
        return QStringLiteral("The engine package uses an archive feature this built-in "
                              "extractor does not support (for example, encryption).");
    case SZ_ERROR_MEM:
        return QStringLiteral("There was not enough memory to extract Root.vhd. Close other "
                              "applications and try again.");
    case SZ_ERROR_CRC:
        return QStringLiteral("The engine archive failed its CRC check; the download may be "
                              "incomplete or damaged.");
    case SZ_ERROR_INPUT_EOF:
        return QStringLiteral("The engine archive is truncated; download it again and retry.");
    default:
        return QStringLiteral("The engine archive could not be read (7-Zip decoder code %1).")
            .arg(int(result));
    }
}

QVariantMap failure(const QString &message)
{
    return { { QStringLiteral("ok"), false }, { QStringLiteral("msg"), message } };
}

} // namespace

QVariantMap extractRootVhd(const QString &archivePath, const QString &cacheBase)
{
    const QFileInfo sourceInfo(archivePath);
    if (!sourceInfo.isFile())
        return failure(QStringLiteral("Choose the engine package EXE or a 7z archive."));
    QFile archive(sourceInfo.absoluteFilePath());
    if (!archive.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("The engine package could not be opened: %1")
                           .arg(archive.errorString()));

    const std::vector<qint64> starts = signatures(archive);
    if (starts.empty())
        return failure(QStringLiteral("No 7z archive was found in this file. Choose the "
                                      "BlueStacks engine package EXE, not the BlueStacks "
                                      "setup installer."));

    static const ISzAlloc allocator { SzAlloc, SzFree };
    static std::once_flag crcTableOnce;
    std::call_once(crcTableOnce, [] { CrcGenerateTable(); });
    CSzArEx database;
    SzArEx_Init(&database);
    Input stream;
    initInput(stream, archive);
    SRes openResult = SZ_ERROR_NO_ARCHIVE;
    for (const qint64 start : starts) {
        archive.seek(start);
        SzArEx_Free(&database, &allocator);
        SzArEx_Init(&database);
        openResult = SzArEx_Open(&database, &stream.vtable, &allocator, &allocator);
        if (openResult == SZ_OK) break;
    }
    if (openResult != SZ_OK) {
        SzArEx_Free(&database, &allocator);
        return failure(QStringLiteral("The selected EXE does not contain a readable 7z "
                                      "engine archive. %1").arg(unpackError(openResult)));
    }

    int rootIndex = -1;
    QString rootName;
    for (UInt32 i = 0; i < database.NumFiles; ++i) {
        if (SzArEx_IsDir(&database, i)) continue;
        const QString name = archiveFileName(database, i);
        if (name.isEmpty() || QFileInfo(name).fileName().compare(
                QLatin1String("Root.vhd"), Qt::CaseInsensitive) != 0)
            continue;
        rootIndex = int(i);
        rootName = name;
        break;
    }
    if (rootIndex < 0) {
        SzArEx_Free(&database, &allocator);
        return failure(QStringLiteral("The engine package opened, but it contains no "
                                      "Root.vhd file."));
    }

    const QString identity = sourceInfo.absoluteFilePath() + QLatin1Char('|')
        + QString::number(sourceInfo.size()) + QLatin1Char('|')
        + QString::number(sourceInfo.lastModified().toMSecsSinceEpoch());
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    const QString cacheRoot = cacheBase.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        : cacheBase;
    if (cacheRoot.isEmpty()) {
        SzArEx_Free(&database, &allocator);
        return failure(QStringLiteral("The application data folder is not available for the "
                                      "private Root.vhd cache."));
    }
    const QString folder = QDir(cacheRoot).filePath(QStringLiteral("engine-images/") + key);
    const QString destination = QDir(folder).filePath(QStringLiteral("Root.vhd"));
    const UInt64 expectedSize = SzArEx_GetFileSize(&database, UInt32(rootIndex));
    if (QFileInfo::exists(destination)
        && quint64(QFileInfo(destination).size()) == expectedSize && expectedSize > 0) {
        SzArEx_Free(&database, &allocator);
        return { { QStringLiteral("ok"), true },
                 { QStringLiteral("path"), destination },
                 { QStringLiteral("msg"), QStringLiteral("Using the cached Root.vhd extracted "
                                                          "from %1.").arg(sourceInfo.fileName()) } };
    }
    if (!QDir().mkpath(folder)) {
        SzArEx_Free(&database, &allocator);
        return failure(QStringLiteral("The private engine-image cache could not be created."));
    }

    const UInt32 rootFolder = database.FileToFolder[rootIndex];
    const UInt64 folderSize = rootFolder == 0xFFFFFFFFu
        ? expectedSize : SzAr_GetFolderUnpackSize(&database.db, rootFolder);
    if (expectedSize == 0 || expectedSize > kMaxRootVhdAndSolidFolder
        || folderSize > kMaxRootVhdAndSolidFolder
        || expectedSize > std::numeric_limits<size_t>::max()
        || expectedSize > std::uint64_t(std::numeric_limits<qint64>::max())) {
        SzArEx_Free(&database, &allocator);
        return failure(QStringLiteral("Root.vhd or its solid archive folder is larger than the "
                                      "built-in extractor's 8 GiB safety limit."));
    }

    Byte *output = nullptr;
    size_t outputCapacity = 0;
    size_t outputOffset = 0;
    size_t outputSize = 0;
    UInt32 blockIndex = 0xFFFFFFFFu;
    const SRes extracted = SzArEx_Extract(&database, &stream.vtable,
        UInt32(rootIndex), &blockIndex, &output, &outputCapacity,
        &outputOffset, &outputSize, &allocator, &allocator);
    if (extracted != SZ_OK) {
        ISzAlloc_Free(&allocator, output);
        SzArEx_Free(&database, &allocator);
        return failure(unpackError(extracted));
    }
    if (!output || outputSize != size_t(expectedSize)
        || outputOffset > outputCapacity || outputSize > outputCapacity - outputOffset) {
        ISzAlloc_Free(&allocator, output);
        SzArEx_Free(&database, &allocator);
        return failure(QStringLiteral("The decoder returned an incomplete Root.vhd."));
    }

    const QString temporary = destination + QStringLiteral(".partial");
    QFile::remove(temporary);
    QFile extractedFile(temporary);
    bool written = extractedFile.open(QIODevice::WriteOnly);
    qint64 copied = 0;
    constexpr qint64 chunkSize = 64ll * 1024 * 1024;
    while (written && copied < qint64(outputSize)) {
        const qint64 amount = std::min(chunkSize, qint64(outputSize) - copied);
        if (extractedFile.write(reinterpret_cast<const char *>(output + outputOffset + size_t(copied)),
                                amount) != amount)
            written = false;
        else
            copied += amount;
    }
    written = written && extractedFile.flush();
    extractedFile.close();
    ISzAlloc_Free(&allocator, output);
    SzArEx_Free(&database, &allocator);
    if (!written) {
        QFile::remove(temporary);
        return failure(QStringLiteral("Root.vhd could not be written to the private cache: %1")
                           .arg(extractedFile.errorString()));
    }
    QFile::remove(destination);
    if (!QFile::rename(temporary, destination)) {
        QFile::remove(temporary);
        return failure(QStringLiteral("The extracted Root.vhd could not be moved into the "
                                      "private cache."));
    }
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("path"), destination },
             { QStringLiteral("msg"), QStringLiteral("Extracted %1 from %2. The downloaded "
                                                      "package was left unchanged.")
                  .arg(rootName, sourceInfo.fileName()) } };
}

} // namespace SevenZipArchive
