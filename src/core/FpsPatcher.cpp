#include "FpsPatcher.h"

#include "ByteUtils.h"
#include "BsDetector.h"
#include "PatchFile.h"
#include "PeUtils.h"
#include "X64Prologue.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace ByteUtils;

namespace FpsPatcher {

namespace {

const QMap<QString, QString> &wanted()
{
    // MSVC + Itanium manglings for QQuickSlider::setTo / QQuickRangeSlider::setTo
    static const QMap<QString, QString> m = [] {
        QMap<QString, QString> w;
        for (const QString &cls : { QStringLiteral("QQuickSlider"),
                                    QStringLiteral("QQuickRangeSlider") }) {
            w[QStringLiteral("?setTo@%1@@QEAAXN@Z").arg(cls)] = cls;
            w[QStringLiteral("_ZN") + QString::number(cls.size()) + cls
              + QStringLiteral("5setToEd")] = cls;
        }
        return w;
    }();
    return m;
}

const QByteArray prologue()
{
    static const QByteArray p = QByteArray::fromHex("48895C2408");
    return p;
}

constexpr int kSlotLen = 64;
constexpr int kV1SlotLen = 48;
constexpr int kLegacySlotLen = 80;

QByteArray le32s(qint32 v)
{
    QByteArray b(4, '\0');
    putLe32(b, 0, quint32(v));
    return b;
}

QByteArray d64(double v)
{
    QByteArray b(8, '\0');
    QDataStream ds(&b, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    ds << v;
    return b;
}

// Read-only compatibility with the previous 48-byte max-only patch.
QByteArray v1SlotForVerification(quint32 funcRva, quint32 caveRva, double hi)
{
    QByteArray s;
    s += QByteArray::fromHex("660F2F0D") + le32s(32 - 8);   // comisd xmm1,[rip->240.0]
    s += QByteArray::fromHex("7A") + char(20 - 10);        // jp -> original (NaN is not 240)
    s += QByteArray::fromHex("75") + char(20 - 12);        // jne -> original, including 60.0
    s += QByteArray::fromHex("F20F100D") + le32s(40 - 20); // movsd xmm1,[rip->hi]
    s += prologue();                                      // original mov [rsp+8],rbx
    s += QByteArray::fromHex("E9") + le32s(qint32(qint64(funcRva) + 5 - (qint64(caveRva) + 30)));
    s += QByteArray::fromHex("9090");                     // padding before constants, never executed
    s += d64(240.0) + d64(hi);
    Q_ASSERT(s.size() == kV1SlotLen);
    return s;
}

// Read/verification compatibility only. This is the 80-byte detour in the
// user's read it.txt reference and earlier releases. Never emit it in patch().
QByteArray legacySlotForVerification(quint32 funcRva, quint32 caveRva, double hi, double lo)
{
    QByteArray s;
    s += QByteArray::fromHex("660F2F0D") + le32s(48 - 8);   // comisd xmm1,[rip->240.0]
    s += QByteArray::fromHex("75") + char(20 - 10);          // jne -> legacy normal-mode check
    s += QByteArray::fromHex("F20F100D") + le32s(64 - 18);  // movsd xmm1,[rip->hi]
    s += QByteArray::fromHex("EB") + char(38 - 20);          // jmp -> prologue
    s += QByteArray::fromHex("660F2F0D") + le32s(56 - 28);  // comisd xmm1,[rip->60.0]
    s += QByteArray::fromHex("75") + char(38 - 30);          // jne -> prologue
    s += QByteArray::fromHex("F20F100D") + le32s(72 - 38);  // movsd xmm1,[rip->lo]
    s += QByteArray::fromHex("48895C2408");                  // mov [rsp+8], rbx
    s += QByteArray::fromHex("E9") + le32s(qint32(qint64(funcRva) + 5 - (qint64(caveRva) + 48)));
    s += d64(240.0) + d64(60.0) + d64(hi) + d64(lo);
    Q_ASSERT(s.size() == kLegacySlotLen);
    return s;
}

double readDouble(const QByteArray &bytes, qint64 offset)
{
    QDataStream ds(bytes.mid(offset, 8));
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
    double value = 0;
    ds >> value;
    return value;
}

// The new slot replays whole entry instructions. Metadata identifies their
// length, allowing verified repatching/restoration even for RIP-relative code.
QByteArray buildSlot(quint32 funcRva, quint32 caveRva, double hi,
                     const QByteArray &original, QString *error = nullptr)
{
    QByteArray relocated;
    if (original.size() < 5 || original.size() > X64Prologue::MaxSize
        || caveRva > std::numeric_limits<quint32>::max() - kSlotLen
        || !X64Prologue::relocate(original, funcRva, caveRva + 20, &relocated, error))
        return {};
    const qint64 back = qint64(funcRva) + original.size()
                        - (qint64(caveRva) + 20 + original.size() + 5);
    if (back < std::numeric_limits<qint32>::min() || back > std::numeric_limits<qint32>::max()) {
        if (error)
            *error = QStringLiteral("code cave is too far from the setter");
        return {};
    }
    QByteArray s(kSlotLen, char(0x90));
    const QByteArray dispatch = QByteArray::fromHex("660F2E0D") + le32s(48 - 8)
                                + QByteArray::fromHex("7A0A7508F20F100D") + le32s(56 - 20);
    s.replace(0, 20, dispatch); // UCOMISD: only 240 matches; quiet NaNs/other values pass unchanged
    s.replace(20, relocated.size(), relocated);
    s.replace(20 + relocated.size(), 5, QByteArray::fromHex("E9") + le32s(qint32(back)));
    s.replace(44, 3, QByteArray::fromHex("465001")); // FP, format version 1
    s[47] = char(original.size());
    s.replace(48, 8, d64(240.0));
    s.replace(56, 8, d64(hi));
    return s;
}

bool recognizeSlot(const QByteArray &bytes, qint64 offset, quint32 slotRva, Target *target)
{
    if (inRange(bytes, offset, kSlotLen) && bytes.mid(offset + 44, 3) == QByteArray::fromHex("465001")) {
        const int length = uchar(bytes[int(offset + 47)]);
        const double hi = readDouble(bytes, offset + 56);
        QByteArray original;
        if (length >= 5 && length <= X64Prologue::MaxSize && std::isfinite(hi)
            && slotRva <= std::numeric_limits<quint32>::max() - kSlotLen
            && X64Prologue::relocate(bytes.mid(offset + 20, length), slotRva + 20, target->rva, &original)
            && bytes.mid(offset, kSlotLen) == buildSlot(target->rva, slotRva, hi, original)
            && inRange(bytes, target->off, length)
            && bytes.mid(target->off + 5, length - 5) == QByteArray(length - 5, char(0x90))) {
            target->slotSize = kSlotLen;
            target->originalBytes = original;
        }
    }
    if (!target->slotSize && inRange(bytes, offset, kV1SlotLen)) {
        const double hi = readDouble(bytes, offset + 40);
        if (std::isfinite(hi) && bytes.mid(offset, kV1SlotLen) == v1SlotForVerification(target->rva, slotRva, hi)) {
            target->slotSize = kV1SlotLen;
            target->originalBytes = prologue();
        }
    }
    if (!target->slotSize && inRange(bytes, offset, kLegacySlotLen)) {
        const double hi = readDouble(bytes, offset + 64);
        const double normal = readDouble(bytes, offset + 72);
        if (std::isfinite(hi) && std::isfinite(normal)
            && bytes.mid(offset, kLegacySlotLen) == legacySlotForVerification(target->rva, slotRva, hi, normal)) {
            target->slotSize = kLegacySlotLen;
            target->originalBytes = prologue();
        }
    }
    if (!target->slotSize)
        return false;
    target->slotOff = offset;
    target->state = QStringLiteral("patched");
    return true;
}

// Unlike the general PE RVA mapper, executable bytes must really be present
// in the file, in a single executable section (not virtual zero-fill or data).
qint64 codeOffset(const QByteArray &bytes, const QList<PeSection> &sections, quint32 rva, int size)
{
    qint64 found = -1;
    if (size <= 0 || qint64(rva) + size > 0x100000000LL)
        return -1;
    for (const auto &section : sections) {
        const qint64 relative = qint64(rva) - section.va;
        if (!(section.chars & 0x20000000) || relative < 0 || relative + size > section.rawSize)
            continue;
        const qint64 offset = qint64(section.rawPtr) + relative;
        if (!inRange(bytes, offset, size))
            continue;
        if (found >= 0 && found != offset)
            return -1;
        found = offset;
    }
    return found;
}

bool isRuntimeFunctionStart(const QByteArray &bytes, const QList<PeSection> &sections, quint32 rva)
{
    // Follow an unrecognized export jump only to a compiler-described function
    // entry. An arbitrary E9 to a third-party code cave is NOT a clean prologue.
    const qint64 opt = PeUtils::peHeaderOffset(bytes) + 24;
    if (PeUtils::optionalHeaderSize(bytes) < 144 || !inRange(bytes, opt + 136, 8))
        return false;
    const quint32 tableRva = le32(bytes, opt + 136);
    const quint32 size = le32(bytes, opt + 140);
    const qint64 table = PeUtils::rvaToRaw(sections, tableRva);
    if (!tableRva || !size || size % 12 || size / 12 > 65536 || !inRange(bytes, table, size))
        return false;
    for (quint32 i = 0; i < size; i += 12) {
        if (le32(bytes, table + i) != rva)
            continue;
        const quint32 end = le32(bytes, table + i + 4);
        const quint32 unwindRva = le32(bytes, table + i + 8);
        const qint64 unwind = PeUtils::rvaToRaw(sections, unwindRva);
        const qint64 length = qint64(end) - rva;
        if (length < 5 || length > std::numeric_limits<int>::max()
            || codeOffset(bytes, sections, rva, int(length)) < 0 || (unwindRva & 3)
            || !inRange(bytes, unwind, 4))
            return false;
        const int version = uchar(bytes[int(unwind)]) & 7;
        const int flags = uchar(bytes[int(unwind)]) >> 3;
        const int count = uchar(bytes[int(unwind + 2)]);
        const int infoSize = ((4 + 2 * count + 3) & ~3) + ((flags & 4) ? 12 : ((flags & 3) ? 4 : 0));
        return (version == 1 || version == 2) && flags <= 4
               && uchar(bytes[int(unwind + 1)]) <= length && inRange(bytes, unwind, infoSize);
    }
    return false;
}

void inspectTarget(const QByteArray &bytes, const QList<PeSection> &sections, Target *target)
{
    target->state = QStringLiteral("unknown");
    target->rva = target->entryRva;
    QSet<quint32> visited;
    for (int hops = 0; hops < 5; ++hops) {
        target->off = codeOffset(bytes, sections, target->rva, 5);
        if (target->off < 0) {
            target->detail = QStringLiteral("entry is not backed by executable file bytes");
            return;
        }
        // Preserve CET's indirect-branch landing instruction at the real entry.
        if (bytes.mid(target->off, 4) == QByteArray::fromHex("F30F1EFA")) {
            if (target->rva > std::numeric_limits<quint32>::max() - 4)
                break;
            target->rva += 4;
            target->off = codeOffset(bytes, sections, target->rva, 5);
            if (target->off < 0)
                break;
        }
        target->entryBytes = bytes.mid(target->off, X64Prologue::MaxSize);
        if (visited.contains(target->rva))
            break;
        visited.insert(target->rva);
        const uchar first = uchar(bytes[int(target->off)]);
        if (first == 0xe9 || first == 0xeb) {
            const int jumpSize = first == 0xe9 ? 5 : 2;
            const qint64 jumpRva = qint64(target->rva) + jumpSize
                                   + (first == 0xe9 ? qint32(le32(bytes, target->off + 1))
                                                    : qint8(bytes[int(target->off + 1)]));
            if (jumpRva >= 0 && jumpRva <= std::numeric_limits<quint32>::max()) {
                const qint64 slotOff = codeOffset(bytes, sections, quint32(jumpRva), kV1SlotLen);
                if (first == 0xe9 && slotOff >= 0 && recognizeSlot(bytes, slotOff, quint32(jumpRva), target)
                    && codeOffset(bytes, sections, quint32(jumpRva), target->slotSize) == slotOff
                    && codeOffset(bytes, sections, target->rva, int(target->originalBytes.size())) == target->off)
                    return;
                target->slotSize = 0;
                target->slotOff = -1;
                target->originalBytes.clear();
                target->state = QStringLiteral("unknown");
                if (isRuntimeFunctionStart(bytes, sections, quint32(jumpRva))) {
                    target->rva = quint32(jumpRva);
                    continue;
                }
            }
            target->detail = QStringLiteral("unrecognized jump/detour (not a verified compiler entry)");
            return;
        }
        const auto decoded = X64Prologue::decode(target->entryBytes);
        if (decoded.ok() && codeOffset(bytes, sections, target->rva, int(decoded.bytes.size())) == target->off) {
            target->originalBytes = decoded.bytes;
            target->state = QStringLiteral("clean");
            return;
        }
        target->detail = decoded.error.isEmpty() ? QStringLiteral("entry crosses the executable section") : decoded.error;
        return;
    }
    target->detail = QStringLiteral("cyclic, invalid, or excessive entry thunks");
}

QByteArray buildJmp(quint32 fromRva, quint32 toRva)
{
    return QByteArray::fromHex("E9") + le32s(qint32(qint64(toRva) - (qint64(fromRva) + 5)));
}

QString readAsciiz(const QByteArray &b, qint64 off)
{
    qint64 e = off;
    while (e < b.size() && b[int(e)] != '\0')
        ++e;
    return QString::fromLatin1(b.mid(int(off), int(e - off)));
}

} // namespace

bool isSupportedPath(const QString &dllPath)
{
    QString name = dllPath;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    name = name.section(QLatin1Char('/'), -1);
    return name.compare(QLatin1String("Qt5QuickTemplates2.dll"), Qt::CaseInsensitive) == 0
           || name.compare(QLatin1String("Qt6QuickTemplates2.dll"), Qt::CaseInsensitive) == 0;
}

Analysis analyze(const QByteArray &b)
{
    Analysis out;
    if (!PeUtils::isPe32Plus(b) || PeUtils::machine(b) != 0x8664) {
        out.state = QStringLiteral("unsupported");
        out.msg = (PeUtils::peHeaderOffset(b) < 0)
                      ? QStringLiteral("not a Windows DLL/EXE (no MZ header)")
                      : QStringLiteral("not a 64-bit PE32+ DLL - unsupported");
        return out;
    }

    const QList<PeSection> secs = PeUtils::sections(b);
    const quint32 expRva = PeUtils::exportRva(b);
    if (expRva == 0) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("DLL has no export table");
        return out;
    }

    const qint64 ed = PeUtils::rvaToRaw(secs, expRva);
    if (ed < 0 || !inRange(b, ed + 36, 4)) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("bad export directory");
        return out;
    }

    const quint32 nNames = le32(b, ed + 24);
    const qint64 funcsOff = PeUtils::rvaToRaw(secs, le32(b, ed + 28));
    const qint64 namesOff = PeUtils::rvaToRaw(secs, le32(b, ed + 32));
    const qint64 ordsOff = PeUtils::rvaToRaw(secs, le32(b, ed + 36));
    if (funcsOff < 0 || namesOff < 0 || ordsOff < 0) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("bad export directory RVAs");
        return out;
    }

    if (!inRange(b, namesOff, qint64(nNames) * 4)
        || !inRange(b, ordsOff, qint64(nNames) * 2)) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("truncated export table");
        return out;
    }
    QSet<qint64> seenFunctions;
    for (quint32 i = 0; i < nNames; ++i) {
        if (!inRange(b, namesOff + i * 4, 4))
            break;
        const qint64 nameOff = PeUtils::rvaToRaw(secs, le32(b, namesOff + i * 4));
        if (nameOff < 0)
            continue;
        const QString name = readAsciiz(b, nameOff);
        if (!wanted().contains(name))
            continue;
        if (!inRange(b, ordsOff + qint64(i) * 2, 2))
            continue;
        const quint16 ord = quint16(le16(b, ordsOff + qint64(i) * 2));
        if (!inRange(b, funcsOff + ord * 4, 4))
            continue;
        const quint32 frva = le32(b, funcsOff + ord * 4);
        Target t;
        t.name = name;
        t.cls = wanted().value(name);
        t.entryRva = frva;
        inspectTarget(b, secs, &t);
        if (t.off >= 0 && seenFunctions.contains(t.off))
            continue; // exported aliases/thunks resolving to the same setter need one detour
        if (t.off >= 0)
            seenFunctions.insert(t.off);
        out.targets << t;
    }

    if (out.targets.size() > 2) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("unexpected duplicate slider setters");
        return out;
    }
    if (out.targets.isEmpty()) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("slider functions (setTo) not found in exports");
        return out;
    }

    bool anyClean = false, anyPatched = false, anyUnknown = false;
    for (const Target &t : out.targets) {
        anyClean |= (t.state == QLatin1String("clean"));
        anyPatched |= (t.state == QLatin1String("patched"));
        anyUnknown |= (t.state == QLatin1String("unknown"));
    }
    if (anyUnknown) {
        out.state = QStringLiteral("unsupported");
        QStringList details;
        for (const auto &target : out.targets) {
            if (target.state != QLatin1String("unknown"))
                continue;
            details << QStringLiteral("%1::setTo at RVA 0x%2: %3\nEntry bytes: %4")
                           .arg(target.cls, QString::number(target.rva, 16), target.detail,
                                QString::fromLatin1(target.entryBytes.toHex(' ')));
        }
        out.msg = QStringLiteral("Unsupported setter; no DLL bytes changed.\n%1\n"
                                 "Use an original DLL or copy these details and provide the DLL/.bak for support.")
                      .arg(details.join(QLatin1Char('\n')));
        return out;
    }
    if (anyPatched && anyClean) {
        out.ok = true;
        out.state = QStringLiteral("partial");
        out.msg = QStringLiteral("Recognized partial FPS patch. Patch FPS will rebuild all setters using the matching clean .bak.");
        return out;
    }
    if (anyPatched && !anyClean) {
        out.ok = true;
        out.state = QStringLiteral("patched");
        bool legacy = false;
        for (const auto &target : out.targets)
            legacy |= target.slotSize == kLegacySlotLen;
        out.msg = legacy
                      ? QStringLiteral("Legacy high/normal patch detected. Patch again for max-FPS only.")
                      : QStringLiteral("%1 setTo function(s) patched: high-FPS maximum only; normal mode unchanged.")
                            .arg(out.targets.size());
        return out;
    }
    if (!anyClean) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("setTo found but unknown prologue");
        return out;
    }

    // Longest zero run in .text
    const PeSection *text = nullptr;
    for (const PeSection &s : secs) {
        if (s.name == QLatin1String(".text") && (s.chars & 0x20000000)) {
            text = &s;
            break;
        }
    }
    if (!text) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("no .text section");
        return out;
    }

    QList<QPair<qint64, qint64>> referenced;
    for (const auto &target : out.targets) {
        const auto prefix = X64Prologue::decode(target.originalBytes);
        for (const auto &instruction : prefix.instructions) {
            if (instruction.ripDisplacement < 0)
                continue;
            const qint64 rva = qint64(target.rva) + instruction.offset + instruction.size
                               + qint32(le32(target.originalBytes, instruction.offset + instruction.ripDisplacement));
            if (rva < 0 || rva > std::numeric_limits<quint32>::max())
                continue;
            const qint64 raw = PeUtils::rvaToRaw(secs, quint32(rva));
            if (raw >= 0)
                referenced << qMakePair(raw, raw + 16); // conservative maximum scalar/SSE operand size
        }
    }
    std::sort(referenced.begin(), referenced.end());
    qint64 best = -1, bestLen = 0, run = 0;
    const auto consider = [&](qint64 start, qint64 size) {
        const qint64 end = start + size;
        const auto gap = [&](qint64 at, qint64 length) {
            if (length > bestLen) {
                best = at;
                bestLen = length;
            }
        };
        for (const auto &range : referenced) {
            if (range.second <= start || range.first >= end)
                continue;
            gap(start, range.first - start);
            start = qMax(start, qMin(end, range.second));
        }
        gap(start, end - start);
    };
    const qint64 i0 = text->rawPtr;
    const qint64 i1 = qMin<qint64>(b.size(), qint64(text->rawPtr) + text->rawSize);
    for (qint64 i = i0; i < i1; ++i) {
        if (b[int(i)] == '\0') {
            ++run;
        } else {
            consider(i - run, run);
            run = 0;
        }
    }
    consider(i1 - run, run);
    if (bestLen < out.targets.size() * kSlotLen) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("no empty padding area found");
        return out;
    }

    out.ok = true;
    out.state = QStringLiteral("clean");
    out.caveOff = best;
    out.caveRva = quint32(best - text->rawPtr + text->va);
    out.msg = QStringLiteral("%1 setTo function(s) found; cave %2 bytes")
                  .arg(out.targets.size())
                  .arg(bestLen);
    return out;
}

Result patch(const QString &dllPath, double hi)
{
    if (!isSupportedPath(dllPath))
        return { false, QStringLiteral("Select Qt5QuickTemplates2.dll or Qt6QuickTemplates2.dll, "
                                        "not the OpenGL renderer DLL.") };
    if (!std::isfinite(hi) || hi < 60.0 || hi > 1440.0)
        return { false, QStringLiteral("Max FPS must be between 60 and 1440.") };
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances (HD-Player.exe) before patching FPS.") };
    QFile current(dllPath);
    if (!current.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read %1").arg(dllPath) };
    const QByteArray original = current.readAll();
    current.close();
    const auto currentAnalysis = analyze(original);
    if (!currentAnalysis.ok)
        return { false, currentAnalysis.msg };
    const QString bak = dllPath + QStringLiteral(".bak");

    QString src = dllPath;
    if (QFileInfo::exists(bak)) {
        src = bak; // always patch from the pristine backup
    } else {
        if (currentAnalysis.state != QLatin1String("clean"))
            return { false, QStringLiteral("The DLL is already patched and its clean .bak is missing.") };
        if (!QFile::copy(dllPath, bak))
            return { false, QStringLiteral("Could not create backup.") };
    }

    QFile f(src);
    if (!f.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read %1").arg(src) };
    QByteArray data = f.readAll();
    f.close();

    // A backup from a different BlueStacks/Qt version must never overwrite
    // today's DLL. Strip only our verified detours before comparing it.
    QByteArray unpatched = original;
    for (const auto &target : currentAnalysis.targets) {
        if (target.state != QLatin1String("patched"))
            continue;
        unpatched.replace(target.slotOff, target.slotSize, QByteArray(target.slotSize, '\0'));
        unpatched.replace(target.off, target.originalBytes.size(), target.originalBytes);
    }
    if (unpatched != data)
        return { false, QStringLiteral("The .bak does not match this DLL version. No file was "
                                        "written; keep the old backup separately and reload a clean DLL.") };

    const Analysis t = analyze(data);
    if (!t.ok || t.state != QLatin1String("clean"))
        return { false, QStringLiteral("A clean backup is required: %1").arg(t.msg) };

    // Allocate only the number of slots actually needed by the exported setters.
    for (int i = 0; i < t.targets.size() * kSlotLen; ++i) {
        if (char(data[int(t.caveOff) + i]) != '\0')
            return { false, QStringLiteral("cave area not empty") };
    }

    int slot = 0;
    for (const Target &tg : t.targets) {
        if (tg.state != QLatin1String("clean"))
            continue;
        const quint32 cRva = t.caveRva + quint32(slot * kSlotLen);
        QString slotError;
        const QByteArray s = buildSlot(tg.rva, cRva, hi, tg.originalBytes, &slotError);
        const qint64 entryJump = qint64(cRva) - (qint64(tg.rva) + 5);
        if (s.size() != kSlotLen || entryJump < std::numeric_limits<qint32>::min()
            || entryJump > std::numeric_limits<qint32>::max())
            return { false, QStringLiteral("Cannot relocate %1::setTo safely: %2").arg(tg.cls, slotError) };
        const QByteArray jmp = buildJmp(tg.rva, cRva)
                               + QByteArray(tg.originalBytes.size() - 5, char(0x90));
        data.replace(int(t.caveOff) + slot * kSlotLen, kSlotLen, s);
        data.replace(tg.off, tg.originalBytes.size(), jmp);
        ++slot;
    }
    if (slot == 0)
        return { false, QStringLiteral("nothing to patch (already patched?)") };

    const auto verified = analyze(data);
    if (!verified.ok || verified.state != QLatin1String("patched") || verified.targets.size() != t.targets.size())
        return { false, QStringLiteral("The FPS patch failed verification; no DLL bytes were changed.") };
    QString error;
    if (!PatchFile::writeVerified(dllPath, original, data, &error))
        return { false, error };
    return { true, QStringLiteral("Patched %1: high-FPS maximum %2; normal mode stays at 60. Restart BlueStacks.")
                       .arg(QFileInfo(dllPath).fileName()).arg(hi) };
}

Result restore(const QString &dllPath)
{
    if (!isSupportedPath(dllPath))
        return { false, QStringLiteral("Select Qt5QuickTemplates2.dll or Qt6QuickTemplates2.dll.") };
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before restoring FPS.") };
    const QString bak = dllPath + QStringLiteral(".bak");
    if (!QFileInfo::exists(bak))
        return { false, QStringLiteral("No backup found.") };
    QFile backup(bak), current(dllPath);
    if (!backup.open(QIODevice::ReadOnly) || !current.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read the DLL or backup.") };
    const QByteArray original = current.readAll();
    const QByteArray restored = backup.readAll();
    current.close();
    backup.close();
    QString error;
    if (!PatchFile::writeVerified(dllPath, original, restored, &error))
        return { false, error };
    return { true, QStringLiteral("ORIGINAL RESTORED.") };
}

} // namespace FpsPatcher
