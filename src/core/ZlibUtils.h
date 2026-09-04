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

// Inflate the zlib stream at the START of rawStream, ignoring any trailing
// bytes after the stream end.  This is what makes detection robust: the caller
// can hand over a generous slice of the EXE without knowing the exact
// compressed length (qUncompress stops at Z_STREAM_END and leaves the rest
// unconsumed).
//
// sizeHint (>= 0) is qUncompress' pre-allocation hint.  Qt only uses it to
// size the output buffer (it grows on Z_BUF_ERROR), so an inexact hint still
// works - but the exact uncompressed size is tried first because that is the
// cheap, always-correct case.  Pass -1 when the size is unknown; a sweep of
// increasing hints is used instead.
//
// Returns an empty array if nothing inflates.
QByteArray inflateLoose(const QByteArray &rawStream, qint64 sizeHint = -1);

// Offsets of plausible zlib stream starts (78 DA / 78 9C / 78 01).
QList<qint64> findMagicOffsets(const QByteArray &data);

} // namespace ZlibUtils
