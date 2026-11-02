#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

// rcc v1 has 14-byte nodes; v2/v3 have 22-byte nodes (including a BE64
// modification time). Offsets in a resource tree are MEMORY offsets, not
// differences between file positions in different PE sections.
namespace QtResources {

struct Link
{
    bool found = false;
    bool explicitBases = false;
    bool registered = false;
    quint16 compression = 0; // 0 = raw, 1 = zlib
    QString sourceName;
    QStringList sourceNames;
    QString detail;
    bool strict = false;
    bool ambiguous = false;
    int support = 0;
    int nodeSize = 0;
    qint64 nodeOff = -1;
    qint64 treeOff = -1;
    qint64 nameRecordOff = -1;
    qint64 nameBaseRaw = -1;
    qint64 dataBaseRaw = -1;
    qint64 dataBaseRva = -1;
    quint32 dataOffset = 0;
    // All aliases of this payload must be repointed/refreshed together.
    QList<qint64> nodes;
    // Embedded companion .qmlc resources (entry offset, compression flags).
    QList<QPair<qint64, quint16>> themeCaches;
    // [file offset, exclusive end): resource payloads, names and tree. Zero
    // bytes inside another resource/header are NOT free space for a patch.
    QList<QPair<qint64, qint64>> reserved;
};

Link findLink(const QByteArray &data, qint64 entryOff,
              const QString &name = QStringLiteral("UiThemeObject.qml"));

struct File
{
    QString name;   // the leaf name the node stores ("logo.png")
    qint64 entryOff = -1;
    quint16 flags = 0;
    // The resource path rebuilt from the tree's directory nodes
    // ("images/logo.png"). Empty when the caller did not ask for it; `name`
    // keeps its old meaning so every existing matcher is unchanged.
    QString path;
};
// Enumerate files only in an already-validated pool. This lets consumers find
// related QML (e.g. a multimedia import) without guessing a small filename list.
// `File::name` is the leaf the node stores, `File::path` the full resource path
// rebuilt from the directory nodes ("images/logo.png").
QList<File> filesInPool(const QByteArray &data, const Link &pool);
QList<File> registeredFiles(const QByteArray &data);
QByteArray readFile(const QByteArray &data, const File &file, qint64 limit = 5000000);

// Changes the source timestamp, making Qt reject .qmlc files cached by an
// earlier HD-Player run. Does nothing to v1 trees (Qt uses the EXE timestamp).
bool refreshTimestamp(QByteArray &data, const Link &link);

// Invalidate only built-in compilation units whose sourceFileIndex resolves
// to UiThemeObject.qml. They otherwise take precedence over edited QML source,
// even with the disk cache empty. Never disable the entire Qt cache globally.
int invalidateThemeUnits(QByteArray &data);
int invalidateUnits(QByteArray &data, const QStringList &sourceNames,
                    const QStringList &identifiers = {});
// Number of matching compiled units that remain active (format version != 0).
// Used by patchers to refuse a source-only success when embedded bytecode would
// still take precedence over the edited QML.
int activeUnits(const QByteArray &data, const QStringList &sourceNames,
                const QStringList &identifiers = {});
// Qt 5.15 units often leave sourceFileIndex empty. Fall back to the unit's
// string table so a patched PlrSidebar/plr.qml is not shadowed by bytecode.
// Also drop the on-disk qmlcache next to HD-Player.exe (BlueStacks uses
// <install>/cache/qmlcache). Only qmlcache-named folders are removed.
int clearPlayerQmlCache(const QString &exePath);
// Grow only a validated PE's mapped, read-only resource storage. Never append
// an unmapped blob or overwrite an arbitrary copy of an offset.
qint64 appendStorage(QByteArray &data, qint64 size, QString *error);
// The largest payload the entry can hold **in place**: its current
// [size][payload] record plus the zero padding that follows it, bounded by the
// next resource, name record, tree or section end. Nothing is written. This is
// the budget a replacement must respect when the file size may not grow; -1
// when the entry cannot be measured.
qint64 inPlaceCapacity(const QByteArray &data, qint64 entry,
                       const QString &name = QString(), QString *error = nullptr);

// Free space inside a mapped PE section that `need` bytes fit in without
// touching any resource payload, name record or tree the link knows. Used to
// move a resource whose payload outgrew its slot: the record is built in the
// cave and the tree node is re-pointed at it, so the file does not grow by a
// single byte. -1 when the file has no such space.
qint64 findFreeSpace(const QByteArray &data, const Link &link, qint64 need,
                     QString *error = nullptr);

// Atomic-in-memory resource rewrite: on failure the input bytes are unchanged.
// With allowGrowth = false the record must fit in the space the entry already
// occupies; the tool never appends a section and never moves the resource, so
// the EXE cannot grow - a replacement that does not fit is refused instead.
// `forcedDestination` places an oversized record at a verified free offset
// (see findFreeSpace) and re-points the tree at it; the file size still cannot
// change because the destination is inside the mapped section already.
bool replaceResource(QByteArray &data, qint64 entry, const Link &link,
                     const QByteArray &source, QString *error, qint64 *newEntry = nullptr,
                     bool allowGrowth = true, qint64 forcedDestination = -1);
bool replaceCompressed(QByteArray &data, qint64 entry, const Link &link,
                       const QByteArray &source, QString *error, qint64 *newEntry = nullptr);
bool invalidateThemeCacheResources(QByteArray &data, const Link &link, QString *error);

} // namespace QtResources
