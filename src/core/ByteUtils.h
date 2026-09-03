#pragma once

#include <QByteArray>
#include <QtEndian>

// Big/little endian helpers — same semantics as the Python tool's
// be32/be64/le16/le32/put_be32/put_be64.
namespace ByteUtils {

inline quint16 be16(const QByteArray &b, qsizetype o)
{
    return qFromBigEndian<quint16>(b.constData() + o);
}
inline quint32 be32(const QByteArray &b, qsizetype o)
{
    return qFromBigEndian<quint32>(b.constData() + o);
}
inline quint32 le16(const QByteArray &b, qsizetype o)
{
    return qFromLittleEndian<quint16>(b.constData() + o);
}
inline quint32 le32(const QByteArray &b, qsizetype o)
{
    return qFromLittleEndian<quint32>(b.constData() + o);
}
inline void putBe16(QByteArray &b, qsizetype o, quint16 v)
{
    qToBigEndian<quint16>(v, b.data() + o);
}
inline void putBe32(QByteArray &b, qsizetype o, quint32 v)
{
    qToBigEndian<quint32>(v, b.data() + o);
}
inline void putLe32(QByteArray &b, qsizetype o, quint32 v)
{
    qToLittleEndian<quint32>(v, b.data() + o);
}
inline void putLe16(QByteArray &b, qsizetype o, quint16 v)
{
    qToLittleEndian<quint16>(v, b.data() + o);
}

inline bool inRange(const QByteArray &b, qsizetype o, qsizetype n)
{
    return o >= 0 && n >= 0 && o + n <= b.size();
}

} // namespace ByteUtils
