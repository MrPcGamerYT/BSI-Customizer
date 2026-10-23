#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

// Every file embedded in HD-Player.exe's Qt resource pools, not just pictures.
//
// The player ships its whole UI as resources: QML, JavaScript, SVG icon
// markup, PNG/JPEG/BMP art, compiled caches and fonts. This is one list of all
// of them with a preview/editor per kind:
//
//  * pictures  - decoded preview, replaced by another picture of the same kind
//  * text      - QML, JS, SVG, JSON, XML, CSS, HTML, ... shown as code and
//                edited in place (the source is what Qt loads once the matching
//                .qmlc cache is invalidated, which the writer does)
//  * binary    - listed, extracted and replaceable with bytes of the same kind
//
// SIZE RULES, both enforced here and by the writer:
//  1. the embedded file itself never grows - a replacement with more decoded
//     bytes than the entry already has is refused before anything is touched;
//  2. HD-Player.exe never grows - the record has to fit the space the entry
//     already owns, or a verified empty gap inside the same PE section, which
//     the writer re-points the tree node at. Nothing is ever appended, no
//     section changes and the file size is asserted again after the rewrite.
// A replacement that breaks either rule is refused and the EXE is untouched.
namespace EmbeddedResources {

enum class Kind { Image, Svg, Text, Binary };

struct Entry
{
    QString path;         // the resource path the pool stores ("images/logo.png")
    QString leaf;         // its last segment ("logo.png")
    QString folder;       // everything before it ("images")
    QString format;       // lower-case extension ("png", "qml", "qmlc", ...)
    Kind kind = Kind::Binary;
    qint64 entryOff = -1; // offset of the [size][payload] record
    quint16 flags = 0;    // 0 = stored raw, 1 = zlib
    qint64 size = 0;      // decoded bytes
    qint64 stored = 0;    // bytes the record currently occupies (payload only)
    int width = 0;        // pictures only; 0 when the header could not be read
    int height = 0;
    bool text = false;    // the decoded bytes are UTF-8 the editor can show
};

// Every embedded file, sorted by path. `includeBinary = false` drops compiled
// caches and other opaque entries; the Resources tab shows them all.
QList<Entry> list(const QByteArray &image, bool includeBinary = true);

// The decoded bytes of one entry, read from the file with a seek so a preview
// never needs the whole EXE in memory. Empty when it cannot be read.
QByteArray readEntry(const QString &exePath, qint64 entryOff, quint16 flags,
                     qint64 limit = 32 * 1024 * 1024);

// Decoded bytes -> the text the code editor shows. Empty when the bytes are not
// valid UTF-8 (a BOM is allowed and dropped).
QString decodeText(const QByteArray &bytes);

// A data: URL for QML's Image element, scaled to fit maxSide (0 = full size).
// Empty when the bytes are not a picture Qt can decode.
QString previewDataUrl(const QByteArray &bytes, const QString &format, int maxSide);

struct Budget
{
    bool known = false;    // the mapping was found and measured
    qint64 capacity = 0;   // most payload bytes the entry can hold in place
    qint64 used = 0;       // payload bytes it holds now
    QString detail;        // why it is unknown, or a short summary
};

// How much room the entry has without growing the file.
Budget budget(const QByteArray &image, qint64 entryOff, const QString &leaf);

// The payload size `bytes` would take in this entry (compressed when the entry
// is compressed). Used by the editor to show the budget before saving.
qint64 payloadSizeFor(const QByteArray &bytes, quint16 flags);

struct Result
{
    bool ok = false;
    QString msg;
};

// Write `bytes` into the entry, refusing anything that would need the file to
// grow. The EXE is backed up first, verified, and only then replaced on disk.
Result replaceBytes(const QString &exePath, const QString &path, const QByteArray &bytes);

// Text entry: the same write, with the bytes taken from the editor. A UTF-8 BOM
// is preserved when the original entry had one.
Result replaceText(const QString &exePath, const QString &path, const QString &text);

// Picture entry: reads the file and applies the same checks as the Images tab
// (same format family, decodable, never bigger than the entry's space).
Result replacePicture(const QString &exePath, const QString &path, const QString &newImagePath);

} // namespace EmbeddedResources
