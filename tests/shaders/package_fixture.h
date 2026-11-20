#pragma once
#include "ByteUtils.h"
#include "ShaderPackage.h"
#include <QList>
#include <QMap>
#include <QStringList>

// Synthetic, never executable fixtures. RVAs == file offsets by construction.
namespace ShaderPackageFixture {
using namespace ByteUtils;
struct Import { QByteArray module; QList<QByteArray> symbols; bool delay = false; };
inline QByteArray image(bool dll, const QList<Import> &imports = {},
                        const QMap<QByteArray, QByteArray> &exports = {}, const QString &version = {})
{
    QByteArray b(0xa000, '\0');
    b.replace(0, 2, "MZ"); putLe32(b, 0x3c, 0x80);
    b.replace(0x80, 4, QByteArray("PE\0\0", 4));
    putLe16(b, 0x84, 0x8664); putLe16(b, 0x86, 3); putLe16(b, 0x94, 240);
    putLe16(b, 0x96, dll ? 0x2022 : 0x22); putLe16(b, 0x98, 0x20b);
    putLe32(b, 0x98 + 16, 0x1000); putLe32(b, 0x98 + 24, 0x400000);
    putLe32(b, 0x98 + 32, 0x1000); putLe32(b, 0x98 + 36, 0x200);
    putLe32(b, 0x98 + 56, 0xa000); putLe32(b, 0x98 + 60, 0x1000); putLe32(b, 0x98 + 108, 16);
    auto section = [&](int i, const char *name, quint32 va, quint32 size, quint32 flags) {
        const int p = 0x188 + i * 40;
        b.replace(p, int(qstrlen(name)), name); putLe32(b, p + 8, size); putLe32(b, p + 12, va);
        putLe32(b, p + 16, size); putLe32(b, p + 20, va); putLe32(b, p + 36, flags);
    };
    section(0, ".text", 0x1000, 0x1000, 0x60000020);
    section(1, ".rdata", 0x2000, 0x6000, 0x40000040);
    section(2, ".rsrc", 0x8000, 0x2000, 0x40000040);
    b[0x1000] = char(0xc3);
    quint32 pool = 0x3000;
    auto append = [&](QByteArray value) {
        const quint32 address = pool;
        value += '\0'; b.replace(pool, value.size(), value); pool += quint32(value.size());
        return address;
    };
    for (bool delay : {false, true}) {
        QList<Import> selected;
        for (const auto &i : imports) if (i.delay == delay) selected << i;
        if (selected.isEmpty()) continue;
        const int width = delay ? 32 : 20, base = delay ? 0x2200 : 0x2000, index = delay ? 13 : 1;
        putLe32(b, 0x98 + 112 + index * 8, base);
        putLe32(b, 0x98 + 116 + index * 8, (selected.size() + 1) * width);
        for (int i = 0; i < selected.size(); ++i) {
            const auto &m = selected.at(i); const int p = base + i * width;
            const quint32 name = append(m.module);
            pool = (pool + 7) & ~quint32(7);
            const quint32 table = pool, iat = pool + (m.symbols.size() + 1) * 8;
            pool = iat + (m.symbols.size() + 1) * 8;
            for (int j = 0; j < m.symbols.size(); ++j) {
                quint64 entry = 0;
                if (m.symbols.at(j).startsWith('#')) entry = (1ULL << 63) | m.symbols.at(j).mid(1).toUInt();
                else { pool += 2; entry = append(m.symbols.at(j)) - 2; }
                qToLittleEndian<quint64>(entry, b.data() + table + j * 8);
                qToLittleEndian<quint64>(entry, b.data() + iat + j * 8);
            }
            if (delay) { putLe32(b, p, 1); putLe32(b, p + 4, name); putLe32(b, p + 12, iat); putLe32(b, p + 16, table); }
            else { putLe32(b, p, table); putLe32(b, p + 12, name); putLe32(b, p + 16, iat); }
        }
    }
    if (!exports.isEmpty()) {
        putLe32(b, 0x98 + 112, 0x5000); putLe32(b, 0x98 + 116, 0x1800);
        putLe32(b, 0x5010, 1); putLe32(b, 0x5014, exports.size()); putLe32(b, 0x5018, exports.size());
        putLe32(b, 0x501c, 0x5100); putLe32(b, 0x5020, 0x5500); putLe32(b, 0x5024, 0x5900);
        pool = 0x5b00;
        int i = 0;
        for (auto e = exports.cbegin(); e != exports.cend(); ++e, ++i) {
            putLe32(b, 0x5500 + i * 4, append(e.key())); putLe16(b, 0x5900 + i * 2, i);
            quint32 address = 0x1000;
            if (e.value().startsWith("data:")) {
                address = 0x7000; const auto content = e.value().mid(5) + '\0'; b.replace(address, content.size(), content);
            } else if (e.value().startsWith("forward:")) address = append(e.value().mid(8));
            putLe32(b, 0x5100 + i * 4, address);
        }
    }
    if (!version.isEmpty()) {
        putLe32(b, 0x98 + 128, 0x8000); putLe32(b, 0x98 + 132, 0x2000);
        putLe16(b, 0x800e, 1); putLe32(b, 0x8010, 16); putLe32(b, 0x8014, 0x80000020);
        putLe16(b, 0x802e, 1); putLe32(b, 0x8030, 1); putLe32(b, 0x8034, 0x80000040);
        putLe16(b, 0x804e, 1); putLe32(b, 0x8050, 1033); putLe32(b, 0x8054, 0x60);
        putLe32(b, 0x8060, 0x8100); putLe32(b, 0x8064, 92);
        putLe16(b, 0x8100, 92); putLe16(b, 0x8102, 52);
        const auto key = QByteArray::fromHex("560053005f00560045005200530049004f004e005f0049004e0046004f000000");
        b.replace(0x8106, key.size(), key); putLe32(b, 0x8128, 0xfeef04bd); putLe32(b, 0x812c, 0x10000);
        const auto numbers = version.split(QLatin1Char('.'));
        const quint32 ms = (numbers.at(0).toUInt() << 16) | numbers.at(1).toUInt(), ls = numbers.at(2).toUInt() << 16;
        putLe32(b, 0x8130, ms); putLe32(b, 0x8134, ls); putLe32(b, 0x8138, ms); putLe32(b, 0x813c, ls);
        putLe32(b, 0x8140, 0x3f); putLe32(b, 0x8148, 0x40004); putLe32(b, 0x814c, 2);
    }
    return b;
}
inline QList<Import> qtImports(int major)
{
    QList<Import> out;
    for (const auto &name : {"Core", "Gui", "Qml"})
        out << Import{QByteArray("Qt") + QByteArray::number(major) + name + ".dll", {QByteArray("?bsi_fixture@@YAXXZ")}, false};
    return out;
}
inline ShaderPackage::Payload payload(const QString &version = QStringLiteral("6.8.3"), const QList<Import> &extra = {},
                                      bool publicQtApi = false)
{
    const int major = version.left(1).toInt();
    const auto descriptor = QByteArray(publicQtApi ? "data:BSI_SHADERS_V3;QT=" : "data:BSI_SHADERS_V2;QT=") + version.toLatin1()
        + ";ARCH=x64;ABI=msvc;BUILD=release;NS=default;" + (publicQtApi ? "API=public-v1;" : "") + "END;";
    const QMap<QByteArray, QByteArray> exports{{QByteArray("BSIShadersDescriptor"), descriptor},
        {QByteArray("qt_plugin_instance"), {}},
        {major == 6 && version.section(QLatin1Char('.'), 1, 1).toInt() >= 3
            ? QByteArray("qt_plugin_query_metadata_v2") : QByteArray("qt_plugin_query_metadata"), {}}};
    return {image(true, qtImports(major) + extra, exports), ShaderPackage::moduleDefinition()};
}
inline QMap<QString, QByteArray> runtime(const QString &version = QStringLiteral("6.8.3"))
{
    QMap<QString, QByteArray> out;
    for (const auto &i : qtImports(version.left(1).toInt()))
        out.insert(QString::fromLatin1(i.module).toLower(), image(true, {}, {{QByteArray("?bsi_fixture@@YAXXZ"), {}}}, version));
    return out;
}
} // namespace ShaderPackageFixture
