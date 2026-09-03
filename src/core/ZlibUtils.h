#pragma once

#include <QByteArray>
#include <QList>

// Raw zlib helpers built on qCompress/qUncompress (Qt's bundled zlib).
//
// Qt resource compressed files store a RAW zlib stream (no length prefix);
// qCompress prepends a 4-byte big-endian size, so we strip/add it to convert.
namespace ZlibUtils {

// Decompress a raw zlib stream. If expectedSize >= 0 the result must match
// exactly, otherwise an empty array is returned (mirrors the Python
// decompressobj().eof / unused_data / length checks).
QByteArray tryDecompress(const QByteArray &rawStream, qint64 expectedSize = -1);

// Compress to a raw zlib stream at level 9.
QByteArray compress(const QByteArray &data);

// Offsets of plausible zlib stream starts (78 DA / 78 9C / 78 01).
QList<qint64> findMagicOffsets(const QByteArray &data);

} // namespace ZlibUtils
