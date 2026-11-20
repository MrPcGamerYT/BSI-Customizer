#include "ZlibUtils.h"
#include "ByteUtils.h"
#include "BoundedZopfli.h"

namespace ZlibUtils {

QByteArray tryDecompress(const QByteArray &rawStream, qint64 expectedSize)
{
    if (rawStream.isEmpty())
        return {};

    // qUncompress wants [be32 uncompressed size][zlib stream].
    QByteArray prefixed;
    prefixed.resize(4);
    if (expectedSize >= 0) {
        ByteUtils::putBe32(prefixed, 0, quint32(expectedSize));
    } else {
        // Unknown: use a generous upper bound; qUncompress validates the
        // stream itself, and we re-check the real size below.
        ByteUtils::putBe32(prefixed, 0, 64u * 1024u * 1024u);
    }
    prefixed += rawStream;

    const QByteArray out = qUncompress(prefixed);
    if (out.isEmpty() && !rawStream.isEmpty())
        return {};
    if (expectedSize >= 0 && out.size() != expectedSize)
        return {};
    return out;
}

QByteArray compress(const QByteArray &data)
{
    const QByteArray withHeader = qCompress(data, 9);
    if (withHeader.size() <= 4)
        return {};
    return withHeader.mid(4); // strip qCompress' 4-byte BE size header
}

QByteArray compressForCapacity(const QByteArray &data, qint64 capacity)
{
    const QByteArray ordinary = qCompress(data, 9);
    constexpr qsizetype maxTightInput = 256 * 1024;
    if (capacity < 0 || ordinary.size() <= capacity || data.isEmpty() || data.size() > maxTightInput)
        return ordinary;
    if (ordinary.size() <= 10) return ordinary;
    // A strictly smaller result is the only useful result. Upstream allocations
    // and exit paths are contained by a bounded, leak-cleaning C guard, with no
    // longjmp across Qt/C++ frames. OOM/failure keeps ordinary compression; the
    // caller still refuses a slot that does not fit it, before any disk write.
    QByteArray tight(ordinary.size() - 1, '\0');
    size_t size = 0;
    if (!bsiBoundedZopfli(reinterpret_cast<const unsigned char *>(data.constData()), size_t(data.size()),
                          reinterpret_cast<unsigned char *>(tight.data() + 4), size_t(tight.size() - 4), &size))
        return ordinary;
    tight.resize(4 + int(size));
    ByteUtils::putBe32(tight, 0, quint32(data.size()));
    // Same decoder as the player. Keep all source bytes, including BOM/CRLF,
    // and never trust an encoder result merely because it is shorter.
    return qUncompress(tight) == data ? tight : ordinary;
}

QByteArray inflateLoose(const QByteArray &rawStream, qint64 sizeHint)
{
    if (rawStream.size() < 2)
        return {};

    // qUncompress wants [be32 uncompressed size][zlib stream].  The size is a
    // pre-allocation hint: Qt grows the buffer on Z_BUF_ERROR and stops at
    // Z_STREAM_END, so trailing bytes past the stream end are simply left
    // unconsumed.  That is what lets the caller over-read the EXE instead of
    // having to know the exact compressed length up front.
    auto attempt = [&rawStream](qint64 hint) -> QByteArray {
        QByteArray prefixed;
        prefixed.resize(4);
        ByteUtils::putBe32(prefixed, 0, quint32(hint));
        prefixed += rawStream;
        return qUncompress(prefixed);
    };

    // Exact size first: correct whether or not Qt enforces the hint, and the
    // cheapest path (no buffer doubling).
    if (sizeHint > 0) {
        const QByteArray out = attempt(sizeHint);
        if (!out.isEmpty())
            return out;
    }

    // Unknown or wrong hint: sweep upwards.  Each attempt either inflates the
    // whole stream or fails outright, so the first non-empty result wins.
    static const qint64 hints[] = { 1 << 12, 1 << 16, 1 << 20, 1 << 23 };
    for (const qint64 h : hints) {
        if (sizeHint > 0 && h <= sizeHint)
            continue; // already tried something at least this large
        const QByteArray out = attempt(h);
        if (!out.isEmpty())
            return out;
    }
    return {};
}

QList<qint64> findMagicOffsets(const QByteArray &data)
{
    static const char magics[4][2] = { { '\x78', '\xDA' }, { '\x78', '\x9C' }, { '\x78', '\x01' }, { '\x78', '\x5E' } };
    QList<qint64> out;
    for (const auto &m : magics) {
        qsizetype pos = 0;
        while (true) {
            pos = data.indexOf(QByteArray(m, 2), pos);
            if (pos < 0)
                break;
            out << pos;
            pos += 1;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace ZlibUtils
