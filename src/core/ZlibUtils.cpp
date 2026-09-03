#include "ZlibUtils.h"
#include "ByteUtils.h"

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

QList<qint64> findMagicOffsets(const QByteArray &data)
{
    static const char magics[3][2] = { { '\x78', '\xDA' }, { '\x78', '\x9C' }, { '\x78', '\x01' } };
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
