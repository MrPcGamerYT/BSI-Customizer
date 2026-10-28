#include "RootVhd.h"

#include "rootdisk/RootDisk.h"
#include "SevenZipArchive.h"

#include <QDateTime>
#include <QLocale>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace RootVhd {

namespace {

QString stateDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/rootvhd");
}

QString stamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
}

// "base" + "/" + "name", with the root handled so the result never starts
// with two slashes (which QDir::cleanPath keeps on Windows, as a UNC path).
QString joinInner(const QString &base, const QString &name)
{
    if (base.isEmpty() || base == QLatin1String("/"))
        return QLatin1Char('/') + name;
    return base + QLatin1Char('/') + name;
}

QVariantMap failure(const QString &message)
{
    return { { QStringLiteral("ok"), false }, { QStringLiteral("msg"), message } };
}

QString native(const std::string &path)
{
    return QDir::toNativeSeparators(QString::fromStdString(path));
}

// The undo record an image already has, or an empty string. A session keeps
// exactly one, so Undo goes back to how the image was before the tool started
// editing it, not merely before the last click.
QString manifestFor(const QString &imagePath)
{
    const QDir dir(stateDir());
    const QString target = QFileInfo(imagePath).absoluteFilePath();
    QString found;
    for (const QString &name : dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        if (object.value(QStringLiteral("image")).toString() == target)
            found = dir.filePath(name);
    }
    return found;
}

// ---- the block journal ---------------------------------------------------

/**
 * Wraps the image and, on the first write, copies the original bytes of every
 * 512-byte range about to change into one journal file. The manifest keeps the
 * image the journal belongs to and the order of the ranges, so Undo can put
 * them back in reverse and leave every other byte untouched.
 *
 * A record belongs to the image, not to one click: the next change appends to
 * it, so a whole session is undone at once. commit() is what makes the change
 * real; a change that fails half way calls rollback(), which puts back only the
 * ranges this change added and leaves the earlier record alone.
 */
class Journalled final : public RootDisk::Image
{
public:
    Journalled(std::unique_ptr<RootDisk::Image> inner, QString imagePath)
        : m_inner(std::move(inner)), m_imagePath(std::move(imagePath)) {}

    ~Journalled() override { abort(); }

    std::uint64_t size() const override { return m_inner->size(); }
    bool read(std::uint64_t offset, void *buffer, std::size_t length) override
    {
        return m_inner->read(offset, buffer, length);
    }
    bool write(std::uint64_t offset, const void *buffer, std::size_t length) override;
    bool flush() override { return m_inner->flush(); }
    std::string kind() const override { return m_inner->kind(); }
    bool writable() const override { return m_inner->writable(); }

    bool commit();
    void abort();
    // Puts the bytes back the way they were before this record: used when a
    // change fails half way, so a refused write leaves no trace.
    bool rollback();

private:
    bool openJournal();

    std::unique_ptr<RootDisk::Image> m_inner;
    QString m_imagePath;
    QString m_journalPath;
    QString m_manifestPath;
    std::FILE *m_data = nullptr;
    QJsonArray m_ranges;
    // Where this change starts inside the record: the ranges before it belong
    // to changes that succeeded and stay undoable.
    int m_firstRange = 0;
    bool m_continued = false;    // appended to a record that already existed
    bool m_committed = false;
};

bool Journalled::openJournal()
{
    if (m_data) return true;
    const QString folder = stateDir();
    if (!QDir().mkpath(folder)) return false;

    // Continue the record this image already has, if there is one.
    const QString existing = manifestFor(m_imagePath);
    if (!existing.isEmpty()) {
        QJsonObject object;
        {
            QFile manifest(existing);
            if (manifest.open(QIODevice::ReadOnly))
                object = QJsonDocument::fromJson(manifest.readAll()).object();
        }
        const QString bin = object.value(QStringLiteral("journal")).toString();
        if (!bin.isEmpty() && QFileInfo::exists(bin)) {
            if (std::FILE *file = std::fopen(QFile::encodeName(bin).constData(), "r+b")) {
                m_data = file;
                m_journalPath = bin;
                m_manifestPath = existing;
                m_ranges = object.value(QStringLiteral("ranges")).toArray();
                m_continued = true;
                m_firstRange = m_ranges.size();
                return true;
            }
        }
    }

    const QString base = stamp() + QLatin1Char('-')
        + QFileInfo(m_imagePath).fileName();
    m_journalPath = QDir(folder).filePath(base + QStringLiteral(".bin"));
    m_manifestPath = QDir(folder).filePath(base + QStringLiteral(".json"));
    m_data = std::fopen(QFile::encodeName(m_journalPath).constData(), "wb+");
    m_continued = false;
    m_firstRange = 0;
    return m_data != nullptr;
}

bool Journalled::write(std::uint64_t offset, const void *buffer, std::size_t length)
{
    if (!openJournal()) return false;
    // Whole sectors are saved, so Undo never has to reason about partial ones.
    const std::uint64_t start = (offset / 512) * 512;
    const std::uint64_t end = ((offset + length + 511) / 512) * 512;
    std::vector<std::uint8_t> original(std::size_t(end - start), 0);
    if (!m_inner->read(start, original.data(), original.size())) return false;
    std::fflush(m_data);
    std::fseek(m_data, 0, SEEK_END);
    const long at = std::ftell(m_data);
    if (at < 0) return false;
    if (std::fwrite(original.data(), 1, original.size(), m_data) != original.size()) return false;
    std::fflush(m_data);
    m_ranges.append(QJsonObject { { QStringLiteral("offset"), double(start) },
                                  { QStringLiteral("length"), double(original.size()) },
                                  { QStringLiteral("at"), double(at) } });
    return m_inner->write(offset, buffer, length);
}

bool Journalled::rollback()
{
    if (m_data) {
        std::fflush(m_data);
        // Only this change's ranges go back; the ones before it belong to
        // changes that succeeded and stay on the image.
        for (int i = m_ranges.size() - 1; i >= m_firstRange; --i) {
            const QJsonObject range = m_ranges.at(i).toObject();
            const auto offset = std::uint64_t(range.value(QStringLiteral("offset")).toDouble());
            const auto length = std::size_t(range.value(QStringLiteral("length")).toDouble());
            const long at = long(range.value(QStringLiteral("at")).toDouble());
            std::vector<std::uint8_t> original(length, 0);
            if (std::fseek(m_data, at, SEEK_SET) != 0) return false;
            if (std::fread(original.data(), 1, length, m_data) != length) return false;
            if (!m_inner->write(offset, original.data(), length)) return false;
        }
        m_inner->flush();
        QJsonArray kept;
        for (int i = 0; i < m_firstRange && i < m_ranges.size(); ++i)
            kept.append(m_ranges.at(i));
        m_ranges = kept;
    }
    abort();
    return true;
}

bool Journalled::commit()
{
    if (!m_data) return true;                 // this change wrote nothing
    if (m_ranges.isEmpty()) return true;
    std::fflush(m_data);
    std::fclose(m_data);
    m_data = nullptr;
    const QJsonObject root { { QStringLiteral("version"), 1 },
                             { QStringLiteral("image"),
                               QFileInfo(m_imagePath).absoluteFilePath() },
                             { QStringLiteral("journal"), m_journalPath },
                             { QStringLiteral("ranges"), m_ranges } };
    QSaveFile file(m_manifestPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) return false;
    m_committed = true;
    return true;
}

void Journalled::abort()
{
    if (m_committed) return;
    if (m_data) {
        std::fclose(m_data);
        m_data = nullptr;
    }
    // A record that was already there before this change holds earlier
    // changes that are still applied: it is never deleted from here.
    if (m_continued) return;
    if (!m_journalPath.isEmpty()) QFile::remove(m_journalPath);
    if (!m_manifestPath.isEmpty()) QFile::remove(m_manifestPath);
}

// One write, journalled: the record only becomes real once the change did.
template <typename Change>
QVariantMap withJournal(const QString &imagePath, Change change)
{
    std::string why;
    auto inner = RootDisk::openImage(QFile::encodeName(imagePath).toStdString(), &why);
    if (!inner) return failure(QString::fromStdString(why));
    if (!inner->writable())
        return failure(QStringLiteral("The image file could not be opened for writing. Use Enable editing to clear its Windows Read-only attribute, or close BlueStacks and check the file permissions."));

    auto *journal = new Journalled(std::move(inner), imagePath);
    Journalled *record = journal;                 // the volume owns it from here
    // The volume owns the journal from here; a failed mount destroys it, and
    // its destructor throws the empty journal away.
    auto volume = RootDisk::Volume::mount(std::unique_ptr<RootDisk::Image>(journal), &why);
    if (!volume) return failure(QString::fromStdString(why));
    if (!volume->writable())
        return failure(QString::fromStdString(volume->readOnlyReason().empty()
                                                  ? "The image is read-only."
                                                  : volume->readOnlyReason()));
    std::string error;
    if (!change(*volume, &error)) {
        // Whatever the change managed to write is put back before the record
        // is thrown away, so a refused change is a change that did not happen.
        record->rollback();
        return failure(QString::fromStdString(error));
    }
    volume->flush();
    if (!record->commit())
        return failure(QStringLiteral("The change was written, but its undo record could not be "
                                      "saved in %1.").arg(QDir::toNativeSeparators(stateDir())));
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Done.") } };
}

bool copyEntry(RootDisk::Volume &volume, std::uint32_t sourceInode,
               std::uint32_t destinationDir, const std::string &newName,
               std::uint64_t *copiedBytes, std::uint32_t *itemCount,
               unsigned depth, std::string *error)
{
    constexpr std::uint64_t maxFileBytes = 512ull * 1024 * 1024;
    constexpr std::uint64_t maxCopyBytes = 2ull * 1024 * 1024 * 1024;
    constexpr std::uint32_t maxItems = 100000;
    constexpr unsigned maxDepth = 128;
    if (depth > maxDepth || ++*itemCount > maxItems) {
        if (error) *error = "The copy exceeds the safe folder depth or item limit.";
        return false;
    }

    const RootDisk::Stat source = volume.stat(sourceInode, error);
    if (source.inode == 0) return false;
    if (source.dir()) {
        if (!volume.makeDir(destinationDir, newName, std::uint16_t(source.mode & 0x0FFF),
                            error, source.uid, source.gid, source.mtime))
            return false;
        const RootDisk::Listing destination = volume.list(destinationDir, error);
        std::uint32_t copiedDir = 0;
        for (const auto &entry : destination.entries)
            if (entry.name == newName) { copiedDir = entry.inode; break; }
        if (copiedDir == 0) {
            if (error) *error = "The copied folder could not be reopened.";
            return false;
        }
        const RootDisk::Listing children = volume.list(sourceInode, error);
        if (children.inode == 0) return false;
        for (const auto &child : children.entries)
            if (!copyEntry(volume, child.inode, copiedDir, child.name,
                           copiedBytes, itemCount, depth + 1, error))
                return false;
        return true;
    }
    if (source.symlink()) {
        std::vector<std::uint8_t> target = volume.readFile(sourceInode, error);
        if (target.size() != source.size) {
            if (error && error->empty()) *error = "The symbolic-link target could not be read completely.";
            return false;
        }
        if (source.size > maxCopyBytes - *copiedBytes
            || !volume.putSymlink(destinationDir, newName, target, error,
                                  source.uid, source.gid, source.mtime))
            return false;
        *copiedBytes += source.size;
        return true;
    }
    if (!source.regular()) {
        if (error) *error = "This item is not a regular file or folder, so it cannot be copied.";
        return false;
    }
    if (source.size > maxFileBytes || source.size > maxCopyBytes - *copiedBytes) {
        if (error) *error = "The copy exceeds the 512 MB per-file or 2 GB total safe limit.";
        return false;
    }
    std::vector<std::uint8_t> bytes = volume.readFile(sourceInode, error);
    if (bytes.size() != source.size) {
        if (error && error->empty()) *error = "A source file could not be read completely.";
        return false;
    }
    if (!volume.putFile(destinationDir, newName, bytes,
                        std::uint16_t(source.mode & 0x0FFF), error,
                        source.uid, source.gid, source.mtime))
        return false;
    *copiedBytes += source.size;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// where the image is
// ---------------------------------------------------------------------------

QStringList engineFolders(const QString &exePath)
{
    QStringList roots;
    const auto add = [&roots](const QString &path) {
        if (path.isEmpty()) return;
        const QString clean = QDir::cleanPath(path);
        if (!roots.contains(clean, Qt::CaseInsensitive)) roots << clean;
    };
    const QString programData = qEnvironmentVariable("ProgramData");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!programData.isEmpty()) {
        for (const QString &name : { QStringLiteral("BlueStacks_nxt"),
                                     QStringLiteral("BlueStacks_msi5"),
                                     QStringLiteral("BlueStacks_bgp64"),
                                     QStringLiteral("BlueStacks") }) {
            add(programData + QLatin1Char('/') + name + QStringLiteral("/Engine"));
        }
    }
    if (!localAppData.isEmpty())
        add(localAppData + QStringLiteral("/BlueStacks_nxt/Engine"));
    const QFileInfo exe(exePath);
    if (exe.absolutePath().length() > 0) {
        add(exe.absolutePath() + QStringLiteral("/Engine"));
        add(exe.absolutePath());
    }
    return roots;
}

QString findImage(const QString &exePath)
{
    for (const QString &root : engineFolders(exePath)) {
        QDir dir(root);
        if (!dir.exists()) continue;
        // An engine folder is `<Engine>/<Arch>/Root.vhd`.
        const QStringList candidates =
            dir.entryList({ QStringLiteral("*") }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : candidates) {
            const QString image = QDir(dir.filePath(name)).filePath(QStringLiteral("Root.vhd"));
            if (QFileInfo::exists(image)) return QDir::toNativeSeparators(image);
        }
        const QString direct = dir.filePath(QStringLiteral("Root.vhd"));
        if (QFileInfo::exists(direct)) return QDir::toNativeSeparators(direct);
    }
    return QString();
}

QVariantMap extractEngineArchive(const QString &archivePath)
{
    return SevenZipArchive::extractRootVhd(archivePath);
}

// ---------------------------------------------------------------------------
// reading
// ---------------------------------------------------------------------------

QVariantMap info(const QString &imagePath)
{
    if (imagePath.isEmpty()) return failure(QStringLiteral("Choose a VHD or VHDX image first."));
    if (!QFileInfo::exists(imagePath))
        return failure(QStringLiteral("“%1” does not exist.").arg(QDir::toNativeSeparators(imagePath)));
    std::string why;
    auto image = RootDisk::openImage(QFile::encodeName(imagePath).toStdString(), &why);
    if (!image) return failure(QString::fromStdString(why));
    const QString kind = QString::fromStdString(image->kind());
    auto volume = RootDisk::Volume::mount(std::move(image), &why);
    if (!volume) {
        return { { QStringLiteral("ok"), false },
                 { QStringLiteral("msg"), QString::fromStdString(why) },
                 { QStringLiteral("kind"), kind } };
    }
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Image opened.") },
             { QStringLiteral("kind"), kind },
             { QStringLiteral("description"), QString::fromStdString(volume->description()) },
             { QStringLiteral("features"), QString::fromStdString(volume->features()) },
             { QStringLiteral("writable"), volume->writable() },
             { QStringLiteral("reason"),
               QString::fromStdString(volume->readOnlyReason()) },
             { QStringLiteral("blockSize"), int(volume->blockSize()) },
             { QStringLiteral("freeBlocks"), double(volume->freeBlocks()) },
             { QStringLiteral("inodes"), int(volume->inodeCount()) },
             { QStringLiteral("freeInodes"), int(volume->freeInodes()) },
             { QStringLiteral("undo"), hasUndo(imagePath) } };
}

QVariantMap enableWriteAccess(const QString &imagePath)
{
    if (imagePath.isEmpty())
        return failure(QStringLiteral("Choose a VHD or VHDX image first."));
    if (!QFileInfo::exists(imagePath))
        return failure(QStringLiteral("The disk image no longer exists."));
#ifndef Q_OS_WIN
    Q_UNUSED(imagePath);
    return failure(QStringLiteral("Enabling a Windows read-only VHD is supported only on Windows."));
#else
    const QString nativePath = QDir::toNativeSeparators(imagePath);
    const std::wstring path = nativePath.toStdWString();
    const DWORD originalAttributes = GetFileAttributesW(path.c_str());
    if (originalAttributes == INVALID_FILE_ATTRIBUTES)
        return failure(QStringLiteral("Windows could not inspect the image file (error %1). Check the path and folder access.")
                           .arg(qulonglong(GetLastError())));

    const bool hadReadOnlyAttribute = (originalAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    if (hadReadOnlyAttribute) {
        DWORD writableAttributes = originalAttributes & ~FILE_ATTRIBUTE_READONLY;
        if (writableAttributes == 0)
            writableAttributes = FILE_ATTRIBUTE_NORMAL;
        if (!SetFileAttributesW(path.c_str(), writableAttributes))
            return failure(QStringLiteral("Windows could not clear the image's Read-only attribute (error %1).")
                               .arg(qulonglong(GetLastError())));
    }

    auto restoreAttributes = [&]() {
        return !hadReadOnlyAttribute || SetFileAttributesW(path.c_str(), originalAttributes) != 0;
    };

    // Probe with the exact native access this file manager needs. Share-mode
    // conflicts identify an open/locked image separately from an ACL denial;
    // merely being elevated cannot override either condition.
    HANDLE probe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe == INVALID_HANDLE_VALUE) {
        const DWORD openError = GetLastError();
        const bool restored = restoreAttributes();
        QString message;
        if (openError == ERROR_SHARING_VIOLATION || openError == ERROR_LOCK_VIOLATION) {
            message = QStringLiteral("Windows reports that another program has this image open without write sharing (error %1). Fully close BlueStacks and try again.")
                          .arg(qulonglong(openError));
        } else if (openError == ERROR_ACCESS_DENIED) {
            message = QStringLiteral("Windows returned Access denied (error %1). The Read-only attribute was cleared if present, but your account or a security policy still blocks Modify access; check the file and folder Security permissions.")
                          .arg(qulonglong(openError));
        } else {
            message = QStringLiteral("Windows could not open the image for writing (error %1). Check its permissions and whether another program is using it.")
                          .arg(qulonglong(openError));
        }
        if (!restored)
            message += QStringLiteral(" The original Read-only attribute could not be restored automatically.");
        return failure(message);
    }
    CloseHandle(probe);

    std::string why;
    auto image = RootDisk::openImage(QFile::encodeName(imagePath).toStdString(), &why);
    if (!image || !image->writable()) {
        const bool restored = restoreAttributes();
        QString message = image
            ? QStringLiteral("Windows allowed the native write-open, but BSI Customizer's file open fell back to read-only. This may be a path-encoding or file-sharing mismatch; restart BSI Customizer and close other programs using the image.")
            : QStringLiteral("Windows allowed the native write-open, but BSI Customizer could not parse/reopen the image: %1")
                  .arg(QString::fromStdString(why));
        if (!restored)
            message += QStringLiteral(" The original Read-only attribute could not be restored automatically.");
        return failure(message);
    }
    auto volume = RootDisk::Volume::mount(std::move(image), &why);
    if (!volume || !volume->writable()) {
        const QString reason = volume
            ? QString::fromStdString(volume->readOnlyReason())
            : QString::fromStdString(why);
        const bool restored = restoreAttributes();
        QString message = QStringLiteral("The image was not made writable: %1").arg(reason);
        if (!restored)
            message += QStringLiteral(" The original Read-only attribute could not be restored automatically.");
        return failure(message);
    }
    const QString successMessage = hadReadOnlyAttribute
        ? QStringLiteral("Editing enabled. The Windows Read-only attribute was cleared; file contents were not changed.")
        : QStringLiteral("Editing enabled. Windows confirmed write access; file contents were not changed.");
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), successMessage } };
#endif
}

QVariantMap listing(const QString &imagePath, const QString &innerPath)
{
    std::string why;
    auto image = RootDisk::openImage(QFile::encodeName(imagePath).toStdString(), &why);
    if (!image) return failure(QString::fromStdString(why));
    auto volume = RootDisk::Volume::mount(std::move(image), &why);
    if (!volume) return failure(QString::fromStdString(why));

    const std::string path = innerPath.toStdString();
    std::string pathError;
    RootDisk::Listing listing = volume->list(path.empty() ? "/" : path, &pathError);
    if (listing.inode == 0)
        return failure(QStringLiteral("“%1” is not in the image: %2")
                           .arg(innerPath, QString::fromStdString(pathError)));

    QVariantList entries;
    for (const auto &entry : listing.entries) {
        const RootDisk::Stat info = volume->stat(entry.inode, nullptr);
        entries << QVariantMap {
            { QStringLiteral("name"), QString::fromStdString(entry.name) },
            { QStringLiteral("path"),
              QDir::cleanPath(QString::fromStdString(RootDisk::joinPath(path.empty() ? "/" : path,
                                                                        entry.name))) },
            { QStringLiteral("dir"), entry.dir },
            { QStringLiteral("symlink"), info.symlink() },
            { QStringLiteral("size"), double(info.size) },
            { QStringLiteral("mode"), int(info.mode & 0x0FFF) },
            { QStringLiteral("uid"), info.uid },
            { QStringLiteral("gid"), info.gid },
            { QStringLiteral("links"), info.links },
            { QStringLiteral("mtime"), double(info.mtime) },
        };
    }
    QString parent = QString::fromStdString(path);
    const int slash = parent.lastIndexOf(QLatin1Char('/'));
    parent = slash > 0 ? parent.left(slash) : QStringLiteral("/");
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("path"), QString::fromStdString(path.empty() ? "/" : path) },
             { QStringLiteral("parent"), parent },
             { QStringLiteral("entries"), entries },
             { QStringLiteral("writable"), volume->writable() },
             { QStringLiteral("reason"), QString::fromStdString(volume->readOnlyReason()) },
             { QStringLiteral("undo"), hasUndo(imagePath) } };
}

QVariantMap search(const QString &imagePath, const QString &query, int limit)
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) return failure(QStringLiteral("Type something to search for."));

    std::string why;
    auto image = RootDisk::openImage(QFile::encodeName(imagePath).toStdString(), &why);
    if (!image) return failure(QString::fromStdString(why));
    auto volume = RootDisk::Volume::mount(std::move(image), &why);
    if (!volume) return failure(QString::fromStdString(why));

    QVariantList matches;
    // Breadth first, with hard caps: an image with an unexpectedly deep tree or
    // a symlink loop must end, not spin.
    QStringList queue { QStringLiteral("/") };
    qint64 scanned = 0;
    bool truncated = false;
    const int maxMatches = qBound(1, limit, 5000);
    while (!queue.isEmpty() && !truncated) {
        const QString path = queue.takeFirst();
        std::string listError;
        const RootDisk::Listing listing = volume->list(path.toStdString(), &listError);
        if (listing.inode == 0) continue;
        for (const auto &entry : listing.entries) {
            if (++scanned > 400000) {
                truncated = true;
                break;
            }
            const QString name = QString::fromStdString(entry.name);
            const QString child = joinInner(path, name);
            if (name.contains(needle, Qt::CaseInsensitive)) {
                if (matches.size() >= maxMatches) {
                    truncated = true;
                    break;
                }
                const RootDisk::Stat info = volume->stat(entry.inode, nullptr);
                matches << QVariantMap {
                    { QStringLiteral("name"), name },
                    { QStringLiteral("path"), child },
                    { QStringLiteral("dir"), entry.dir },
                    { QStringLiteral("size"), double(info.size) },
                    { QStringLiteral("parent"),
                      path.isEmpty() ? QStringLiteral("/") : path },
                };
            }
            // Only real folders are walked: a symlink is never followed.
            if (entry.dir) queue.append(child);
        }
    }

    return { { QStringLiteral("ok"), true },
             { QStringLiteral("entries"), matches },
             { QStringLiteral("truncated"), truncated },
             { QStringLiteral("msg"),
               truncated
                   ? QStringLiteral("Showing the first %1 matches; narrow the search.")
                         .arg(matches.size())
                   : QStringLiteral("%1 match(es).").arg(matches.size()) } };
}

QVariantMap extract(const QString &imagePath, const QString &innerPath, const QString &localPath)
{
    std::string why;
    auto image = RootDisk::openImage(QFile::encodeName(imagePath).toStdString(), &why);
    if (!image) return failure(QString::fromStdString(why));
    auto volume = RootDisk::Volume::mount(std::move(image), &why);
    if (!volume) return failure(QString::fromStdString(why));

    std::string pathError;
    const std::uint32_t inode = volume->resolve(innerPath.toStdString(), &pathError);
    if (inode == 0)
        return failure(QStringLiteral("“%1” is not in the image.").arg(innerPath));

    int files = 0;
    qint64 bytes = 0;
    QString error;

    // The recursion is a lambda calling itself: folders come out of the image
    // with their structure intact.
    std::function<bool(std::uint32_t, const QString &)> copy =
        [&](std::uint32_t current, const QString &target) {
        const RootDisk::Stat info = volume->stat(current, nullptr);
        if (info.dir()) {
            if (!QDir().mkpath(target)) {
                error = QStringLiteral("Could not create %1").arg(QDir::toNativeSeparators(target));
                return false;
            }
            std::string listError;
            const RootDisk::Listing inside = volume->list(current, &listError);
            for (const auto &entry : inside.entries) {
                if (!copy(entry.inode, QDir(target).filePath(QString::fromStdString(entry.name))))
                    return false;
            }
            return true;
        }
        std::string readError;
        const std::vector<std::uint8_t> data = volume->readFile(current, &readError);
        if (!readError.empty()) {
            error = QString::fromStdString(readError);
            return false;
        }
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly)) {
            error = QStringLiteral("Could not write %1").arg(QDir::toNativeSeparators(target));
            return false;
        }
        file.write(reinterpret_cast<const char *>(data.data()), qint64(data.size()));
        if (!file.commit()) {
            error = QStringLiteral("Could not write %1").arg(QDir::toNativeSeparators(target));
            return false;
        }
        ++files;
        bytes += qint64(data.size());
        return true;
    };

    QString target = localPath;
    if (innerPath.isEmpty() || innerPath == QLatin1String("/")) {
        if (!copy(inode, target)) return failure(error);
    } else {
        // A folder goes into a folder of the same name; a file goes to the
        // exact path the user chose.
        const RootDisk::Stat info = volume->stat(inode, nullptr);
        if (info.dir()) target = QDir(localPath).filePath(QFileInfo(innerPath).fileName());
        if (!copy(inode, target)) return failure(error);
    }
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("files"), files },
             { QStringLiteral("bytes"), double(bytes) },
             { QStringLiteral("msg"), QStringLiteral("Extracted %1 file(s) to %2.")
                                          .arg(files)
                                          .arg(QDir::toNativeSeparators(localPath)) } };
}

// ---------------------------------------------------------------------------
// writing
// ---------------------------------------------------------------------------

QVariantMap addFile(const QString &imagePath, const QString &innerDir, const QString &localFile)
{
    QFile file(localFile);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("“%1” could not be read.").arg(QDir::toNativeSeparators(localFile)));
    const qint64 size = file.size();
    // A 2 GB write into an image that may be a copy-on-write disk is refused
    // before anything is touched.
    if (size > qint64(2) * 1024 * 1024 * 1024)
        return failure(QStringLiteral("“%1” is larger than 2 GB, more than an engine image takes.")
                           .arg(QFileInfo(localFile).fileName()));
    const QByteArray bytes = file.readAll();
    file.close();

    std::vector<std::uint8_t> data(std::size_t(bytes.size()));
    std::memcpy(data.data(), bytes.constData(), std::size_t(bytes.size()));
    const QString name = QFileInfo(localFile).fileName();

    const QVariantMap done = withJournal(
        imagePath,
        [&](RootDisk::Volume &volume, std::string *error) {
            const std::uint32_t dir = volume.resolve(innerDir.toStdString(), error);
            if (dir == 0) return false;
            const auto current = volume.list(dir, error);
            for (const auto &entry : current.entries) {
                if (entry.name == name) {
                    if (error) *error = "“" + name.toStdString()
                        + "” already exists. Use Replace to change its contents.";
                    return false;
                }
            }
            return volume.putFile(dir, name.toStdString(), data, 0644, error);
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Added “%1” (%2) to %3.")
                                          .arg(name)
                                          .arg(QLocale().formattedDataSize(size))
                                          .arg(innerDir) } };
}

QVariantMap replaceFile(const QString &imagePath, const QString &innerPath,
                        const QString &localFile)
{
    const QFileInfo localInfo(localFile);
    if (!localInfo.isFile())
        return failure(QStringLiteral("Choose a replacement file that exists."));
    if (localInfo.canonicalFilePath() == QFileInfo(imagePath).canonicalFilePath())
        return failure(QStringLiteral("The image cannot be used as its own replacement file."));
    QFile file(localFile);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("“%1” could not be read.").arg(QDir::toNativeSeparators(localFile)));
    const qint64 size = file.size();
    if (size > qint64(2) * 1024 * 1024 * 1024)
        return failure(QStringLiteral("“%1” is larger than the 2 GB replacement limit.")
                           .arg(localInfo.fileName()));
    const QByteArray bytes = file.readAll();
    file.close();
    std::vector<std::uint8_t> data(std::size_t(bytes.size()));
    std::memcpy(data.data(), bytes.constData(), std::size_t(bytes.size()));

    const QString innerName = QFileInfo(innerPath).fileName();
    const QString parentPath = QFileInfo(innerPath).path();
    const QVariantMap done = withJournal(
        imagePath,
        [&](RootDisk::Volume &volume, std::string *error) {
            const std::uint32_t inode = volume.resolve(innerPath.toStdString(), error);
            if (inode == 0) return false;
            if (!volume.stat(inode, error).regular()) {
                if (error) *error = "Only an existing regular file can be replaced.";
                return false;
            }
            const std::uint32_t parent = volume.resolve(parentPath.toStdString(), error);
            if (parent == 0) return false;
            return volume.putFile(parent, innerName.toStdString(), data, 0644, error);
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Replaced %1 with %2 (%3).")
                                          .arg(innerPath, localInfo.fileName(),
                                               QLocale().formattedDataSize(size)) } };
}

QVariantMap addFolder(const QString &imagePath, const QString &innerDir, const QString &localDir)
{
    const QFileInfo source(localDir);
    if (source.isSymLink())
        return failure(QStringLiteral("Import the real folder, not a symbolic-link shortcut."));
    if (!source.isDir())
        return failure(QStringLiteral("“%1” is not a folder.").arg(QDir::toNativeSeparators(localDir)));

    // The tree is walked into one change, so the whole folder lands or none
    // of it does.
    struct Item
    {
        QString relative;
        QString localPath;
        bool dir = false;
    };
    QList<Item> items;
    QString walkError;
    std::function<bool(const QString &, const QString &)> walk =
        [&](const QString &local, const QString &relative) {
        const QDir dir(local);
        const auto entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                                               QDir::Name | QDir::DirsFirst);
        for (const QFileInfo &entry : entries) {
            if (entry.isSymLink()) {
                walkError = QStringLiteral("“%1” is a symbolic link. Symbolic links are not imported from host folders.")
                                .arg(entry.fileName());
                return false;
            }
            const QString rel = relative.isEmpty() ? entry.fileName()
                                                   : relative + QLatin1Char('/') + entry.fileName();
            if (entry.isDir()) {
                items << Item { rel, entry.absoluteFilePath(), true };
                if (!walk(entry.absoluteFilePath(), rel)) return false;
            } else if (entry.isFile()) {
                if (entry.size() > qint64(512) * 1024 * 1024) {
                    walkError = QStringLiteral("“%1” is larger than 512 MB.")
                                    .arg(entry.fileName());
                    return false;
                }
                items << Item { rel, entry.absoluteFilePath(), false };
            }
        }
        return true;
    };
    if (!walk(source.absoluteFilePath(), QString()))
        return failure(walkError.isEmpty() ? QStringLiteral("The folder could not be read.") : walkError);

    const QVariantMap done = withJournal(
        imagePath,
        [&](RootDisk::Volume &volume, std::string *error) {
            const std::uint32_t base = volume.resolve(innerDir.toStdString(), error);
            if (base == 0) return false;
            if (!volume.makeDir(base, source.fileName().toStdString(), 0755, error)) return false;
            const QString root = joinInner(innerDir, source.fileName());
            for (const Item &item : items) {
                // Every item names its parent by path, so the tree is walked
                // once and resolved as it goes.
                const QString full = joinInner(root, item.relative);
                const QString parentPath = full.left(full.lastIndexOf(QLatin1Char('/')));
                const std::uint32_t parent = volume.resolve(parentPath.toStdString(), error);
                if (parent == 0) return false;
                if (item.dir) {
                    if (!volume.makeDir(parent, QFileInfo(item.localPath).fileName().toStdString(),
                                        0755, error))
                        return false;
                    continue;
                }
                QFile file(item.localPath);
                if (!file.open(QIODevice::ReadOnly)) {
                    *error = "“" + item.localPath.toStdString() + "” could not be read.";
                    return false;
                }
                const QByteArray bytes = file.readAll();
                file.close();
                std::vector<std::uint8_t> data(std::size_t(bytes.size()));
                std::memcpy(data.data(), bytes.constData(), std::size_t(bytes.size()));
                if (!volume.putFile(parent, QFileInfo(item.localPath).fileName().toStdString(), data,
                                    0644, error))
                    return false;
            }
            return true;
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Added “%1” (%2 item(s)).")
                                          .arg(source.fileName())
                                          .arg(items.size()) } };
}

QVariantMap addPath(const QString &imagePath, const QString &innerDir, const QString &localPath)
{
    const QFileInfo info(localPath);
    if (info.isDir()) return addFolder(imagePath, innerDir, localPath);
    if (!info.exists())
        return failure(QStringLiteral("“%1” does not exist.").arg(QDir::toNativeSeparators(localPath)));
    return addFile(imagePath, innerDir, localPath);
}

QVariantMap makeFolder(const QString &imagePath, const QString &innerDir, const QString &name)
{
    const QVariantMap done = withJournal(
        imagePath,
        [&](RootDisk::Volume &volume, std::string *error) {
            const std::uint32_t dir = volume.resolve(innerDir.toStdString(), error);
            if (dir == 0) return false;
            return volume.makeDir(dir, name.toStdString(), 0755, error);
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Created “%1” in %2.").arg(name, innerDir) } };
}

QVariantMap rename(const QString &imagePath, const QString &innerPath, const QString &newName)
{
    if (innerPath.isEmpty() || innerPath == QLatin1String("/"))
        return failure(QStringLiteral("The image root cannot be renamed."));
    if (newName.contains(QLatin1Char('/')) || newName.contains(QLatin1Char('\\')))
        return failure(QStringLiteral("Enter a name, not a path."));
    const QFileInfo inner(innerPath);
    const QString oldName = inner.fileName();
    const QString parent = inner.path();
    const QVariantMap done = withJournal(
        imagePath,
        [&](RootDisk::Volume &volume, std::string *error) {
            const std::uint32_t dir = volume.resolve(parent.toStdString(), error);
            if (dir == 0) return false;
            return volume.rename(dir, oldName.toStdString(), newName.toStdString(), error);
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Renamed “%1” to “%2”.")
                                          .arg(innerPath, newName) } };
}

QVariantMap move(const QString &imagePath, const QString &innerPath,
                 const QString &destinationDir)
{
    const QString sourcePath = QDir::cleanPath(innerPath);
    const QString destination = QDir::cleanPath(destinationDir);
    if (sourcePath.isEmpty() || sourcePath == QLatin1String("/"))
        return failure(QStringLiteral("The image root cannot be moved."));
    if (destination == sourcePath || destination.startsWith(sourcePath + QLatin1Char('/')))
        return failure(QStringLiteral("A folder cannot be moved into itself or one of its descendants."));

    const QFileInfo source(sourcePath);
    const QString name = source.fileName();
    const QString sourceParent = source.path();
    const QVariantMap done = withJournal(
        imagePath,
        [&](RootDisk::Volume &volume, std::string *error) {
            const std::uint32_t from = volume.resolve(sourceParent.toStdString(), error);
            if (from == 0) return false;
            const std::uint32_t to = volume.resolve(destination.toStdString(), error);
            if (to == 0) return false;
            return volume.move(from, name.toStdString(), to, name.toStdString(), error);
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Moved “%1” to %2.").arg(sourcePath, destination) } };
}

QVariantMap copy(const QString &imagePath, const QString &innerPath,
                 const QString &destinationDir, const QString &newName)
{
    const QString sourcePath = QDir::cleanPath(innerPath);
    const QString destination = QDir::cleanPath(destinationDir);
    if (sourcePath.isEmpty() || sourcePath == QLatin1String("/"))
        return failure(QStringLiteral("The image root cannot be copied as an item."));
    if (destination == sourcePath || destination.startsWith(sourcePath + QLatin1Char('/')))
        return failure(QStringLiteral("A folder cannot be copied inside itself or one of its descendants."));

    const QFileInfo source(sourcePath);
    const QString name = newName.trimmed().isEmpty() ? source.fileName() : newName.trimmed();
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return failure(QStringLiteral("Enter a valid single name for the copy."));
    std::uint64_t copiedBytes = 0;
    std::uint32_t itemCount = 0;
    const QVariantMap done = withJournal(
        imagePath,
        [&](RootDisk::Volume &volume, std::string *error) {
            const std::uint32_t inode = volume.resolve(sourcePath.toStdString(), error);
            if (inode == 0) return false;
            const std::uint32_t destinationInode = volume.resolve(destination.toStdString(), error);
            if (destinationInode == 0) return false;
            const RootDisk::Listing existing = volume.list(destinationInode, error);
            if (existing.inode == 0) return false;
            for (const auto &entry : existing.entries)
                if (entry.name == name) {
                    if (error) *error = "“" + name.toStdString()
                        + "” already exists in the destination folder.";
                    return false;
                }
            if (!copyEntry(volume, inode, destinationInode, name.toStdString(),
                           &copiedBytes, &itemCount, 0, error))
                return false;
            return true;
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Copied %1 item(s), %2.")
                                          .arg(itemCount)
                                          .arg(QLocale().formattedDataSize(qint64(copiedBytes))) } };
}

QVariantMap remove(const QString &imagePath, const QString &innerPath)
{
    if (innerPath.isEmpty() || innerPath == QLatin1String("/"))
        return failure(QStringLiteral("The root folder is never removed."));
    const QFileInfo inner(innerPath);
    const QString name = inner.fileName();
    const QString parent = inner.path();

    const QVariantMap done = withJournal(
        imagePath,
        [&](RootDisk::Volume &volume, std::string *error) {
            const std::uint32_t dir = volume.resolve(parent.toStdString(), error);
            if (dir == 0) return false;
            return volume.remove(dir, name.toStdString(), error);
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Removed “%1”.").arg(innerPath) } };
}

// ---------------------------------------------------------------------------
// undo
// ---------------------------------------------------------------------------

bool hasUndo(const QString &imagePath)
{
    return !manifestFor(imagePath).isEmpty();
}

QVariantMap undo(const QString &imagePath)
{
    const QString manifestPath = manifestFor(imagePath);
    if (manifestPath.isEmpty())
        return failure(QStringLiteral("There is nothing to undo for this image."));
    QJsonObject manifest;
    {
        QFile file(manifestPath);
        if (!file.open(QIODevice::ReadOnly))
            return failure(QStringLiteral("The undo record could not be read."));
        manifest = QJsonDocument::fromJson(file.readAll()).object();
    }

    const QString bin = manifest.value(QStringLiteral("journal")).toString();
    QFile data(bin);
    if (!data.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("The undo record is missing: %1")
                           .arg(QDir::toNativeSeparators(bin)));

    std::string why;
    auto image = RootDisk::openImage(QFile::encodeName(imagePath).toStdString(), &why);
    if (!image) return failure(QString::fromStdString(why));
    if (!image->writable()) return failure(QStringLiteral("The image is read-only."));

    // Reverse order, so overlapping ranges end up as they were at the start.
    const QJsonArray ranges = manifest.value(QStringLiteral("ranges")).toArray();
    for (int i = ranges.size() - 1; i >= 0; --i) {
        const QJsonObject range = ranges.at(i).toObject();
        const qint64 offset = qint64(range.value(QStringLiteral("offset")).toDouble());
        const qint64 length = qint64(range.value(QStringLiteral("length")).toDouble());
        const qint64 at = qint64(range.value(QStringLiteral("at")).toDouble());
        if (!data.seek(at)) return failure(QStringLiteral("The undo record is damaged."));
        const QByteArray bytes = data.read(length);
        if (bytes.size() != length) return failure(QStringLiteral("The undo record is truncated."));
        if (!image->write(std::uint64_t(offset), bytes.constData(), std::size_t(length)))
            return failure(QStringLiteral("The image could not be written."));
    }
    image->flush();
    data.close();
    if (!QFile::remove(manifestPath))
        return failure(QStringLiteral("The record could not be cleared."));
    QFile::remove(bin);
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Undone: the image is back to how it was before "
                                                     "this tool changed it.") } };
}

QVariantMap discardUndo(const QString &imagePath)
{
    const QDir dir(stateDir());
    const QString target = QFileInfo(imagePath).absoluteFilePath();
    int removed = 0;
    for (const QString &name : dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        if (object.value(QStringLiteral("image")).toString() != target) continue;
        QFile::remove(object.value(QStringLiteral("journal")).toString());
        if (QFile::remove(dir.filePath(name))) ++removed;
    }
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), removed
                   ? QStringLiteral("The undo record was deleted; the change stays.")
                   : QStringLiteral("There was nothing to delete.") } };
}

} // namespace RootVhd
