#pragma once
#include "ByteUtils.h"
#include "settings_fixture.h"
#include <QMap>
#include <algorithm>

namespace ShaderSettingsImageFixture {
using namespace ByteUtils;
inline quint32 hash(const QString &name)
{
    quint32 value = 0;
    for (auto c : name) { value = (value << 4) + c.unicode(); value ^= (value & 0xf0000000) >> 23; value &= 0x0fffffff; }
    return value;
}
inline QStringList nativeControlNames()
{
    return ShaderSettingsFixture::nativeControls;
}
inline QByteArray fixture(bool dense = false, bool withNativeControls = true, const QString &omittedNativeControl = {})
{
    QByteArray image(0x20000, '\0');
    image.replace(0, 2, "MZ"); putLe32(image, 0x3c, 0x80);
    image.replace(0x80, 4, QByteArray("PE\0\0", 4));
    putLe16(image, 0x84, 0x8664); putLe16(image, 0x86, 2);
    putLe16(image, 0x94, 0xf0); putLe16(image, 0x98, 0x20b);
    putLe32(image, 0x98 + 32, 0x1000); putLe32(image, 0x98 + 36, 0x200);
    putLe32(image, 0x98 + 56, 0x20000); putLe32(image, 0x98 + 60, 0x1000); putLe32(image, 0x98 + 108, 16);
    image.replace(0x188, 8, QByteArray(".rdata\0\0", 8));
    putLe32(image, 0x190, 0x17000); putLe32(image, 0x194, 0x1000);
    putLe32(image, 0x198, 0x17000); putLe32(image, 0x19c, 0x1000); putLe32(image, 0x1ac, 0x40000040);
    image.replace(0x1b0, 8, QByteArray(".text\0\0\0", 8));
    putLe32(image, 0x1b8, 0x8000); putLe32(image, 0x1bc, 0x18000);
    putLe32(image, 0x1c0, 0x8000); putLe32(image, 0x1c4, 0x18000); putLe32(image, 0x1d4, 0x60000020);
    if (dense) image.replace(0x1000, 0x17000, QByteArray(0x17000, char(0xa5)));
    image.replace(0x18000, 0x8000, QByteArray(0x8000, char(0x90)));
    // A real qRegisterResourceData import and x64 call site, never executed.
    image.replace(0x1000, 0x300, QByteArray(0x300, '\0'));
    putLe32(image, 0x98 + 120, 0x1000); putLe32(image, 0x98 + 124, 40);
    putLe32(image, 0x1000, 0x1100); putLe32(image, 0x100c, 0x1200); putLe32(image, 0x1010, 0x1120);
    putLe32(image, 0x1100, 0x1220); putLe32(image, 0x1120, 0x1220);
    const auto dll = QByteArray("Qt6Core.dll\0", 12);
    const auto symbol = QByteArray("?qRegisterResourceData@@YA_NHPEBE00@Z\0", 36);
    image.replace(0x1200, dll.size(), dll); image.replace(0x1222, symbol.size(), symbol);
    QByteArray code;
    for (const auto &arg : {qMakePair(QByteArray::fromHex("4c8d0d"), 0x2000), qMakePair(QByteArray::fromHex("4c8d05"), 0x6000), qMakePair(QByteArray::fromHex("488d15"), 0x7000)}) {
        const int begin = code.size(); code += arg.first + QByteArray(4, '\0');
        putLe32(code, begin + 3, quint32(arg.second - (0x18000 + code.size())));
    }
    code += QByteArray::fromHex("b902000000ff1500000000c3");
    putLe32(code, 28, quint32(0x1120 - (0x18000 + 32)));
    image.replace(0x18000, code.size(), code);
    QMap<QString, QByteArray> sources;
    sources.insert(QStringLiteral("plr.qml"), ShaderSettingsFixture::player().toUtf8());
    auto window = ShaderSettingsFixture::window();
    window.replace(QStringLiteral("//BSI v17"), QStringLiteral("//BSI v21"));
    sources.insert(QStringLiteral("UiSettingsWindow.qml"), window.toUtf8());
    // Resource-namespace doubles only. Real component loading is separately
    // tested against read-only resources from the four pinned player samples.
    if (withNativeControls)
        for (const auto &name : nativeControlNames())
            if (name != omittedNativeControl) sources.insert(name, QByteArray("import QtQuick 2.15\nItem{}\n"));
    for (const auto &page : ShaderSettingsFixture::pages) sources.insert(page, QByteArray("import QtQuick 2.15\nItem{}\n"));
    sources.insert(QStringLiteral("Unrelated.qml"), QByteArray("import QtQuick 2.15\nItem{property string untouched:'user edit'}\n"));
    QStringList ordered = sources.keys();
    std::sort(ordered.begin(), ordered.end(), [](const QString &a, const QString &b) { return hash(a) < hash(b); });
    image.replace(0x7000, (ordered.size() + 1) * 22, QByteArray((ordered.size() + 1) * 22, '\0'));
    putBe16(image, 0x7004, 2); putBe32(image, 0x7006, ordered.size()); putBe32(image, 0x700a, 1);
    qint64 data = 0x2000, name = 0x6000;
    for (int i = 0; i < ordered.size(); ++i) {
        const auto &leaf = ordered.at(i);
        const auto payload = qCompress(sources.value(leaf), 9);
        QByteArray record(4, '\0'); putBe32(record, 0, payload.size()); record += payload;
        image.replace(data, record.size(), record);
        const qint64 node = 0x7000 + (i + 1) * 22;
        putBe32(image, node, name - 0x6000); putBe16(image, node + 4, 1);
        putBe16(image, node + 8, 1); putBe32(image, node + 10, data - 0x2000); putBe64(image, node + 14, 1600000000000ULL);
        putBe16(image, name, leaf.size()); putBe32(image, name + 2, hash(leaf));
        for (int c = 0; c < leaf.size(); ++c) putBe16(image, name + 6 + c * 2, leaf.at(c).unicode());
        name += 6 + leaf.size() * 2; data += record.size();
    }
    return image;
}
} // namespace ShaderSettingsImageFixture
