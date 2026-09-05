#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

// rcc v1 has 14-byte nodes; v2/v3 have 22-byte nodes (including a BE64
// modification time). Offsets in a resource tree are MEMORY offsets, not
// differences between file positions in different PE sections.
namespace QtResources {

struct Link
{
    bool found = false;
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

// Changes the source timestamp, making Qt reject .qmlc files cached by an
// earlier HD-Player run. Does nothing to v1 trees (Qt uses the EXE timestamp).
bool refreshTimestamp(QByteArray &data, const Link &link);

// Invalidate only built-in compilation units whose sourceFileIndex resolves
// to UiThemeObject.qml. They otherwise take precedence over edited QML source,
// even with the disk cache empty. Never disable the entire Qt cache globally.
int invalidateThemeUnits(QByteArray &data);
bool invalidateCompressedThemeCaches(QByteArray &data, const Link &link, QString *error);

} // namespace QtResources
