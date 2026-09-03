#include "FpsPatcher.h"

#include "ByteUtils.h"
#include "PeUtils.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QMap>

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
            w[QStringLiteral("_ZN%1%25setToEd").arg(cls.size()).arg(cls)] = cls;
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

constexpr int kSlotLen = 80;
constexpr int kCaveMin = 2 * kSlotLen + 16;

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

QByteArray buildSlot(quint32 funcRva, quint32 caveRva, double hi, double lo)
{
    QByteArray s;
    s += QByteArray::fromHex("660F2F0D") + le32s(48 - 8);   // comisd xmm1,[rip->240.0]
    s += QByteArray::fromHex("75") + char(20 - 10);          // jne -> min check
    s += QByteArray::fromHex("F20F100D") + le32s(64 - 18);  // movsd xmm1,[rip->hi]
    s += QByteArray::fromHex("EB") + char(38 - 20);          // jmp -> prologue
    s += QByteArray::fromHex("660F2F0D") + le32s(56 - 28);  // comisd xmm1,[rip->60.0]
    s += QByteArray::fromHex("75") + char(38 - 30);          // jne -> prologue
    s += QByteArray::fromHex("F20F100D") + le32s(72 - 38);  // movsd xmm1,[rip->lo]
    s += QByteArray::fromHex("48895C2408");                  // mov [rsp+8], rbx
    s += QByteArray::fromHex("E9") + le32s(qint32(funcRva + 5) - qint32(caveRva + 48));
    s += d64(240.0) + d64(60.0) + d64(hi) + d64(lo);
    Q_ASSERT(s.size() == kSlotLen);
    return s;
}

QByteArray buildJmp(quint32 fromRva, quint32 toRva)
{
    return QByteArray::fromHex("E9") + le32s(qint32(toRva) - qint32(fromRva + 5));
}

QString readAsciiz(const QByteArray &b, qint64 off)
{
    qint64 e = off;
    while (e < b.size() && b[int(e)] != '\0')
        ++e;
    return QString::fromLatin1(b.mid(int(off), int(e - off)));
}

} // namespace

Analysis analyze(const QByteArray &b)
{
    Analysis out;
    if (!PeUtils::isPe32Plus(b)) {
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

    for (quint32 i = 0; i < nNames; ++i) {
        if (!inRange(b, namesOff + i * 4, 4))
            break;
        const qint64 nameOff = PeUtils::rvaToRaw(secs, le32(b, namesOff + i * 4));
        if (nameOff < 0)
            continue;
        const QString name = readAsciiz(b, nameOff);
        if (!wanted().contains(name))
            continue;
        const quint16 ord = quint16(le32(b, ordsOff + i * 2) & 0xFFFF);
        if (!inRange(b, funcsOff + ord * 4, 4))
            continue;
        const quint32 frva = le32(b, funcsOff + ord * 4);
        const qint64 off = PeUtils::rvaToRaw(secs, frva);
        if (off < 0 || !inRange(b, off, 5))
            continue;

        Target t;
        t.name = name;
        t.cls = wanted().value(name);
        t.rva = frva;
        t.off = off;
        if (b.mid(int(off), 5) == prologue())
            t.state = QStringLiteral("clean");
        else if (char(b[int(off)]) == char(0xE9))
            t.state = QStringLiteral("patched");
        else
            t.state = QStringLiteral("unknown");
        out.targets << t;
    }

    if (out.targets.isEmpty()) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("slider functions (setTo) not found in exports");
        return out;
    }

    bool anyClean = false, anyPatched = false;
    for (const Target &t : out.targets) {
        anyClean |= (t.state == QLatin1String("clean"));
        anyPatched |= (t.state == QLatin1String("patched"));
    }
    if (anyPatched && !anyClean) {
        out.state = QStringLiteral("patched");
        out.msg = QStringLiteral("%1 setTo function(s), already patched").arg(out.targets.size());
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
        if (s.name == QLatin1String(".text")) {
            text = &s;
            break;
        }
    }
    if (!text) {
        out.state = QStringLiteral("unsupported");
        out.msg = QStringLiteral("no .text section");
        return out;
    }

    qint64 best = -1, bestLen = 0, run = 0;
    const qint64 i0 = text->rawPtr;
    const qint64 i1 = qMin<qint64>(b.size(), text->rawPtr + text->rawSize);
    for (qint64 i = i0; i < i1; ++i) {
        if (b[int(i)] == '\0') {
            ++run;
        } else {
            if (run > bestLen) {
                bestLen = run;
                best = i - run;
            }
            run = 0;
        }
    }
    if (run > bestLen) {
        bestLen = run;
        best = i1 - run;
    }
    if (bestLen < kCaveMin) {
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
    const double lo = 60.0;
    const QString bak = dllPath + QStringLiteral(".bak");

    QString src = dllPath;
    if (QFileInfo::exists(bak)) {
        src = bak; // always patch from the pristine backup
    } else if (!QFile::copy(dllPath, bak)) {
        return { false, QStringLiteral("Could not create backup.") };
    }

    QFile f(src);
    if (!f.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read %1").arg(src) };
    QByteArray data = f.readAll();
    f.close();

    const Analysis t = analyze(data);
    if (!t.ok)
        return { false, t.msg };

    // Cave must be empty for two slots.
    for (int i = 0; i < 2 * kSlotLen; ++i) {
        if (char(data[int(t.caveOff) + i]) != '\0')
            return { false, QStringLiteral("cave area not empty") };
    }

    int slot = 0;
    for (const Target &tg : t.targets) {
        if (tg.state != QLatin1String("clean"))
            continue;
        const quint32 cRva = t.caveRva + quint32(slot * kSlotLen);
        const QByteArray s = buildSlot(tg.rva, cRva, hi, lo);
        const QByteArray jmp = buildJmp(tg.rva, cRva);
        data.replace(int(t.caveOff) + slot * kSlotLen, kSlotLen, s);
        data.replace(int(tg.off), 5, jmp);
        ++slot;
    }
    if (slot == 0)
        return { false, QStringLiteral("nothing to patch (already patched?)") };

    QFile out(dllPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return { false, QStringLiteral("Write error (run as administrator?).") };
    out.write(data);
    out.close();
    return { true, QStringLiteral("PATCHED! high-FPS slider max: %1").arg(hi) };
}

Result restore(const QString &dllPath)
{
    const QString bak = dllPath + QStringLiteral(".bak");
    if (!QFileInfo::exists(bak))
        return { false, QStringLiteral("No backup found.") };
    QFile::remove(dllPath);
    if (!QFile::copy(bak, dllPath))
        return { false, QStringLiteral("Restore failed.") };
    return { true, QStringLiteral("ORIGINAL RESTORED.") };
}

} // namespace FpsPatcher
