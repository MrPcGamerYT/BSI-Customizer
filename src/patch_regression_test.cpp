#include "core/Backend.h"
#include "core/BootVideoPatcher.h"
#include "core/IntegrityPatcher.h"
#include "core/ByteUtils.h"
#include "core/FpsPatcher.h"
#include "core/PatchFile.h"
#include "core/PeUtils.h"
#include "core/SettingsManager.h"
#include "core/ThemeEngine.h"
#include "core/ZlibUtils.h"
#include "core/X64Prologue.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>

#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

using namespace ByteUtils;

namespace {
int failures = 0;
void check(bool ok, const char *message)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok)
        ++failures;
}
QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray peFile()
{
    QByteArray data(0x20000, '\0');
    data.replace(0, 2, "MZ");
    putLe32(data, 0x3c, 0x80);
    data.replace(0x80, 4, QByteArray("PE\0\0", 4));
    putLe16(data, 0x84, 0x8664);
    putLe16(data, 0x86, 1);
    putLe16(data, 0x94, 0xf0);
    putLe16(data, 0x98, 0x20b);
    data.replace(0x188, 8, QByteArray(".rdata\0\0", 8));
    putLe32(data, 0x190, 0x1f000);
    putLe32(data, 0x194, 0x1000);
    putLe32(data, 0x198, 0x1f000);
    putLe32(data, 0x19c, 0x1000);
    putLe32(data, 0x1ac, 0x40000040);
    return data;
}
quint32 resourceHash(const QString &name)
{
    quint32 hash = 0;
    for (const auto c : name) {
        hash = (hash << 4) + c.unicode();
        hash ^= (hash & 0xf0000000) >> 23;
        hash &= 0x0fffffff;
    }
    return hash;
}
QString theme(const QString &color)
{
    return QStringLiteral("import QtQuick\nQtObject {\n property QtObject colors: QtObject {\n"
                          "  property color primary100: '%1'; property color accentNormal: '#1a90ff'\n"
                          "  property color primary90: '#232642'\n }\n}\n").arg(color);
}
QByteArray record(const QByteArray &source, bool compressed = true)
{
    QByteArray payload = compressed ? qCompress(source, 9) : source;
    QByteArray result(4, '\0');
    putBe32(result, 0, payload.size());
    return result + payload;
}

struct Fixture
{
    QByteArray bytes;
    int version = 2;
    qint64 tree = 0x7000;
    qint64 names = 0x6000;
    qint64 payloads = 0x2000;
    qint64 themeNode = -1;
    qint64 neighbor = -1;
};
Fixture fixture(int version, const QString &color, const QByteArray &compiled = {})
{
    Fixture f;
    f.version = version;
    f.bytes = peFile();
    const int stride = version == 1 ? 14 : 22;
    struct Entry { QString name; QByteArray data; quint16 flags; qint64 offset = 0; };
    QList<Entry> entries {
        { QStringLiteral("UiThemeObject.qml"), record(theme(color).toUtf8()), 1 },
        { QStringLiteral("Other.qml"), record("import QtQuick\nQtObject { property int count: 3 }\n"), 1 }
    };
    if (!compiled.isEmpty())
        entries << Entry { QStringLiteral("UiThemeObject.qmlc"), record(compiled, false), 0 };
    qint64 cursor = f.payloads;
    // Deliberately put the next length header DIRECTLY after the zlib stream.
    // Its leading zero bytes used to be counted as spare theme capacity.
    for (auto &entry : entries) {
        entry.offset = cursor;
        f.bytes.replace(cursor, entry.data.size(), entry.data);
        if (entry.name == QLatin1String("Other.qml"))
            f.neighbor = cursor;
        cursor += entry.data.size();
    }
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return resourceHash(a.name) < resourceHash(b.name);
    });
    putBe16(f.bytes, f.tree + 4, 2);
    putBe32(f.bytes, f.tree + 6, entries.size());
    putBe32(f.bytes, f.tree + 10, 1);
    qint64 nr = f.names;
    for (int i = 0; i < entries.size(); ++i) {
        const auto &entry = entries.at(i);
        putBe16(f.bytes, nr, entry.name.size());
        putBe32(f.bytes, nr + 2, resourceHash(entry.name));
        for (int c = 0; c < entry.name.size(); ++c)
            putBe16(f.bytes, nr + 6 + c * 2, entry.name.at(c).unicode());
        const qint64 node = f.tree + (i + 1) * stride;
        putBe32(f.bytes, node, nr - f.names);
        putBe16(f.bytes, node + 4, entry.flags);
        putBe16(f.bytes, node + 8, 1); // QLocale::C
        putBe32(f.bytes, node + 10, entry.offset - f.payloads);
        if (stride == 22)
            putBe64(f.bytes, node + 14, 1600000000000ULL);
        if (entry.name == QLatin1String("UiThemeObject.qml"))
            f.themeNode = node;
        nr += 6 + entry.name.size() * 2;
    }
    return f;
}
bool loadInQt(const QString &path, const Fixture &f, const QString &cache, const QString &expected)
{
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QML_DISABLE_DISK_CACHE"));
    env.insert(QStringLiteral("QML_DISK_CACHE_PATH"), cache);
    env.insert(QStringLiteral("QML_FORCE_DISK_CACHE"), QStringLiteral("1"));
    env.insert(QStringLiteral("QT_LOGGING_RULES"), QStringLiteral("qt.qml.diskcache.debug=true"));
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    process.setProcessEnvironment(env);
    const auto secs = PeUtils::sections(read(path));
    process.start(QCoreApplication::applicationFilePath(), {
        QStringLiteral("--theme-runtime-test"), path, QString::number(f.version),
        QString::number(PeUtils::rawToRva(secs, f.tree)),
        QString::number(PeUtils::rawToRva(secs, f.names)),
        QString::number(PeUtils::rawToRva(secs, f.payloads)), expected
    });
    if (!process.waitForStarted(10000) || !process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        std::printf("    child error: %s\n", qPrintable(process.errorString()));
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        std::printf("    %s\n    %s\n", process.readAllStandardOutput().constData(),
                    process.readAllStandardError().constData());
        return false;
    }
    return true;
}

struct ResourceFixture {
    Fixture f;
    QMap<QString, qint64> entries, nodes;
};
ResourceFixture resources(const QList<QPair<QString, QString>> &sources, bool dense = false)
{
    ResourceFixture r;
    r.f.bytes = peFile();
    if (dense) r.f.bytes.replace(0x1000, 0x1f000, QByteArray(0x1f000, char(0xa5)));
    putLe32(r.f.bytes, 0x98 + 32, 0x1000);
    putLe32(r.f.bytes, 0x98 + 36, 0x200);
    putLe32(r.f.bytes, 0x98 + 56, 0x20000);
    putLe32(r.f.bytes, 0x98 + 60, 0x1000);
    qint64 cursor = r.f.payloads;
    for (const auto &source : sources) {
        const auto bytes = record(source.second.toUtf8());
        r.entries[source.first] = cursor;
        r.f.bytes.replace(cursor, bytes.size(), bytes);
        cursor += bytes.size();
    }
    auto ordered = sources;
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return resourceHash(a.first) < resourceHash(b.first); });
    r.f.bytes.replace(r.f.tree, (ordered.size() + 1) * 22, QByteArray((ordered.size() + 1) * 22, '\0'));
    putBe16(r.f.bytes, r.f.tree + 4, 2); putBe32(r.f.bytes, r.f.tree + 6, ordered.size()); putBe32(r.f.bytes, r.f.tree + 10, 1);
    qint64 name = r.f.names;
    for (int i = 0; i < ordered.size(); ++i) {
        const auto key = ordered[i].first;
        putBe16(r.f.bytes, name, key.size()); putBe32(r.f.bytes, name + 2, resourceHash(key));
        for (int c = 0; c < key.size(); ++c) putBe16(r.f.bytes, name + 6 + 2*c, key[c].unicode());
        const qint64 node = r.f.tree + (i + 1) * 22;
        putBe32(r.f.bytes, node, name - r.f.names); putBe16(r.f.bytes, node + 4, 1);
        putBe16(r.f.bytes, node + 8, 1); putBe32(r.f.bytes, node + 10, r.entries[key] - r.f.payloads);
        putBe64(r.f.bytes, node + 14, 1600000000000ULL); r.nodes[key] = node;
        if (key == QLatin1String("UiThemeObject.qml")) r.f.themeNode = node;
        name += 6 + 2 * key.size();
    }
    return r;
}
void addRegistration(ResourceFixture &r, bool nonCanonical)
{
    auto &b = r.f.bytes;
    putLe16(b, 0x86, 2); putLe32(b, 0x190, 0x17000); putLe32(b, 0x198, 0x17000);
    b.replace(0x1b0, 40, QByteArray(40, '\0')); b.replace(0x1b0, 8, QByteArray(".text\0\0\0", 8));
    putLe32(b, 0x1b8, 0x8000); putLe32(b, 0x1bc, 0x18000); putLe32(b, 0x1c0, 0x8000);
    putLe32(b, 0x1c4, 0x18000); putLe32(b, 0x1d4, 0x60000020);
    b.replace(0x18000, 0x8000, QByteArray(0x8000, char(0x90)));
    b.replace(0x1000, 0x300, QByteArray(0x300, '\0'));
    putLe32(b, 0x98 + 120, 0x1000); putLe32(b, 0x98 + 124, 40);
    putLe32(b, 0x1000, 0x1100); putLe32(b, 0x100c, 0x1200); putLe32(b, 0x1010, 0x1120);
    putLe32(b, 0x1100, 0x1220); putLe32(b, 0x1120, 0x1220);
    const QByteArray dll = QByteArray("Qt6Core.dll") + '\0';
    const QByteArray symbol = QByteArray("?qRegisterResourceData@@YA_NHPEBE00@Z") + '\0';
    b.replace(0x1200, dll.size(), dll); b.replace(0x1222, symbol.size(), symbol);
    QByteArray code;
    for (const auto &pair : { qMakePair(QByteArray::fromHex("4c8d0d"), r.f.payloads),
                              qMakePair(QByteArray::fromHex("4c8d05"), r.f.names),
                              qMakePair(QByteArray::fromHex("488d15"), r.f.tree) }) {
        const int begin = code.size(); code += pair.first + QByteArray(4, '\0');
        putLe32(code, begin + 3, quint32(pair.second - (0x18000 + code.size())));
    }
    code += QByteArray::fromHex("b902000000ff1500000000c3");
    putLe32(code, 28, quint32(0x1120 - (0x18000 + 32)));
    b.replace(0x18000, code.size(), code);
    if (nonCanonical) {
        const int count = r.nodes.size();
        const auto children = b.mid(r.f.tree + 22, count * 22);
        b.replace(r.f.tree + 44, children.size(), children);
        b.replace(r.f.tree + 22, 22, QByteArray(22, char(0xcc))); // unreachable, not a child
        putBe32(b, r.f.tree, 0x12345678); putBe32(b, r.f.tree + 10, 2);
        for (auto it = r.nodes.begin(); it != r.nodes.end(); ++it) it.value() += 22;
        r.f.themeNode += 22;
    }
}
QString resourceText(const QByteArray &bytes, const ResourceFixture &r, const QString &name)
{
    const auto node = r.nodes.value(name, -1);
    if (node < 0) return {};
    const qint64 rva = r.f.payloads + be32(bytes, node + 10);
    const qint64 raw = PeUtils::rvaToRaw(PeUtils::sections(bytes), quint32(rva));
    if (!inRange(bytes, raw, 4) || !inRange(bytes, raw + 4, be32(bytes, raw))) return {};
    return QString::fromUtf8(qUncompress(bytes.mid(raw + 4, be32(bytes, raw))));
}

QByteArray sliderDll(bool mingw)
{
    auto data = peFile();
    data.replace(0x188, 8, QByteArray(".text\0\0\0", 8));
    putLe32(data, 0x1ac, 0x60000020);
    putLe32(data, 0x98 + 112, 0x1000);
    putLe32(data, 0x1000 + 20, 2);
    putLe32(data, 0x1000 + 24, 2);
    putLe32(data, 0x1000 + 28, 0x1200);
    putLe32(data, 0x1000 + 32, 0x1220);
    putLe32(data, 0x1000 + 36, 0x1240);
    const QList<QByteArray> names = mingw
        ? QList<QByteArray> { "_ZN12QQuickSlider5setToEd", "_ZN17QQuickRangeSlider5setToEd" }
        : QList<QByteArray> { "?setTo@QQuickSlider@@QEAAXN@Z", "?setTo@QQuickRangeSlider@@QEAAXN@Z" };
    for (int i = 0; i < 2; ++i) {
        data.replace(0x1100 + i * 0x80, names[i].size() + 1, names[i] + '\0');
        putLe32(data, 0x1200 + i * 4, 0x1300 + i * 0x100);
        putLe32(data, 0x1220 + i * 4, 0x1100 + i * 0x80);
        putLe16(data, 0x1240 + i * 2, i);
        // Controlled Windows x64 setter: original prologue, store XMM1 through
        // RCX, then return. This lets us execute the detour, not just inspect it.
        data.replace(0x1300 + i * 0x100, 10, QByteArray::fromHex("48895c2408f20f1109c3"));
    }
    return data;
}

QByteArray differentPrologueDll(bool mingw, int variant)
{
    auto data = sliderDll(mingw);
    const QList<QByteArray> prefixes {
        QByteArray::fromHex("4889742410"),             // MSVC: save RSI instead of RBX
        QByteArray::fromHex("48899c2408000000"),       // 8-byte stack store: cannot cut at five
        QByteArray::fromHex("40534883ec20"),           // REX push + stack allocation (6 bytes)
        QByteArray::fromHex("554889e54883ec20"),       // MinGW-style frame pointer (8 bytes)
        QByteArray::fromHex("5356574154"),             // multiple pushes, including R12
        QByteArray::fromHex("f20f100500000000"),       // RIP-relative scalar load (8 bytes)
        QByteArray::fromHex("488d0500000000"),         // RIP-relative LEA (7 bytes)
        QByteArray::fromHex("f30f1efa4889742410"),      // CET landing instruction must stay at entry
        QByteArray::fromHex("488bc448895808")          // MSVC: mov rax,rsp; mov [rax+8],rbx
    };
    const QList<QByteArray> epilogues {
        QByteArray(), QByteArray(), QByteArray::fromHex("4883c4205b"),
        QByteArray::fromHex("4883c4205d"), QByteArray::fromHex("415c5f5e5b"),
        QByteArray(), QByteArray(), QByteArray(), QByteArray()
    };
    for (int i = 0; i < 2; ++i) {
        const quint32 rva = 0x1300 + i * 0x100;
        auto prefix = prefixes[variant];
        QByteArray body;
        if (variant == 5 || variant == 6) {
            putLe32(prefix, variant == 5 ? 4 : 3, 0x1600 - (rva + prefix.size()));
            if (variant == 6)
                body += QByteArray::fromHex("f20f1000"); // movsd xmm0,[rax]
            // Multiplication by 1 preserves the argument, including signed zero.
            // Reading the wrong RIP-relative address makes the native test fail.
            body += QByteArray::fromHex("f20f59c8");
        }
        body += QByteArray::fromHex("f20f1109") + epilogues[variant] + char(0xc3);
        data.replace(rva, prefix.size() + body.size(), prefix + body);
    }
    data.replace(0x1600, 8, QByteArray::fromHex("000000000000f03f")); // 1.0
    return data;
}

QByteArray thunkDll(bool shortJump)
{
    auto data = sliderDll(false);
    const quint32 body = shortJump ? 0x1340 : 0x1500;
    data.replace(body, 10, data.mid(0x1300, 10));
    data.replace(0x1300, 10, QByteArray(10, char(0x90)));
    if (shortJump) {
        data[0x1300] = char(0xeb);
        data[0x1301] = char(body - 0x1302);
    } else {
        data[0x1300] = char(0xe9);
        putLe32(data, 0x1301, body - 0x1305);
    }
    // A separate, non-executable .pdata section with an actual RUNTIME_FUNCTION
    // entry distinguishes compiler thunks from arbitrary existing patch jumps.
    putLe16(data, 0x86, 2);
    putLe32(data, 0x190, 0x5000);
    putLe32(data, 0x198, 0x5000);
    data.replace(0x1b0, 8, QByteArray(".pdata\0\0", 8));
    putLe32(data, 0x1b8, 0x1000);
    putLe32(data, 0x1bc, 0x7000);
    putLe32(data, 0x1c0, 0x1000);
    putLe32(data, 0x1c4, 0x7000);
    putLe32(data, 0x1d4, 0x40000040);
    putLe32(data, 0x98 + 136, 0x7100);
    putLe32(data, 0x98 + 140, 12);
    putLe32(data, 0x7100, body);
    putLe32(data, 0x7104, body + 10);
    putLe32(data, 0x7108, 0x7200);
    data.replace(0x7200, 4, QByteArray::fromHex("01050000"));
    return data;
}

QByteArray makePartialFpsPatch(QByteArray patched)
{
    const auto analysis = FpsPatcher::analyze(patched);
    if (!analysis.ok || analysis.targets.size() != 2)
        return {};
    const auto &second = analysis.targets[1];
    patched.replace(second.off, second.originalBytes.size(), second.originalBytes);
    patched.replace(second.slotOff, second.slotSize, QByteArray(second.slotSize, '\0'));
    return patched;
}

// Independent fixture for the old BAT/PowerShell/reference detour. This is
// deliberately not generated by FpsPatcher, so its compatibility is tested.
QByteArray legacyFpsDll(const QByteArray &clean, double hi, double normal, int slotSize = 80)
{
    auto data = clean;
    const auto analysis = FpsPatcher::analyze(clean);
    if (!analysis.ok || analysis.caveOff < 0 || analysis.targets.size() != 2)
        return {};
    for (int i = 0; i < analysis.targets.size(); ++i) {
        const auto &target = analysis.targets[i];
        const quint32 rva = analysis.caveRva + i * slotSize;
        QByteArray slot = slotSize == 48
            ? QByteArray::fromHex("660f2f0d180000007a0a7508f20f100d1400000048895c2408e9000000009090")
            : QByteArray::fromHex("660f2f0d28000000750af20f100d2e000000eb12"
                                  "660f2f0d1c0000007508f20f100d2200000048895c2408e900000000");
        putLe32(slot, slotSize == 48 ? 26 : 44, target.rva + 5 - (rva + (slotSize == 48 ? 30 : 48)));
        QByteArray constants;
        QDataStream ds(&constants, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds.setFloatingPointPrecision(QDataStream::DoublePrecision);
        if (slotSize == 48)
            ds << 240.0 << hi;
        else
            ds << 240.0 << 60.0 << hi << normal;
        slot += constants;
        data.replace(analysis.caveOff + i * slotSize, slotSize, slot);
        data[int(target.off)] = char(0xe9);
        putLe32(data, target.off + 1, rva - (target.rva + 5));
    }
    return data;
}

bool hasOnlyMaxSlots(const QByteArray &data)
{
    const auto analysis = FpsPatcher::analyze(data);
    return analysis.ok && analysis.targets.size() == 2
           && std::all_of(analysis.targets.cbegin(), analysis.targets.cend(), [](const auto &target) {
                  return target.state == QLatin1String("patched") && target.slotOff >= 0 && target.slotSize == 64;
              });
}

void checkNativeMaxOnly(const QByteArray &data, double hi)
{
#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    const auto analysis = FpsPatcher::analyze(data);
    check(analysis.ok && analysis.targets.size() == 2, "two verified setter entry points for native FPS test");
    if (!analysis.ok || analysis.targets.size() != 2)
        return;
    // sliderDll() has identity RVA/file-offset mapping. Never execute arbitrary
    // input DLLs here: only the controlled store-and-return fixture above.
    auto *code = static_cast<unsigned char *>(
        VirtualAlloc(nullptr, SIZE_T(data.size()), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(code != nullptr, "allocate controlled FPS fixture for native execution");
    if (!code)
        return;
    std::memcpy(code, data.constData(), size_t(data.size()));
    DWORD oldProtection = 0;
    const bool executable = VirtualProtect(code, SIZE_T(data.size()), PAGE_EXECUTE_READ, &oldProtection)
                            && FlushInstructionCache(GetCurrentProcess(), code, SIZE_T(data.size()));
    check(executable, "make controlled FPS fixture executable");
    if (executable) {
        // The Windows x64 ABI uses RCX for the object and XMM1 for its double.
        using Setter = void (*)(double *, double);
        const double inputs[] = { 240.0, 60.0, 30.0, 120.0, 360.0, 480.0, 1440.0,
                                  0.0, -0.0, -60.0, 239.5, 240.5,
                                  std::nextafter(240.0, 0.0), std::nextafter(240.0, 300.0),
                                  std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity(),
                                  std::numeric_limits<double>::quiet_NaN() };
        for (const auto &target : analysis.targets) {
            const auto setter = reinterpret_cast<Setter>(code + target.entryRva);
            bool matches = true;
            for (const double input : inputs) {
                double actual = -12345.0;
                setter(&actual, input);
                const double expected = input == 240.0 ? hi : input;
                matches &= std::isnan(expected) ? std::isnan(actual)
                                               : std::memcmp(&actual, &expected, sizeof(double)) == 0;
            }
            check(matches, "native setter changes only 240; 60, other values, signed zero, infinity and NaN pass unchanged");
        }
    }
    VirtualFree(code, 0, MEM_RELEASE);
#else
    Q_UNUSED(data);
    Q_UNUSED(hi);
    std::printf("  SKIP  native FPS detour execution requires Windows x64\n");
#endif
}

} // namespace

int runPatchRegressionTests()
{
    failures = 0;
    QTemporaryDir temp;
    check(temp.isValid(), "regression temp directory available");
    if (!temp.isValid())
        return failures;
    std::printf("[theme-runtime] real Qt resources, disk caches and restarts\n");
    QByteArray diskUnit;
    for (const int version : { 1, 2, 3 }) {
        const auto f = fixture(version, QStringLiteral("#112233"));
        const QString path = temp.filePath(QStringLiteral("resource%1.exe").arg(version));
        const QString cache = temp.filePath(QStringLiteral("qmlcache%1").arg(version));
        check(write(path, f.bytes), "write untouched resource fixture");
        const auto initial = ThemeEngine::detectTheme(f.bytes, QStringLiteral("qml"));
        check(initial.hasQrcNode && !initial.ambiguous && initial.qrcNodeOff == f.themeNode
                  && initial.resource.nodeSize == (version == 1 ? 14 : 22),
              "detect real rcc v1/v2/v3 node, not an isolated flag pattern");
        check(loadInQt(path, f, cache, QStringLiteral("#112233")), "Qt loads original colors before patch");
        if (version == 2) {
            const auto files = QDir(cache).entryList({ QStringLiteral("*.qmlc") }, QDir::Files);
            check(!files.isEmpty(), "a first HD-Player-like launch really creates a disk cache");
            for (const auto &file : files) {
                const auto unit = read(QDir(cache).filePath(file));
                auto probe = unit;
                if (QtResources::invalidateThemeUnits(probe) == 1) {
                    diskUnit = unit;
                    break;
                }
            }
            check(!diskUnit.isEmpty(), "cache unit sourceFileIndex resolves to the theme, not an imported component");
            // Reproduce the bug independently: editing the source but keeping
            // rcc's old timestamp must still show the cached palette.
            const auto stale = fixture(version, QStringLiteral("#aa0033"));
            write(path, stale.bytes);
            check(loadInQt(path, stale, cache, QStringLiteral("#112233")),
                  "reproduce stale colors after a source-only patch following a launch");
            write(path, f.bytes);
        }
        for (const auto &color : { QStringLiteral("#fa3401"), QStringLiteral("#4cbbed") }) {
            const auto before = read(path);
            const auto info = ThemeEngine::detectTheme(before, QStringLiteral("qml"));
            const auto patched = ThemeEngine::patchTheme(path, info, ThemeEngine::parseQmlEntries(info.qml),
                                                        { { QStringLiteral("primary100"), color } });
            if (!patched.ok)
                std::printf("    patch error: %s\n", qPrintable(patched.msg));
            check(patched.ok, "patch works without running another theme changer first");
            const auto after = read(path);
            check(after.mid(f.neighbor, 4 + be32(f.bytes, f.neighbor))
                      == f.bytes.mid(f.neighbor, 4 + be32(f.bytes, f.neighbor)),
                  "next resource's zero-prefixed length header is preserved");
            if (version > 1)
                check(be64(after, f.themeNode + 14) > be64(before, f.themeNode + 14),
                      "each repatch advances the resource cache timestamp");
            check(loadInQt(path, f, cache, color), "Qt loads selected colors after patch, launch, repatch, relaunch");
        }
        check(read(path + QStringLiteral(".bak")) == f.bytes, "first backup is preserved across theme repatches");
        const auto current = ThemeEngine::detectTheme(read(path), QStringLiteral("qml"));
        const auto repair = ThemeEngine::patchTheme(path, current, ThemeEngine::parseQmlEntries(current.qml), {});
        check(repair.ok && repair.msg.contains(QLatin1String("already")),
              "reapplying existing colors refreshes stale caches instead of claiming nothing to do");
    }

    std::printf("[theme-runtime] embedded .qmlc takes priority over source until invalidated\n");
    if (!diskUnit.isEmpty()) {
        // Precompiled resource units normally have a zero source timestamp,
        // which means source mtime checks alone cannot invalidate them.
        std::fill(diskUnit.begin() + 16, diskUnit.begin() + 24, '\0');
        const auto f = fixture(2, QStringLiteral("#556677"), diskUnit);
        const QString path = temp.filePath(QStringLiteral("compiled.exe"));
        const QString cache = temp.filePath(QStringLiteral("embedded-cache"));
        write(path, f.bytes);
        check(loadInQt(path, f, cache, QStringLiteral("#112233")),
              "reproduce unchanged colors: embedded cached QML overrides different source colors");
        const auto info = ThemeEngine::detectTheme(f.bytes, QStringLiteral("qml"));
        const auto r = ThemeEngine::patchTheme(path, info, ThemeEngine::parseQmlEntries(info.qml),
                                              { { QStringLiteral("primary100"), QStringLiteral("#cf50ab") } });
        check(r.ok, "patch source and invalidate only its compiled theme unit");
        const QByteArray after = read(path);
        const auto unitPos = after.indexOf(QByteArray("qv4cdata\0", 8));
        check(unitPos >= 0 && le32(after, unitPos + 8) == 0,
              "embedded theme unit has an invalid format version, not merely changed source text");
        check(loadInQt(path, f, cache, QStringLiteral("#cf50ab")),
              "real Qt rejects the old embedded unit and renders the selected source color");
    }

    std::printf("[theme] repatch a padded resource left by another changer\n");
    {
        auto f = fixture(2, QStringLiteral("#112233"));
        const int padding = 256;
        const auto neighbor = f.bytes.mid(f.neighbor, 4 + be32(f.bytes, f.neighbor));
        f.bytes.replace(f.neighbor + padding, neighbor.size(), neighbor);
        f.bytes.replace(f.neighbor, padding, QByteArray(padding, '\0'));
        putBe32(f.bytes, f.payloads, be32(f.bytes, f.payloads) + padding);
        for (int i = 1; i <= 2; ++i) {
            const qint64 node = f.tree + i * 22;
            if (node != f.themeNode)
                putBe32(f.bytes, node + 10, be32(f.bytes, node + 10) + padding);
        }
        f.neighbor += padding;
        const QString path = temp.filePath(QStringLiteral("padded.exe"));
        const QString cache = temp.filePath(QStringLiteral("padded-cache"));
        write(path, f.bytes);
        const auto info = ThemeEngine::detectTheme(f.bytes, QStringLiteral("qml"));
        check(info.hasQrcNode && info.headerBytes == 8 && info.storedSize > info.compSize + 4,
              "a padded Qt resource remains linked, not mistaken for a bare qCompress stream");
        check(loadInQt(path, f, cache, QStringLiteral("#112233")), "Qt loads and caches the other changer's padded theme");
        const auto r = ThemeEngine::patchTheme(path, info, ThemeEngine::parseQmlEntries(info.qml),
                                              { { QStringLiteral("primary100"), QStringLiteral("#ab43de") } });
        check(r.ok && loadInQt(path, f, cache, QStringLiteral("#ab43de")),
              "repatching after that launch invalidates its cache and changes the rendered color");
        const auto after = read(path);
        check(after.mid(f.neighbor, neighbor.size()) == neighbor
                  && be64(after, f.themeNode + 14) > be64(f.bytes, f.themeNode + 14),
              "padded repatch preserves neighbors and advances the real resource timestamp");
    }

    std::printf("[theme] mapped relocation across PE sections and orphan selection\n");
    for (const int version : { 1, 2, 3 }) {
        auto f = fixture(version, QStringLiteral("#112233"));
        const auto payload = f.bytes.mid(f.payloads, f.neighbor + 4 + be32(f.bytes, f.neighbor) - f.payloads);
        f.bytes.replace(0x1000, 0x4000, QByteArray(0x4000, 'X'));
        f.bytes.replace(f.payloads, payload.size(), payload);
        putLe16(f.bytes, 0x86, 2);
        putLe32(f.bytes, 0x190, 0x4000);
        putLe32(f.bytes, 0x198, 0x4000);
        f.bytes.replace(0x1b0, 8, QByteArray(".data\0\0\0", 8));
        putLe32(f.bytes, 0x1b8, 0x1b000);
        putLe32(f.bytes, 0x1bc, 0x15000); // RVA differs from raw offset by 64 KiB
        putLe32(f.bytes, 0x1c0, 0x1b000);
        putLe32(f.bytes, 0x1c4, 0x5000);
        putLe32(f.bytes, 0x1d4, 0x40000040);
        const auto orphan = record(theme(QStringLiteral("#aabbcc")).toUtf8());
        f.bytes.replace(0x1100, orphan.size(), orphan);
        const QString path = temp.filePath(QStringLiteral("mapped%1.exe").arg(version));
        write(path, f.bytes);
        const auto info = ThemeEngine::detectTheme(f.bytes, QStringLiteral("qml"));
        check(info.entryOff == f.payloads && info.qrcNodeOff == f.themeNode && !info.ambiguous,
              "14/22-byte tree selects live colors over an earlier orphan");
        const auto r = ThemeEngine::patchTheme(path, info, ThemeEngine::parseQmlEntries(info.qml), {
            { QStringLiteral("primary100"), QStringLiteral("#ff12ab34") },
            { QStringLiteral("primary90"), QStringLiteral("#803cb789") },
            { QStringLiteral("accentNormal"), QStringLiteral("#90124ef0") }
        });
        if (!r.ok)
            std::printf("    mapped error: %s\n", qPrintable(r.msg));
        const auto after = ThemeEngine::detectTheme(read(path), QStringLiteral("qml"));
        check(r.ok && after.entryOff >= 0x5000 && after.resource.dataBaseRva == 0x2000,
              "relocation preserves the original memory data base across non-identity PE sections");
        check(loadInQt(path, f, temp.filePath(QStringLiteral("mapped-cache%1").arg(version)),
                       QStringLiteral("#ff12ab34")),
              "Qt's resource loader follows the relocated RVA, not a raw file offset");
    }

    std::printf("[theme] invalid metadata/ambiguous copies and failed writes\n");
    {
        auto f = fixture(2, QStringLiteral("#112233"));
        // A plain zero buffer is not another resource tree.
        putBe32(f.bytes, f.tree + 10, 0xfffffff0);
        check(!ThemeEngine::detectTheme(f.bytes, QStringLiteral("qml")).hasQrcNode,
              "out-of-range child indices cannot produce a trusted qrc link");
        f = fixture(2, QStringLiteral("#112233"));
        const qint64 payloadEnd = f.neighbor + 4 + be32(f.bytes, f.neighbor);
        f.bytes.replace(0x9000, payloadEnd - f.payloads, f.bytes.mid(f.payloads, payloadEnd - f.payloads));
        const QString ambiguousPath = temp.filePath(QStringLiteral("ambiguous.exe"));
        write(ambiguousPath, f.bytes);
        const auto uncertain = ThemeEngine::detectTheme(f.bytes, QStringLiteral("qml"));
        check(uncertain.ambiguous
                  && !ThemeEngine::patchTheme(ambiguousPath, uncertain, ThemeEngine::parseQmlEntries(uncertain.qml),
                      { { QStringLiteral("primary100"), QStringLiteral("#abcdef") } }).ok
                  && read(ambiguousPath) == f.bytes,
              "equally plausible live resource copies are refused without changing the file");
        const QString path = temp.filePath(QStringLiteral("atomic.exe"));
        write(path, "original");
        QString error;
        check(!PatchFile::writeVerified(path, "stale", "replacement", &error)
                  && read(path) == "original", "stale write cannot overwrite the current file");
    }

    std::printf("[qml] single quotes, inline properties, comments and full expressions\n");
    {
        const QString qml = theme(QStringLiteral("#112233"))
                            + QStringLiteral("// property color fake: '#000000'\n");
        const auto entries = ThemeEngine::parseQmlEntries(qml);
        check(entries.size() == 3 && entries.first().value == QLatin1String("#112233"),
              "single-quoted and same-line properties are parsed without commented-out colors");
        QStringList skipped;
        const QString updated = ThemeEngine::buildQml(qml, entries,
            { { QStringLiteral("primary100"), QStringLiteral("#abcdef") },
              { QStringLiteral("accentNormal"), QStringLiteral("#123456") } }, true, &skipped);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(updated.toUtf8(), QUrl());
        std::unique_ptr<QObject> object(component.create());
        check(object != nullptr && skipped.isEmpty(), "edited QML compiles in Qt, not just a byte-pattern test");
        const QString expression = QStringLiteral(
            "import QtQuick\nQtObject { property color notice: true ? // choose foreground\n"
            " '#abcdef'\n :\n '#123456'; property color other: '#987654' }\n");
        const auto exprEntries = ThemeEngine::parseQmlEntries(expression);
        const QString exprOut = ThemeEngine::buildQml(expression, exprEntries,
            { { QStringLiteral("notice"), QStringLiteral("#a1b2c3") } }, true);
        QQmlComponent exprComponent(&engine);
        exprComponent.setData(exprOut.toUtf8(), QUrl());
        std::unique_ptr<QObject> exprObject(exprComponent.create());
        check(exprEntries.size() == 2 && exprObject
                  && exprObject->property("notice").value<QColor>() == QColor(QStringLiteral("#a1b2c3")),
              "multi-line ternary and its internal comment are replaced as one valid expression");
        ThemeEngine::buildQml(qml, entries, { { QStringLiteral("missingColor"), QStringLiteral("#abcdef") } },
                              true, &skipped);
        check(!skipped.isEmpty(), "missing staged properties are reported, not silently discarded");
    }

#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
    {
#ifdef BSI_QT5_REFERENCE_DLL
        const QString overrideQt5 = qEnvironmentVariable("BSI_FPS_QT5_DLL");
        const QString qt5Dll = overrideQt5.isEmpty() ? QString::fromUtf8(BSI_QT5_REFERENCE_DLL) : overrideQt5;
        // CI cannot skip this check. A distributed executable need not retain
        // its build machine's fixture path; an explicit local override is required.
        const bool requireQt5 = qEnvironmentVariable("GITHUB_ACTIONS") == QLatin1String("true")
                                || !overrideQt5.isEmpty() || QFileInfo::exists(qt5Dll);
#else
        const QString qt5Dll = qEnvironmentVariable("BSI_FPS_QT5_DLL");
        const bool requireQt5 = !qt5Dll.isEmpty(); // explicitly supplied local reference
#endif
        struct Reference { QString name, path; QByteArray core; bool required; };
        const QList<Reference> references {
            { QStringLiteral("Qt5QuickTemplates2.dll"), qt5Dll, "qt5core.dll", requireQt5 },
            { QStringLiteral("Qt6QuickTemplates2.dll"),
              QDir(QLibraryInfo::path(QLibraryInfo::BinariesPath)).filePath(QStringLiteral("Qt6QuickTemplates2.dll")),
              "qt6core.dll", qEnvironmentVariable("GITHUB_ACTIONS") == QLatin1String("true") }
        };
        for (const auto &reference : references) {
            std::printf("[fps-sdk] real %s (read-only source, temporary patch copy)\n", qPrintable(reference.name));
            const bool exists = !reference.path.isEmpty() && QFileInfo::exists(reference.path);
            if (reference.required)
                check(exists, "required Qt reference DLL is present (not a skipped test)");
            if (!exists) {
                std::printf("  SKIP  optional %s reference is not present\n", qPrintable(reference.name));
                continue;
            }
            const auto original = read(reference.path);
            check(QFileInfo(reference.path).fileName().compare(reference.name, Qt::CaseInsensitive) == 0
                      && original.toLower().contains(reference.core),
                  "reference is from the expected Qt major, not merely a renamed DLL");
            const auto a = FpsPatcher::analyze(original);
            const bool cleanSetters = a.ok && a.state == QLatin1String("clean") && a.targets.size() == 2
                && std::all_of(a.targets.cbegin(), a.targets.cend(), [](const auto &target) {
                       return target.state == QLatin1String("clean");
                   });
            if (!cleanSetters)
                std::printf("    %s\n", qPrintable(a.msg));
            check(cleanSetters, "both real Qt setter prologues and executable padding are supported");
            if (!cleanSetters)
                continue;
            const QString directory = temp.filePath(QStringLiteral("sdk-") + reference.name.left(3).toLower());
            check(QDir().mkpath(directory), "create isolated Qt DLL copy directory");
            const QString path = QDir(directory).filePath(reference.name);
            check(write(path, original), "copy Qt DLL without touching the installed or loaded library");
            const auto patched = FpsPatcher::patch(path, 720);
            if (!patched.ok)
                std::printf("    %s\n", qPrintable(patched.msg));
            check(patched.ok && hasOnlyMaxSlots(read(path)), "patch real Qt DLL copy and verify both high-FPS-only detours");
            check(FpsPatcher::patch(path, 480).ok, "repatch real Qt DLL copy from its matching backup");
            check(FpsPatcher::restore(path).ok && read(path) == original, "restore real Qt DLL copy byte-for-byte");
            check(read(reference.path) == original, "original Qt reference library remains untouched");
        }
    }
#endif

    std::printf("[boot-msi] actual component name and multimedia import from a sibling resource\n");
    {
        const QString boot = QStringLiteral("import QtQuick\nItem{width:320;height:200;property bool bootBackgroundImage:true;"
            "property bool pBootImageButtonVisible:true;property bool pBootGridVisible:true;"
            "Image{id:iBootImage;objectName:'bootImage';visible:true}"
            "Text{id:iBootLoadingText;objectName:'loadingText';visible:false}"
            "function fSetBootPromotion(){iBootImage.visible=true;pBootGridVisible=true;iBootLoadingText.visible=false}}\n");
        const auto r = resources({ { QStringLiteral("PlrBootProgressBar.qml"), boot },
                                   { QStringLiteral("Notification.qml"), QStringLiteral("import QtQuick\nimport QtMultimedia 5.15\nItem{}\n") },
                                   { QStringLiteral("Other.qml"), QStringLiteral("import QtQuick; QtObject {}") } }, true);
        const auto folder = temp.filePath(QStringLiteral("msi boot with spaces")); QDir().mkpath(folder);
        const auto path = QDir(folder).filePath(QStringLiteral("HD-Player.exe")); write(path, r.f.bytes);
        const auto outcome = BootVideoPatcher::patch(path);
        if (!outcome.ok) std::printf("    %s\n", qPrintable(outcome.msg));
        check(outcome.ok, "PlrBootProgressBar works without a stock BootVideo.qml or an import in the boot file");
        const auto modified = resourceText(read(path), r, QStringLiteral("PlrBootProgressBar.qml"));
        check(modified.contains(QLatin1String("import QtMultimedia 5.15 as BsiMedia"))
                  && modified.contains(QLatin1String("sourceComponent:Component{BsiMedia.Video")),
              "media support is obtained from another validated file in the same resource pool");
        // A tiny, explicitly synthetic QtMultimedia API validates the generated
        // QML/argument flow without playing media or running extracted user QML.
        const auto imports = QDir(folder).filePath(QStringLiteral("imports/QtMultimedia")); QDir().mkpath(imports);
        write(QDir(imports).filePath(QStringLiteral("qmldir")), "module QtMultimedia\nVideo 5.15 Video.qml\nMediaPlayer 5.15 MediaPlayer.qml\nVideoOutput 5.15 VideoOutput.qml\n");
        write(QDir(imports).filePath(QStringLiteral("Video.qml")), "import QtQuick\nItem{property url source;property int loops;property int fillMode;property bool started:false;function play(){started=true}}\n");
        write(QDir(imports).filePath(QStringLiteral("MediaPlayer.qml")), "import QtQuick\nQtObject{enum Loops{Infinite=1}}\n");
        write(QDir(imports).filePath(QStringLiteral("VideoOutput.qml")), "import QtQuick\nQtObject{enum FillMode{PreserveAspectCrop=1}}\n");
        QQmlEngine engine; engine.addImportPath(QDir(folder).filePath(QStringLiteral("imports")));
        QQmlComponent component(&engine); component.setData(modified.toUtf8(), QUrl::fromLocalFile(QDir(folder).filePath(QStringLiteral("PlrBootProgressBar.qml"))));
        std::unique_ptr<QObject> object(component.create());
        if (!object) std::printf("    %s\n", qPrintable(component.errorString()));
        check(bool(object), "injected boot Video QML compiles against the expected multimedia API");
        if (object) {
            const auto video = object->findChild<QObject *>(QStringLiteral("bsiBootVideo"));
            check(video && video->property("started").toBool()
                      && video->property("source").toUrl() == QUrl::fromLocalFile(QDir(folder).filePath(QStringLiteral("Assets/Promo.mp4"))),
                  "the inline video starts and receives the installation-relative media URL");
        }
        check(BootVideoPatcher::patch(path).ok && read(path + QStringLiteral(".bak")) == r.f.bytes,
              "real-name boot repatching keeps one loader and preserves the original backup");
        const auto twice = resourceText(read(path), r, QStringLiteral("PlrBootProgressBar.qml"));
        check(twice.count(QLatin1String("objectName:\"bsiBootVideo\"")) == 1,
              "repatching does not duplicate the custom video component");
    }
#ifdef BSI_EXE_TEST_FILE
    {
        std::printf("[hdplayer-reference] file parsing and disposable copies only; no uploaded code is executed\n");
        const auto input = QString::fromUtf8(BSI_EXE_TEST_FILE);
        const auto original = read(input);
        std::printf("  Reference bytes: %lld; SHA256: %s\n", static_cast<long long>(original.size()),
                    QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex().constData());
        const auto first = ThemeEngine::findThemeZlib(original);
        check(first.found && first.hasQrcNode && !first.ambiguous, "the supplied EXE has a unique mapped theme resource");
        if (!first.hasQrcNode) std::printf("    %s\n", qPrintable(first.resource.detail));
        const auto folder = temp.filePath(QStringLiteral("real-image-copy")); QDir().mkpath(folder);
        const auto path = QDir(folder).filePath(QStringLiteral("HD-Player.exe")); write(path, original);
        if (first.found && first.hasQrcNode && !first.ambiguous) {
            const auto originalEntries = ThemeEngine::parseQmlEntries(first.qml);
            for (int round = 0; round < 3; ++round) {
                QMap<QString, QString> edits;
                for (int i = 0; i < originalEntries.size(); ++i)
                    edits[originalEntries[i].name] = QStringLiteral("#%1").arg((i * 7919 + 234567 + round * 12345) & 0xffffff, 6, 16, QLatin1Char('0'));
                // Deliberately keep the initial UI snapshot to exercise reload-
                // after-patch handling across growth, boot patching and repatches.
                const auto result = ThemeEngine::patchTheme(path, first, originalEntries, edits);
                if (!result.ok) std::printf("    Theme round %d: %s\n", round, qPrintable(result.msg));
                check(result.ok, "theme patch/repatch succeeds on a copy of the supplied EXE");
                const auto after = ThemeEngine::findThemeZlib(read(path));
                check(after.hasQrcNode && !after.ambiguous, "the actual EXE theme remains linked after patching");
                const auto changedEntries = ThemeEngine::parseQmlEntries(after.qml);
                bool sameColors = after.hasQrcNode && changedEntries.size() == originalEntries.size();
                for (int i = 0; i < changedEntries.size() && i < originalEntries.size(); ++i) {
                    const auto &entry = changedEntries[i];
                    QString expected = edits.value(entry.name);
                    // The public RGB editor deliberately preserves the original
                    // alpha of each ARGB slot (including duplicate property names).
                    const auto oldColor = originalEntries[i].original;
                    if (expected.size() == 7 && oldColor.size() == 9 && oldColor.startsWith(QLatin1Char('#')))
                        expected = QStringLiteral("#") + oldColor.mid(1, 2) + expected.mid(1);
                    if (entry.name != originalEntries[i].name || entry.value.compare(expected, Qt::CaseInsensitive)) {
                        std::printf("    Color mismatch: %s actual=%s expected=%s\n", qPrintable(entry.name), qPrintable(entry.value), qPrintable(expected));
                        sameColors = false;
                    }
                }
                check(sameColors, "mapped real-file colors match the requested RGB while preserving original alpha");
                const auto boot = BootVideoPatcher::patch(path);
                if (!boot.ok) std::printf("    Boot round %d: %s\n", round, qPrintable(boot.msg));
                check(boot.ok, "boot patch/repatch succeeds on the supplied EXE copy without breaking theme mapping");
            }
            check(read(path + QStringLiteral(".bak")) == original, "the real-file copy retains its first complete backup");
        }
        check(read(input) == original, "the supplied EXE remains byte-for-byte untouched");
    }
#endif

    std::printf("[exe-fixes] registered resource layouts, growing boot QML and integrity detection\n");
    check(ZlibUtils::findMagicOffsets(qCompress(theme(QStringLiteral("#abcdef")).toUtf8(), 3)).contains(4),
          "standard level-2-to-5 zlib streams (78 5E) are included in resource discovery");
    {
        auto r = resources({ { QStringLiteral("UiThemeObject.qml"), theme(QStringLiteral("#112233")) },
                             { QStringLiteral("Other.qml"), QStringLiteral("import QtQuick; QtObject {}") } });
        addRegistration(r, true);
        // An unrelated opaque compiled resource must not invalidate the theme's mapping.
        r.f.bytes[int(r.entries[QStringLiteral("Other.qml")]) + 8] = char(0x42);
        const auto path = temp.filePath(QStringLiteral("registered-theme.exe"));
        write(path, r.f.bytes);
        const auto a = ThemeEngine::findThemeZlib(r.f.bytes);
        check(a.hasQrcNode && a.resource.registered && a.resource.nodeSize == 22,
              "registration resolves a nonzero root name, unused node and opaque neighbor");
        const auto cache = temp.filePath(QStringLiteral("registered-cache"));
        check(loadInQt(path, r.f, cache, QStringLiteral("#112233")), "actual Qt reads the noncanonical registered theme tree");
        check(ThemeEngine::patchTheme(path, a, ThemeEngine::parseQmlEntries(a.qml),
              { { QStringLiteral("primary100"), QStringLiteral("#abcdef") } }).ok,
              "a validated alternate resource layout can be patched without removing the safety gate");
        check(loadInQt(path, r.f, cache, QStringLiteral("#abcdef")), "relaunch rejects stale cache after alternate-layout patch");
        auto broken = r.f.bytes; putLe32(broken, 0x18000 + 28, 0);
        check(!ThemeEngine::findThemeZlib(broken).hasQrcNode, "unverified registration bytes do not authorize arbitrary tree layouts");
    }
    {
        QString many = QStringLiteral("import QtQuick\nQtObject{property QtObject colors: QtObject{property color primary100: '#111111';\n");
        QMap<QString, QString> edits;
        edits[QStringLiteral("primary100")] = QStringLiteral("#abcdef");
        for (int i = 0; i < 208; ++i) {
            const auto name = QStringLiteral("tone%1").arg(i);
            many += QStringLiteral("property color %1: '#111111';\n").arg(name);
            edits[name] = QStringLiteral("#%1").arg((i * 7919 + 123456) & 0xffffff, 6, 16, QLatin1Char('0'));
        }
        many += QStringLiteral("}}\n");
        const auto r = resources({ { QStringLiteral("UiThemeObject.qml"), many },
                                  { QStringLiteral("Other.qml"), QStringLiteral("import QtQuick; QtObject {}") } }, true);
        const auto path = temp.filePath(QStringLiteral("growing-theme.exe")); write(path, r.f.bytes);
        const auto a = ThemeEngine::findThemeZlib(r.f.bytes);
        check(ThemeEngine::patchTheme(path, a, ThemeEngine::parseQmlEntries(a.qml), edits).ok,
              "large palette growth adds mapped read-only storage instead of an unmapped append");
        check(PeUtils::sections(read(path)).size() == 2, "resource growth adds a proper PE section");
        check(loadInQt(path, r.f, temp.filePath(QStringLiteral("growth-cache")), QStringLiteral("#abcdef")),
              "Qt actually loads colors relocated into the appended PE section");
    }
    {
        const QString boot = QStringLiteral("import QtQuick\nItem{width:320;height:200;property bool bootBackgroundImage:true;"
            "property bool pBootImageButtonVisible:true;property bool pBootGridVisible:true;"
            "Image{id:iBootImage;objectName:'bootImage';visible:true}"
            "Text{id:iBootLoadingText;objectName:'loadingText';visible:false}"
            "Loader{id:pv;source:'BootVideo.qml'}"
            "function fSetBootPromotion(){iBootImage.visible=true;pBootGridVisible=true;iBootLoadingText.visible=false}}\n");
        const QString video = QStringLiteral("import QtQuick\nMediaPlayer { source:'x';objectName:'mediaPlayer' }\n");
        const auto r = resources({ { QStringLiteral("Boot.qml"), boot }, { QStringLiteral("BootVideo.qml"), video },
                                  { QStringLiteral("Other.qml"), QStringLiteral("import QtQuick; QtObject {}") } }, true);
        const QString folder = temp.filePath(QStringLiteral("boot test with spaces")); QDir().mkpath(folder);
        const QString path = QDir(folder).filePath(QStringLiteral("HD-Player.exe")); write(path, r.f.bytes);
        const auto patch = BootVideoPatcher::patch(path);
        if (!patch.ok) std::printf("    %s\n", qPrintable(patch.msg));
        check(patch.ok && PeUtils::sections(read(path)).size() > 1, "boot media URL can grow beyond its original compressed slot");
        const auto qml = resourceText(read(path), r, QStringLiteral("Boot.qml"));
        const auto media = resourceText(read(path), r, QStringLiteral("BootVideo.qml"));
        const auto expected = QUrl::fromLocalFile(QDir(folder).filePath(QStringLiteral("Assets/Promo.mp4")));
        check(media.contains(expected.toString(QUrl::FullyEncoded)) && !media.contains(QLatin1String("file:///Promo.mp4")),
              "boot player uses the selected installation's Assets/Promo.mp4 URL");
        check(qml.contains(QLatin1String("source:'BootVideo.qml'")), "a QML Loader still loads QML, never the MP4 itself");
        write(QDir(folder).filePath(QStringLiteral("Boot.qml")), qml.toUtf8());
        write(QDir(folder).filePath(QStringLiteral("BootVideo.qml")), media.toUtf8());
        write(QDir(folder).filePath(QStringLiteral("MediaPlayer.qml")), "import QtQuick\nItem{property url source}\n");
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QDir(folder).filePath(QStringLiteral("Boot.qml"))));
        std::unique_ptr<QObject> object(component.create());
        if (!object) std::printf("    %s\n", qPrintable(component.errorString()));
        check(bool(object), "modified boot QML compiles with the stock-player API (no media codec is executed)");
        if (object) {
            const auto image = object->findChild<QObject *>(QStringLiteral("bootImage"));
            const auto player = object->findChild<QObject *>(QStringLiteral("mediaPlayer"));
            check(image && !image->property("visible").toBool() && player && player->property("source").toUrl() == expected,
                  "boot image is hidden and the existing player's source is the custom video");
        }
        check(BootVideoPatcher::patch(path).ok && read(path + QStringLiteral(".bak")) == r.f.bytes,
              "boot repatch is idempotent and preserves the first backup");
    }
    {
        auto bytes = peFile(); bytes.replace(0x188, 8, QByteArray(".text\0\0\0", 8)); putLe32(bytes, 0x1ac, 0x60000020);
        bytes.replace(0x1400, 9, QByteArray::fromHex("e80000000084c07405"));
        bytes.replace(0x1800, 9, QByteArray::fromHex("e80000000084c09090")); // unrelated natural NOPs
        const auto path = temp.filePath(QStringLiteral("integrity.exe")); write(path, bytes);
        const auto a = IntegrityPatcher::analyze(path);
        check(a.ok && a.state == QLatin1String("clean") && a.site == 0x1405 && a.candidates == 1,
              "natural NOP sequences do not turn a unique reference-pattern match into ambiguity");
        check(IntegrityPatcher::patch(path, true).ok && read(path) == bytes && !QFile::exists(path + QStringLiteral(".bak")),
              "integrity dry-run creates no backup and changes no bytes");
        check(IntegrityPatcher::patch(path).ok, "integrity patch follows the provided CALL/TEST/JZ pattern");
        auto expected = bytes; expected.replace(0x1407, 2, QByteArray::fromHex("9090"));
        check(read(path) == expected && IntegrityPatcher::patch(path).ok && read(path) == expected,
              "only the two JZ bytes change; applying integrity again succeeds without further edits");
        check(IntegrityPatcher::restore(path).ok && read(path) == bytes, "integrity restore is verified without deleting the EXE first");
        auto multiple = bytes; multiple.replace(0x1600, 9, QByteArray::fromHex("e80000000084c07405"));
        const QByteArray anchor = QByteArray("Verified the disk integrity!") + '\0'; multiple.replace(0x5000, anchor.size(), anchor);
        multiple.replace(0x1610, 7, QByteArray::fromHex("4c8d0500000000")); putLe32(multiple, 0x1613, 0x5000 - 0x1617);
        write(path, multiple);
        check(IntegrityPatcher::analyze(path).site == 0x1605, "RIP-relative verify/fail references disambiguate multiple clean matches");
        multiple.replace(0x1410, 7, QByteArray::fromHex("488d0d00000000")); putLe32(multiple, 0x1413, 0x5000 - 0x1417);
        write(path, multiple);
        check(!IntegrityPatcher::patch(path).ok && read(path) == multiple, "multiple validated sites are not resolved by blindly taking the first");
        auto shortFile = bytes.left(0x1809); putLe32(shortFile, 0x198, shortFile.size() - 0x1000);
        write(path, shortFile);
        check(IntegrityPatcher::analyze(path).ok, "anchor scans near the end of .text stay inside the file");
    }

    std::printf("[fps-prologues] compiler variants, RIP-relative instructions, thunks and partial recovery\n");
    for (const bool mingw : { false, true }) {
        for (const QString &name : { QStringLiteral("Qt5QuickTemplates2.dll"), QStringLiteral("Qt6QuickTemplates2.dll") }) {
            const QString path = temp.filePath(name);
            std::printf("[fps-matrix] %s / %s exports\n", qPrintable(name), mingw ? "MinGW" : "MSVC");
            for (int variant = 0; variant < 9; ++variant) {
                QFile::remove(path + QStringLiteral(".bak"));
                const auto clean = differentPrologueDll(mingw, variant);
                write(path, clean);
                const auto analysis = FpsPatcher::analyze(clean);
                check(analysis.ok && analysis.state == QLatin1String("clean") && analysis.targets.size() == 2,
                      "alternate MSVC/MinGW entry sequences are supported without a fixed five-byte signature");
                const auto result = FpsPatcher::patch(path, 720);
                if (!result.ok)
                    std::printf("    %s\n", qPrintable(result.msg));
                check(result.ok && hasOnlyMaxSlots(read(path)), "complete instructions are replayed by a verified max-only detour");
                checkNativeMaxOnly(read(path), 720);
                if (variant == 7)
                    check(read(path).mid(0x1300, 4) == clean.mid(0x1300, 4), "CET landing instruction remains at the exported entry");
                check(FpsPatcher::patch(path, 480).ok, "variable-length/RIP-relative prologues can be repatched");
                checkNativeMaxOnly(read(path), 480);
                const auto partial = makePartialFpsPatch(read(path));
                const auto partialAnalysis = FpsPatcher::analyze(partial);
                check(partialAnalysis.ok && partialAnalysis.state == QLatin1String("partial"),
                      "mixed clean and known-patched setters are recoverable, not unsupported");
                write(path, partial);
                check(FpsPatcher::patch(path, 360).ok && hasOnlyMaxSlots(read(path)),
                      "a partial patch is safely rebuilt from the matching original backup");
                checkNativeMaxOnly(read(path), 360);
                check(FpsPatcher::restore(path).ok && read(path) == clean, "all variable-length bytes are restored exactly");

                auto stale = clean;
                stale[0x1800] = char(0x42);
                write(path, partial);
                write(path + QStringLiteral(".bak"), stale);
                check(!FpsPatcher::patch(path, 480).ok && read(path) == partial,
                      "partial recovery still refuses a stale backup without changing the DLL");
                QFile::remove(path + QStringLiteral(".bak"));
                check(!FpsPatcher::patch(path, 480).ok && read(path) == partial
                          && !QFile::exists(path + QStringLiteral(".bak")),
                      "partial recovery never creates a backup from an already-patched DLL");
            }
        }
    }
    for (const bool shortJump : { false, true }) {
        for (const QString &name : { QStringLiteral("Qt5QuickTemplates2.dll"), QStringLiteral("Qt6QuickTemplates2.dll") }) {
            const QString path = temp.filePath(name);
            QFile::remove(path + QStringLiteral(".bak"));
            const auto clean = thunkDll(shortJump);
            const auto analysis = FpsPatcher::analyze(clean);
            check(analysis.ok && analysis.targets.size() == 2 && analysis.targets[0].rva != analysis.targets[0].entryRva,
                  "validated E9/EB compiler entry thunks resolve to the real setter");
            write(path, clean);
            check(FpsPatcher::patch(path, 720).ok, "patch the real body, not the compiler's jump stub");
            check(read(path).mid(0x1300, 10) == clean.mid(0x1300, 10), "original exported thunk stays intact");
            checkNativeMaxOnly(read(path), 720);
            check(FpsPatcher::patch(path, 480).ok, "compiler-thunk patches can be recognized and repatched");
            checkNativeMaxOnly(read(path), 480);
            check(FpsPatcher::restore(path).ok && read(path) == clean, "compiler-thunk patches restore the whole DLL exactly");
            auto unverified = clean;
            putLe32(unverified, 0x98 + 140, 0);
            check(!FpsPatcher::analyze(unverified).ok, "an unverified entry jump is not treated as pristine compiler code");
        }
    }
    {
        const QString path = temp.filePath(QStringLiteral("Qt5QuickTemplates2.dll"));
        QFile::remove(path + QStringLiteral(".bak"));
        for (const auto &code : { QByteArray::fromHex("e900000000"), QByteArray::fromHex("ff2500000000"),
                                 QByteArray::fromHex("48b87856341200000000ffe0"), QByteArray::fromHex("c3"),
                                 QByteArray::fromHex("6748895c2408") }) {
            auto unsupported = sliderDll(false);
            unsupported.replace(0x1300, code.size(), code);
            write(path, unsupported);
            const auto a = FpsPatcher::analyze(unsupported);
            check(!a.ok && a.msg.contains(QLatin1String("QQuickSlider::setTo"))
                      && a.msg.contains(QLatin1String("1300")) && a.msg.contains(QString::fromLatin1(code.toHex(' '))),
                  "unsupported code reports the exact setter, RVA and bytes instead of a generic mixed-prologue error");
            check(!FpsPatcher::patch(path, 720).ok && read(path) == unsupported
                      && !QFile::exists(path + QStringLiteral(".bak")),
                  "unsafe/unrecognized detours are refused without changing the DLL or creating a backup");
        }
        check(!X64Prologue::decode(QByteArray::fromHex("48899c24")).ok(), "truncated instructions are never padded into a valid prologue");
        check(!X64Prologue::decode(QByteArray(19, char(0xf3))).ok(), "overlong prefix sequences are rejected safely");
        QByteArray relocated;
        check(!X64Prologue::relocate(QByteArray::fromHex("488b0510000000"), 0x1000, 0xf0000000u, &relocated),
              "out-of-range RIP-relative relocation is refused");
        check(!FpsPatcher::analyze(sliderDll(false).left(0x1303)).ok, "truncated setter bytes do not escape executable/file bounds");
        auto nonExecutable = sliderDll(false);
        putLe32(nonExecutable, 0x1ac, 0x40000040);
        check(!FpsPatcher::analyze(nonExecutable).ok, "export bytes in a non-executable section are not patched");
    }
    std::printf("[fps] both QuickTemplates2 DLLs, both slider setters, migration and notifications\n");
    for (const bool mingw : { false, true }) {
        for (const QString &name : { QStringLiteral("Qt5QuickTemplates2.dll"), QStringLiteral("Qt6QuickTemplates2.dll") }) {
            const QString path = temp.filePath(name);
            QFile::remove(path + QStringLiteral(".bak"));
            const auto bytes = sliderDll(mingw);
            write(path, bytes);
            check(FpsPatcher::analyze(bytes).targets.size() == 2, "both MSVC/MinGW slider exports are detected");
            check(FpsPatcher::patch(path, 360).ok && FpsPatcher::analyze(read(path)).state == QLatin1String("patched"),
                  "Qt5/Qt6 QuickTemplates2 patch is recognized and verified");
            check(hasOnlyMaxSlots(read(path)), "new patches emit 64-byte high-FPS-only slots with original-instruction metadata");
            checkNativeMaxOnly(read(path), 360);
            check(FpsPatcher::patch(path, 480).ok, "FPS can be changed again from a clean backup");
            checkNativeMaxOnly(read(path), 480);
            check(FpsPatcher::restore(path).ok && read(path) == bytes, "FPS restore returns exact original bytes");
            check(!FpsPatcher::patch(path, std::numeric_limits<double>::infinity()).ok,
                  "non-finite FPS is rejected");
            auto changedVersion = bytes;
            changedVersion[0x1800] = char(0x42);
            write(path, changedVersion);
            check(!FpsPatcher::patch(path, 360).ok && read(path) == changedVersion,
                  "an old backup cannot roll a newly updated Qt DLL back silently");
            write(path, bytes);
            auto unknown = bytes;
            unknown.replace(0x1400, 5, QByteArray::fromHex("e900000000"));
            check(!FpsPatcher::analyze(unknown).ok,
                  "unknown third-party detours cannot be mistaken for our FPS patch");

            const auto previousMax = legacyFpsDll(bytes, 720, 60, 48);
            write(path + QStringLiteral(".bak"), bytes);
            write(path, makePartialFpsPatch(previousMax));
            check(FpsPatcher::analyze(read(path)).state == QLatin1String("partial") && FpsPatcher::patch(path, 480).ok,
                  "a partial previous 48-byte max-only patch upgrades without discarding its backup");
            checkNativeMaxOnly(read(path), 480);
            check(FpsPatcher::restore(path).ok && read(path) == bytes, "previous 48-byte max-only patches restore exactly after upgrade");

            for (const double normal : { 60.0, 120.0 }) {
                const auto legacy = legacyFpsDll(bytes, 1000, normal);
                const auto oldAnalysis = FpsPatcher::analyze(legacy);
                check(oldAnalysis.ok && oldAnalysis.state == QLatin1String("patched")
                          && oldAnalysis.targets.size() == 2
                          && oldAnalysis.targets[0].slotSize == 80 && oldAnalysis.targets[1].slotSize == 80,
                      "reference high/normal detours are recognized, including a changed normal-mode cap");
                if (!oldAnalysis.ok || oldAnalysis.targets.size() != 2)
                    continue;
                write(path, legacy);
                write(path + QStringLiteral(".bak"), bytes);
                check(FpsPatcher::restore(path).ok && read(path) == bytes,
                      "legacy FPS patch remains exactly restorable");
                write(path, makePartialFpsPatch(legacy));
                check(FpsPatcher::analyze(read(path)).state == QLatin1String("partial"),
                      "partial 80-byte reference patches are recognized too");
                check(FpsPatcher::patch(path, 720).ok && hasOnlyMaxSlots(read(path)),
                      "legacy high/normal detours upgrade to high-FPS-only using the clean backup");
                checkNativeMaxOnly(read(path), 720);
                check(FpsPatcher::patch(path, 60).ok, "high-FPS-only patch can be repatched at the minimum allowed maximum");
                checkNativeMaxOnly(read(path), 60);
                check(FpsPatcher::restore(path).ok && read(path) == bytes,
                      "upgraded patch still restores exact original bytes");

                write(path, legacy);
                write(path + QStringLiteral(".bak"), changedVersion);
                check(!FpsPatcher::patch(path, 720).ok && read(path) == legacy,
                      "legacy upgrade rejects a mismatched backup without changing the DLL");
                QFile::remove(path + QStringLiteral(".bak"));
                check(!FpsPatcher::patch(path, 720).ok && read(path) == legacy
                          && !QFile::exists(path + QStringLiteral(".bak")),
                      "legacy upgrade refuses a missing clean backup, without manufacturing a patched backup");
                auto corruptLegacy = legacy;
                corruptLegacy[int(oldAnalysis.targets[0].slotOff) + 8] = char(0x74);
                check(!FpsPatcher::analyze(corruptLegacy).ok,
                      "legacy recognition requires the complete known detour, not just its constants");
            }
        }
    }
    {
        const QString renderer = temp.filePath(QStringLiteral("libOpenglRender.dll"));
        write(renderer, sliderDll(false));
        check(!FpsPatcher::patch(renderer, 360).ok && !QFile::exists(renderer + QStringLiteral(".bak")),
              "renderer DLL is refused even if it happens to contain matching exports");
        const QString exe = temp.filePath(QStringLiteral("HD-Player.exe"));
        write(exe, QByteArray("Qt5Core.dll\0", 12));
        check(FpsPatcher::findDll(temp.path()).endsWith(QLatin1String("Qt5QuickTemplates2.dll")),
              "when both DLLs exist, player Qt5 dependency selects Qt5");
        write(exe, QByteArray("Qt6Core.dll\0", 12));
        check(FpsPatcher::findDll(temp.path()).endsWith(QLatin1String("Qt6QuickTemplates2.dll")),
              "when both DLLs exist, player Qt6 dependency selects Qt6");
        QSettings().setValue(QStringLiteral("paths/exe"), exe);
        QSettings().setValue(QStringLiteral("paths/dll"), renderer);
        SettingsManager settings;
        Backend backend(&settings);
        check(settings.dllPath().endsWith(QLatin1String("Qt6QuickTemplates2.dll")),
              "old persisted OpenGL target migrates to the player's QuickTemplates2 DLL");
        int notifications = 0;
        QObject::connect(&backend, &Backend::dllPathChanged, [&notifications] { ++notifications; });
        settings.setDllPath(temp.filePath(QStringLiteral("Qt5QuickTemplates2.dll")));
        check(notifications == 1 && backend.dllPath() == settings.dllPath(),
              "settings changes notify the QML-facing path instead of leaving a stale display");
        settings.setDllPath(renderer);
        check(!backend.fpsAnalyze().value(QStringLiteral("ok")).toBool(), "FPS Analyze rejects the wrong DLL too");
    }
    return failures;
}
