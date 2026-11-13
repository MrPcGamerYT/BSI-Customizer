#include "QtResourceRegistration.h"
#include "ByteUtils.h"
#include "PeUtils.h"
#include <QSet>

using namespace ByteUtils;
namespace QtResources {
namespace {
qint64 raw(const QByteArray &b, const QList<PeSection> &sections, qint64 rva, int length)
{
    if (rva < 0 || rva > 0xffffffffLL) return -1;
    for (const auto &s : sections) {
        const qint64 relative = rva - s.va;
        if (relative >= 0 && relative + length <= s.rawSize && inRange(b, qint64(s.rawPtr) + relative, length))
            return qint64(s.rawPtr) + relative;
    }
    return -1;
}
QString ascii(const QByteArray &b, qint64 at)
{
    if (!inRange(b, at, 1)) return {};
    const auto end = b.indexOf('\0', at);
    if (end < at || end - at > 512) return {};
    return QString::fromLatin1(b.mid(at, end - at));
}
}
QList<Pool> registeredPools(const QByteArray &b)
{
    QList<Pool> pools;
    const auto sections = PeUtils::sections(b);
    if (sections.isEmpty()) return pools;
    const auto add = [&](qint64 treeRva, qint64 namesRva, qint64 dataRva, int version, bool registered) {
        const auto tree = raw(b, sections, treeRva, version == 1 ? 14 : 22);
        const auto names = raw(b, sections, namesRva, 6);
        if (version < 1 || version > 3 || tree < 0 || names < 0 || raw(b, sections, dataRva, 4) < 0
            || be16(b, tree + 4) != 2) return;
        for (auto &p : pools) {
            if (p.tree == tree && p.names == names && p.dataRva == dataRva && p.version == version) {
                p.registered |= registered;
                return;
            }
        }
        pools << Pool { tree, names, dataRva, version, registered };
    };
    // A binary rcc has explicit offsets. Its root's otherwise-unused name word
    // and the order/spacing of connected nodes are not required to be canonical.
    for (qsizetype at = b.indexOf("qres"); at >= 0; at = b.indexOf("qres", at + 4)) {
        if (!inRange(b, at, 20)) continue;
        const quint32 version = be32(b, at + 4);
        if (version < 1 || version > 3) continue;
        const qint64 base = PeUtils::rawToRva(sections, at);
        if (base < 0) continue;
        add(base + be32(b, at + 8), base + be32(b, at + 16), base + be32(b, at + 12), int(version), false);
    }
    // Qt5-era (and some 32-bit) BlueStacks builds are PE32 x86. They register their
    // compiled-in resources through qRegisterResourceData as well, only with 4-byte
    // thunks, absolute addresses instead of RVAs and push-immediate arguments instead
    // of registers; that shape has to be understood too, otherwise the pools of an x86
    // installation stay invisible and the surfaces inside them are never offered.
    const bool plus = PeUtils::isPe32Plus(b) && PeUtils::machine(b) == 0x8664;
    const bool x86 = !plus && !PeUtils::isPe32Plus(b) && PeUtils::machine(b) == 0x14c;
    if (!plus && !x86) return pools;
    const qint64 opt = PeUtils::peHeaderOffset(b) + 24;
    const int dirOffset = plus ? 120 : 104;                 // IMAGE_DIRECTORY_ENTRY_Import
    const int thunkSize = plus ? 8 : 4;
    if (PeUtils::optionalHeaderSize(b) < (plus ? 128 : 112) || !inRange(b, opt + dirOffset, 8)) return pools;
    const quint32 importRva = le32(b, opt + dirOffset), importSize = le32(b, opt + dirOffset + 4);
    const quint64 imageBase = plus ? quint64(le32(b, opt + 24)) | (quint64(le32(b, opt + 28)) << 32)
                                   : quint64(le32(b, opt + 28));
    const auto imports = raw(b, sections, importRva, 20);
    if (!importRva || imports < 0) return pools;
    QSet<qint64> registerIat;
    const qint64 limit = qMin<qint64>(importSize / 20, 4096);
    for (qint64 i = 0; i < limit; ++i) {
        const qint64 desc = imports + i * 20;
        if (!inRange(b, desc, 20)) break;
        quint32 lookupRva = le32(b, desc);
        const quint32 iatRva = le32(b, desc + 16);
        if (!lookupRva && !iatRva && !le32(b, desc + 12)) break;
        if (!lookupRva) lookupRva = iatRva;
        for (int index = 0; index < 65536; ++index) {
            const qint64 lookup = raw(b, sections, qint64(lookupRva) + index * thunkSize, thunkSize);
            if (lookup < 0) break;
            quint64 value = plus ? quint64(le32(b, lookup)) | (quint64(le32(b, lookup + 4)) << 32)
                                 : quint64(le32(b, lookup));
            if (!value) break;
            if (plus) {
                if (value >> 63 || value > 0xffffffffULL) continue;
            } else {
                if (value & 0x80000000ULL) continue;   // imported by ordinal
                if (imageBase && value < imageBase) continue;
                value -= imageBase;                     // x86 thunks hold virtual addresses
            }
            const auto name = raw(b, sections, qint64(value) + 2, 1);
            const QString symbol = ascii(b, name);
            if (symbol.startsWith(QLatin1String("?qRegisterResourceData@"))
                || symbol == QLatin1String("_Z21qRegisterResourceDataiPKhS0_S0_"))
                registerIat.insert(qint64(iatRva) + index * thunkSize);
        }
    }
    if (registerIat.isEmpty()) return pools;
    for (const auto &s : sections) {
        if (!(s.chars & 0x20000000) || !inRange(b, s.rawPtr, s.rawSize)) continue;
        const qint64 end = qint64(s.rawPtr) + s.rawSize;
        for (qint64 call = qint64(s.rawPtr) + (plus ? 26 : 6); call + 6 <= end; ++call) {
            const qint64 callRva = qint64(s.va) + call - s.rawPtr;
            qint64 iat = -1;
            if (uchar(b[int(call)]) == 0xff && (uchar(b[int(call + 1)]) == 0x15 || (plus && uchar(b[int(call + 1)]) == 0x25))) {
                // x64: call qword ptr [rip+disp]; x86: call dword ptr [absolute address]
                iat = plus ? callRva + 6 + qint32(le32(b, call + 2))
                           : qint64(le32(b, call + 2)) - qint64(imageBase);
            } else if (plus && (uchar(b[int(call)]) == 0xe8 || uchar(b[int(call)]) == 0xe9)) {
                const qint64 thunkRva = callRva + 5 + qint32(le32(b, call + 1));
                const qint64 thunk = raw(b, sections, thunkRva, 6);
                if (thunk >= 0 && b.mid(thunk, 2) == QByteArray::fromHex("ff25"))
                    iat = thunkRva + 6 + qint32(le32(b, thunk + 2));
            }
            if (!registerIat.contains(iat)) continue;
            qint64 tree = -1, names = -1, data = -1;
            int version = 0, count = 0;
            if (x86) {
                // MSVC pushes the four arguments right to left, so reading backwards from
                // the call yields version, tree, names, data - and each immediate is a
                // relocated address rather than the RIP-relative lea the x64 build uses.
                const qint64 earliest = qMax(call - 20, qint64(s.rawPtr));   // never read before .text
                qint64 q = call;
                qint64 arg[4] = { -1, -1, -1, -1 };
                int k = 0;
                for (; k < 4; ++k) {
                    if (q - 5 >= earliest && uchar(b[int(q - 5)]) == 0x68) { arg[k] = qint64(le32(b, q - 4)); q -= 5; }
                    else if (q - 2 >= earliest && uchar(b[int(q - 2)]) == 0x6a) { arg[k] = qint8(b[int(q - 1)]); q -= 2; }
                    else break;
                }
                const auto offset = [&](qint64 v) { return v < 0 || (imageBase && v < qint64(imageBase)) ? -1 : v - qint64(imageBase); };
                if (k == 4) {
                    version = int(arg[0]);
                    tree = offset(arg[1]); names = offset(arg[2]); data = offset(arg[3]);
                    if (version >= 1 && version <= 3 && tree >= 0 && names >= 0 && data >= 0)
                        add(tree, names, data, version, true);
                }
                continue;
            }
            qint64 p = call - 26;
            // Four complete contiguous argument-setting instructions, in any
            // compiler order: ECX=version, RDX=tree, R8=names, R9=data.
            while (p < call) {
                if (p + 5 <= call && uchar(b[int(p)]) == 0xb9 && !version) {
                    version = int(le32(b, p + 1)); p += 5;
                } else if (p + 7 <= call && uchar(b[int(p + 1)]) == 0x8d) {
                    const qint64 pointer = qint64(s.va) + p + 7 - s.rawPtr + qint32(le32(b, p + 3));
                    const auto op = b.mid(p, 3);
                    if (op == QByteArray::fromHex("488d15") && tree < 0) tree = pointer;
                    else if (op == QByteArray::fromHex("4c8d05") && names < 0) names = pointer;
                    else if (op == QByteArray::fromHex("4c8d0d") && data < 0) data = pointer;
                    else break;
                    p += 7;
                } else break;
                ++count;
            }
            if (p == call && count == 4) add(tree, names, data, version, true);
        }
    }
    return pools;
}
} // namespace QtResources
