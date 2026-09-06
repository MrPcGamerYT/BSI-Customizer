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
    QString name;
    qint64 entryOff = -1;
    quint16 flags = 0;
};
// Enumerate files only in an already-validated pool. This lets consumers find
// related QML (e.g. a multimedia import) without guessing a small filename list.
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
int invalidateUnits(QByteArray &data, const QStringList &sourceNames);
// Grow only a validated PE's mapped, read-only resource storage. Never append
// an unmapped blob or overwrite an arbitrary copy of an offset.
qint64 appendStorage(QByteArray &data, qint64 size, QString *error);
// Atomic-in-memory resource rewrite: on failure the input bytes are unchanged.
bool replaceResource(QByteArray &data, qint64 entry, const Link &link,
                     const QByteArray &source, QString *error, qint64 *newEntry = nullptr);
bool replaceCompressed(QByteArray &data, qint64 entry, const Link &link,
                       const QByteArray &source, QString *error, qint64 *newEntry = nullptr);
bool invalidateThemeCacheResources(QByteArray &data, const Link &link, QString *error);

} // namespace QtResources
