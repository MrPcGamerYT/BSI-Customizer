#include "PeUtils.h"
#include "ByteUtils.h"

using namespace ByteUtils;

namespace PeUtils {

int peHeaderOffset(const QByteArray &d)
{
    if (d.size() < 0x100 || d[0] != 'M' || d[1] != 'Z')
        return -1;
    const quint32 pe = le32(d, 0x3C);
    if (pe <= 0 || !inRange(d, pe, 4) || d[pe] != 'P' || d[pe + 1] != 'E'
        || d[pe + 2] != 0 || d[pe + 3] != 0)
        return -1;
    return int(pe);
}

quint16 machine(const QByteArray &d)
{
    const int pe = peHeaderOffset(d);
    if (pe < 0 || !inRange(d, pe + 4, 2))
        return 0;
    return quint16(le16(d, pe + 4));
}

quint32 optionalHeaderSize(const QByteArray &d)
{
    const int pe = peHeaderOffset(d);
    if (pe < 0 || !inRange(d, pe + 20, 2))
        return 0;
    return le16(d, pe + 20);
}

bool isPe32Plus(const QByteArray &d)
{
    const int pe = peHeaderOffset(d);
    if (pe < 0)
        return false;
    const quint32 opt = pe + 24;
    if (!inRange(d, opt, 2))
        return false;
    return le16(d, opt) == 0x20B;
}

quint32 exportRva(const QByteArray &d)
{
    if (!isPe32Plus(d))
        return 0;
    const int pe = peHeaderOffset(d);
    const quint32 opt = pe + 24;
    if (!inRange(d, opt + 112, 4))
        return 0;
    return le32(d, opt + 112);
}

QList<PeSection> sections(const QByteArray &d)
{
    QList<PeSection> out;
    const int pe = peHeaderOffset(d);
    if (pe < 0 || !inRange(d, pe + 6, 2))
        return out;

    const quint16 nsec = quint16(le16(d, pe + 6));
    const quint32 optSize = optionalHeaderSize(d);
    const quint32 secOff = pe + 24 + optSize;

    for (quint16 i = 0; i < nsec; ++i) {
        const quint32 s = secOff + i * 40u;
        if (!inRange(d, s, 40))
            break;
        PeSection sec;
        QByteArray nameRaw = d.mid(s, 8);
        const int z = nameRaw.indexOf('\0');
        if (z >= 0)
            nameRaw.truncate(z);
        sec.name = QString::fromLatin1(nameRaw);
        sec.vsize = le32(d, s + 8);
        sec.va = le32(d, s + 12);
        sec.rawSize = le32(d, s + 16);
        sec.rawPtr = le32(d, s + 20);
        sec.chars = le32(d, s + 36);
        out << sec;
    }
    return out;
}

qint64 rvaToRaw(const QList<PeSection> &secs, quint32 rva)
{
    for (const PeSection &s : secs) {
        const quint32 span = qMax(s.vsize, s.rawSize);
        if (rva >= s.va && rva < s.va + span)
            return qint64(rva) - s.va + s.rawPtr;
    }
    return -1;
}

qint64 rawToRva(const QList<PeSection> &secs, qint64 raw)
{
    for (const PeSection &s : secs) {
        if (raw >= s.rawPtr && raw < qint64(s.rawPtr) + s.rawSize)
            return s.va + (raw - s.rawPtr);
    }
    return -1;
}

const PeSection *sectionForRaw(const QList<PeSection> &secs, qint64 raw)
{
    for (const PeSection &s : secs) {
        if (raw >= s.rawPtr && raw < qint64(s.rawPtr) + s.rawSize)
            return &s;
    }
    return nullptr;
}

} // namespace PeUtils
