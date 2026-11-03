#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Picture helpers for HD-Player.exe's Qt resource pools ("images/...").
//
// The Resources tab (EmbeddedResources) lists the whole pool; this module keeps
// the picture-specific parts it reuses: format sniffing, the decoded preview
// data URL, a bounded entry reader, and the replace flow that validates a
// replacement picture's real format before it is written.
//
// Note that this module's own replace() lets the writer grow the file; the
// Resources tab does not use it for that reason - it writes through
// EmbeddedResources, which refuses any replacement that does not fit in the
// entry's existing space.
namespace EmbeddedImages {

struct Asset
{
    QString path;         // the name the pool stores (":/images/logo.png")
    QString family;       // lower-case format family: png, jpg, bmp, gif, ico, webp, tga, svg
    bool image = false;   // a picture this page can preview and replace
    qint64 entryOff = -1; // offset of the [size][payload] record
    quint16 flags = 0;    // 0 = stored raw, 1 = zlib
    qint64 size = 0;      // decoded size in bytes
    int width = 0;        // 0 when the header could not be read (SVG included)
    int height = 0;
};

// Every image in the EXE, sorted by path. With includeAll the other embedded
// files (QML, caches, fonts, ...) are listed as well - they carry image=false
// and are shown but cannot be previewed or replaced.
QList<Asset> list(const QByteArray &image, bool includeAll = false);

// The decoded bytes of one entry, read straight from the file with a seek so a
// preview never needs the whole EXE in memory. Empty when it cannot be read.
QByteArray readEntry(const QString &exePath, qint64 entryOff, quint16 flags,
                     qint64 limit = 32 * 1024 * 1024);

// A data: URL for QML's Image element, scaled to fit maxSide (0 = full size).
// Empty when the bytes are not a picture Qt can decode.
QString previewDataUrl(const QByteArray &bytes, const QString &family, int maxSide, bool rasterSvg = false);

// The same preview as a file:// URL, used when the data: URL cannot be built
// or when QML refuses it. The bytes are written once per payload into the
// session temp folder with the entry's own extension, because picture plugins
// (SVG in particular) read the suffix as well as the bytes.
QString previewFileUrl(const QByteArray &bytes, const QString &family);

// The format the first bytes prove: png, jpg, gif, bmp, webp, ico or svg.
// Empty for formats without a signature (TGA) and for unknown data.
QString sniffFormat(const QByteArray &bytes);

struct Result
{
    bool ok = false;
    QString msg;
};

// Validate the replacement (its real format must match the entry's, and it has
// to decode) and write it into the pool. The EXE is backed up first and the
// result is verified before it replaces the file on disk.
Result replace(const QString &exePath, const QString &path, const QString &newImagePath);

} // namespace EmbeddedImages
