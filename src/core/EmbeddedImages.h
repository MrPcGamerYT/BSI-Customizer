#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Image files embedded in HD-Player.exe's Qt resource pools ("images/...").
//
// The SVG page scans the EXE for raw <svg> text blobs; every other picture the
// player draws (PNG, JPEG, BMP, GIF, ICO, WEBP, TGA and SVG files) lives in a
// Qt resource tree instead, zlib-compressed and addressed by name. This is the
// same list -> preview -> replace flow for those entries: the pool is
// enumerated, each image is decoded for a preview, and a replacement is
// written back through the validated resource rewrite (compression flag and PE
// layout preserved) with the original EXE backed up first.
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
QString previewDataUrl(const QByteArray &bytes, const QString &family, int maxSide);

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
