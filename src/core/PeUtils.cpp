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

Imports importedModules(const QByteArray &data)
{
    Imports result;
    const int pe = peHeaderOffset(data);
    if (pe < 0 || optionalHeaderSize(data) < 2 || !inRange(data, pe + 24, optionalHeaderSize(data))) {
        result.error = QStringLiteral("Invalid or truncated PE headers"); return result;
    }
    const qint64 opt = pe + 24;
    const quint16 magic = quint16(le16(data, opt));
    const bool plus = magic == 0x20b;
    const int directories = plus ? 112 : 96;
    if ((magic != 0x10b && !plus) || optionalHeaderSize(data) < directories) {
        result.error = QStringLiteral("Unsupported PE optional header"); return result;
    }
    const auto secs = sections(data);
    if (secs.size() != le16(data, pe + 6)) { result.error = QStringLiteral("Truncated section table"); return result; }
    const quint32 headers = le32(data, opt + 60);
    const quint32 number = le32(data, opt + directories - 4);
    const quint64 imageBase = plus ? quint64(le32(data, opt + 24)) | (quint64(le32(data, opt + 28)) << 32)
                                   : le32(data, opt + 28);
    const auto raw = [&](quint64 rva, quint64 length) -> qint64 {
        if (rva + length > 0x100000000ULL) return -1;
        if (rva + length <= headers && inRange(data, qint64(rva), qint64(length))) return qint64(rva);
        qint64 found = -1;
        for (const auto &section : secs) {
            if (rva < section.va || rva - section.va + length > section.rawSize) continue;
            const qint64 offset = qint64(section.rawPtr) + qint64(rva - section.va);
            if (!inRange(data, offset, qint64(length)) || (found >= 0 && found != offset)) return -1;
            found = offset;
        }
        return found;
    };
    const auto addName = [&](quint64 rva) -> bool {
        QByteArray name;
        for (int i = 0; i < 260; ++i) {
            const qint64 offset = raw(rva + i, 1);
            if (offset < 0) return false;
            const uchar c = uchar(data[int(offset)]);
            if (!c) {
                if (name.isEmpty()) return false;
                const QString module = QString::fromLatin1(name).toLower();
                if (!result.modules.contains(module)) result.modules << module;
                return true;
            }
            if (c < 32 || c >= 127 || c == '/' || c == '\\' || c == ':') return false;
            name += char(c);
        }
        return false;
    };
    for (int index : { 1, 13 }) {
        if (number <= quint32(index)) continue;
        if (optionalHeaderSize(data) < directories + (index + 1) * 8) {
            result.error = QStringLiteral("Truncated data directories"); return result;
        }
        const quint32 rva = le32(data, opt + directories + index * 8);
        const quint32 size = le32(data, opt + directories + index * 8 + 4);
        if (!rva && !size) continue;
        const int width = index == 1 ? 20 : 32;
        if (!rva || size < quint32(width)) { result.error = QStringLiteral("Invalid import directory"); return result; }
        bool terminated = false;
        const quint32 limit = qMin<quint32>(size / width, 4096);
        for (quint32 i = 0; i < limit; ++i) {
            const qint64 descriptor = raw(quint64(rva) + quint64(i) * width, width);
            if (descriptor < 0) { result.error = QStringLiteral("Import descriptor is outside file-backed data"); return result; }
            bool zero = true;
            for (int j = 0; j < width; j += 4) zero &= le32(data, descriptor + j) == 0;
            if (zero) { terminated = true; break; }
            quint64 name = le32(data, descriptor + (index == 1 ? 12 : 4));
            if (index == 13) {
                const quint32 attributes = le32(data, descriptor);
                if (attributes & ~1u) { result.error = QStringLiteral("Unknown delay-import attributes"); return result; }
                if (!(attributes & 1)) {
                    if (name < imageBase) { result.error = QStringLiteral("Invalid delay-import name address"); return result; }
                    name -= imageBase;
                }
            }
            if (!name || !addName(name)) { result.error = QStringLiteral("Invalid import module name"); return result; }
        }
        if (!terminated) { result.error = QStringLiteral("Unterminated import descriptors"); return result; }
    }
    result.ok = true;
    return result;
}

} // namespace PeUtils
