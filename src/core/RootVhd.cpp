#include "RootVhd.h"

#include "rootdisk/RootDisk.h"

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
#include <vector>

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

QVariantMap failure(const QString &message)
{
    return { { QStringLiteral("ok"), false }, { QStringLiteral("msg"), message } };
}

QString native(const std::string &path)
{
    return QDir::toNativeSeparators(QString::fromStdString(path));
}

// ---- the block journal ---------------------------------------------------

/**
 * Wraps the image and, on the first write, copies the original bytes of every
 * 512-byte range about to change into one journal file. The manifest keeps the
 * image the journal belongs to and the order of the ranges, so Undo can put
 * them back in reverse and leave every other byte untouched.
 *
 * commit() is what makes the record real: a change that failed half way calls
 * abort() instead and leaves nothing behind.
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
    bool m_committed = false;
};

bool Journalled::openJournal()
{
    if (m_data) return true;
    const QString folder = stateDir();
    if (!QDir().mkpath(folder)) return false;
    const QString base = stamp() + QLatin1Char('-')
        + QFileInfo(m_imagePath).fileName();
    m_journalPath = QDir(folder).filePath(base + QStringLiteral(".bin"));
    m_manifestPath = QDir(folder).filePath(base + QStringLiteral(".json"));
    m_data = std::fopen(QFile::encodeName(m_journalPath).constData(), "wb+");
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
        for (int i = m_ranges.size() - 1; i >= 0; --i) {
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
        m_ranges = QJsonArray();
    }
    abort();
    return true;
}

bool Journalled::commit()
{
    if (m_ranges.isEmpty()) return true;
    if (!m_data) return false;
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
        return failure(QStringLiteral("The image is read-only; run the tool as administrator."));

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

// ---------------------------------------------------------------------------
// reading
// ---------------------------------------------------------------------------

QVariantMap info(const QString &imagePath)
{
    if (imagePath.isEmpty()) return failure(QStringLiteral("Choose the Root.vhd of an engine first."));
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
            { QStringLiteral("size"), double(info.size) },
            { QStringLiteral("mode"), int(info.mode & 0x0FFF) },
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
            return volume.putFile(dir, name.toStdString(), data, 0644, error);
        });
    if (!done.value(QStringLiteral("ok")).toBool()) return done;
    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Added “%1” (%2) to %3.")
                                          .arg(name)
                                          .arg(QLocale().formattedDataSize(size))
                                          .arg(innerDir) } };
}

QVariantMap addFolder(const QString &imagePath, const QString &innerDir, const QString &localDir)
{
    const QFileInfo source(localDir);
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
            const QString root = innerDir + QLatin1Char('/') + source.fileName();
            for (const Item &item : items) {
                // Every item names its parent by path, so the tree is walked
                // once and resolved as it goes.
                const QString full = root + QLatin1Char('/') + item.relative;
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
    const QDir dir(stateDir());
    const QString target = QFileInfo(imagePath).absoluteFilePath();
    for (const QString &name : dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        if (object.value(QStringLiteral("image")).toString() == target) return true;
    }
    return false;
}

QVariantMap undo(const QString &imagePath)
{
    const QDir dir(stateDir());
    const QString target = QFileInfo(imagePath).absoluteFilePath();
    QString manifestPath;
    QJsonObject manifest;
    for (const QString &name : dir.entryList({ QStringLiteral("*.json") }, QDir::Files, QDir::Name)) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        if (object.value(QStringLiteral("image")).toString() != target) continue;
        manifestPath = dir.filePath(name);
        manifest = object;
    }
    if (manifestPath.isEmpty())
        return failure(QStringLiteral("There is nothing to undo for this image."));

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
                                                     "the last change.") } };
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
