#include "core/Backend.h"
#include "core/BootVideoPatcher.h"
#include "core/BsDetector.h"
#include "core/EmbeddedImages.h"
#include "core/EmbeddedResources.h"
#include "core/ExtrasPatcher.h"
#include "core/AboutPatcher.h"
#include "core/IntegrityPatcher.h"
#include "core/LayoutPatcher.h"
#include "core/QmlText.h"
#include "core/ByteUtils.h"
#include "core/FpsPatcher.h"
#include "core/GradientPatcher.h"
#include "core/PatchFile.h"
#include "core/PeUtils.h"
#include "core/QtResources.h"
#include "core/QtResourceRegistration.h"
#include "core/RpcIdPatcher.h"
#include "core/RpcIdScan.h"
#include "core/SettingsManager.h"
#include "core/VersionResource.h"
#include "core/ThemeEngine.h"
#include "core/UpdateChecker.h"
#include "core/ZlibUtils.h"
#include "core/X64Prologue.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QUrl>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <memory>

#if defined(Q_OS_WIN) && defined(Q_PROCESSOR_X86_64)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

using namespace ByteUtils;

// Implemented in cli.cpp: the scriptable Rich Presence/name command line.
int runRpcIdCli(const QStringList &arguments);

namespace {
int failures = 0;
void check(bool ok, const char *message)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) {
        ++failures;
        // Windows CI runs expose the job log only as annotations, so surface each
        // failed assertion as one instead of leaving a red step with no detail.
        if (qEnvironmentVariableIsSet("GITHUB_ACTIONS"))
            std::printf("::error file=src/patch_regression_test.cpp::self-test failure: %s\n", message);
    }
}
void check(bool ok, const QString &message)
{
    check(ok, qPrintable(message));
}
// The job log of a *passing* Windows run is not retrievable either, so the few
// numbers that prove what was really exercised are published as annotations.
void ciNotice(const QString &message)
{
    if (qEnvironmentVariableIsSet("GITHUB_ACTIONS"))
        std::printf("::notice file=src/patch_regression_test.cpp::%s\n", qPrintable(message));
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

// ---------------------------------------------------------------------------
// Windows version-resource fixture (VS_VERSION_INFO), used to exercise the
// native name editor that replaced the old reference script's "name" command.
// ---------------------------------------------------------------------------
QByteArray vrText(const QString &text)
{
    QByteArray out;
    for (const QChar c : text) {
        out += char(c.unicode() & 0xff);
        out += char((c.unicode() >> 8) & 0xff);
    }
    return out;
}
QByteArray vrNode(const QString &key, quint16 type, const QByteArray &value,
                  quint16 valueUnits, const QList<QByteArray> &children = {})
{
    QByteArray out(6, '\0');
    putLe16(out, 2, valueUnits);
    putLe16(out, 4, type);
    out += vrText(key) + QByteArray(2, '\0');
    while (out.size() % 4) out += '\0';
    out += value;
    for (const QByteArray &child : children) {
        while (out.size() % 4) out += '\0';
        out += child;
    }
    putLe16(out, 0, quint16(out.size()));
    return out;
}
// One RT_VERSION resource holding two translations of the same two strings.
QByteArray versionResourceBlob(const QString &description, int descriptionCap,
                               const QString &product)
{
    // The slot the compiler reserved, NUL-padded explicitly: QByteArray::resize()
    // leaves the new bytes uninitialized.
    QByteArray value = vrText(description);
    value += QByteArray(descriptionCap * 2 - value.size(), '\0');
    const QByteArray describe = vrNode(QStringLiteral("FileDescription"), 1, value,
                                       quint16(descriptionCap));
    const QByteArray name = vrNode(QStringLiteral("ProductName"), 1,
                                   vrText(product) + QByteArray(2, '\0'),
                                   quint16(product.size() + 1));
    const QByteArray english = vrNode(QStringLiteral("040904b0"), 1, {}, 0, { describe, name });
    const QByteArray german = vrNode(QStringLiteral("040704b0"), 1, {}, 0, { describe, name });
    const QByteArray strings = vrNode(QStringLiteral("StringFileInfo"), 1, {}, 0, { english, german });
    QByteArray translation(4, '\0');
    putLe16(translation, 0, 0x0409);
    putLe16(translation, 2, 0x04b0);
    const QByteArray var = vrNode(QStringLiteral("VarFileInfo"), 1, {}, 0,
                                  { vrNode(QStringLiteral("Translation"), 0, translation, 4) });
    QByteArray fixed(52, '\0');
    putLe32(fixed, 0, 0xfeef04bd);
    return vrNode(QStringLiteral("VS_VERSION_INFO"), 0, fixed, 52, { strings, var });
}
// A PE whose .rsrc section carries that resource, like a real HD-Player.exe.
QByteArray peWithVersionResource(const QByteArray &resource)
{
    const quint32 headers = 0x200;
    const quint32 rsrcVa = 0x1000;
    const quint32 blobAt = 0x60;    // after the three directory levels
    QByteArray data(headers, '\0');
    data.replace(0, 2, "MZ");
    putLe32(data, 0x3c, 0x40);
    data.replace(0x40, 4, QByteArray("PE\0\0", 4));
    putLe16(data, 0x44, 0x8664);
    putLe16(data, 0x46, 1);
    putLe16(data, 0x54, 0xf0);
    putLe16(data, 0x58, 0x20b);
    const qsizetype dd = 0x40 + 24 + 112;          // data directories (PE32+)
    putLe32(data, dd + 2 * 8, rsrcVa);             // [2] = resources
    putLe32(data, dd + 2 * 8 + 4, 0x1000);
    const qsizetype section = 0x40 + 24 + 0xf0;
    data.replace(section, 8, QByteArray(".rsrc\0\0\0", 8));
    putLe32(data, section + 8, 0x1000);
    putLe32(data, section + 12, rsrcVa);
    putLe32(data, section + 16, 0x1000);
    putLe32(data, section + 20, headers);
    putLe32(data, section + 36, 0x40000040);

    QByteArray body;
    const auto directory = [&body](quint32 id, quint32 child) {
        QByteArray node(24, '\0');
        putLe16(node, 12, 0);      // named entries
        putLe16(node, 14, 1);      // id entries
        putLe32(node, 16, id);
        putLe32(node, 20, child);
        body += node;
    };
    directory(16, 0x18 | 0x80000000u);     // RT_VERSION -> name level
    directory(1, 0x30 | 0x80000000u);      // name 1 -> language level
    directory(0x409, 0x48);                // language -> data entry
    QByteArray leaf(16, '\0');
    putLe32(leaf, 0, rsrcVa + blobAt);
    putLe32(leaf, 4, quint32(resource.size()));
    body += leaf;
    body += QByteArray(blobAt - body.size(), '\0');
    body += resource;
    body += QByteArray(0x1000 - body.size(), '\0');
    return data + body;
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
// Same fixture, but the payloads are bytes: pictures cannot survive a round
// trip through QString the way QML text can.
ResourceFixture resourcesBytes(const QList<QPair<QString, QByteArray>> &sources, bool dense = false,
                               const QSet<QString> &rawNames = {})
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
        const auto bytes = record(source.second, !rawNames.contains(source.first));
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
        putBe32(r.f.bytes, node, name - r.f.names); putBe16(r.f.bytes, node + 4, rawNames.contains(key) ? 0 : 1);
        putBe16(r.f.bytes, node + 8, 1); putBe32(r.f.bytes, node + 10, r.entries[key] - r.f.payloads);
        putBe64(r.f.bytes, node + 14, 1600000000000ULL); r.nodes[key] = node;
        if (key == QLatin1String("UiThemeObject.qml")) r.f.themeNode = node;
        name += 6 + 2 * key.size();
    }
    return r;
}

ResourceFixture resources(const QList<QPair<QString, QString>> &sources, bool dense = false,
                          const QSet<QString> &rawNames = {})
{
    QList<QPair<QString, QByteArray>> payloads;
    payloads.reserve(sources.size());
    for (const auto &source : sources)
        payloads << qMakePair(source.first, source.second.toUtf8());
    return resourcesBytes(payloads, dense, rawNames);
}

// rcc writes one node per path component: "images" is a directory node owning
// a contiguous block of children, and a file node stores only its leaf name.
// The flat builder above puts the whole path in a single node, which the reader
// rightly rejects (a resource name never contains a slash), so the picture tests
// build the shape a real player pool has.
ResourceFixture nestedResources(const QList<QPair<QString, QByteArray>> &sources)
{
    ResourceFixture r = resourcesBytes(sources);
    auto &b = r.f.bytes;
    const qint64 tree = r.f.tree;
    const qint64 stride = 22;

    QStringList dirs;
    QMap<QString, QStringList> dirFiles;
    QStringList rootFiles;
    for (const auto &source : sources) {
        const int slash = source.first.lastIndexOf(QLatin1Char('/'));
        const QString dir = slash > 0 ? source.first.left(slash) : QString();
        const QString leaf = slash >= 0 ? source.first.mid(slash + 1) : source.first;
        if (!dir.isEmpty()) {
            if (!dirs.contains(dir)) dirs << dir;
            dirFiles[dir] << leaf;
        } else {
            rootFiles << leaf;
        }
    }

    const int nodes = 1 + int(dirs.size()) + int(sources.size());
    b.replace(tree, qint64(nodes) * stride, QByteArray(nodes * int(stride), '\0'));

    qint64 nameCursor = r.f.names;
    const auto addName = [&](const QString &text) {
        const qint64 at = nameCursor;
        putBe16(b, at, quint16(text.size()));
        putBe32(b, at + 2, resourceHash(text));
        for (int i = 0; i < text.size(); ++i)
            putBe16(b, at + 6 + 2 * i, text[i].unicode());
        nameCursor += 6 + 2 * text.size();
        return int(at - r.f.names);
    };
    const auto fileNode = [&](int index, const QString &dir, const QString &leaf) {
        const qint64 node = tree + index * stride;
        const QString key = dir.isEmpty() ? leaf : dir + QLatin1Char('/') + leaf;
        putBe32(b, node, quint32(addName(leaf)));
        putBe16(b, node + 4, 1);
        putBe32(b, node + 10, quint32(r.entries.value(key) - r.f.payloads));
        putBe64(b, node + 14, 1600000000000ULL);
    };
    const auto dirNode = [&](int index, const QString &name, int first, int count) {
        const qint64 node = tree + index * stride;
        putBe32(b, node, quint32(addName(name)));
        putBe16(b, node + 4, 2);
        putBe32(b, node + 6, quint32(count));
        putBe32(b, node + 10, quint32(first));
        putBe64(b, node + 14, 1600000000000ULL);
    };

    int next = 1;
    QMap<QString, int> dirAt;
    for (const QString &dir : dirs)
        dirAt.insert(dir, next++);
    // Children of a directory must be contiguous, so the root's own files sit
    // right after the directory nodes and every directory block follows.
    for (const QString &leaf : rootFiles)
        fileNode(next++, QString(), leaf);
    for (const QString &dir : dirs) {
        const QStringList kids = dirFiles.value(dir);
        const int first = next;
        dirNode(dirAt.value(dir), dir, first, kids.size());
        for (const QString &leaf : kids)
            fileNode(next++, dir, leaf);
    }
    putBe16(b, tree + 4, 2);
    putBe32(b, tree + 6, quint32(int(dirs.size()) + int(rootFiles.size())));
    putBe32(b, tree + 10, 1);
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
// The same registration the x64 fixture emits, but in the shape a 32-bit (Qt 5 era)
// build uses: PE32 headers, 4-byte thunks holding virtual addresses, and the four
// arguments pushed right to left instead of passed in registers.
void addRegistrationX86(ResourceFixture &r)
{
    auto &b = r.f.bytes;
    putLe16(b, 0x84, 0x14c); putLe16(b, 0x98, 0x10b);            // machine x86, PE32 magic
    putLe32(b, 0x98 + 28, 0x400000);                              // ImageBase
    putLe16(b, 0x86, 2); putLe32(b, 0x190, 0x17000); putLe32(b, 0x198, 0x17000);
    b.replace(0x1b0, 40, QByteArray(40, '\0')); b.replace(0x1b0, 8, QByteArray(".text\0\0\0", 8));
    putLe32(b, 0x1b8, 0x8000); putLe32(b, 0x1bc, 0x18000); putLe32(b, 0x1c0, 0x8000);
    putLe32(b, 0x1c4, 0x18000); putLe32(b, 0x1d4, 0x60000020);
    b.replace(0x18000, 0x8000, QByteArray(0x8000, char(0x90)));
    b.replace(0x1000, 0x300, QByteArray(0x300, '\0'));
    putLe32(b, 0x98 + 104, 0x1000); putLe32(b, 0x98 + 108, 40);  // import directory (PE32 layout)
    putLe32(b, 0x1000, 0x1100); putLe32(b, 0x100c, 0x1200); putLe32(b, 0x1010, 0x1120);
    putLe32(b, 0x1100, 0x400000 + 0x1220); putLe32(b, 0x1120, 0x400000 + 0x1220);
    const QByteArray dll = QByteArray("Qt5Core.dll") + '\0';
    const QByteArray symbol = QByteArray("?qRegisterResourceData@@YAXHQBEI00@Z") + '\0';
    b.replace(0x1200, dll.size(), dll); b.replace(0x1222, symbol.size(), symbol);
    const auto push = [](quint32 value) {
        QByteArray instruction(1, char(0x68));             // push dword ptr imm32
        instruction += QByteArray(4, '\0'); putLe32(instruction, 1, value); return instruction;
    };
    QByteArray code;
    code += push(0x400000 + quint32(r.f.payloads));
    code += push(0x400000 + quint32(r.f.names));
    code += push(0x400000 + quint32(r.f.tree));
    code += QByteArray::fromHex("6a02");                            // version 2
    code += QByteArray::fromHex("ff15"); code += QByteArray(4, '\0');
    putLe32(code, code.size() - 4, 0x400000 + 0x1120);               // call dword ptr [IAT slot]
    code += QByteArray::fromHex("c3");
    b.replace(0x18000, code.size(), code);
}

QString resourceText(const QByteArray &bytes, const ResourceFixture &r, const QString &name)
{
    const auto node = r.nodes.value(name, -1);
    if (node < 0) return {};
    const qint64 rva = r.f.payloads + be32(bytes, node + 10);
    const qint64 raw = PeUtils::rvaToRaw(PeUtils::sections(bytes), quint32(rva));
    if (!inRange(bytes, raw, 4) || !inRange(bytes, raw + 4, be32(bytes, raw))) return {};
    const auto payload = bytes.mid(raw + 4, be32(bytes, raw));
    return QString::fromUtf8(be16(bytes, node + 4) == 1 ? qUncompress(payload) : payload);
}

QByteArray importedPlayer(const QStringList &normal, const QStringList &delay = {})
{
    auto data = peFile();
    putLe32(data, 0x98 + 60, 0x1000);
    putLe32(data, 0x98 + 108, 16);
    int names = 0x1c00;
    const auto directory = [&](const QStringList &modules, int index, int table, int width) {
        if (modules.isEmpty()) return;
        putLe32(data, 0x98 + 112 + index * 8, table);
        putLe32(data, 0x98 + 116 + index * 8, (modules.size() + 1) * width);
        for (int i = 0; i < modules.size(); ++i) {
            const auto name = modules[i].toLatin1() + '\0';
            data.replace(names, name.size(), name);
            putLe32(data, table + i * width + (index == 1 ? 12 : 4), names);
            if (index == 13) putLe32(data, table + i * width, 1);
            names += name.size();
        }
    };
    directory(normal, 1, 0x1800, 20);
    directory(delay, 13, 0x1a00, 32);
    return data;
}

// The same import table as importedPlayer(), but declared as a 32-bit PE32 image:
// this is what an x86 installation of an older BlueStacks generation looks like.
QByteArray importedPlayer32(const QStringList &modules)
{
    auto data = peFile();
    putLe16(data, 0x84, 0x14c);
    putLe16(data, 0x98, 0x10b);
    putLe32(data, 0x98 + 60, 0x1000);
    putLe32(data, 0x98 + 92, 16);
    const int table = 0x1800;
    int names = 0x1c00;
    putLe32(data, 0x98 + 96 + 8, table);
    putLe32(data, 0x98 + 96 + 12, (modules.size() + 1) * 20);
    for (int i = 0; i < modules.size(); ++i) {
        const auto name = modules[i].toLatin1() + '\0';
        data.replace(names, name.size(), name);
        putLe32(data, table + i * 20 + 12, names);
        names += name.size();
    }
    return data;
}
QByteArray x86Dll()
{
    auto data = peFile();
    putLe16(data, 0x84, 0x14c);
    putLe16(data, 0x98, 0x10b);
    return data;
}

bool gradientPixels(const QString &path, const QString &first, const QString &second, bool horizontal, bool enabled)
{
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
    environment.insert(QStringLiteral("QML_DISABLE_DISK_CACHE"), QStringLiteral("1"));
    process.setProcessEnvironment(environment);
    process.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--gradient-render-test"), path,
                  first, second, horizontal ? QStringLiteral("1") : QStringLiteral("0"), enabled ? QStringLiteral("1") : QStringLiteral("0") });
    if (!process.waitForStarted(10000) || !process.waitForFinished(30000)) {
        process.kill(); process.waitForFinished(); return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        std::printf("    %s\n    %s\n", process.readAllStandardOutput().constData(), process.readAllStandardError().constData());
        return false;
    }
    return true;
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

    std::printf("[gradients] compatible visual targets, native gradient properties and rendered output\n");
    {
        // BlueStacks writes its QML with CRLF endings, a leading copyright block
        // and one member per line, with objectName given as a string literal. That
        // layout is what decides where a binding ends, so the surfaces follow it
        // (a blank-masked literal once swallowed the following property and a
        // "\r" before the line end once rejected the QtQuick import outright),
        // while the theme palette stays minified the way UiThemeObject.qml is.
        const auto crlf = [](QString text) {
            text.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
            return text;
        };
        const QString copyright = QStringLiteral("/* Copyright (C) 2024 Example, Inc. All rights reserved. */\n");
        const QString palette = QStringLiteral("import QtQuick\nQtObject{property QtObject colors:QtObject{property color primary100:'#112233';property color primary90:'#223344';property color accentNormal:'#445566'}"
            "property QtObject gradients:QtObject{property Gradient gradientButton:Gradient{orientation:Gradient.Horizontal;GradientStop{position:0;color:'#ff0000'}GradientStop{position:1;color:'#0000ff'}} property color unaffected:'#010203'}"
            "property QtObject button:QtObject{property QtObject secondary:QtObject{property Gradient borderGradient:null;}}}\n");
        const QString sidebar = crlf(copyright + QStringLiteral(
            "import QtQuick 2.15\n"
            "Rectangle {\n"
            "    objectName: \"sidebar\"\n"
            "    width: 64\n"
            "    height: 64\n"
            "    color: \"#112233\"\n"
            "}\n"));
        const QString topbar = crlf(copyright + QStringLiteral(
            "import QtQuick 2.15 as Quick\n"
            "Quick.Rectangle {\n"
            "    objectName: \"topbar\"\n"
            "    width: 64\n"
            "    height: 64\n"
            "    color: \"#112233\"\n"
            "}\n"));
        const QString button = crlf(copyright + QStringLiteral(
            "import QtQuick 2.15\n"
            "Item {\n"
            "    id: iControl\n"
            "    property QtObject pColorState: QtObject {\n"
            "        property color background: \"#112233\"\n"
            "    }\n"
            "    Component {\n"
            "        id: first\n"
            "        Rectangle { color: iControl.pColorState.background; gradient: null }\n"
            "    }\n"
            "    Component {\n"
            "        id: second\n"
            "        Rectangle { color: iControl.pColorState.background }\n"
            "    }\n"
            "}\n"));
        auto r = resources({ { QStringLiteral("UiThemeObject.qml"), palette }, { QStringLiteral("PlrSidebar.qml"), sidebar },
                             { QStringLiteral("PlrTopbar.qml"), topbar }, { QStringLiteral("UiButton.qml"), button },
                             { QStringLiteral("Other.qml"), QStringLiteral("import QtQuick\nQtObject{ property var stock: UiTheme.gradients.gradientButton }") } }, true,
                           { QStringLiteral("PlrTopbar.qml") });
        addRegistration(r, false);
        const auto original = r.f.bytes;
        const auto discovered = GradientPatcher::analyze(original);
        check(discovered.ok && discovered.targets.size() == 5, "discover sidebar/topbar/button backgrounds and both typed native Gradient slots");
        check(discovered.skippedUnreferenced == 0,
              "a palette gradient is offered because another resource reads it - the read alone is enough");
        QMap<QString, QString> keys;
        for (const auto &target : discovered.targets) keys[target.label] = target.key;
        const QString sideKey = keys.value(QStringLiteral("Sidebar background"));
        const QString topKey = keys.value(QStringLiteral("Top bar background"));
        const QString buttonKey = keys.value(QStringLiteral("Button backgrounds (all states)"));
        check(!sideKey.isEmpty() && !topKey.isEmpty() && !buttonKey.isEmpty(), "surface targets have stable, distinct identifiers");
        const auto path = temp.filePath(QStringLiteral("gradient.exe")); write(path, original);
        const auto applied = GradientPatcher::patch(path, sideKey, { true, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), true });
        if (!applied.ok) std::printf("    %s\n", qPrintable(applied.message));
        check(applied.ok, "solid sidebar background converts to a verified two-stop gradient");
        const auto sideQml = resourceText(read(path), r, QStringLiteral("PlrSidebar.qml"));
        check(sideQml.contains(QStringLiteral("color: \"#112233\"")) && sideQml.contains(QLatin1String("gradient:Gradient")),
              "gradient editing preserves the original solid-color fallback instead of changing its type");
        check(!sideQml.contains(QLatin1String("}};")),
              "an inserted gradient is not terminated with ';' because Qt's QML parser rejects that after an object literal");
        const auto render = temp.filePath(QStringLiteral("GradientPixels.qml")); write(render, sideQml.toUtf8());
        check(gradientPixels(render, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), true, true),
              "actual Qt pixels show the requested horizontal gradient");
        check(GradientPatcher::patch(path, sideKey, { true, QStringLiteral("#00ff00"), QStringLiteral("#0000ff"), false }).ok,
              "an applied gradient can be recolored and changed to vertical");
        write(render, resourceText(read(path), r, QStringLiteral("PlrSidebar.qml")).toUtf8());
        check(gradientPixels(render, QStringLiteral("#00ff00"), QStringLiteral("#0000ff"), false, true),
              "actual Qt pixels show the requested vertical gradient");
        check(GradientPatcher::patch(path, sideKey, { false, {}, {}, true }).ok, "gradient mode can return to the existing solid binding");
        write(render, resourceText(read(path), r, QStringLiteral("PlrSidebar.qml")).toUtf8());
        check(gradientPixels(render, QStringLiteral("#112233"), QStringLiteral("#112233"), true, false),
              "solid fallback renders the untouched original color");
        check(read(path + QStringLiteral(".bak")) == original, "gradient repatches preserve the first complete backup");
        auto image = read(path); QString error;
        check(GradientPatcher::apply(image, topKey, { true, QStringLiteral("#80445566"), QStringLiteral("#ff778899"), true }, &error),
              "raw QML resources and ARGB gradient stops are supported");
        const auto rawTop = resourceText(image, r, QStringLiteral("PlrTopbar.qml"));
        check(rawTop.contains(QLatin1String("#80445566")) && rawTop.contains(QLatin1String("Quick.Gradient"))
                  && be16(image, r.nodes[QStringLiteral("PlrTopbar.qml")] + 4) == 0,
              "gradient stops preserve alpha, qualified QtQuick imports and raw resource storage flags");
        auto topImage = original;
        check(GradientPatcher::apply(topImage, topKey, { true, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), true }, &error),
              "a qualified-import surface accepts the same generated gradient");
        const auto topRender = temp.filePath(QStringLiteral("GradientTopbarPixels.qml"));
        write(topRender, resourceText(topImage, r, QStringLiteral("PlrTopbar.qml")).toUtf8());
        check(gradientPixels(topRender, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), true, true),
              "the patched qualified surface still loads in Qt and renders the requested gradient");
        check(GradientPatcher::apply(image, buttonKey, { true, QStringLiteral("#112233"), QStringLiteral("#aabbcc"), true }, &error),
              "both button-background rendering branches receive the gradient");
        check(resourceText(image, r, QStringLiteral("UiButton.qml")).count(QStringLiteral("GradientStop{position:0;color:\"#112233\"}")) == 2
                  && !resourceText(image, r, QStringLiteral("UiButton.qml")).contains(QLatin1String("gradient:null")),
              "button target edits every supported background branch, replacing and inserting as needed");
        QString borderKey;
        for (const auto &target : discovered.targets) if (target.label.endsWith(QLatin1String("secondary.borderGradient"))) borderKey = target.key;
        check(!borderKey.isEmpty() && GradientPatcher::apply(image, borderKey,
                  { true, QStringLiteral("#334455"), QStringLiteral("#667788"), false }, &error),
              "a native null Gradient property can be converted to a custom border gradient");
        const auto paletteQml = resourceText(image, r, QStringLiteral("UiThemeObject.qml"));
        check(paletteQml.contains(QStringLiteral("borderGradient:Gradient{orientation:Gradient.Vertical"))
                  && !paletteQml.contains(QLatin1String("}};")),
              "a ';' that terminated the replaced value is absorbed so the gradient stays valid QML");
        const auto definitionKey = keys.value(QStringLiteral("Theme · gradients.gradientButton"));
        check(!definitionKey.isEmpty() && GradientPatcher::apply(image, definitionKey,
                  { true, QStringLiteral("#123456"), QStringLiteral("#abcdef"), true }, &error),
              "native gradient definitions have stable named paths in minified QML");
        check(resourceText(image, r, QStringLiteral("UiThemeObject.qml")).contains(QLatin1String("property color unaffected:'#010203'")),
              "a Gradient replacement preserves a following same-line property");
        check(resourceText(image, r, QStringLiteral("UiThemeObject.qml")).contains(QLatin1String("property color primary100:'#112233'")),
              "palette properties typed color remain colors");
        const auto beforeFailure = image;
        check(!GradientPatcher::apply(image, sideKey, { true, QStringLiteral("not a color"), QStringLiteral("#ffffff"), true }, &error)
                  && image == beforeFailure, "invalid stops never write QML or modify the image");
        check(!GradientPatcher::apply(image, QStringLiteral("not-a-real-target"), {}, &error) && image == beforeFailure,
              "stale/unknown gradient targets do not modify the image");
    }

    {
        // Qt 5 based players import an older QtQuick minor version (2.9, 2.7, ...).
        // Those documents do have Gradient, they only lack Gradient::orientation, so
        // they must be offered and patched - just without a direction property.
        const QString legacySurface = QStringLiteral(
            "import QtQuick 2.9\r\n"
            "Rectangle {\r\n"
            "    objectName: \"sidebar\"\r\n"
            "    width: 64\r\n"
            "    height: 64\r\n"
            "    color: \"#223344\"\r\n"
            "}\r\n");
        const QString legacyPalette = QStringLiteral(
            "import QtQuick 2.7\r\n"
            "QtObject{property QtObject gradients:QtObject{property Gradient gradientCard:"
            "Gradient{GradientStop{position:0;color:\"#111111\"}GradientStop{position:1;color:\"#222222\"}}}}\r\n");
        auto r = resources({ { QStringLiteral("UiThemeObject.qml"), legacyPalette },
                             { QStringLiteral("LegacySidebar.qml"), legacySurface },
                             // Keep this file: a palette gradient is only offered when another resource reads it,
                             // and that read is what makes `gradientCard` a real target here. Drop it and the two
                             // assertions below fail for the right reason - nothing in the UI could show it.
                             { QStringLiteral("LegacyOther.qml"), QStringLiteral(
                                 "import QtQuick 2.9\r\nQtObject{ property var stock: UiTheme.gradients.gradientCard }\r\n") } },
                           true, {});
        addRegistration(r, false);
        const auto discovered = GradientPatcher::analyze(r.f.bytes);
        QString surfaceKey, paletteKey;
        bool surfaceFound = false, paletteFound = false;
        for (const auto &target : discovered.targets) {
            if (target.resource == QStringLiteral("LegacySidebar.qml")) { surfaceKey = target.key; surfaceFound = true; }
            if (target.key.endsWith(QLatin1String("gradients.gradientCard"))) { paletteKey = target.key; paletteFound = true; }
        }
        check(discovered.ok && surfaceFound && paletteFound,
              "documents of a Qt 5 era player are discovered instead of being skipped as unsupported");
        for (const auto &target : discovered.targets)
            if (target.resource == QStringLiteral("LegacySidebar.qml"))
                check(!target.supportsOrientation, "a pre-2.12 document reports that it has no gradient direction");
        auto image = r.f.bytes; QString error;
        check(GradientPatcher::apply(image, surfaceKey, { true, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), true }, &error),
              "an inserted gradient is accepted by a QtQuick 2.9 document");
        const auto legacyText = resourceText(image, r, QStringLiteral("LegacySidebar.qml"));
        check(legacyText.contains(QLatin1String("gradient:Gradient{GradientStop{position:0;color:\"#FF0000\"}GradientStop{position:1;color:\"#0000FF\"}}"))
                  && !legacyText.contains(QLatin1String("orientation")),
              "no orientation property is written into a QtQuick version that predates it");
        check(GradientPatcher::apply(image, surfaceKey, { true, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), true }, &error)
                  && GradientPatcher::apply(image, surfaceKey, { false, {}, {}, false }, &error),
              "an orientation-less gradient verifies and can be reverted on a pre-2.12 document");
        check(GradientPatcher::apply(image, paletteKey, { true, QStringLiteral("#aabbcc"), QStringLiteral("#ddeeff"), true }, &error)
                  && resourceText(image, r, QStringLiteral("UiThemeObject.qml"))
                         .contains(QLatin1String("gradientCard:Gradient{GradientStop{position:0;color:\"#AABBCC\"}")),
              "a native gradient definition in an old palette is replaced without an orientation");
        // QtQuick 1.x really cannot express this: no GradientStop children there.
        const QString ancient = QStringLiteral("import QtQuick 1.1\r\nRectangle { objectName: \"ancient\"\r\n color: \"#101010\"\r\n}\r\n");
        auto old = resources({ { QStringLiteral("PlrAncient.qml"), ancient } }, true, {});
        addRegistration(old, false);
        check(!GradientPatcher::analyze(old.f.bytes).ok,
              "a QtQuick 1.x document is refused rather than patched into something unloadable");
    }

    {
        // Real Qt 5 era installs additionally store parts of their QML with a UTF-8
        // byte order mark (9 of the 118 embedded .qml files in the old 64-bit
        // HD-Player.exe), one of them with the QtQuick import as its very first
        // line. catalog() unwraps the marker before decoding, so the marked file has
        // to be discovered, edited and left parseable exactly like an unmarked one -
        // which is what this fixture pins, including the CRLF layout of that build.
        const QString markedPalette = QStringLiteral(
            "import QtQuick 2.7\r\n"
            "QtObject{property QtObject gradients:QtObject{property Gradient gradientCard:"
            "Gradient{GradientStop{position:0;color:'#111111'}GradientStop{position:1;color:'#222222'}}}}\r\n");
        const QString markedSidebar = QString(QChar(0xFEFF)) + QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    objectName: \"sidebar\"\r\n"
            "    width: 64\r\n"
            "    color: \"#334455\"\r\n"
            "}\r\n");
        auto r = resources({ { QStringLiteral("UiThemeObject.qml"), markedPalette },
                             { QStringLiteral("MarkedSidebar.qml"), markedSidebar } }, true, {});
        addRegistration(r, false);
        const auto marked = GradientPatcher::analyze(r.f.bytes);
        QString key;
        for (const auto &target : marked.targets)
            if (target.label == QLatin1String("Sidebar background")) key = target.key;
        check(marked.ok && !key.isEmpty(),
              "a UTF-8 BOM in front of the QtQuick import does not hide the gradient targets");
        auto image = r.f.bytes;
        QString error;
        const bool markedApplied = !key.isEmpty()
            && GradientPatcher::apply(image, key, { true, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), false }, &error);
        check(markedApplied, "a BOM-marked surface accepts the generated gradient");
        const auto text = resourceText(image, r, QStringLiteral("MarkedSidebar.qml"));
        check(text.contains(QLatin1String("gradient:Gradient")),
              "the BOM-marked file gains the gradient binding");
        // The marker itself is deliberately not asserted: it is unwrapped before
        // decoding and the QML loader accepts the file with or without it, so the
        // parsed result and the written binding are what have to hold.
        check(text.contains(QStringLiteral("import QtQuick 2.15")) && text.contains(QLatin1String("objectName: \"sidebar\"")),
              "editing a BOM-marked file leaves its import and object identity intact");
    }

    {
        // A 32-bit installation used to expose no resource pools at all, because only
        // the x64 qRegisterResourceData call shape was understood. Theme gradients and
        // the surfaces beside them have to be discoverable there as well.
        const QString palette = QStringLiteral("import QtQuick\nQtObject{property QtObject gradients:QtObject{property Gradient gradientButton:Gradient{orientation:Gradient.Horizontal;GradientStop{position:0;color:'#ff0000'}GradientStop{position:1;color:'#0000ff'}}}}\n");
        const QString sidebar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    objectName: \"sidebar\"\r\n"
            "    width: 64\r\n"
            "    height: 64\r\n"
            "    color: \"#112233\"\r\n"
            "}\r\n");
        auto r = resources({ { QStringLiteral("UiThemeObject.qml"), palette },
                             { QStringLiteral("PlrSidebar.qml"), sidebar } }, true, {});
        addRegistrationX86(r);
        check(!QtResources::registeredPools(r.f.bytes).isEmpty(),
              "the compiled-in resource pool of a 32-bit image is recovered from its push-shaped registration");
        const auto discovered = GradientPatcher::analyze(r.f.bytes);
        QString sideKey;
        for (const auto &target : discovered.targets)
            if (target.resource == QStringLiteral("PlrSidebar.qml")) sideKey = target.key;
        check(discovered.ok && !sideKey.isEmpty(),
              "gradient targets beside the theme palette are offered for a 32-bit player");
        auto image = r.f.bytes; QString error;
        const bool x86Applied = GradientPatcher::apply(image, sideKey, { true, QStringLiteral("#001122"), QStringLiteral("#334455"), false }, &error);
        check(x86Applied, QStringLiteral("a gradient is applied and read back inside a 32-bit image (%1)").arg(error.isEmpty() ? QStringLiteral("no error reported") : error));
        check(resourceText(image, r, QStringLiteral("PlrSidebar.qml"))
                  .contains(QLatin1String("gradient:Gradient{orientation:Gradient.Vertical;GradientStop{position:0;color:\"#001122\"}")),
              "the written gradient is found again through the recovered pool");
    }

    std::printf("[gradient-filter] unread palette gradients, surviving chrome surfaces, Qt generation gate\n");
    {
        // Measured on the two players supplied with this work: `colors.` is read 760
        // (Qt 5 / MSI 5) and 1123 (Qt 6) times while `gradients.` is read by nobody,
        // and the old build's single used gradient paints the 1 px ring around a
        // button. So a palette gradient nothing reads is dropped from the list, the
        // surfaces that actually paint the theme survive it, and a Qt 5 player is
        // pointed at the hex method's control instead of an unusable QML list.
        const QString palette = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "QtObject{property QtObject colors:QtObject{property color primary70:'#232642' property color primary10:'#101218'}"
            "property QtObject gradients:QtObject{property Gradient gradientUnused:Gradient{orientation:Gradient.Horizontal;"
            "GradientStop{position:0;color:'#ff0000'}GradientStop{position:1;color:'#00ff00'}}"
            "property Gradient borderGradient:Gradient{GradientStop{position:0;color:'#111111'}GradientStop{position:1;color:'#222222'}}}}"
            "\r\n");
        const QString sidebar = QStringLiteral(
            "import QtQuick 2.15\r\nRectangle {\r\n    id: iSidebar\r\n    objectName: \"sidebar\"\r\n"
            "    color: UiTheme.colors.primary70\r\n}\r\n");
        const QString topbar = QStringLiteral(
            "import QtQuick 2.15\r\nRectangle {\r\n    objectName: \"topbar\"\r\n"
            "    color: UiTheme.colors.primary10\r\n}\r\n");
        const QString button = QStringLiteral(
            "import QtQuick 2.15\r\nItem { Rectangle { gradient: UiTheme.gradients.borderGradient } }\r\n");
        auto r = resources({ { QStringLiteral("UiThemeObject.qml"), palette }, { QStringLiteral("PlrSidebar.qml"), sidebar },
                             { QStringLiteral("PlrTopbar.qml"), topbar }, { QStringLiteral("UiButton.qml"), button } }, true, {});
        addRegistration(r, false);
        const auto analysis = GradientPatcher::analyze(r.f.bytes);
        bool unusedOffered = false, usedOffered = false, sideOffered = false, topOffered = false;
        for (const auto &target : analysis.targets) {
            if (target.key.endsWith(QLatin1String("gradients.gradientUnused"))) unusedOffered = true;
            if (target.key.endsWith(QLatin1String("gradients.borderGradient"))) usedOffered = true;
            if (target.label == QLatin1String("Sidebar background")) sideOffered = true;
            if (target.label == QLatin1String("Top bar background")) topOffered = true;
        }
        check(!unusedOffered && usedOffered && analysis.skippedUnreferenced == 1,
              QStringLiteral("a palette gradient no resource reads is left out while one that is read stays offered "
                             "(skipped %1)").arg(analysis.skippedUnreferenced));
        check(sideOffered && topOffered, "the themed surfaces a gradient can paint survive the filter");
        QString sideKey, topKey;
        for (const auto &target : analysis.targets) {
            if (target.label == QLatin1String("Sidebar background"))
                sideKey = target.key;
            if (target.label == QLatin1String("Top bar background"))
                topKey = target.key;
        }
        // The Hex (old) gradient editor covers every surface the QML (new)
        // editor lists: all catalog targets carry the flag.
        for (const auto &target : analysis.targets) {
            check(target.hexGradient,
                  QStringLiteral("every catalog surface carries the hex gradient flag (%1)")
                      .arg(target.label));
        }
        auto image = r.f.bytes;
        QString error;
        GradientPatcher::Spec spec;
        spec.first = QStringLiteral("#ff0000");
        spec.second = QStringLiteral("#0000ff");
        const bool topbars = GradientPatcher::applySurfaces(image, GradientPatcher::allGradientKey(), spec, &error);
        check(topbars, QStringLiteral("one hex call writes every gradient surface (%1)")
                  .arg(error.isEmpty() ? QStringLiteral("ok") : error));
        const QString sideText = resourceText(image, r, QStringLiteral("PlrSidebar.qml"));
        check(sideText.contains(QLatin1String("gradient:Gradient"))
                  && sideText.contains(QStringLiteral("color: UiTheme.colors.primary70")),
              "the hex gradient paints the sidebar too and keeps its solid fallback color");
        const QString topText = resourceText(image, r, QStringLiteral("PlrTopbar.qml"));
        check(topText.contains(QLatin1String("gradient:Gradient"))
                  && topText.contains(QStringLiteral("color: UiTheme.colors.primary10")),
              "the hex gradient paints the top bar and keeps its solid fallback color");
        const auto beforeRefusal = image;
        check(!GradientPatcher::applySurfaces(image, QStringLiteral("9fff:no-such.qml:surface.root"), spec, &error)
                  && image == beforeRefusal && error.contains(QLatin1String("Reload the gradient list")),
              QStringLiteral("an unknown key is refused by the hex gradient editor (%1)").arg(error));
        check(GradientPatcher::applySurfaces(image, topKey, { true, QStringLiteral("#112233"), QStringLiteral("#445566"), false },
                                           &error),
              "a single top bar surface accepts its own custom stops and direction");
        check(GradientPatcher::applySurfaces(image, sideKey, { true, QStringLiteral("#445566"), QStringLiteral("#778899"), true },
                                           &error),
              "a single sidebar surface accepts its own custom stops and direction through the hex editor");
        int major = 0;
        QString why;
        const auto qt5Image = importedPlayer({ QStringLiteral("Qt5Core.dll") });
        const auto qt6Image = importedPlayer({ QStringLiteral("Qt6Core.dll") });
        const auto imports = PeUtils::importedModules(qt5Image);
        ciNotice(QStringLiteral("gate-diag ok=%1 modules=[%2]").arg(imports.ok ? 1 : 0)
                     .arg(imports.modules.join(QStringLiteral(", "))));
        check(imports.ok && imports.modules.contains(QStringLiteral("qt5core.dll")),
              "the synthetic player fixture really exposes an import table for the Qt generation gate");
        check(!GradientPatcher::qmlMethodSupported(qt5Image, &major, &why) && major == 5
                  && why.contains(QLatin1String("Hex")),
              QStringLiteral("a Qt 5 player is pointed at the hex method instead of an empty QML gradient list (major %1)")
                  .arg(major));
        check(GradientPatcher::qmlMethodSupported(qt6Image, &major, nullptr) && major == 6,
              QStringLiteral("a Qt 6 player keeps the QML gradient method (major %1)").arg(major));
        check(GradientPatcher::qmlMethodSupported(QByteArray(), nullptr, nullptr),
              "an unreadable import table never hides the gradient editor");
    }

    std::printf("[gradients] player-window chrome instances, title bar scope, Qt5 unit invalidation, QML gate\n");
    {
        const QString host = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Item {\r\n"
            "    PlrTopbar {\r\n"
            "        id: iTopbar\r\n"
            "        color: oem === \"msi5\" ? primary70 : primary80\r\n"
            "    }\r\n"
            "    PlrTitleBar {\r\n"
            "        id: iTitleBar\r\n"
            "        color: primary80\r\n"
            "    }\r\n"
            "    PlrSidebar {\r\n"
            "        id: iSidebar\r\n"
            "        color: oem === \"msi5\" ? primary70 : primary80\r\n"
            "    }\r\n"
            "}\r\n");
        const QString typeSidebar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iSidebar\r\n"
            "    objectName: \"sidebar\"\r\n"
            "    color: \"#112233\"\r\n"
            "}\r\n");
        const QString typeTopbar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    objectName: \"topbar\"\r\n"
            "    color: \"#112233\"\r\n"
            "}\r\n");
        auto r = resources({ { QStringLiteral("plr.qml"), host },
                             { QStringLiteral("PlrSidebar.qml"), typeSidebar },
                             { QStringLiteral("PlrTopbar.qml"), typeTopbar } }, true, {});
        addRegistration(r, false);
        const auto discovered = GradientPatcher::analyze(r.f.bytes);
        bool chromeSide = false, chromeTop = false, chromeTitle = false, typeSide = false, typeTop = false;
        QString sideKey, titleKey;
        int hexCount = 0;
        for (const auto &target : discovered.targets) {
            if (target.kind != QLatin1String("chrome") && target.kind != QLatin1String("background"))
                continue;
            chromeSide |= target.label == QLatin1String("Sidebar (player window)") && target.kind == QLatin1String("chrome")
                          && target.hexGradient;
            chromeTop |= target.label == QLatin1String("Top bar (player window)") && target.kind == QLatin1String("chrome")
                         && target.hexGradient;
            chromeTitle |= target.label == QLatin1String("Title bar (player window)") && target.kind == QLatin1String("chrome")
                           && target.hexGradient;
            typeSide |= target.label == QLatin1String("Sidebar background") && target.hexGradient;
            typeTop |= target.label == QLatin1String("Top bar background") && target.hexGradient;
            if (target.hexGradient) ++hexCount;
            if (target.label == QLatin1String("Sidebar (player window)")) sideKey = target.key;
            if (target.label == QLatin1String("Title bar (player window)")) titleKey = target.key;
        }
        check(discovered.ok && chromeSide && chromeTop && chromeTitle && typeSide && typeTop,
              "player-window PlrSidebar/PlrTopbar/title-bar instances are all chrome targets flagged for hex");
        check(hexCount == 5, "every player-window chrome instance and type-level surface is a hex gradient target");
        check(!discovered.targets.isEmpty() && discovered.targets.first().kind == QLatin1String("chrome"),
              "chrome instance targets are listed before type-level backgrounds");
        auto image = r.f.bytes; QString error;
        check(GradientPatcher::applySurfaces(image, GradientPatcher::allGradientKey(),
                                           { true, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), true }, &error),
              QStringLiteral("hex gradient combined apply writes every plr.qml chrome instance (%1)")
                  .arg(error.isEmpty() ? QStringLiteral("ok") : error));
        const auto plr = resourceText(image, r, QStringLiteral("plr.qml"));
        check(plr.contains(QLatin1String("gradient:Gradient{orientation:Gradient.Horizontal;"))
                  && plr.contains(QLatin1String("id: iTopbar")) && plr.contains(QLatin1String("id: iSidebar"))
                  && plr.contains(QLatin1String("color: oem === \"msi5\"")),
              "hex gradient keeps the solid color fallback on every player-window instance");
        {
            // The gradient is added to every player-window chrome instance: three
            // gradient bindings in plr.qml, including the sidebar instance now.
            const int iSidebarPos = plr.indexOf(QLatin1String("id: iSidebar"));
            const int gradientCount = plr.count(QLatin1String("gradient:Gradient"));
            const QString sidebarBody = plr.mid(iSidebarPos > 0 ? plr.lastIndexOf(QLatin1String("PlrSidebar"), iSidebarPos) : 0);
            check(gradientCount == 3 && sidebarBody.contains(QLatin1String("gradient:Gradient")),
                  "the top bar, title bar and sidebar player-window instances receive the gradient");
        }
        check(resourceText(image, r, QStringLiteral("PlrTopbar.qml")).contains(QLatin1String("gradient:Gradient")),
              "hex apply paints the type-level top bar surface");
        check(resourceText(image, r, QStringLiteral("PlrSidebar.qml")).contains(QLatin1String("gradient:Gradient")),
              "hex apply paints the type-level sidebar surface too");
        check(GradientPatcher::applySurfaces(image, titleKey,
                                           { true, QStringLiteral("#00ff00"), QStringLiteral("#ffffff"), false }, &error),
              "a single title bar instance accepts custom stops and its own direction");
        check(GradientPatcher::applySurfaces(image, sideKey,
                                            { true, QStringLiteral("#00ff00"), QStringLiteral("#ffffff"), true }, &error),
              "the sidebar instance can be gradient-edited through the hex editor like everywhere else");
        const QString path = temp.filePath(QStringLiteral("chrome-gradient.exe"));
        write(path, r.f.bytes);
        const QString cacheDir = QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("cache/qmlcache"));
        QDir().mkpath(cacheDir);
        write(QDir(cacheDir).filePath(QStringLiteral("stale.qmlc")), QByteArray("stale"));
        const auto patched = GradientPatcher::patchSurfaces(path, GradientPatcher::allGradientKey(),
                                                          { true, QStringLiteral("#aabbcc"), QStringLiteral("#ddeeff"), false });
        if (!patched.ok) std::printf("    %s\n", qPrintable(patched.message));
        check(patched.ok, "patchSurfaces writes the EXE and reports success");
        check(!QDir(cacheDir).exists(), "patchSurfaces deletes the player qmlcache so Qt 6 reloads source");
    }

    {
        QByteArray unit(400, '\0');
        unit.replace(0, 8, QByteArray("qv4cdata", 8));
        putLe32(unit, 8, 41);
        putLe32(unit, 12, 0x50f04);
        putLe32(unit, 24, 400);
        putLe32(unit, 112, 2);
        putLe32(unit, 116, 248);
        putLe32(unit, 236, 0);
        putLe32(unit, 248, 256);
        putLe32(unit, 252, 300);
        putLe32(unit, 256, 0xffffffff);
        putLe32(unit, 260, 0);
        putLe32(unit, 300, 0xffffffff);
        putLe32(unit, 304, 10);
        putLe32(unit, 308, 0);
        putLe32(unit, 312, 0x18);
        putLe32(unit, 316, 0x18);
        putLe32(unit, 320, 0);
        const QString ident = QStringLiteral("PlrSidebar");
        for (int i = 0; i < ident.size(); ++i)
            putLe16(unit, 324 + i * 2, ident.at(i).unicode());
        auto hit = unit;
        check(QtResources::invalidateUnits(hit, { QStringLiteral("PlrSidebar.qml") }) == 1
                  && le32(hit, 8) == 0,
              "Qt 5.15 units with empty sourceFileIndex are invalidated via the string table");
        auto miss = unit;
        check(QtResources::invalidateUnits(miss, { QStringLiteral("UiThemeObject.qml") }) == 0
                  && le32(miss, 8) == 41,
              "unrelated compiled units are not blanket-invalidated");
        auto plr = unit;
        const QString chromeId = QStringLiteral("iSidebar");
        putLe32(plr, 304, quint32(chromeId.size()));
        for (int i = 0; i < chromeId.size(); ++i)
            putLe16(plr, 324 + i * 2, chromeId.at(i).unicode());
        check(QtResources::invalidateUnits(plr, { QStringLiteral("plr.qml") }) == 1,
              "plr.qml invalidation matches iSidebar in the compiled unit");
    }

    {
        const QString qt5Path = temp.filePath(QStringLiteral("old-player.exe"));
        write(qt5Path, importedPlayer({ QStringLiteral("Qt5Core.dll") }));
        QSettings().setValue(QStringLiteral("paths/exe"), qt5Path);
        SettingsManager settings;
        Backend backend(&settings);
        backend.loadTheme(QStringLiteral("qml"));
        check(!backend.qmlThemeAvailable() && !backend.themeFound(),
              "forcing QML on a Qt 5 EXE loads nothing");
        check(backend.resolvedThemeMethod() == QLatin1String("qml") && backend.detectedQtMajor() == 5
                  && backend.themeDetection().contains(QLatin1String("Manually")),
              "a forced QML load on a Qt 5 EXE reports the manual pick and the detected generation");
        backend.loadTheme(QStringLiteral("auto"));
        check(!backend.qmlThemeAvailable() && backend.themeMode() == QLatin1String("hex"),
              "auto-load on a Qt 5 EXE uses Hex (old)");
        check(backend.resolvedThemeMethod() == QLatin1String("hex") && backend.detectedQtMajor() == 5
                  && backend.themeDetection().contains(QLatin1String("Auto-detected a Qt 5"))
                  && backend.themeDetection().contains(QLatin1String("Hex (old)")),
              QStringLiteral("auto-load resolves and explains the hex method on a Qt 5 EXE (%1)")
                  .arg(backend.themeDetection()));
        const QString qt6Path = temp.filePath(QStringLiteral("new-player.exe"));
        write(qt6Path, importedPlayer({ QStringLiteral("Qt6Core.dll") }));
        settings.setExePath(qt6Path);
        // Changing the EXE re-armed the chooser, so the page behaves like a fresh load.
        check(backend.resolvedThemeMethod() == QLatin1String("hex") || backend.resolvedThemeMethod() == QLatin1String("auto"),
              "an EXE change leaves the loaded method explictly re-resolvable");
        backend.loadTheme(QStringLiteral("auto"));
        check(backend.qmlThemeAvailable(),
              "a Qt 6 player offers the QML method even when no theme blob is present");
        check(backend.resolvedThemeMethod() == QLatin1String("qml") && backend.detectedQtMajor() == 6
                  && backend.themeDetection().contains(QLatin1String("Auto-detected a Qt 6"))
                  && backend.themeDetection().contains(QLatin1String("QML (new)")),
              QStringLiteral("auto-load resolves and explains the QML method on a Qt 6 EXE (%1)")
                  .arg(backend.themeDetection()));
        backend.loadTheme(QStringLiteral("hex"));
        check(backend.resolvedThemeMethod() == QLatin1String("hex") && backend.detectedQtMajor() == 6,
              "Hex (old) stays selectable as an informed override on a Qt 6 player");
    }

    std::printf("[settings-gradient] settings panel surface in both gradient editors\n");
    {
        // The settings window paints its whole panel with one full-window
        // Rectangle under UiShadowContainer (root objectName "settingsWindow").
        // The catalog used to stop at the player chrome, so the old players
        // showed a themed sidebar beside an untouched settings panel - the
        // report that "the gradient only works on the sidebar". The panel
        // background has to become a first-class surface, while the transparent
        // content pane and the decorative fade divider stay untouched.
        const QString settings = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "import QtQuick.Window 2.15\r\n"
            "Window {\r\n"
            "    id: iSettingsWindow\r\n"
            "    objectName: \"settingsWindow\"\r\n"
            "    color: \"transparent\"\r\n"
            "    UiShadowContainer {\r\n"
            "        id: iSettingsWindowShadowContainer\r\n"
            "        Rectangle {\r\n"
            "            anchors.fill: parent\r\n"
            "            color: UiTheme.colors.primary80\r\n"
            "            Rectangle {\r\n"
            "                Layout.preferredWidth: 548\r\n"
            "                color: \"transparent\"\r\n"
            "            }\r\n"
            "            Rectangle {\r\n"
            "                width: 1\r\n"
            "                gradient: Gradient {\r\n"
            "                    GradientStop { position: 0.0; color: \"#ff0000\" }\r\n"
            "                    GradientStop { position: 1.0; color: \"#0000ff\" }\r\n"
            "                }\r\n"
            "            }\r\n"
            "        }\r\n"
            "    }\r\n"
            "}\r\n");
        const QString palette = QStringLiteral(
            "import QtQuick\nQtObject{property QtObject colors:QtObject{property color primary80:'#223344'}}\n");
        auto r = resources({ { QStringLiteral("UiThemeObject.qml"), palette },
                             { QStringLiteral("UiSettingsWindow.qml"), settings } }, true, {});
        addRegistration(r, false);
        const auto discovered = GradientPatcher::analyze(r.f.bytes);
        QString key;
        int painted = 0;
        for (const auto &target : discovered.targets) {
            if (target.label == QLatin1String("Settings window background")) {
                key = target.key;
                ++painted;
            }
            check(target.hexGradient, "the settings surfaces are offered to the hex gradient editor too");
        }
        check(discovered.ok && !key.isEmpty() && painted == 1,
              "the settings panel background is offered exactly once as a gradient surface");
        auto image = r.f.bytes;
        QString error;
        check(GradientPatcher::apply(image, key, { true, QStringLiteral("#ff0000"), QStringLiteral("#0000ff"), true }, &error),
              QStringLiteral("the settings panel accepts a two-stop gradient (%1)")
                  .arg(error.isEmpty() ? QStringLiteral("ok") : error));
        const auto text = resourceText(image, r, QStringLiteral("UiSettingsWindow.qml"));
        check(text.contains(QLatin1String("Rectangle {\ngradient:Gradient"))
                  && text.contains(QStringLiteral("color: UiTheme.colors.primary80"))
                  && text.contains(QStringLiteral("anchors.fill: parent")),
              "the panel background keeps its solid fallback and gains the gradient");
        check(text.contains(QStringLiteral("color: \"transparent\""))
                  && text.contains(QStringLiteral("GradientStop { position: 0.0; color: \"#ff0000\" }")),
              "the transparent content pane and the decorative fade divider stay untouched");
        check(GradientPatcher::apply(image, key, { false, {}, {}, true }, &error)
                  && resourceText(image, r, QStringLiteral("UiSettingsWindow.qml"))
                         .contains(QLatin1String("Rectangle {\ngradient:null")),
              "the settings panel gradient can be disabled back to the solid binding");
        // The combined Hex (old) action paints every surface at once - on the
        // old players that is the only gradient editor, so the settings panel
        // must ride along with the sidebar and top bar there.
        auto all = r.f.bytes;
        GradientPatcher::Spec spec;
        spec.first = QStringLiteral("#ff0000");
        spec.second = QStringLiteral("#0000ff");
        check(GradientPatcher::applySurfaces(all, GradientPatcher::allGradientKey(), spec, &error),
              QStringLiteral("one hex call paints the settings panel with the rest (%1)")
                  .arg(error.isEmpty() ? QStringLiteral("ok") : error));
        check(resourceText(all, r, QStringLiteral("UiSettingsWindow.qml")).contains(QLatin1String("Rectangle {\ngradient:Gradient")),
              "the all-at-once hex gradient reaches the settings panel");
    }

    std::printf("[extras-removals] removed sidebar buttons collapse the QML layout\n");
    {
        // Nulling an icon name only blanks the glyph: the button keeps its
        // 32px slot and the sidebar shows an empty gap where it used to be
        // (reported against the latest player). A sidebar removal has to
        // disable the owning PlrSidebarElement too, so the element leaves
        // pSidebarItems and the column closes up - the fix "by qml".
        const QString sidebar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iSidebar\r\n"
            "    property list<QtObject> pAllSidebarItems: [\r\n"
            "        PlrSidebarElement {\r\n"
            "            pElementId: pRotate\r\n"
            "            pAsset: \"SidebarRotate\"\r\n"
            "            pTooltipText: qsTranslate(\"QObject\", \"Rotate\")\r\n"
            "            pIsFeatureEnable: iSidebarProperties.pIsRotateEnabled\r\n"
            "            pShowOnKebabMenu: true\r\n"
            "        },\r\n"
            "        PlrSidebarElement {\r\n"
            "            pElementId: pLockCursor\r\n"
            "            pAsset: pMouseCursorLocked ? \"SidebarLockCursorActive\" : \"SidebarLockCursor\"\r\n"
            "            pIsFeatureEnable: uiBackend.confGetBoolDefaultWhenEmpty(\"bst.feature.macros\",\r\n"
            "                uiBackend.confGetBool(\"bst.feature.macros\"))\r\n"
            "        }\r\n"
            "    ]\r\n"
            "}\r\n");
        auto r = resources({ { QStringLiteral("PlrSidebar.qml"), sidebar } }, true, {});
        addRegistration(r, false);
        const QString path = temp.filePath(QStringLiteral("sidebar-removals.exe"));
        write(path, r.f.bytes);
        const auto rr = ExtrasPatcher::applyRemovals(path,
            { QStringLiteral("Sidebar Rotate"), QStringLiteral("Sidebar Lock Cursor"),
              QStringLiteral("Sidebar Macro") });
        check(rr.ok && rr.msg.contains(QLatin1String("Hid 2")),
              QStringLiteral("the two matching sidebar buttons are hidden in QML (%1)").arg(rr.msg));
        const auto text = resourceText(read(path), r, QStringLiteral("PlrSidebar.qml"));
        check(text.contains(QStringLiteral("pIsFeatureEnable: false"))
                  && !text.contains(QLatin1String("iSidebarProperties.pIsRotateEnabled"))
                  && !text.contains(QLatin1String("confGetBoolDefaultWhenEmpty")),
              "both feature switches are replaced with false, including the wrapped call");
        check(text.count(QStringLiteral("pIsFeatureEnable: false")) == 2
                  && text.contains(QStringLiteral("pAsset: \"SidebarRotate\""))
                  && text.contains(QStringLiteral("pAsset: pMouseCursorLocked")),
              "the elements themselves stay intact - only the switch flips");
        const auto again = ExtrasPatcher::applyRemovals(path,
            { QStringLiteral("Sidebar Rotate"), QStringLiteral("Sidebar Lock Cursor") });
        check(again.ok, "a repeated removal stays idempotent instead of corrupting the sidebar");
        check(resourceText(read(path), r, QStringLiteral("PlrSidebar.qml"))
                  .count(QStringLiteral("pIsFeatureEnable: false")) == 2,
              "a repeated removal does not stack further edits");
    }

    std::printf("[layout] sidebar-to-titlebar layout tweak by QML\n");
    {
        // The layout family's first tweak: the side strip is gone and every
        // sidebar button becomes one centered row inside the title bar. Two
        // real player generations are covered here:
        //   * the current player anchors PlrSidebar to the right-hand strip and
        //     sizes it with `width: pSidebarWidth`;
        //   * the old 5.10/5.12 player lays it out as a `PlrSidebar` item of a
        //     UiRowLayout and sizes it with `Layout.preferredWidth`.
        // Both used to be reduced to a zero-width strip and host a second
        // instance inside the title bar. Reversibility is pinned at the
        // document level: toggling the option off writes every edited QML file
        // back byte-for-byte, so the change needs no backup restore to undo.
        const QString sidebar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iSidebar\r\n"
            "    property int pSidebarElementSize: 32\r\n"
            "    property int pVisibleSidebarElementsCount: 0\r\n"
            "    property string pSound: \"sound\"\r\n"
            "    property string pRecent: \"recent\"\r\n"
            "    property string pHome: \"home\"\r\n"
            "    property string pBack: \"back\"\r\n"
            "    property string pSettings: \"settings\"\r\n"
            "    property list<QtObject> pAllSidebarItems: [\r\n"
            "        PlrSidebarElement { pElementId: pSound; pShowViaRepaterOnly: true; pIsFeatureEnable: true },\r\n"
            "        PlrSidebarElement { pElementId: \"screen\"; pShowViaRepaterOnly: true; pIsFeatureEnable: false },\r\n"
            "        PlrSidebarElement { pElementId: pRecent; pShowViaRepaterOnly: false; pIsFeatureEnable: true; pShowOnKebabMenu: !iRecentBtn.visible },\r\n"
            "        PlrSidebarElement { pElementId: pHome; pShowViaRepaterOnly: false; pIsFeatureEnable: true; pShowOnKebabMenu: !iHomeBtn.visible },\r\n"
            "        PlrSidebarElement { pElementId: pBack; pShowViaRepaterOnly: false; pIsFeatureEnable: true; pShowOnKebabMenu: !iBackBtn.visible },\r\n"
            "        PlrSidebarElement { pElementId: pSettings; pShowViaRepaterOnly: false; pIsFeatureEnable: true; pShowOnKebabMenu: !iSettingsBtn.visible }\r\n"
            "    ]\r\n"
            "    onHeightChanged: {\r\n"
            "        fResetSidebar()\r\n"
            "    }\r\n"
            "    function fResetSidebar() {\r\n"
            "        var availableHeight = iSidebar.height - iCollapseBtn.height\r\n"
            "        if (iSidebar.height < 32 * 7)\r\n"
            "            availableHeight = availableHeight - iKebabMenuBtn.height\r\n"
            "        if (availableHeight >= iRecentBtn.height) { iRecentBtn.visible = true; availableHeight -= iRecentBtn.height }\r\n"
            "        else iRecentBtn.visible = false\r\n"
            "        if (availableHeight >= iHomeBtn.height) { iHomeBtn.visible = true; availableHeight -= iHomeBtn.height }\r\n"
            "        else iHomeBtn.visible = false\r\n"
            "        if (availableHeight >= iBackBtn.height) { iBackBtn.visible = true; availableHeight -= iBackBtn.height }\r\n"
            "        else iBackBtn.visible = false\r\n"
            "        if (availableHeight >= iSettingsBtn.height) { iSettingsBtn.visible = true; availableHeight -= iSettingsBtn.height }\r\n"
            "        else iSettingsBtn.visible = false\r\n"
            "    }\r\n"
            "    Component {\r\n"
            "        id: iSoundPopupComponent\r\n"
            "        UiWindowedPopup {\r\n"
            "            id: iSoundPopup\r\n"
            "            onWidthChanged: {\r\n"
            "                iSoundPopup.x = iSidebar.mapToGlobal(0, 0).x - iSoundPopup.width + iSoundPopup.shadowThickness\r\n"
            "            }\r\n"
            "        }\r\n"
            "    }\r\n"
            "    Component {\r\n"
            "        id: iMenuPopupComponent\r\n"
            "        UiWindowedPopup {\r\n"
            "            Flow {\r\n"
            "                Repeater {\r\n"
            "                    model: pAllSidebarItems\r\n"
            "                    delegate: UiImageButton {\r\n"
            "                        visible: (index >= pVisibleSidebarElementsCount) && model.modelData.pIsFeatureEnable && model.modelData.pShowOnKebabMenu\r\n"
            "                    }\r\n"
            "                }\r\n"
            "            }\r\n"
            "        }\r\n"
            "    }\r\n"
            "    UiColumnLayout {\r\n"
            "        anchors.fill: parent\r\n"
            "        UiImageButton { id: iCollapseBtn }\r\n"
            "        Repeater {\r\n"
            "            model: pAllSidebarItems\r\n"
            "            delegate: UiImageButton {\r\n"
            "                visible: index < pVisibleSidebarElementsCount && modelData.pIsFeatureEnable\r\n"
            "            }\r\n"
            "        }\r\n"
            "        UiImageButton {\r\n"
            "            id: iKebabMenuBtn\r\n"
            "            Layout.alignment: Qt.AlignHCenter\r\n"
            "            onClicked: {\r\n"
            "                var menuWin = iSoundPopupComponent.createObject(iAppwin)\r\n"
            "                menuWin.x = iSidebar.mapToGlobal(0, 0).x - menuWin.width + menuWin.shadowThickness\r\n"
            "                menuWin.y = iKebabMenuBtn.mapToGlobal(0, 0).y - menuWin.shadowThickness\r\n"
            "            }\r\n"
            "        }\r\n"
            "        Item {\r\n"
            "            Layout.fillHeight: true\r\n"
            "        }\r\n"
            "        UiImageButton {\r\n"
            "            id: iSettingsBtn\r\n"
            "            Layout.preferredWidth: pSidebarElementSize\r\n"
            "        }\r\n"
            "        UiImageButton { id: iBackBtn }\r\n"
            "        UiImageButton { id: iHomeBtn }\r\n"
            "        UiImageButton { id: iRecentBtn }\r\n"
            "    }\r\n"
            "    function fOpenSoundPopup() {\r\n"
            "        var soundWin = iSoundPopupComponent.createObject(iAppwin)\r\n"
            "        soundWin.x = iSidebar.mapToGlobal(0, 0).x - soundWin.width + soundWin.shadowThickness\r\n"
            "        soundWin.y = fGetPopupTopPoint(pSound) - soundWin.shadowThickness\r\n"
            "    }\r\n"
            "    function fGetPopupTopPoint(elementId) { return iSidebar.mapToGlobal(0, 0).y }\r\n"
            "}\r\n");
        const QString topbar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iTopbar\r\n"
            "    property list<QtObject> pNonFixedTopbarItems: [\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: true\r\n"
            "            pWidth: iBackBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iBackBtn.visible = visibility\r\n"
            "            }\r\n"
            "        },\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: true\r\n"
            "            pWidth: iHomeBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iHomeBtn.visible = visibility\r\n"
            "            }\r\n"
            "        },\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: true\r\n"
            "            pWidth: iRecentBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iRecentBtn.visible = visibility\r\n"
            "            }\r\n"
            "        }\r\n"
            "    ]\r\n"
            "    Row {\r\n"
            "        UiImageButton {\r\n"
            "            id: iSidebarShowBtn\r\n"
            "            asset: \"TitlebarCollapse\"\r\n"
            "        }\r\n"
            "        UiImageButton {\r\n"
            "            id: iBackBtn\r\n"
            "        }\r\n"
            "    }\r\n"
            "}\r\n");
        // Current player: the sidebar is a sibling of the title bar inside the
        // main content rectangle.
        const QString anchoredPlr = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iAppwin\r\n"
            "    property int pSidebarWidth: 32\r\n"
            "    PlrTopbar {\r\n"
            "        id: iTopbar\r\n"
            "    }\r\n"
            "    PlrSidebar {\r\n"
            "        id: iSidebar\r\n"
            "        anchors.bottom: parent.bottom\r\n"
            "        anchors.right: parent.right\r\n"
            "        anchors.top: iTopbar.bottom\r\n"
            "        color: uiBackend.oem === \"msi5\" ? UiTheme.colors.primary70 : UiTheme.colors.primary80\r\n"
            "        width: pSidebarWidth\r\n"
            "        visible: uiBackend.confGetInstanceBool(\"show_sidebar\")\r\n"
            "        onSidebarSettingClicked: fOpenSettingsWindow()\r\n"
            "    }\r\n"
            "    function fSetUiParams(sidebarVisible) {\r\n"
            "        plrBackend.setBackendUiParams(\r\n"
            "                    pBorderWidth + adsBackend.pExtendedBannerAdWidth,\r\n"
            "                    iTopbar.height + pBorderWidth,\r\n"
            "                    pBorderWidth + (sidebarVisible ? iAppwin.pSidebarWidth : 0),\r\n"
            "                    pBorderWidth,\r\n"
            "                    7 * iTopbar.height + 50 + pBorderWidth * 2)\r\n"
            "    }\r\n"
            "}\r\n");
        // Old 5.10/5.12 player: a column root, the title bar and a row layout
        // are siblings, and the sidebar is an item of that row layout.
        const QString legacyPlr = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iAppwin\r\n"
            "    property int pSidebarWidth: 32\r\n"
            "    UiColumnLayout {\r\n"
            "        anchors.fill: parent\r\n"
            "        PlrTopbar {\r\n"
            "            id: iTopbar\r\n"
            "        }\r\n"
            "        UiRowLayout {\r\n"
            "            id: iMainContentLayout\r\n"
            "            Loader {\r\n"
            "                id: iBootLoader\r\n"
            "                Layout.fillWidth: true\r\n"
            "            }\r\n"
            "            PlrSidebar {\r\n"
            "                id: iSidebar\r\n"
            "                color: uiBackend.oem === \"msi5\" ? UiTheme.colors.primary70 : UiTheme.colors.primary80\r\n"
            "                Layout.preferredWidth: pSidebarWidth\r\n"
            "                Layout.fillHeight: true\r\n"
            "                visible: uiBackend.confGetInstanceBool(\"show_sidebar\")\r\n"
            "                onSidebarSettingClicked: fOpenSettingsWindow()\r\n"
            "            }\r\n"
            "        }\r\n"
            "    }\r\n"
            "    function fSetUiParams(sidebarVisible) {\r\n"
            "        plrBackend.setBackendUiParams(\r\n"
            "                    pBorderWidth + adsBackend.pExtendedBannerAdWidth,\r\n"
            "                    iTopbar.height + pBorderWidth,\r\n"
            "                    pBorderWidth + (sidebarVisible ? iAppwin.pSidebarWidth : 0),\r\n"
            "                    pBorderWidth,\r\n"
            "                    7 * iTopbar.height + 50 + pBorderWidth * 2)\r\n"
            "    }\r\n"
            "}\r\n");

        // The inserted title-bar copy must sit inside the PlrTopbar body (it is
        // appended just before the closing brace so it paints above the bar).
        const auto insideTitlebar = [](const QString &text) {
            const int topbarAt = text.indexOf(QLatin1String("PlrTopbar {"));
            const int overlayAt = text.indexOf(QLatin1String("iSidebarTitlebar"));
            if (topbarAt < 0 || overlayAt < 0) return false;
            int depth = 0;
            for (int i = text.indexOf(QLatin1Char('{'), topbarAt); i < text.size(); ++i) {
                if (text.at(i) == QLatin1Char('{')) ++depth;
                else if (text.at(i) == QLatin1Char('}') && --depth == 0) return overlayAt < i;
            }
            return false;
        };

        const QString id = QStringLiteral("sidebar-to-titlebar");
        const QString smartId = QStringLiteral("responsive-sidebar");
        check(LayoutPatcher::tweaks().size() == 3
                  && LayoutPatcher::tweaks().front().id == id
                  && LayoutPatcher::tweaks().at(1).id == QStringLiteral("clean-player-ui")
                  && LayoutPatcher::tweaks().last().id == smartId,
              "the layout family offers the clean player UI plus responsive overflow for the stock sidebar and title bar");

        auto r = resources({ { QStringLiteral("plr.qml"), anchoredPlr },
                             { QStringLiteral("PlrSidebar.qml"), sidebar },
                             { QStringLiteral("PlrTopbar.qml"), topbar } }, true,
                           { QStringLiteral("PlrTopbar.qml") });
        addRegistration(r, false);
        const QByteArray original = r.f.bytes;
        const auto stock = LayoutPatcher::analyze(original, id);
        check(stock.supported && !stock.applied, "the stock anchored player shape is recognized as supported and unpatched");
        check(LayoutPatcher::analyze(original, smartId).supported
                  && !LayoutPatcher::analyze(original, smartId).applied,
              "smart overflow is available on the stock vertical sidebar");
        check(!LayoutPatcher::analyze(original, QStringLiteral("no-such-tweak")).supported,
              "an unknown tweak id is unsupported instead of guessed at");
        const QString smartPath = temp.filePath(QStringLiteral("layout-smart.exe"));
        write(smartPath, original);
        const auto smartApplied = LayoutPatcher::apply(smartPath, smartId, true);
        if (!smartApplied.ok) std::printf("    %s\n", qPrintable(smartApplied.msg));
        check(smartApplied.ok && LayoutPatcher::analyze(read(smartPath), smartId).applied,
              "stock sidebar can turn on responsive overflow without moving buttons");
        const QString smartSidebar = resourceText(read(smartPath), r, QStringLiteral("PlrSidebar.qml"));
        check(smartSidebar.contains(QLatin1String("var available = iSidebar.height"))
                  && smartSidebar.contains(QLatin1String("var cutoff = -1"))
                  && smartSidebar.contains(QLatin1String("fBSIPopupPoint(soundWin, pSound).y"))
                  && smartSidebar.contains(QLatin1String("fBSIPopupPoint(menuWin, null).y"))
                  && smartSidebar.contains(QLatin1String("fBSIPopupPoint(iSoundPopup, iSoundPopup.pBSIAnchor).x"))
                  && smartSidebar.contains(QLatin1String("fBSIInOverflow(model.modelData)")),
              "stock layout fits enabled buttons, sends only overflow to kebab, and anchors panels below their trigger");
        check(LayoutPatcher::apply(smartPath, id, true).ok,
              "the responsive stock sidebar can move to the title bar");
        const auto movedSmart = resourceText(read(smartPath), r, QStringLiteral("PlrSidebar.qml"));
        check(movedSmart.contains(QLatin1String("var available = iSidebar.width"))
                  && !movedSmart.contains(QLatin1String("var available = iSidebar.height"))
                  && movedSmart.contains(QLatin1String("iBSISidebarLayout.leftPadding"))
                  && !movedSmart.contains(QLatin1String("stock overflow only")),
              "an independently enabled smart overflow budget follows the title-bar width");
        check(LayoutPatcher::apply(smartPath, id, false).ok
                  && resourceText(read(smartPath), r, QStringLiteral("PlrSidebar.qml")) == smartSidebar
                  && LayoutPatcher::analyze(read(smartPath), smartId).applied,
              "restoring the sidebar keeps a separately enabled responsive fix, byte-for-byte");
        check(LayoutPatcher::apply(smartPath, smartId, false).ok
                  && resourceText(read(smartPath), r, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(read(smartPath), r, QStringLiteral("plr.qml")) == anchoredPlr,
              "turning off the responsive fix restores the original stock QML byte-for-byte");
        const QString path = temp.filePath(QStringLiteral("layout.exe"));
        write(path, original);
        const auto applied = LayoutPatcher::apply(path, id, true);
        if (!applied.ok) std::printf("    %s\n", qPrintable(applied.msg));
        check(applied.ok, "the sidebar-to-titlebar tweak applies to the current player shape");
        const auto patchedPlr = resourceText(read(path), r, QStringLiteral("plr.qml"));
        check(patchedPlr.contains(QLatin1String("width: 0"))
                  && !patchedPlr.contains(QLatin1String("width: pSidebarWidth")),
              "the side strip shrinks to zero width instead of staying visible over the title bar");
        check(patchedPlr.contains(QLatin1String("anchors.top: iTopbar.bottom"))
                  && patchedPlr.contains(QLatin1String("anchors.right: parent.right")),
              "the stock content-area anchors are left alone so the game view stays full width");
        check(patchedPlr.contains(QLatin1String("id: iSidebarTitlebar"))
                  && patchedPlr.contains(QLatin1String("anchors.fill: parent"))
                  && insideTitlebar(patchedPlr),
              "a transparent host is inserted inside the title bar");
        check(QmlText::objects(QmlText::mask(patchedPlr), QStringLiteral("PlrSidebar")).size() == 1
                  && patchedPlr.contains(QStringLiteral("iSidebar.parent = iSidebarTitlebar")),
              "one original sidebar owns the controls, backend connections and animations");
        check(patchedPlr.contains(QStringLiteral("anchors.leftMargin: Math.min(144, parent.width / 4)"))
                  && patchedPlr.contains(QStringLiteral("anchors.rightMargin: Math.min(144, parent.width / 4)")),
              "the moved row reserves space for title-bar controls instead of covering them");

        // Execute the actual injected mount QML, not a reimplementation. The
        // original sidebar is deliberately declared AFTER its host, as in plr.
        QString hostQml;
        for (const auto &item : QmlText::objects(QmlText::mask(patchedPlr), QStringLiteral("Item"))) {
            const QString body = patchedPlr.mid(item.nameBegin, item.end - item.nameBegin);
            if (body.contains(QStringLiteral("id: iSidebarTitlebar"))) hostQml = body;
        }
        QQmlEngine mountEngine;
        QQmlComponent mountComponent(&mountEngine);
        const QString mountSource = QStringLiteral(
            "import QtQuick 2.15\n"
            "Item { id: scene; width: 1000; height: 600\n"
            " Rectangle { id: iTopbar; width: parent.width; height: 32\n")
            + hostQml + QStringLiteral(
            "\n }\n"
            " Rectangle { id: iSidebar; objectName: 'movedSidebar'; width: 0\n"
            "  anchors.right: parent.right; anchors.bottom: parent.bottom; anchors.top: iTopbar.bottom\n"
            "  property int resets: 0\n"
            "  function fResetSidebar() { resets += 1 }\n"
            "  Rectangle { objectName: 'volumeButton'; x: 40; width: 32; height: 32 }\n"
            " }\n}\n");
        mountComponent.setData(mountSource.toUtf8(), QUrl());
        std::unique_ptr<QObject> mounted(mountComponent.create());
        check(mounted != nullptr, "the single-owner title-bar mount compiles as real QML");
        if (mounted) {
            auto *scene = qobject_cast<QQuickItem *>(mounted.get());
            auto *moved = mounted->findChild<QQuickItem *>(QStringLiteral("movedSidebar"));
            auto *button = mounted->findChild<QQuickItem *>(QStringLiteral("volumeButton"));
            // Flush the deferred mount and the subsequent overflow recalculation.
            for (int i = 0; i < 4; ++i) QCoreApplication::processEvents();
            check(moved && moved->width() == 712 && moved->height() == 32
                      && moved->mapToItem(scene, QPointF()) == QPointF(144, 0),
                  "the original controls use title-bar geometry, not the old side strip");
            check(button && button->mapToItem(scene, QPointF()) == QPointF(184, 0),
                  "button coordinates for popup placement follow the moved top-bar button");
            scene->setWidth(600);
            for (int i = 0; i < 4; ++i) QCoreApplication::processEvents();
            check(moved && moved->width() == 312 && moved->property("resets").toInt() >= 1,
                  "resizing updates the available width and refreshes the overflow budget");
            check(mounted->findChildren<QQuickItem *>(QStringLiteral("movedSidebar")).size() == 1,
                  "mounting and resizing never create another animated sidebar instance");
        }
        check(patchedPlr.contains(QLatin1String("color: \"transparent\""))
                  && patchedPlr.contains(QStringLiteral("pBorderWidth + 0"))
                  && !patchedPlr.contains(QLatin1String("sidebarVisible ? iAppwin.pSidebarWidth")),
              "the zero-width strip is transparent and the Android frame stops reserving it");
        const auto patchedSidebar = resourceText(read(path), r, QStringLiteral("PlrSidebar.qml"));
        check(patchedSidebar.contains(QLatin1String("UiRowLayout"))
                  && !patchedSidebar.contains(QLatin1String("UiColumnLayout"))
                  && patchedSidebar.contains(QLatin1String("anchors.centerIn: parent")),
              "the button column becomes a row centered inside the title bar");
        check(patchedSidebar.contains(QLatin1String("visible: iSidebar.width > 0")),
              "the zero-width original hides its row instead of painting outside the strip");
        check(patchedSidebar.contains(QLatin1String("onWidthChanged"))
                  && !patchedSidebar.contains(QLatin1String("onHeightChanged"))
                  && !patchedSidebar.contains(QLatin1String("iSidebar.height"))
                  && patchedSidebar.contains(QLatin1String("var available = iSidebar.width"))
                  && patchedSidebar.contains(QLatin1String("iKebabMenuBtn.visible = overflow && capacity > 0")),
              "the kebab appears only when enabled buttons exceed the width budget");
        check(patchedSidebar.contains(QLatin1String("var cutoff = -1"))
                  && patchedSidebar.contains(QLatin1String("pVisibleSidebarElementsCount = cutoff + 1"))
                  && patchedSidebar.contains(QLatin1String("fBSIInOverflow(model.modelData)"))
                  && patchedSidebar.contains(QLatin1String("return !delegate || !delegate.visible")),
              "disabled items do not consume a slot or hide later buttons from the overflow menu");
        check(patchedSidebar.contains(QLatin1String("fBSIPopupPoint(soundWin, pSound).x"))
                  && patchedSidebar.contains(QLatin1String("fBSIPopupPoint(soundWin, pSound).y"))
                  && patchedSidebar.contains(QLatin1String("fBSIPopupPoint(menuWin, null).y"))
                  && patchedSidebar.contains(QLatin1String("iBSISidebarButtons.itemAt(i)"))
                  && patchedSidebar.contains(QLatin1String("button.mapToGlobal(button.width / 2, button.height)")),
              "sound and other panels follow their real button (or the three dots), below it");
        // Execute the injected QML itself with real QtQuick items: a disabled
        // model entry must not steal a slot, and popup coordinates must come
        // from the delegate actually under the sound button (or the kebab).
        const int helperAt = patchedSidebar.indexOf(QStringLiteral("    // BSI CUSTOMIZER: responsive sidebar begin"));
        const int helperEnd = patchedSidebar.indexOf(QStringLiteral("    // BSI CUSTOMIZER: responsive sidebar end"), helperAt);
        check(helperAt >= 0 && helperEnd > helperAt, "responsive overflow and popup helpers are present in the patched source");
        if (helperAt >= 0 && helperEnd > helperAt) {
            const QString helpers = patchedSidebar.mid(helperAt, helperEnd - helperAt
                + QStringLiteral("    // BSI CUSTOMIZER: responsive sidebar end").size());
            // The sidebar is its own component, exactly like the real
            // PlrSidebar.qml: QML resolves an unqualified property name
            // through the component root, so a plain nested Item would make
            // pAllSidebarItems/pVisibleSidebarElementsCount invisible to the
            // generated helpers and to the Repeater's model/delegate.
            const QString testQml = QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Window 2.15
Window {
    id: iAppwin
    visible: false
    width: 900; height: 600
    component MockSidebar: Item {
        id: iSidebar
        objectName: "responsiveBar"
        x: 144; width: 288; height: 32
        property int pSidebarElementSize: 32
        property int pVisibleSidebarElementsCount: 0
        property bool soundOverflow: false
        property string pRecent: "recent"
        property string pHome: "home"
        property string pBack: "back"
        property string pSettings: "settings"
        property list<QtObject> pAllSidebarItems: [
            QtObject { property string pElementId: "first"; property bool pIsFeatureEnable: true; property bool pShowViaRepaterOnly: true; property bool pShowOnKebabMenu: false },
            QtObject { property string pElementId: "disabled"; property bool pIsFeatureEnable: false; property bool pShowViaRepaterOnly: true; property bool pShowOnKebabMenu: false },
            QtObject { property string pElementId: "sound"; property bool pIsFeatureEnable: true; property bool pShowViaRepaterOnly: true; property bool pShowOnKebabMenu: false },
            QtObject { property string pElementId: "third"; property bool pIsFeatureEnable: true; property bool pShowViaRepaterOnly: true; property bool pShowOnKebabMenu: false },
            QtObject { property string pElementId: "fourth"; property bool pIsFeatureEnable: true; property bool pShowViaRepaterOnly: true; property bool pShowOnKebabMenu: false },
            QtObject { property string pElementId: "fifth"; property bool pIsFeatureEnable: true; property bool pShowViaRepaterOnly: true; property bool pShowOnKebabMenu: false }
        ]
        Item {
            id: iBSISidebarLayout
            property int spacing: 0
            property int leftPadding: 0
            property int rightPadding: 0
            Repeater {
                id: iBSISidebarButtons
                model: iSidebar.pAllSidebarItems
                delegate: Rectangle {
                    objectName: modelData.pElementId
                    x: index * 32
                    width: 32; height: 32
                    visible: index < iSidebar.pVisibleSidebarElementsCount && modelData.pIsFeatureEnable
                }
            }
        }
        Rectangle { id: iKebabMenuBtn; objectName: "threeDots"; x: iSidebar.pVisibleSidebarElementsCount * 32; width: 32; height: 32 }
        Rectangle { id: iCollapseBtn; visible: false; width: 32; height: 32 }
        Rectangle { id: iSettingsBtn; width: 32; height: 32 }
        Rectangle { id: iBackBtn; width: 32; height: 32 }
        Rectangle { id: iHomeBtn; width: 32; height: 32 }
        Rectangle { id: iRecentBtn; width: 32; height: 32 }
        QtObject {
            id: mockPopup
            objectName: "soundPanel"
            property real x: 0; property real y: 0
            property real width: 100; property real height: 90
            property int shadowThickness: 4
            property var pBSIAnchor: undefined
            property var screen: null
        }
        function probeSound() {
            var point = fBSIPopupPoint(mockPopup, "sound")
            mockPopup.x = point.x; mockPopup.y = point.y
        }
        function probeSoundModel() {
            // Some legacy and current player builds pass the model QObject
            // (pSound) to their popup helper instead of its element-id string.
            var point = fBSIPopupPoint(mockPopup, pAllSidebarItems[2])
            mockPopup.x = point.x; mockPopup.y = point.y
        }
        function probeMenu() {
            var point = fBSIPopupPoint(mockPopup, null)
            mockPopup.x = point.x; mockPopup.y = point.y
        }
        function soundIsOverflow() { soundOverflow = fBSIInOverflow(pAllSidebarItems[2]) }
)QML") + helpers + QStringLiteral("\n    }\n    MockSidebar { }\n}\n");
            QQmlEngine runtimeEngine;
            QQmlComponent runtime(&runtimeEngine);
            runtime.setData(testQml.toUtf8(), QUrl());
            std::unique_ptr<QObject> window(runtime.create());
            if (!window) for (const auto &error : runtime.errors())
                std::printf("    layout QML: %s\n", qPrintable(error.toString()));
            check(window != nullptr, "the injected responsive sidebar helpers compile in QtQuick");
            if (window) {
                auto *bar = window->findChild<QQuickItem *>(QStringLiteral("responsiveBar"));
                auto *dots = window->findChild<QQuickItem *>(QStringLiteral("threeDots"));
                auto *panel = window->findChild<QObject *>(QStringLiteral("soundPanel"));
                // A Repeater's delegates are visual children without a QObject
                // parent, so findChild() cannot reach them: walk the item tree.
                for (int i = 0; i < 4; ++i) QCoreApplication::processEvents();
                QQuickItem *sound = nullptr;
                QList<QQuickItem *> pending { bar };
                while (!pending.isEmpty() && !sound) {
                    QQuickItem *item = pending.takeFirst();
                    if (!item) continue;
                    if (item->objectName() == QLatin1String("sound")) sound = item;
                    pending += item->childItems();
                }
                if (bar && dots && sound && panel) {
                    bar->setWidth(288); // five enabled repeatable + four fixed = nine 32px slots
                    QMetaObject::invokeMethod(bar, "fBSIUpdateOverflow");
                    QCoreApplication::processEvents();
                    check(!dots->isVisible() && bar->property("pVisibleSidebarElementsCount").toInt() == 6
                              && sound->isVisible(),
                          "no three dots at exact fit, including a disabled model entry in the index cutoff");
                    QMetaObject::invokeMethod(bar, "probeSound");
                    const QPointF soundBottom = sound->mapToGlobal(QPointF(sound->width() / 2, sound->height()));
                    check(qAbs(panel->property("y").toReal() - (soundBottom.y() - 4)) < 1,
                          "the sound panel opens flush below its own visible top-bar button");
                    const QPointF soundCenter = sound->mapToGlobal(
                        QPointF(sound->width() / 2, sound->height() / 2));
                    const qreal expectedSoundX = soundCenter.x() - panel->property("width").toReal() / 2;
                    QMetaObject::invokeMethod(bar, "probeSoundModel");
                    check(std::isfinite(panel->property("x").toReal())
                              && qAbs(panel->property("x").toReal() - expectedSoundX) < 1
                              && qAbs(panel->property("y").toReal() - (soundBottom.y() - 4)) < 1,
                          "a model-object popup anchor resolves to the actual volume button, not the window origin");
                    bar->setWidth(256);
                    QMetaObject::invokeMethod(bar, "fBSIUpdateOverflow");
                    QCoreApplication::processEvents();
                    check(dots->isVisible() && bar->property("pVisibleSidebarElementsCount").toInt() == 4
                              && sound->isVisible(),
                          "one slot lost triggers overflow without hiding earlier enabled buttons");
                    bar->setWidth(32);
                    QMetaObject::invokeMethod(bar, "fBSIUpdateOverflow");
                    QCoreApplication::processEvents();
                    QMetaObject::invokeMethod(bar, "soundIsOverflow");
                    check(dots->isVisible() && !sound->isVisible()
                              && bar->property("soundOverflow").toBool(),
                          "a hidden sound button moves into the three-dot menu even if its stock menu flag is false");
                    QMetaObject::invokeMethod(bar, "probeSound");
                    const QPointF dotsBottom = dots->mapToGlobal(QPointF(dots->width() / 2, dots->height()));
                    check(qAbs(panel->property("y").toReal() - (dotsBottom.y() - 4)) < 1,
                          "the overflowed sound panel opens flush below the three-dot button");
                    bar->setWidth(288);
                    QMetaObject::invokeMethod(bar, "fBSIUpdateOverflow");
                    QCoreApplication::processEvents();
                    check(!dots->isVisible() && sound->isVisible(),
                          "resizing larger restores every fitting button and hides the three dots again");
                } else check(false, "the responsive QML exposes its panel, buttons and three-dot menu");
            }
        }
        const auto patchedTopbar = resourceText(read(path), r, QStringLiteral("PlrTopbar.qml"));
        check(patchedTopbar.count(QStringLiteral("visible: false")) == 1
                  && !patchedTopbar.contains(QLatin1String("iBackBtn.visible: false")),
              "the show-sidebar button is disabled without touching the toggled Back entry");
        check(patchedTopbar.count(QStringLiteral("pVisible: false")) == 3
                  && patchedTopbar.count(QStringLiteral("pVisible: true")) == 0,
              "the duplicated Back/Home/Recent entries leave the title bar");
        // An older responsive helper was shipped before the fixed-button
        // model relationship and unmodelled Discord were accounted for. The
        // version marker upgrades it in place without changing the ownership
        // of an independently selected stock-sidebar option.
        const auto olderResponsive = [](QString qml) {
            qml.replace(QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin [titlebar] [v2]"),
                        QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin [titlebar]"));
            qml.replace(QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin [v2]"),
                        QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin"));
            return qml;
        };
        auto earlierStock = resources({ { QStringLiteral("plr.qml"), anchoredPlr },
                                        { QStringLiteral("PlrSidebar.qml"), olderResponsive(smartSidebar) },
                                        { QStringLiteral("PlrTopbar.qml"), topbar } }, true, {});
        addRegistration(earlierStock, false);
        const QString earlierStockPath = temp.filePath(QStringLiteral("layout-earlier-smart.exe"));
        write(earlierStockPath, earlierStock.f.bytes);
        const auto earlierStockState = LayoutPatcher::analyze(read(earlierStockPath), smartId);
        check(earlierStockState.supported && earlierStockState.applied && earlierStockState.needsUpdate,
              "the stock sidebar offers Update for an older responsive helper");
        check(LayoutPatcher::apply(earlierStockPath, smartId, true).ok
                  && resourceText(read(earlierStockPath), earlierStock, QStringLiteral("PlrSidebar.qml")) == smartSidebar,
              "updating the stock sidebar replaces the old helper in place");
        check(LayoutPatcher::apply(earlierStockPath, smartId, false).ok
                  && resourceText(read(earlierStockPath), earlierStock, QStringLiteral("PlrSidebar.qml")) == sidebar,
              "the upgraded stock sidebar still restores the original QML");
        auto earlierTitlebar = resources({ { QStringLiteral("plr.qml"), patchedPlr },
                                           { QStringLiteral("PlrSidebar.qml"), olderResponsive(patchedSidebar) },
                                           { QStringLiteral("PlrTopbar.qml"), patchedTopbar } }, true, {});
        addRegistration(earlierTitlebar, false);
        const QString earlierTitlebarPath = temp.filePath(QStringLiteral("layout-earlier-titlebar.exe"));
        write(earlierTitlebarPath, earlierTitlebar.f.bytes);
        const auto earlierTitlebarState = LayoutPatcher::analyze(read(earlierTitlebarPath), id);
        check(earlierTitlebarState.supported && earlierTitlebarState.applied && earlierTitlebarState.needsUpdate,
              "an older title-bar helper offers an in-place Update");
        check(LayoutPatcher::apply(earlierTitlebarPath, id, true).ok
                  && resourceText(read(earlierTitlebarPath), earlierTitlebar, QStringLiteral("PlrSidebar.qml")) == patchedSidebar,
              "Update installs the current helper without moving the sidebar twice");
        check(LayoutPatcher::apply(earlierTitlebarPath, id, false).ok
                  && resourceText(read(earlierTitlebarPath), earlierTitlebar, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(read(earlierTitlebarPath), earlierTitlebar, QStringLiteral("plr.qml")) == anchoredPlr,
              "the updated title bar restores every original document");
        // The intermediate release added a line break after the Collapse
        // comment but did not version that comment. Even an inline button
        // must still be restored exactly after its helper is upgraded.
        QString intermediateSidebar = olderResponsive(patchedSidebar);
        const QString currentCollapse = QStringLiteral(
            "visible: false // BSI CUSTOMIZER: no sidebar to collapse in the title bar [v2]\r\n");
        const QString intermediateCollapse = QStringLiteral(
            "visible: false // BSI CUSTOMIZER: no sidebar to collapse in the title bar\r\n");
        check(intermediateSidebar.count(currentCollapse) == 1,
              "the inline Collapse fixture contains the versioned, terminated comment");
        intermediateSidebar.replace(currentCollapse, intermediateCollapse);
        auto intermediate = resources({ { QStringLiteral("plr.qml"), patchedPlr },
                                         { QStringLiteral("PlrSidebar.qml"), intermediateSidebar },
                                         { QStringLiteral("PlrTopbar.qml"), patchedTopbar } }, true, {});
        addRegistration(intermediate, false);
        const QString intermediatePath = temp.filePath(QStringLiteral("layout-intermediate.exe"));
        write(intermediatePath, intermediate.f.bytes);
        check(LayoutPatcher::analyze(read(intermediatePath), id).needsUpdate
                  && LayoutPatcher::apply(intermediatePath, id, true).ok,
              "the intermediate unversioned Collapse marker permits a responsive-helper Update");
        check(LayoutPatcher::apply(intermediatePath, id, false).ok
                  && resourceText(read(intermediatePath), intermediate, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(read(intermediatePath), intermediate, QStringLiteral("plr.qml")) == anchoredPlr,
              "the intermediate inline Collapse marker restores its original line ending exactly");
        auto earlierManual = resources({ { QStringLiteral("plr.qml"), patchedPlr },
                                         { QStringLiteral("PlrSidebar.qml"), olderResponsive(movedSmart) },
                                         { QStringLiteral("PlrTopbar.qml"), patchedTopbar } }, true, {});
        addRegistration(earlierManual, false);
        const QString earlierManualPath = temp.filePath(QStringLiteral("layout-earlier-manual.exe"));
        write(earlierManualPath, earlierManual.f.bytes);
        const auto manualState = LayoutPatcher::analyze(read(earlierManualPath), id);
        const auto manualUpgrade = LayoutPatcher::apply(earlierManualPath, id, true);
        const auto manualMoved = resourceText(read(earlierManualPath), earlierManual, QStringLiteral("PlrSidebar.qml"));
        check(manualState.needsUpdate && manualUpgrade.ok
                  && manualMoved.contains(QLatin1String("responsive sidebar begin [v2]"))
                  && !manualMoved.contains(QLatin1String("responsive sidebar begin [titlebar]"))
                  && manualMoved.contains(QLatin1String("var available = iSidebar.width - (iBSISidebarLayout.leftPadding")),
              "an older manual helper is updated for horizontal space without becoming title-bar-owned");
        const auto manualRestore = LayoutPatcher::apply(earlierManualPath, id, false);
        const auto manualStock = resourceText(read(earlierManualPath), earlierManual, QStringLiteral("PlrSidebar.qml"));
        const auto manualSmartState = LayoutPatcher::analyze(read(earlierManualPath), smartId);
        check(manualRestore.ok && manualSmartState.supported && manualSmartState.applied
                  && !manualSmartState.needsUpdate
                  && manualStock.contains(QLatin1String("var available = iSidebar.height - (iBSISidebarLayout.topPadding")),
              "restoring that title bar keeps the independent smart sidebar with vertical spacing");
        check(LayoutPatcher::apply(earlierManualPath, smartId, false).ok
                  && resourceText(read(earlierManualPath), earlierManual, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(read(earlierManualPath), earlierManual, QStringLiteral("plr.qml")) == anchoredPlr,
              "the upgraded manual helper can also be removed to restore the original QML byte-for-byte");
        // Upgrade an EXE patched by the previous duplicate-instance version.
        // That version had the horizontal row but no responsive/popup fix.
        QString oldSidebar = sidebar;
        oldSidebar.replace(QStringLiteral("onHeightChanged"), QStringLiteral("onWidthChanged"));
        oldSidebar.replace(QStringLiteral("iSidebar.height"), QStringLiteral("iSidebar.width"));
        oldSidebar.replace(QStringLiteral("    UiColumnLayout {\r\n        anchors.fill: parent"),
                           QStringLiteral("    UiRowLayout {\nvisible: iSidebar.width > 0\r\n        anchors.centerIn: parent"));
        const QString oldHost = QStringLiteral(
            "\r\n        PlrSidebar {\r\n"
            "            // BSI CUSTOMIZER: sidebar buttons over the title bar\r\n"
            "            id: iSidebarTitlebar\r\n"
            "            anchors.fill: parent\r\n"
            "            color: \"transparent\"\r\n"
            "            visible: true\r\n"
            "            onSidebarSettingClicked: fOpenSettingsWindow()\r\n"
            "        }");
        QString oldPlr = patchedPlr;
        const int hostAt = oldPlr.indexOf(hostQml);
        check(hostAt >= 0, "the generated host can be located for the upgrade fixture");
        if (hostAt >= 0) {
            // hostQml starts at Item, after the preceding newline + indentation.
            oldPlr.replace(QStringLiteral("\r\n        ") + hostQml, oldHost);
            auto previous = resources({ { QStringLiteral("plr.qml"), oldPlr },
                                        { QStringLiteral("PlrSidebar.qml"), oldSidebar },
                                        { QStringLiteral("PlrTopbar.qml"), patchedTopbar } }, true, {});
            addRegistration(previous, false);
            const QString previousPath = temp.filePath(QStringLiteral("layout-upgrade.exe"));
            write(previousPath, previous.f.bytes);
            const auto previousState = LayoutPatcher::analyze(previous.f.bytes, id);
            check(previousState.supported && previousState.applied,
                  "previous duplicate-instance patches remain recognized");
            check(LayoutPatcher::apply(previousPath, id, true).ok,
                  "Apply upgrades an already-enabled old layout instead of returning a no-op");
            const QString upgraded = resourceText(read(previousPath), previous, QStringLiteral("plr.qml"));
            check(QmlText::objects(QmlText::mask(upgraded), QStringLiteral("PlrSidebar")).size() == 1
                      && upgraded == patchedPlr,
                  "the upgrade removes the duplicate and writes the single-owner layout");
            check(LayoutPatcher::apply(previousPath, id, false).ok
                      && resourceText(read(previousPath), previous, QStringLiteral("plr.qml")) == anchoredPlr
                      && resourceText(read(previousPath), previous, QStringLiteral("PlrSidebar.qml")) == sidebar,
                  "the upgraded version still restores the original stock documents exactly");
        }
        auto oldSingle = resources({ { QStringLiteral("plr.qml"), patchedPlr },
                                     { QStringLiteral("PlrSidebar.qml"), oldSidebar },
                                     { QStringLiteral("PlrTopbar.qml"), patchedTopbar } }, true, {});
        addRegistration(oldSingle, false);
        const QString oldSinglePath = temp.filePath(QStringLiteral("layout-old-single.exe"));
        write(oldSinglePath, oldSingle.f.bytes);
        const auto oldState = LayoutPatcher::analyze(read(oldSinglePath), id);
        check(oldState.supported && oldState.applied && oldState.needsUpdate,
              "earlier single-owner title bars offer an in-place overflow/popup update");
        check(LayoutPatcher::apply(oldSinglePath, id, true).ok
                  && resourceText(read(oldSinglePath), oldSingle, QStringLiteral("PlrSidebar.qml")) == patchedSidebar
                  && !LayoutPatcher::analyze(read(oldSinglePath), id).needsUpdate,
              "Update adds smart overflow and anchored panels without changing the existing title bar");
        check(LayoutPatcher::apply(oldSinglePath, id, false).ok
                  && resourceText(read(oldSinglePath), oldSingle, QStringLiteral("PlrSidebar.qml")) == sidebar,
              "an updated earlier title bar still restores the stock sidebar byte-for-byte");
        const QByteArray afterFirst = read(path);
        const auto again = LayoutPatcher::apply(path, id, true);
        check(again.ok && read(path) == afterFirst
                  && resourceText(read(path), r, QStringLiteral("plr.qml")) == patchedPlr,
              "applying an already-applied tweak is a no-op");
        const auto patchedState = LayoutPatcher::analyze(read(path), id);
        check(patchedState.supported && patchedState.applied, "the patched player reports the tweak as applied");
        check(LayoutPatcher::apply(path, id, false).ok, "turning the option off restores the stock layout");
        const QByteArray restoredBytes = read(path);
        check(resourceText(restoredBytes, r, QStringLiteral("plr.qml")) == anchoredPlr
                  && resourceText(restoredBytes, r, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(restoredBytes, r, QStringLiteral("PlrTopbar.qml")) == topbar,
              "the restore writes every edited document back byte-for-byte");
        const auto restored = LayoutPatcher::analyze(read(path), id);
        check(restored.supported && !restored.applied, "the restored player reports stock again");

        // ---- a build that spells the palette, the sidebar width and the
        // Android-frame inset differently, and has no sidebar toggle. Both
        // layout options used to report "Unavailable" together because the
        // stock shape was recognized by exact strings. ----
        QString variantPlr = anchoredPlr;
        variantPlr.replace(
            QStringLiteral("color: uiBackend.oem === \"msi5\" ? UiTheme.colors.primary70 : UiTheme.colors.primary80"),
            QStringLiteral("color: UiTheme.colors.sidebarSurface"));
        variantPlr.replace(QStringLiteral("width: pSidebarWidth"),
                           QStringLiteral("width: iAppwin.pSidebarWidth"));
        variantPlr.replace(
            QStringLiteral("pBorderWidth + (sidebarVisible ? iAppwin.pSidebarWidth : 0)"),
            QStringLiteral("pBorderWidth + (sidebarVisible ? Math.round(pSidebarWidth) : 0)"));
        QString variantTopbar = topbar;
        variantTopbar.replace(QStringLiteral("        UiImageButton {\r\n"
                                             "            id: iSidebarShowBtn\r\n"
                                             "            asset: \"TitlebarCollapse\"\r\n"
                                             "        }\r\n"),
                              QString());
        auto variant = resources({ { QStringLiteral("plr.qml"), variantPlr },
                                   { QStringLiteral("PlrSidebar.qml"), sidebar },
                                   { QStringLiteral("PlrTopbar.qml"), variantTopbar } }, true, {});
        addRegistration(variant, false);
        const auto variantStock = LayoutPatcher::analyze(variant.f.bytes, id);
        check(variantStock.supported && !variantStock.applied,
              "a build with an unfamiliar palette, width and inset is recognized instead of disabled");
        check(LayoutPatcher::analyze(variant.f.bytes, smartId).supported,
              "responsive overflow is offered independently of the title-bar spelling");
        const QString variantPath = temp.filePath(QStringLiteral("layout-variant.exe"));
        write(variantPath, variant.f.bytes);
        const auto variantApplied = LayoutPatcher::apply(variantPath, id, true);
        if (!variantApplied.ok) std::printf("    %s\n", qPrintable(variantApplied.msg));
        const auto variantPatchedPlr = resourceText(read(variantPath), variant, QStringLiteral("plr.qml"));
        check(variantApplied.ok
                  && variantPatchedPlr.contains(QLatin1String("width: 0 /* BSI CUSTOMIZER: width original:"))
                  && variantPatchedPlr.contains(QLatin1String("color: \"transparent\" /* BSI CUSTOMIZER: color original:"))
                  && variantPatchedPlr.contains(QLatin1String("0 /* BSI CUSTOMIZER: inset original:"))
                  && variantPatchedPlr.contains(QStringLiteral("id: iSidebarTitlebar")),
              "the unfamiliar expressions are replaced and their originals kept in tags");
        check(LayoutPatcher::apply(variantPath, id, false).ok
                  && resourceText(read(variantPath), variant, QStringLiteral("plr.qml")) == variantPlr
                  && resourceText(read(variantPath), variant, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(read(variantPath), variant, QStringLiteral("PlrTopbar.qml")) == variantTopbar,
              "the unfamiliar build restores every document byte-for-byte from the tags");

        // ---- a sidebar that only carries the button strip: no three-dot
        // button, no Repeater model and no windowed popups. The title-bar move
        // is still well defined, but it used to be refused together with the
        // smart helper ("does not have the supported button strip, overflow
        // logic and popup anchors"). ----
        const QString plainSidebar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iSidebar\r\n"
            "    property int pSidebarElementSize: 32\r\n"
            "    onHeightChanged: fResetSidebar()\r\n"
            "    function fResetSidebar() {\r\n"
            "        var availableHeight = iSidebar.height\r\n"
            "        if (availableHeight >= iSoundBtn.height) iSoundBtn.visible = true\r\n"
            "    }\r\n"
            "    UiColumnLayout {\r\n"
            "        anchors.fill: parent\r\n"
            "        UiImageButton { id: iSoundBtn }\r\n"
            "        UiImageButton { id: iSettingsBtn }\r\n"
            "    }\r\n"
            "}\r\n");
        auto plain = resources({ { QStringLiteral("plr.qml"), anchoredPlr },
                                 { QStringLiteral("PlrSidebar.qml"), plainSidebar },
                                 { QStringLiteral("PlrTopbar.qml"), topbar } }, true, {});
        addRegistration(plain, false);
        const auto plainStock = LayoutPatcher::analyze(plain.f.bytes, id);
        check(plainStock.supported && !plainStock.applied,
              "a sidebar with only the button strip still offers the title-bar move");
        check(!LayoutPatcher::analyze(plain.f.bytes, smartId).supported,
              "that sidebar is not offered the smart overflow helper it cannot host");
        const QString plainPath = temp.filePath(QStringLiteral("layout-plain.exe"));
        write(plainPath, plain.f.bytes);
        const auto plainApplied = LayoutPatcher::apply(plainPath, id, true);
        if (!plainApplied.ok) std::printf("    %s\n", qPrintable(plainApplied.msg));
        const QString plainPatched = resourceText(read(plainPath), plain, QStringLiteral("PlrSidebar.qml"));
        check(plainApplied.ok
                  && plainPatched.contains(QLatin1String("UiRowLayout"))
                  && plainPatched.contains(QLatin1String("anchors.centerIn: parent"))
                  && plainPatched.contains(QLatin1String("visible: iSidebar.width > 0"))
                  && plainPatched.contains(QLatin1String("stock overflow only"))
                  && plainPatched.contains(QLatin1String("var availableHeight = iSidebar.width"))
                  && plainPatched.contains(QLatin1String("onWidthChanged")),
              "the strip becomes a centered row and keeps the build's own overflow code");
        const auto plainState = LayoutPatcher::analyze(read(plainPath), id);
        check(plainState.supported && plainState.applied && !plainState.needsUpdate,
              "the patched plain build reports applied without asking for an upgrade");
        check(resourceText(read(plainPath), plain, QStringLiteral("plr.qml"))
                  .contains(QStringLiteral("id: iSidebarTitlebar")),
              "the plain build gets the same single-owner title-bar host");
        check(LayoutPatcher::apply(plainPath, id, false).ok
                  && resourceText(read(plainPath), plain, QStringLiteral("PlrSidebar.qml")) == plainSidebar
                  && resourceText(read(plainPath), plain, QStringLiteral("plr.qml")) == anchoredPlr
                  && resourceText(read(plainPath), plain, QStringLiteral("PlrTopbar.qml")) == topbar,
              "the plain build restores every document byte-for-byte");

        // ---- old row-layout player (the shape that used to be disabled) ----
        auto legacy = resources({ { QStringLiteral("plr.qml"), legacyPlr },
                                  { QStringLiteral("PlrSidebar.qml"), sidebar },
                                  { QStringLiteral("PlrTopbar.qml"), topbar } }, true,
                                { QStringLiteral("PlrTopbar.qml") });
        addRegistration(legacy, false);
        const auto legacyStock = LayoutPatcher::analyze(legacy.f.bytes, id);
        check(legacyStock.supported && !legacyStock.applied,
              "the old row-layout player is recognized instead of being refused as unsafe");
        const QString legacyPath = temp.filePath(QStringLiteral("layout-legacy.exe"));
        write(legacyPath, legacy.f.bytes);
        const auto legacyApplied = LayoutPatcher::apply(legacyPath, id, true);
        if (!legacyApplied.ok) std::printf("    %s\n", qPrintable(legacyApplied.msg));
        check(legacyApplied.ok, "the tweak applies to the old row-layout player");
        const auto legacyPlrText = resourceText(read(legacyPath), legacy, QStringLiteral("plr.qml"));
        check(legacyPlrText.contains(QLatin1String("Layout.preferredWidth: 0"))
                  && !legacyPlrText.contains(QLatin1String("Layout.preferredWidth: pSidebarWidth")),
              "the old sidebar item takes no horizontal space once the layout is moved");
        check(legacyPlrText.contains(QLatin1String("id: iSidebarTitlebar"))
                  && insideTitlebar(legacyPlrText)
                  && legacyPlrText.contains(QStringLiteral("pBorderWidth + 0")),
              "the old player gets the same single-owner host and full-width Android frame");
        const auto legacySidebarText = resourceText(read(legacyPath), legacy, QStringLiteral("PlrSidebar.qml"));
        check(legacySidebarText.contains(QLatin1String("UiRowLayout"))
                  && legacySidebarText.contains(QLatin1String("visible: iSidebar.width > 0")),
              "the old sidebar component uses the same centered row");
        check(legacySidebarText.contains(QLatin1String("typeof elementId.mapToGlobal === 'function'"))
                  && legacySidebarText.contains(QLatin1String("items[i].pElementId === lookupId"))
                  && legacySidebarText.contains(QLatin1String("fBSIPopupPoint(soundWin, pSound).x")),
              "the old player also anchors each popup to its resolved button instead of the sidebar edge");
        const auto legacyState = LayoutPatcher::analyze(read(legacyPath), id);
        check(legacyState.supported && legacyState.applied, "the old patched player reports applied");
        check(LayoutPatcher::apply(legacyPath, id, false).ok, "the old row-layout player restores");
        const QByteArray legacyRestored = read(legacyPath);
        check(resourceText(legacyRestored, legacy, QStringLiteral("plr.qml")) == legacyPlr
                  && resourceText(legacyRestored, legacy, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(legacyRestored, legacy, QStringLiteral("PlrTopbar.qml")) == topbar,
              "the old player restores every edited document byte-for-byte");

        // ---- current player: filtered list, conditional AI/Discord controls,
        // decorated Settings Item, and the correct "Repeater" spelling ----
        QString filteredSidebar = sidebar;
        filteredSidebar.replace(QStringLiteral("    property list<QtObject> pAllSidebarItems:"),
                                QStringLiteral("    property list<QtObject> pSidebarItems\r\n"
                                               "    property list<QtObject> pAllSidebarItems:"));
        filteredSidebar.replace(QStringLiteral("pShowViaRepaterOnly"),
                                QStringLiteral("pShowViaRepeaterOnly"));
        filteredSidebar.replace(QStringLiteral("model: pAllSidebarItems"),
                                QStringLiteral("model: pSidebarItems"));
        filteredSidebar.replace(QStringLiteral("        else iSettingsBtn.visible = false\r\n"),
                                QStringLiteral("        else iSettingsBtn.visible = false\r\n"
                                               "        iDiscordBtn.visible = iAppwin.pIsBootComplete\r\n"
                                               "        pSidebarItems = []\r\n"
                                               "        for (var i = 0; i < pAllSidebarItems.length; ++i)\r\n"
                                               "            if (pAllSidebarItems[i].pIsFeatureEnable && "
                                               "pAllSidebarItems[i].pShowViaRepeaterOnly)\r\n"
                                               "                pSidebarItems.push(pAllSidebarItems[i])\r\n"));
        filteredSidebar.replace(QStringLiteral("        UiImageButton {\r\n"
                                               "            id: iSettingsBtn\r\n"
                                               "            Layout.preferredWidth: pSidebarElementSize\r\n"
                                               "        }"),
                                QStringLiteral("        UiImageButton { id: iDiscordBtn; "
                                               "visible: iAppwin.pIsBootComplete }\r\n"
                                               "        Item {\r\n"
                                               "            id: iSettingsBtn\r\n"
                                               "            Layout.preferredWidth: pSidebarElementSize\r\n"
                                               "            UiImageButton { }\r\n"
                                               "        }"));
        auto filtered = resources({ { QStringLiteral("plr.qml"), anchoredPlr },
                                    { QStringLiteral("PlrSidebar.qml"), filteredSidebar },
                                    { QStringLiteral("PlrTopbar.qml"), topbar } }, true, {});
        addRegistration(filtered, false);
        const auto filteredStock = LayoutPatcher::analyze(filtered.f.bytes, id);
        check(filteredStock.supported, "filtered current-player model and decorated Settings are supported");
        const QString filteredPath = temp.filePath(QStringLiteral("layout-filtered.exe"));
        write(filteredPath, filtered.f.bytes);
        const auto filteredApplied = LayoutPatcher::apply(filteredPath, id, true);
        if (!filteredApplied.ok) std::printf("    %s\n", qPrintable(filteredApplied.msg));
        const auto fixedQml = resourceText(read(filteredPath), filtered, QStringLiteral("PlrSidebar.qml"));
        check(filteredApplied.ok && fixedQml.contains(QLatin1String("pSidebarItems = enabledItems"))
                  && fixedQml.contains(QLatin1String("enabledItems.push(pAllSidebarItems[i])"))
                  && fixedQml.contains(QLatin1String("items[i].pShowViaRepeaterOnly"))
                  && fixedQml.contains(QLatin1String("elementId: pRecent"))
                  && fixedQml.contains(QLatin1String("fixed[n].allowed && !fixed[n].button.visible"))
                  && fixedQml.contains(QLatin1String("var unmodelled = [{ button: iDiscordBtn, allowed: (iAppwin.pIsBootComplete) }]")),
              "modelled fixed buttons overflow only when eligible; unmodelled Discord stays visible and budgeted");
        check(LayoutPatcher::apply(filteredPath, id, false).ok
                  && resourceText(read(filteredPath), filtered, QStringLiteral("PlrSidebar.qml")) == filteredSidebar,
              "filtered current-player shape restores byte-for-byte");

        // ---- 5.10 title bar whose toggle already binds `visible` ----
        // A second `visible:` member would be a duplicate binding (a QML error),
        // so the existing expression has to be wrapped and unwrapped, not
        // replaced. This is the shape real 5.10.150 uses.
        const QString boundTopbar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iTopbar\r\n"
            "    property list<QtObject> pNonFixedTopbarItems: [\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: true\r\n"
            "            pWidth: iBackBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iBackBtn.visible = visibility\r\n"
            "            }\r\n"
            "        },\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: true\r\n"
            "            pWidth: iHomeBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iHomeBtn.visible = visibility\r\n"
            "            }\r\n"
            "        },\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: true\r\n"
            "            pWidth: iRecentBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iRecentBtn.visible = visibility\r\n"
            "            }\r\n"
            "        }\r\n"
            "    ]\r\n"
            "    Row {\r\n"
            "        UiImageButton {\r\n"
            "            id: iSidebarShowBtn\r\n"
            "            asset: \"TitlebarExpand\"\r\n"
            "            visible: !iSidebar.visible\r\n"
            "        }\r\n"
            "    }\r\n"
            "}\r\n");
        auto bound = resources({ { QStringLiteral("plr.qml"), anchoredPlr },
                                 { QStringLiteral("PlrSidebar.qml"), sidebar },
                                 { QStringLiteral("PlrTopbar.qml"), boundTopbar } }, true,
                               { QStringLiteral("PlrTopbar.qml") });
        addRegistration(bound, false);
        check(LayoutPatcher::analyze(bound.f.bytes, id).supported,
              "a title bar whose toggle already binds visible is still supported");
        const QString boundPath = temp.filePath(QStringLiteral("layout-bound.exe"));
        write(boundPath, bound.f.bytes);
        const auto boundApplied = LayoutPatcher::apply(boundPath, id, true);
        if (!boundApplied.ok) std::printf("    %s\n", qPrintable(boundApplied.msg));
        check(boundApplied.ok, "the tweak applies when the toggle already binds visible");
        const auto boundText = resourceText(read(boundPath), bound, QStringLiteral("PlrTopbar.qml"));
        check(boundText.contains(QLatin1String("visible: false && (!iSidebar.visible)"))
                  && !boundText.contains(QLatin1String("visible: !iSidebar.visible")),
              "an existing visibility binding is wrapped, not duplicated");
        check(LayoutPatcher::apply(boundPath, id, false).ok
                  && resourceText(read(boundPath), bound, QStringLiteral("PlrTopbar.qml")) == boundTopbar
                  && resourceText(read(boundPath), bound, QStringLiteral("plr.qml")) == anchoredPlr
                  && resourceText(read(boundPath), bound, QStringLiteral("PlrSidebar.qml")) == sidebar,
              "the wrapped visibility binding unwraps byte-for-byte on restore");

        auto other = resources({ { QStringLiteral("Other.qml"),
                                   QStringLiteral("import QtQuick\r\nRectangle { }\r\n") } }, true, {});
        addRegistration(other, false);
        check(!LayoutPatcher::analyze(other.f.bytes, id).supported,
              "a player without the known layout documents is refused instead of rewritten");
        const QString otherPath = temp.filePath(QStringLiteral("layout-other.exe"));
        write(otherPath, other.f.bytes);
        check(!LayoutPatcher::apply(otherPath, id, true).ok && read(otherPath) == other.f.bytes,
              "an unsupported player is left untouched");
    }

    std::printf("[boot-discovery] renamed/raw controllers and the host's selected component\n");
    {
        const QString controller = QStringLiteral("import QtQuick\nItem{Image{id:iBootImage}Text{id:iBootLoadingText}"
            "function fSetBootPromotion(){iBootImage.visible=true;}}\n");
        const QString host = QStringLiteral("import QtQuick\nItem{Component{id:iBootProgressComponent;RenamedStartup{}}Loader{id:iBootLoader}}\n");
        auto r = resources({ { QStringLiteral("RenamedStartup.qml"), controller },
                             { QStringLiteral("UnusedPortrait.qml"), controller },
                             { QStringLiteral("Shell.qml"), host },
                             { QStringLiteral("MediaSupport.qml"), QStringLiteral("import QtQuick\nimport QtMultimedia 5.15\nItem{}") } },
                           true, { QStringLiteral("RenamedStartup.qml"), QStringLiteral("MediaSupport.qml") });
        addRegistration(r, false);
        const QString path = temp.filePath(QStringLiteral("renamed-boot.exe")); write(path, r.f.bytes);
        const auto result = BootVideoPatcher::patch(path);
        if (!result.ok) std::printf("    %s\n", qPrintable(result.msg));
        check(result.ok, "boot discovery does not depend on a fixed boot filename or zlib storage");
        check(resourceText(read(path), r, QStringLiteral("RenamedStartup.qml")).contains(QLatin1String("bsiBootVideo"))
                  && resourceText(read(path), r, QStringLiteral("UnusedPortrait.qml")) == controller,
              "the host's boot Component selects one of several valid controller variants");
        check(BootVideoPatcher::patch(path).ok, "raw renamed boot resources support repeated patching");
        const qint64 node = r.nodes[QStringLiteral("RenamedStartup.qml")];
        check(be16(read(path), node + 4) == 0, "raw-resource rewrites preserve the node's storage flags");
    }

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
        ciNotice(QStringLiteral("reference EXE %1 (%2 bytes) - solid and gradient paths below run against it")
                     .arg(QFileInfo(input).fileName()).arg(original.size()));
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
                if (round == 2)
                    ciNotice(QStringLiteral("solid theme mode: %1 palette entries re-applied and re-read %2x on a copy")
                                 .arg(originalEntries.size()).arg(round + 1));
                const auto boot = BootVideoPatcher::patch(path);
                if (!boot.ok) std::printf("    Boot round %d: %s\n", round, qPrintable(boot.msg));
                check(boot.ok, "boot patch/repatch succeeds on the supplied EXE copy without breaking theme mapping");
            }
            const auto gradientTargets = GradientPatcher::analyze(read(path));
            std::printf("    Gradients: %d target(s), %d of %d QML file(s) parsed: %s\n",
                        static_cast<int>(gradientTargets.targets.size()), gradientTargets.parsed,
                        gradientTargets.candidates, qPrintable(gradientTargets.message));
            ciNotice(QStringLiteral("gradient discovery: %1 target(s) from %2/%3 parsed QML file(s)")
                         .arg(gradientTargets.targets.size()).arg(gradientTargets.parsed).arg(gradientTargets.candidates));
            check(gradientTargets.ok, "compatible gradient targets are found in the supplied EXE without executing its QML");
            check(gradientTargets.candidates == 0 || gradientTargets.parsed > 0,
                  "the QML of the supplied EXE is parsed instead of being silently skipped");
            for (const auto &label : { QStringLiteral("Sidebar background"), QStringLiteral("Top bar background"),
                                      QStringLiteral("Sidebar (player window)"), QStringLiteral("Top bar (player window)"),
                                      QStringLiteral("Button backgrounds (all states)"),
                                      QStringLiteral("Theme gradients.gradientButton") }) {
                QString key, resource;
                for (const auto &target : gradientTargets.targets) {
                    // The theme-level definition label carries a separator that is
                    // awkward to spell in a source file, so match on its tail.
                    if ((label == QStringLiteral("Theme gradients.gradientButton")
                             ? target.label.endsWith(QLatin1String("gradients.gradientButton"))
                             : target.label == label)) {
                        key = target.key; resource = target.resource;
                    }
                }
                check(!key.isEmpty(), QStringLiteral("supplied EXE exposes the %1 gradient target").arg(label));
                if (key.isEmpty()) continue;
                const auto gradient = GradientPatcher::patch(path, key,
                    { true, QStringLiteral("#112244"), QStringLiteral("#8844cc"), true });
                if (!gradient.ok) std::printf("    Gradient %s: %s\n", qPrintable(label), qPrintable(gradient.message));
                check(gradient.ok, "gradient resources are patched and verified on a disposable real-EXE copy");
                const auto patched = read(path);
                bool readBack = false;
                for (const auto &file : QtResources::registeredFiles(patched)) {
                    if (file.name.compare(resource, Qt::CaseInsensitive) != 0) continue;
                    readBack = true;
                    const auto text = QString::fromUtf8(QtResources::readFile(patched, file));
                    // Stops are written upper-cased by GradientPatcher::normalized().
                    check(text.contains(QStringLiteral("#112244")) && text.contains(QStringLiteral("#8844CC"))
                              && !text.contains(QLatin1String("}};")),
                          QStringLiteral("the gradient written into %1 is QML Qt still accepts").arg(resource));
                    break;
                }
                check(readBack, QStringLiteral("the patched %1 resource can be read back").arg(resource));
                const auto cleared = GradientPatcher::patch(path, key, { false, {}, {}, true });
                if (!cleared.ok) std::printf("    Gradient clear %s: %s\n", qPrintable(label), qPrintable(cleared.message));
                check(cleared.ok, "real-EXE gradient targets can return to their preserved solid binding");
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

    std::printf("[fps-autodetect] actual imports, selected directory, version changes and path handling\n");
    {
        QTemporaryDir installations;
        const auto nxt = installations.filePath(QStringLiteral("BlueStacks_nxt"));
        const auto msi = installations.filePath(QStringLiteral("MSI App Player"));
        QDir().mkpath(nxt); QDir().mkpath(msi);
        const auto player = QDir(nxt).filePath(QStringLiteral("HD-Player.exe"));
        const auto qt5 = QDir(nxt).filePath(QStringLiteral("Qt5QuickTemplates2.dll"));
        const auto qt6 = QDir(nxt).filePath(QStringLiteral("Qt6QuickTemplates2.dll"));
        write(qt5, sliderDll(false)); write(qt6, sliderDll(false));
        auto image = importedPlayer({ QStringLiteral("Qt5Core.dll") });
        image.replace(0x2800, 11, QByteArray("Qt6Core.dll")); // unrelated string must not win
        write(player, image);
        auto found = FpsPatcher::detectDll(player);
        check(found.ok && found.qtMajor == 5 && found.path.endsWith(QLatin1String("Qt5QuickTemplates2.dll")),
              "real Qt5 imports beat an unrelated Qt6 string when both DLLs exist");
        write(player, importedPlayer({}, { QStringLiteral("Qt6Core.dll") }));
        found = FpsPatcher::detectDll(player);
        check(found.ok && found.qtMajor == 6, "delay-loaded Qt6 imports choose the Qt6 slider DLL");
        write(player, importedPlayer({ QStringLiteral("Qt5Core.dll"), QStringLiteral("Qt6Core.dll") }));
        check(!FpsPatcher::detectDll(player).ok, "ambiguous Qt majors are not resolved by guessing");
        write(player, importedPlayer({}));
        check(!FpsPatcher::detectDll(player).ok, "two DLL versions without import evidence require manual selection");
        QFile::remove(qt6);
        check(FpsPatcher::detectDll(player).qtMajor == 5, "a single local DLL version is an explicit fallback");
        write(player, importedPlayer({ QStringLiteral("Qt6Core.dll") }));
        check(!FpsPatcher::detectDll(player).ok, "known Qt6 must not silently fall back to the present Qt5 DLL");
        const auto bin = QDir(nxt).filePath(QStringLiteral("bin")); QDir().mkpath(bin);
        write(QDir(bin).filePath(QStringLiteral("Qt6QuickTemplates2.dll")), sliderDll(false));
        check(FpsPatcher::detectDll(player).path.contains(QLatin1String("bin")), "a supported runtime subdirectory is searched inside the selected install");
        QFile::remove(QDir(bin).filePath(QStringLiteral("Qt6QuickTemplates2.dll")));
        write(qt6, sliderDll(false));
        write(player, importedPlayer({ QStringLiteral("PlayerUi.dll") }));
        write(QDir(nxt).filePath(QStringLiteral("PlayerUi.dll")), importedPlayer({ QStringLiteral("Qt5Quick.dll") }));
        check(FpsPatcher::detectDll(player).qtMajor == 5, "a local UI dependency can establish the Qt major");
        // Older x86 installations used to be refused as "not a readable x64 PE" before
        // their Qt version was looked at, which is indistinguishable from Qt5 detection
        // not working at all. The version is now reported and only the patch is limited.
        const auto legacyDir = installations.filePath(QStringLiteral("BlueStacks_legacy"));
        QDir().mkpath(legacyDir);
        const auto legacyPlayer = QDir(legacyDir).filePath(QStringLiteral("HD-Player.exe"));
        write(legacyPlayer, importedPlayer32({ QStringLiteral("Qt5Core.dll") }));
        write(QDir(legacyDir).filePath(QStringLiteral("Qt5QuickTemplates2.dll")), x86Dll());
        const auto legacy = FpsPatcher::detectDll(legacyPlayer);
        check(!legacy.ok && legacy.qtMajor == 5 && legacy.playerBits == 32
                  && legacy.msg.contains(QLatin1String("32-bit")) && legacy.msg.contains(QLatin1String("Themes, gradients")),
              "a 32-bit Qt5 installation is identified and told apart from an unreadable file");
        write(QDir(legacyDir).filePath(QStringLiteral("Qt5QuickTemplates2.dll")), sliderDll(false));
        const auto mixed = FpsPatcher::detectDll(legacyPlayer);
        check(mixed.ok && mixed.qtMajor == 5 && mixed.playerBits == 32,
              "an x86 player whose slider DLL is 64-bit is still patched");
        const auto qtLayout = installations.filePath(QStringLiteral("BlueStacks_qt5"));
        QDir().mkpath(QDir(qtLayout).filePath(QStringLiteral("Qt5/bin")));
        write(QDir(qtLayout).filePath(QStringLiteral("HD-Player.exe")), importedPlayer({ QStringLiteral("Qt5Core.dll") }));
        write(QDir(qtLayout).filePath(QStringLiteral("Qt5/bin/Qt5QuickTemplates2.dll")), sliderDll(false));
        const auto sub = FpsPatcher::detectDll(QDir(qtLayout).filePath(QStringLiteral("HD-Player.exe")));
        // The detected path is returned with native separators, so compare against the
        // directory name rather than the whole "Qt5/bin" fragment.
        check(sub.ok && sub.path.contains(QLatin1String("bin")) && sub.path.endsWith(QLatin1String("Qt5QuickTemplates2.dll")),
              "Qt5/bin is searched for the slider DLL too");
        auto malformed = importedPlayer({ QStringLiteral("Qt5Core.dll") });
        putLe32(malformed, 0x1800 + 12, 0xfffffff0);
        check(!PeUtils::importedModules(malformed).ok, "out-of-file import names are refused");

        write(player, image);
        QSettings().setValue(QStringLiteral("paths/exe"), player);
        QSettings().setValue(QStringLiteral("paths/dll"), qt6); // stale setting from another Qt version
        QSettings().setValue(QStringLiteral("fps/autoDetect"), true);
        SettingsManager settings;
        Backend backend(&settings);
        check(settings.dllPath().endsWith(QLatin1String("Qt5QuickTemplates2.dll")), "startup corrects a stale saved Qt DLL target");
        write(player, importedPlayer({ QStringLiteral("Qt6Core.dll") }));
        settings.setExePath(player);
        check(settings.dllPath().endsWith(QLatin1String("Qt6QuickTemplates2.dll")), "same-path in-place Qt upgrades refresh the selected DLL");
        write(player, image);
        check(backend.fpsPatch(360).value(QStringLiteral("ok")).toBool()
                  && settings.dllPath().endsWith(QLatin1String("Qt5QuickTemplates2.dll"))
                  && read(qt6) == sliderDll(false),
              "FPS actions re-detect and patch only the matching DLL in the selected directory");
        const auto spaced = QDir(msi).filePath(QStringLiteral("HD-Player.exe"));
        check(backend.localFilePath(QUrl::fromLocalFile(spaced)) == QDir::toNativeSeparators(spaced),
              "file-picker URLs preserve Program Files spaces instead of literal percent-20 paths");
        const auto unicode = QDir(msi).filePath(QStringLiteral("тест % file.dll"));
        check(backend.localFilePath(QUrl::fromLocalFile(unicode)) == QDir::toNativeSeparators(unicode),
              "file-picker URLs round-trip Unicode and percent characters");
        write(spaced, importedPlayer({}, { QStringLiteral("Qt6Core.dll") }));
        write(QDir(msi).filePath(QStringLiteral("Qt6QuickTemplates2.dll")), sliderDll(true));
        const auto detected = BsDetector::findBlueStacks({ installations.path() }, false);
        // This fixture deliberately keeps four install folders (the two below plus
        // the legacy and Qt5 ones created earlier in the block), so what matters is
        // that both a plain BlueStacks name and the MSI brand are recognised, with
        // their own data, and not an exact count.
        bool nxtSeen = false, msiSeen = false;
        for (const auto &value : detected) {
            const auto item = value.toMap();
            const QString root = item.value(QStringLiteral("root")).toString();
            nxtSeen |= root.contains(QLatin1String("BlueStacks_nxt"), Qt::CaseInsensitive);
            msiSeen |= root.contains(QLatin1String("MSI App Player"), Qt::CaseInsensitive)
                       && item.value(QStringLiteral("profile")).toString() == QLatin1String("MSI5");
        }
        check(nxtSeen, "Program Files discovery recognizes an ordinary BlueStacks install folder");
        check(msiSeen, "Program Files discovery recognizes the space-bearing MSI App Player folder name");
        bool correct = true;
        for (const auto &value : detected) {
            const auto item = value.toMap();
            correct &= !item.value(QStringLiteral("fps_dll")).toString().isEmpty()
                       && item.value(QStringLiteral("fps_dll")).toString().startsWith(item.value(QStringLiteral("root")).toString());
        }
        check(correct, "each detected installation keeps its own player, Qt major and slider DLL");
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
        write(exe, importedPlayer({ QStringLiteral("Qt5Core.dll") }));
        check(FpsPatcher::findDll(temp.path()).endsWith(QLatin1String("Qt5QuickTemplates2.dll")),
              "when both DLLs exist, player Qt5 dependency selects Qt5");
        write(exe, importedPlayer({ QStringLiteral("Qt6Core.dll") }));
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
        settings.setFpsAutoDetect(false);
        settings.setDllPath(renderer);
        check(!backend.fpsAnalyze().value(QStringLiteral("ok")).toBool(), "FPS Analyze rejects the wrong DLL too");
    }
    {
        // Rich Presence is a native HD-Player patch: no deployed script and
        // no interpreter is involved. Exercise detection, repeated patch,
        // remembered state, fixed-size writes and undo through the shipping API.
        const QString firstId = QStringLiteral("821361671729709086");
        const QString secondId = QStringLiteral("1234567890123456789");
        QByteArray image(32, '\x90');
        image += QByteArray::fromHex("48b8");
        quint64 numeric = firstId.toULongLong();
        for (int i = 0; i < 8; ++i) image += char((numeric >> (8 * i)) & 0xff);
        image += QByteArray("discord\0", 8);
        image += firstId.toLatin1() + QByteArray(3, '\0');
        for (const QChar c : firstId) { image += char(c.unicode()); image += '\0'; }
        image += QByteArray(2, '\0');
        image += QByteArray(32, '\x90');

        const auto records = RpcIdScan::harvest(
            std::string_view(image.constData(), static_cast<std::size_t>(image.size())));
        const auto pick = RpcIdScan::pickCandidate(records);
        check(pick.index >= 0 && records[std::size_t(pick.index)].id == firstId.toStdString(),
              "native Rich Presence scanner finds the HD-Player Application ID");

        const QString exe = temp.filePath(QStringLiteral("HD-Rpc.exe"));
        write(exe, image);
        const qint64 originalSize = QFileInfo(exe).size();
        const auto changed = RpcIdPatcher::replace(exe, secondId);
        const auto changedStatus = RpcIdPatcher::status(exe);
        check(changed.ok && changedStatus.patched && changedStatus.currentId == secondId,
              "native Rich Presence patch records and reports the replacement ID");
        check(QFileInfo(exe).size() == originalSize
                  && QFileInfo::exists(RpcIdPatcher::backupPath(exe)),
              "Rich Presence patch preserves file size and creates its backup");
        const auto repeated = RpcIdPatcher::replace(exe, firstId);
        check(repeated.ok && RpcIdPatcher::status(exe).currentId == firstId,
              "Rich Presence ID can be changed repeatedly without rescanning a stale ID");
        const auto undone = RpcIdPatcher::undo(exe);
        QFile restored(exe); restored.open(QIODevice::ReadOnly);
        check(undone.ok && restored.readAll() == image && !QFileInfo::exists(RpcIdPatcher::statePath(exe)),
              "Rich Presence Undo restores the original bytes and clears patch state");
    }
    {
        // Detection is value-agnostic: an Application ID the tool has never
        // seen is found by the evidence it leaves (immediate loads, digit
        // strings, repeated stored constants), never by matching a hardcoded
        // value. The layouts below mirror the stock fixture with an unknown
        // ID, and noise must never outrank it - acting on a wrong "detection"
        // is what corrupted executables before.
        const QString otherId = QStringLiteral("1284004339950120960");
        const auto imageOf = [](const QString &id) {
            QByteArray image(32, '\x90');
            image += QByteArray::fromHex("48b8");
            quint64 numeric = id.toULongLong();
            for (int i = 0; i < 8; ++i) image += char((numeric >> (8 * i)) & 0xff);
            image += QByteArray("discord\0", 8);
            image += id.toLatin1() + QByteArray(3, '\0');
            for (const QChar c : id) { image += char(c.unicode()); image += '\0'; }
            image += QByteArray(2, '\0');
            image += QByteArray(32, '\x90');
            return image;
        };
        const auto picked = [](const QByteArray &image) {
            const auto records = RpcIdScan::harvest(
                std::string_view(image.constData(), static_cast<std::size_t>(image.size())));
            const auto pick = RpcIdScan::pickCandidate(records);
            return pick.index >= 0 ? QString::fromStdString(records[std::size_t(pick.index)].id) : QString();
        };

        const QByteArray image = imageOf(otherId);
        check(picked(image) == otherId,
              "an unknown Application ID is detected in every stored form, not just the hardcoded one");

        const QString exe = temp.filePath(QStringLiteral("HD-Rpc-Other.exe"));
        write(exe, image);
        const auto changed = RpcIdPatcher::replace(exe, QStringLiteral("1234567890123456789"));
        check(changed.ok && RpcIdPatcher::status(exe).currentId == QStringLiteral("1234567890123456789"),
              "an unknown Application ID can be replaced just like the documented one");
        const auto undone = RpcIdPatcher::undo(exe);
        QFile restored(exe); restored.open(QIODevice::ReadOnly);
        check(undone.ok && restored.readAll() == image,
              "unknown-ID patches restore the original bytes exactly");

        // The real ID as a bare text string must beat a snowflake-shaped
        // junk constant; the old ranking picked the junk one and patching
        // then clobbered it, breaking the executable.
        QByteArray noisy(64, '\x90');
        const quint64 junk = 0x0123456789ABCDEFULL;
        for (const int at : { 4, 32 }) {
            noisy[at] = '\x48'; noisy[at + 1] = char(0xB8);
            for (int i = 0; i < 8; ++i) noisy[at + 2 + i] = char((junk >> (8 * i)) & 0xff);
        }
        noisy += QByteArray("discord rpc\0", 12);
        noisy += otherId.toLatin1() + QByteArray(2, '\0');
        check(picked(noisy) == otherId,
              "a junk code constant cannot outrank the real text ID");

        // An ID that only exists as repeated stored constants (Discord Game
        // SDK structs) is found too, exactly like the stock ID used to be.
        QByteArray stored(256, '\0');
        const quint64 value = otherId.toULongLong();
        for (const int at : { 0, 64, 128 })
            for (int i = 0; i < 8; ++i) stored[at + i] = char((value >> (8 * i)) & 0xff);
        stored += QByteArray("discord\0", 8);
        check(picked(stored) == otherId,
              "repeated stored constants are detected for any ID, not only the documented one");

        // Two unrelated constants must never be guessed: safety is refusing
        // to pick, which keeps replace from touching the file at all.
        QByteArray ambiguous(64, '\x90');
        for (const int at : { 4, 32 }) {
            const quint64 other = at == 4 ? 0x0B0123456789ABCDULL : 0x0C0123456789ABCDULL;
            ambiguous[at] = '\x48'; ambiguous[at + 1] = char(0xB8);
            for (int i = 0; i < 8; ++i) ambiguous[at + 2 + i] = char((other >> (8 * i)) & 0xff);
        }
        ambiguous += QByteArray("discord\0", 8);
        const auto records = RpcIdScan::harvest(
            std::string_view(ambiguous.constData(), static_cast<std::size_t>(ambiguous.size())));
        check(RpcIdScan::pickCandidate(records).index < 0,
              "ambiguous constants refuse to be guessed instead of reporting a random ID");
        const QString safe = temp.filePath(QStringLiteral("HD-Rpc-Ambiguous.exe"));
        write(safe, ambiguous);
        check(!RpcIdPatcher::replace(safe, QStringLiteral("1234567890123456789")).ok
                  && read(safe) == ambiguous && !QFileInfo::exists(RpcIdPatcher::backupPath(safe)),
              "an unconfirmed scan can never patch the wrong bytes");
    }
    {
        // 32-bit builds have no single imm64 instruction: the 64-bit ID is
        // pushed (or moved) as two separate 32-bit halves. That form is only
        // looked for inside a section an actual 32-bit PE image marks
        // executable, so the fixture needs real (if minimal) PE32 headers -
        // not just bare instruction bytes - to exercise the exact
        // auto-detection path RpcIdPatcher uses on a real 32-bit
        // HD-Player.exe, instead of a shortcut that only proves the opcode
        // decoder works in isolation.
        const QString id = QStringLiteral("821361671729709086");
        const quint64 numeric = id.toULongLong();

        QByteArray image = peFile();
        putLe16(image, 0x84, 0x14c);                                   // machine: x86
        putLe16(image, 0x98, 0x10b);                                   // PE32 (not PE32+) magic
        image.replace(0x188, 8, QByteArray(".text\0\0\0", 8));
        putLe32(image, 0x190, 0x1000); putLe32(image, 0x194, 0x1000);  // vsize, va
        putLe32(image, 0x198, 0x1000); putLe32(image, 0x19c, 0x1f000); // rawSize, raw
        putLe32(image, 0x1ac, 0x60000020);                             // CODE | EXECUTE | READ

        const int at = 0x1f100; // inside the executable .text range above
        image.replace(at, 1, QByteArray::fromHex("68"));               // push imm32 (low half)
        putLe32(image, at + 1, quint32(numeric & 0xffffffffu));
        image.replace(at + 5, 1, QByteArray::fromHex("68"));           // push imm32 (high half)
        putLe32(image, at + 6, quint32(numeric >> 32));

        const RpcIdScan::Pe pe(std::string_view(image.constData(), static_cast<std::size_t>(image.size())));
        check(pe.ok() && pe.is32(), "the Split32 fixture is recognized as a 32-bit PE image");

        const auto records = RpcIdScan::harvest(
            std::string_view(image.constData(), static_cast<std::size_t>(image.size())), &pe);
        const auto pick = RpcIdScan::pickCandidate(records);
        check(pick.index >= 0 && records[std::size_t(pick.index)].id == id.toStdString(),
              "32-bit split push/push halves are harvested back into one Application ID");

        const QString exe = temp.filePath(QStringLiteral("HD-Rpc-Split32.exe"));
        write(exe, image);
        const auto changed = RpcIdPatcher::replace(exe, QStringLiteral("1234567890123456789"));
        check(changed.ok && RpcIdPatcher::status(exe).currentId == QStringLiteral("1234567890123456789"),
              "a Split32 site is patched just like an imm64 one");
        check(QFileInfo(exe).size() == image.size(),
              "patching a Split32 pair never changes the file size");
    }
    {
        // A signature captured from the exact slot just patched can find it
        // again even after the surrounding code shifts position and the
        // stored value changes to something that was never seen as text
        // anywhere - exactly what a BlueStacks self-update that keeps the
        // same structural layout looks like. Nothing here is specific to any
        // one build: the signature is captured from whatever was actually
        // written, not from a hardcoded table.
        const QString firstId = QStringLiteral("821361671729709086");
        QByteArray image(32, '\x90');
        image += QByteArray::fromHex("48b8");
        quint64 numeric = firstId.toULongLong();
        for (int i = 0; i < 8; ++i) image += char((numeric >> (8 * i)) & 0xff);
        image += QByteArray(32, '\x90');

        const QString exe = temp.filePath(QStringLiteral("HD-Rpc-Sig.exe"));
        write(exe, image);
        const QString secondId = QStringLiteral("1284004339950120960");
        check(RpcIdPatcher::replace(exe, secondId).ok,
              "the signature-capture fixture's first patch succeeds");

        const QJsonObject state = QJsonDocument::fromJson(read(RpcIdPatcher::statePath(exe))).object();
        check(state.value(QStringLiteral("sig")).toObject().value(QStringLiteral("pre")).toString().size() > 0,
              "a signature around the slot is captured right after patching it");

        QByteArray rebuilt = read(exe);
        const int at = rebuilt.indexOf(QByteArray::fromHex("48b8"));
        check(at >= 0, "the imm64 opcode is still present to simulate a rebuild against");
        rebuilt.insert(at, QByteArray(3, '\x90'));
        const QString neverSeenId = QStringLiteral("999999999999999991");
        const quint64 shifted = neverSeenId.toULongLong();
        for (int i = 0; i < 8; ++i) rebuilt[at + 3 + 2 + i] = char((shifted >> (8 * i)) & 0xff);
        write(exe, rebuilt);

        const RpcIdPatcher::Status relocated = RpcIdPatcher::status(exe);
        check(relocated.ok && relocated.currentId == neverSeenId,
              "a remembered signature relocates the slot after the surrounding code shifts");
        const auto repatched = RpcIdPatcher::replace(exe, QStringLiteral("1111111111111111111"));
        check(repatched.ok && RpcIdPatcher::status(exe).currentId == QStringLiteral("1111111111111111111"),
              "replace() itself uses the same signature relocation, not only status()");
    }
    {
        // A file matching a documented build's exact byte layout is found
        // even with no state file at all - the "never patched before, but a
        // known build" case.
        const QString exe = temp.filePath(QStringLiteral("HD-Rpc-Builtin.exe"));
        QByteArray image(16, '\x90');
        image += QByteArray::fromHex("c0743c48c70000000000488d4b1848b8");
        const QString knownSlotId = QStringLiteral("555555555555555551");
        const quint64 v = knownSlotId.toULongLong();
        for (int i = 0; i < 8; ++i) image += char((v >> (8 * i)) & 0xff);
        image += QByteArray::fromHex("c6431000ba0201000048894308e84c19");
        image += QByteArray(16, '\x90');
        write(exe, image);

        const RpcIdPatcher::Status located = RpcIdPatcher::status(exe);
        check(located.ok && located.currentId == knownSlotId,
              "a documented build's byte layout is recognized by its built-in signature with no prior state");
    }
    {
        // Last resort: no state file, no signature match, but a
        // `.rpcid-bak` sits next to the file and differs from it in exactly
        // one short run - the id slot.
        const QString exe = temp.filePath(QStringLiteral("HD-Rpc-BackupDiff.exe"));
        QByteArray original(16, '\x90');
        original += QByteArray::fromHex("48b8");
        const quint64 firstV = QStringLiteral("821361671729709086").toULongLong();
        for (int i = 0; i < 8; ++i) original += char((firstV >> (8 * i)) & 0xff);
        original += QByteArray(16, '\x90');
        write(RpcIdPatcher::backupPath(exe), original);

        QByteArray current = original;
        const QString bakId = QStringLiteral("444444444444444441");
        const quint64 bakV = bakId.toULongLong();
        for (int i = 0; i < 8; ++i) current[18 + i] = char((bakV >> (8 * i)) & 0xff);
        write(exe, current);

        const RpcIdPatcher::Status located = RpcIdPatcher::status(exe);
        check(located.ok && located.currentId == bakId,
              "with no state and no signature match, a difference against .rpcid-bak locates the slot");
    }
    {
        // The Windows version-resource editor ("name") and the scriptable
        // command line are part of the application itself now. Nothing here
        // starts another process, and no interpreter is required.
        const QString exe = temp.filePath(QStringLiteral("HD-Version.exe"));
        const QByteArray image = peWithVersionResource(
            versionResourceBlob(QStringLiteral("BlueStacks Player"), 40, QStringLiteral("BlueStacks")));
        write(exe, image);

        const auto viewOf = [](const QByteArray &bytes) {
            return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        };
        const VersionResource::Info info = VersionResource::read(viewOf(image));
        const auto described = VersionResource::withKey(info, "FileDescription");
        check(info.ok && described.size() == 2
                  && described.front()->value == std::string("BlueStacks Player"),
              "version resource strings are read from every translation");
        check(VersionResource::withKey(info, "Translation").empty(),
              "binary VarFileInfo values are not offered as editable text");

        QByteArray tooLong = image;
        std::string why;
        check(!VersionResource::setString(tooLong.data(), std::size_t(tooLong.size()),
                                          *VersionResource::withKey(info, "ProductName").front(),
                                          std::string("A name far beyond the reserved slot"), &why)
                  && tooLong == image,
              "a name longer than the reserved slot is refused without touching the file");

        check(runRpcIdCli({ QStringLiteral("name"), exe }) == 0,
              "the command line lists the names stored in a file");
        check(runRpcIdCli({ QStringLiteral("name"), exe, QStringLiteral("--set"),
                            QStringLiteral("My Player") }) == 1
                  && read(exe) == image,
              "a name change without --yes previews instead of writing");
        check(runRpcIdCli({ QStringLiteral("name"), exe, QStringLiteral("--set"),
                            QStringLiteral("My Player"), QStringLiteral("--yes") }) == 0,
              "the command line writes a confirmed name change");

        const QByteArray renamed = read(exe);
        const VersionResource::Info after = VersionResource::read(viewOf(renamed));
        const auto names = VersionResource::withKey(after, "FileDescription");
        check(renamed.size() == image.size() && names.size() == 2
                  && names.front()->value == std::string("My Player")
                  && names.back()->value == std::string("My Player"),
              "every translation is renamed and the file size never changes");
        check(VersionResource::withKey(after, "ProductName").front()->value
                  == std::string("My Player"),
              "the default keys cover ProductName as well");
        check(QFileInfo::exists(RpcIdPatcher::backupPath(exe)),
              "the first name change keeps a backup of the original file");

        // The UI drives the very same engine, so the feature is not
        // command-line only: Backend is what QML calls.
        SettingsManager nameSettings;
        Backend nameBackend(&nameSettings);
        const QVariantMap listed = nameBackend.rpcidNames(exe);
        check(listed.value(QStringLiteral("ok")).toBool()
                  && listed.value(QStringLiteral("entries")).toList().size() == 4,
              "the QML-facing API lists every editable name with its limit");
        const QVariantMap previewed = nameBackend.rpcidSetName(exe, QStringLiteral("Preview"),
                                                               QString(), true);
        check(previewed.value(QStringLiteral("ok")).toBool() && read(exe) == renamed,
              "a previewed rename writes nothing");
        const QVariantMap applied = nameBackend.rpcidSetName(exe, QStringLiteral("Player X"),
                                                             QStringLiteral("ProductName"), false);
        const QByteArray uiBytes = read(exe);
        const VersionResource::Info uiNamed = VersionResource::read(viewOf(uiBytes));
        check(applied.value(QStringLiteral("ok")).toBool()
                  && VersionResource::withKey(uiNamed, "ProductName").front()->value
                         == std::string("Player X")
                  && VersionResource::withKey(uiNamed, "FileDescription").front()->value
                         == std::string("My Player"),
              "the UI can rename a single key without touching the others");
        const QVariantMap refused = nameBackend.rpcidSetName(
            exe, QStringLiteral("A name far beyond the reserved slot"), QStringLiteral("ProductName"), false);
        check(!refused.value(QStringLiteral("ok")).toBool()
                  && refused.value(QStringLiteral("msg")).toString().contains(QLatin1String("slot holds")),
              "an oversized name is refused with the exact limit instead of being truncated");
        check(!nameBackend.rpcidSetName(exe, QString(), QString(), false)
                   .value(QStringLiteral("ok")).toBool(),
              "an empty name is refused");

        // Argument handling: a wrong command line must be reported, never
        // guessed at, and must not write anything.
        check(runRpcIdCli({ QStringLiteral("replace"), exe }) == 2,
              "replace without --new is a usage error");
        check(runRpcIdCli({ QStringLiteral("nonsense"), exe }) == 2,
              "an unknown command is a usage error");
        check(runRpcIdCli({ QStringLiteral("--frobnicate") }) == 2,
              "an unknown option is a usage error");
        check(runRpcIdCli({ QStringLiteral("help") }) == 0, "help is not an error");

        const QString rpc = temp.filePath(QStringLiteral("HD-Cli.exe"));
        QByteArray player(32, '\x90');
        player += QByteArray::fromHex("48b8");
        const quint64 numeric = QStringLiteral("821361671729709086").toULongLong();
        for (int i = 0; i < 8; ++i) player += char((numeric >> (8 * i)) & 0xff);
        player += QByteArray("discord\0", 8);
        player += QByteArray("821361671729709086") + QByteArray(3, '\0');
        for (const QChar c : QStringLiteral("821361671729709086")) {
            player += char(c.unicode());
            player += '\0';
        }
        player += QByteArray(2, '\0');
        player += QByteArray(32, '\x90');
        write(rpc, player);
        check(runRpcIdCli({ QStringLiteral("status"), rpc, QStringLiteral("--json") }) == 0,
              "status reports a detected ID as machine-readable JSON");
        check(runRpcIdCli({ QStringLiteral("replace"), rpc, QStringLiteral("--new"),
                            QStringLiteral("1234567890123456789") }) == 1
                  && read(rpc) == player,
              "replace without --yes previews instead of writing");
        check(runRpcIdCli({ QStringLiteral("replace"), rpc, QStringLiteral("--new"),
                            QStringLiteral("1234567890123456789"), QStringLiteral("--yes") }) == 0
                  && read(rpc).size() == player.size()
                  && RpcIdPatcher::status(rpc).currentId == QStringLiteral("1234567890123456789"),
              "replace --yes writes through the same engine the UI uses");
        check(runRpcIdCli({ QStringLiteral("undo"), rpc }) == 0 && read(rpc) == player,
              "undo restores the original bytes from the command line too");
    }
    std::printf("[layout] one-click clean-up hides the extra title-bar and sidebar buttons\n");
    {
        const QString cleanPlr = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iAppwin\r\n"
            "    property int pSidebarWidth: 32\r\n"
            "    PlrTopbar { id: iTopbar }\r\n"
            "    PlrSidebar {\r\n"
            "        id: iSidebar\r\n"
            "        width: pSidebarWidth\r\n"
            "        visible: uiBackend.confGetInstanceBool(\"show_sidebar\")\r\n"
            "    }\r\n"
            "}\r\n");
        const QString cleanSidebar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iSidebar\r\n"
            "    UiColumnLayout {\r\n"
            "        anchors.fill: parent\r\n"
            "        UiImageButton { id: iCollapseBtn }\r\n"
            "        UiImageButton { id: iSoundBtn }\r\n"
            "    }\r\n"
            "    property list<QtObject> pAllSidebarItems: [\r\n"
            "        PlrSidebarElement {\r\n"
            "            pElementId: pFullscreen\r\n"
            "            pAsset: \"SidebarFullScreen\"\r\n"
            "            pIsFeatureEnable: iSidebarProperties.pIsFullScreenEnabled\r\n"
            "        },\r\n"
            "        PlrSidebarElement {\r\n"
            "            pElementId: pVolume\r\n"
            "            pIsFeatureEnable: iSidebarProperties.pIsVolumeEnabled\r\n"
            "        },\r\n"
            "        PlrSidebarElement {\r\n"
            "            pElementId: pScreenshot\r\n"
            "            pAsset: \"SidebarScreenshot\"\r\n"
            "            pIsFeatureEnable: iSidebarProperties.pIsScreenshotEnabled\r\n"
            "        },\r\n"
            "        PlrSidebarElement {\r\n"
            "            pElementId: pRotate\r\n"
            "            pAsset: \"SidebarRotate\"\r\n"
            "        }\r\n"
            "    ]\r\n"
            "}\r\n");
        const QString cleanTopbar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iTopbar\r\n"
            "    property list<QtObject> pNonFixedTopbarItems: [\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: true\r\n"
            "            pWidth: iBackBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iBackBtn.visible = visibility\r\n"
            "            }\r\n"
            "        },\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: true\r\n"
            "            pWidth: iHelpBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iHelpBtn.visible = visibility\r\n"
            "            }\r\n"
            "        },\r\n"
            "        PlrTopbarElement {\r\n"
            "            pVisible: pIsNowbuxEnabled\r\n"
            "            pWidth: iCfgBtn.width\r\n"
            "            onChangeVisibility: function(visibility) {\r\n"
            "                iCfgBtn.visible = visibility\r\n"
            "            }\r\n"
            "        }\r\n"
            "    ]\r\n"
            "    Row {\r\n"
            "        UiImageButton {\r\n"
            "            id: iSidebarShowBtn\r\n"
            "        }\r\n"
            "        UiImageButton { id: iBackBtn }\r\n"
            "        UiImageButton { id: iHomeBtn }\r\n"
            "        UiImageButton { id: iRecentBtn }\r\n"
            "        UiImageButton { id: iHelpBtn }\r\n"
            "        UiImageButton { id: iCfgBtn }\r\n"
            "    }\r\n"
            "}\r\n");
        auto r = resources({ { QStringLiteral("plr.qml"), cleanPlr },
                             { QStringLiteral("PlrSidebar.qml"), cleanSidebar },
                             { QStringLiteral("PlrTopbar.qml"), cleanTopbar } }, true, {});
        addRegistration(r, false);
        const QByteArray original = r.f.bytes;
        const QString cleanId = QStringLiteral("clean-player-ui");
        const auto stock = LayoutPatcher::analyze(original, cleanId);
        check(stock.supported && !stock.applied,
              "the one-click clean-up is offered on a stock sidebar/title bar");
        const QString cleanPath = temp.filePath(QStringLiteral("layout-clean.exe"));
        write(cleanPath, original);
        const auto applied = LayoutPatcher::apply(cleanPath, cleanId, true);
        check(applied.ok, QStringLiteral("the one-click clean-up applies (%1)").arg(applied.msg));
        const auto cleanBytes = read(cleanPath);
        const auto cleanPlrText = resourceText(cleanBytes, r, QStringLiteral("plr.qml"));
        const auto cleanSidebarText = resourceText(cleanBytes, r, QStringLiteral("PlrSidebar.qml"));
        const auto cleanTopbarText = resourceText(cleanBytes, r, QStringLiteral("PlrTopbar.qml"));
        check(cleanPlrText == cleanPlr,
              "the sidebar instance is left exactly as stock");
        check(cleanSidebarText.contains(QLatin1String("pIsFeatureEnable: false /* BSI CUSTOMIZER: sidebar clean:"))
                  && cleanSidebarText.contains(QLatin1String("pIsFeatureEnable: false // BSI CUSTOMIZER: sidebar clean"))
                  && cleanSidebarText.contains(QLatin1String("pIsFeatureEnable: iSidebarProperties.pIsFullScreenEnabled"))
                  && cleanSidebarText.contains(QLatin1String("pIsFeatureEnable: iSidebarProperties.pIsVolumeEnabled"))
                  && !cleanSidebarText.contains(QLatin1String("pIsScreenshotEnabled")),
              "the sidebar keeps fullscreen and volume and switches screenshot and rotate off");
        check(cleanTopbarText.count(QLatin1String("pVisible: false /* BSI CUSTOMIZER: clean player UI visible:")) == 2
                  && cleanTopbarText.contains(QLatin1String("pVisible: true\r\n            pWidth: iBackBtn.width"))
                  && cleanTopbarText.contains(QLatin1String("visible: false // BSI CUSTOMIZER: clean player UI")),
              "the help/menu elements stop being shown by fReloadTopbarElements while Back stays, and the sidebar toggle is hidden");
        check(cleanTopbarText.contains(QLatin1String("UiImageButton { visible: false /* BSI CUSTOMIZER: clean player UI */; id: iHelpBtn }"))
                  && cleanTopbarText.contains(QLatin1String("UiImageButton { id: iBackBtn }")),
              "an object written on a single line is hidden inline instead of being split across lines");
        check(LayoutPatcher::analyze(cleanBytes, cleanId).applied,
              "the written clean UI is detected on read-back");
        // The clean-up writes nothing into plr.qml, so it never blocks the
        // title-bar move (which is the only option that removes the sidebar).
        check(!cleanPlrText.contains(QLatin1String("BSI CUSTOMIZER")),
              "the clean-up leaves no marker in plr.qml, so the title-bar move is still offered");
        const auto restored = LayoutPatcher::apply(cleanPath, cleanId, false);
        const auto restoredBytes = read(cleanPath);
        check(restored.ok, QStringLiteral("the clean player UI restores (%1)").arg(restored.msg));
        check(resourceText(restoredBytes, r, QStringLiteral("plr.qml")) == cleanPlr
                  && resourceText(restoredBytes, r, QStringLiteral("PlrSidebar.qml")) == cleanSidebar
                  && resourceText(restoredBytes, r, QStringLiteral("PlrTopbar.qml")) == cleanTopbar,
              "turning the clean UI off restores every document byte-for-byte");

        // A clean-up written by an older version only hid the title-bar buttons
        // and the whole sidebar strip. It is reported as an update and the
        // missing half is written without restoring first.
        const QString legacyPlr = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iAppwin\r\n"
            "    property int pSidebarWidth: 32\r\n"
            "    PlrTopbar { id: iTopbar }\r\n"
            "    PlrSidebar {\r\n"
            "        visible: false // BSI CUSTOMIZER: clean player UI\r\n"
            "        id: iSidebar\r\n"
            "        width: 0 /* BSI CUSTOMIZER: clean player UI width: cFNpZGViYXJXaWR0aA== */\r\n"
            "        visible: uiBackend.confGetInstanceBool(\"show_sidebar\")\r\n"
            "    }\r\n"
            "}\r\n");
        auto legacy = resources({ { QStringLiteral("plr.qml"), legacyPlr },
                                  { QStringLiteral("PlrSidebar.qml"), cleanSidebar },
                                  { QStringLiteral("PlrTopbar.qml"), cleanTopbar } }, true, {});
        addRegistration(legacy, false);
        const QString legacyPath = temp.filePath(QStringLiteral("layout-clean-legacy.exe"));
        write(legacyPath, legacy.f.bytes);
        const auto legacyState = LayoutPatcher::analyze(legacy.f.bytes, cleanId);
        check(legacyState.supported && legacyState.applied && legacyState.needsUpdate,
              "a clean-up written by an older version is offered as an update");
        check(LayoutPatcher::apply(legacyPath, cleanId, true).ok,
              "the update completes an older clean-up instead of refusing it");
        const auto updatedSidebar = resourceText(read(legacyPath), legacy, QStringLiteral("PlrSidebar.qml"));
        check(resourceText(read(legacyPath), legacy, QStringLiteral("plr.qml")) == cleanPlr,
              "the update brings the sidebar strip back (the old patch hid it)");
        check(updatedSidebar.contains(QLatin1String("pIsFeatureEnable: false /* BSI CUSTOMIZER: sidebar clean:"))
                  && !updatedSidebar.contains(QLatin1String("iSidebarProperties.pIsScreenshotEnabled")),
              "the update hides the extra sidebar buttons");
        check(!LayoutPatcher::analyze(read(legacyPath), cleanId).needsUpdate,
              "the updated clean-up is reported as complete");
        check(LayoutPatcher::apply(legacyPath, cleanId, false).ok
                  && resourceText(read(legacyPath), legacy, QStringLiteral("plr.qml")) == cleanPlr
                  && resourceText(read(legacyPath), legacy, QStringLiteral("PlrSidebar.qml")) == cleanSidebar
                  && resourceText(read(legacyPath), legacy, QStringLiteral("PlrTopbar.qml")) == cleanTopbar,
              "restoring the updated clean-up leaves the stock documents");

        // MSI 5.9 / 5.10 shape: the sidebar instance is a layout child sized by
        // Layout.preferredWidth and the top bar re-asserts `visible` from a
        // ListModel instead of PlrTopbarElement objects.
        const QString oldPlr = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iAppwin\r\n"
            "    RowLayout {\r\n"
            "        PlrTopbar { id: iTopbar }\r\n"
            "        PlrSidebar {\r\n"
            "            id: iSidebar\r\n"
            "            Layout.preferredWidth: pSidebarWidth\r\n"
            "            visible: uiBackend.confGetInstanceBool(\"show_sidebar\")\r\n"
            "        }\r\n"
            "    }\r\n"
            "}\r\n");
        const QString oldTopbar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "Rectangle {\r\n"
            "    id: iRoot\r\n"
            "    ListModel { id: iTopbarItems }\r\n"
            "    function fReloadTopbarElements() {\r\n"
            "        iTopbarItems.clear()\r\n"
            "        iTopbarItems.append({ \"id\" : iMacroDetail,       \"visible\" : pIsMacroRunning, \"width\" : 8 })\r\n"
            "        iTopbarItems.append({ \"id\" : iCfgBtn,            \"visible\" : true,            \"width\" : iCfgBtn.width })\r\n"
            "        iTopbarItems.append({ \"id\" : iTitleLayout,       \"visible\" : true,            \"width\" : 12 })\r\n"
            "        iTopbarItems.append({ \"id\" : iBackBtn,           \"visible\" : true,            \"width\" : iBackBtn.width })\r\n"
            "        iTopbarItems.append({ \"id\" : iHelpBtn,           \"visible\" : true,            \"width\" : iHelpBtn.width })\r\n"
            "        iTopbarItems.append({ \"id\" : iQuestbtn,          \"visible\" : pIsQuestEnabled, \"width\" : iQuestbtn.width })\r\n"
            "    }\r\n"
            "    Row {\r\n"
            "        UiImageButton { id: iSidebarShowBtn }\r\n"
            "        UiImageButton { id: iBackBtn }\r\n"
            "        UiImageButton { id: iHelpBtn }\r\n"
            "        UiImageButton { id: iCfgBtn }\r\n"
            "        UiImageButton { id: iQuestbtn; visible: pIsQuestEnabled }\r\n"
            "    }\r\n"
            "}\r\n");
        auto old = resources({ { QStringLiteral("plr.qml"), oldPlr },
                               { QStringLiteral("PlrSidebar.qml"), cleanSidebar },
                               { QStringLiteral("PlrTopbar.qml"), oldTopbar } }, true, {});
        addRegistration(old, false);
        const QString oldPath = temp.filePath(QStringLiteral("layout-clean-old.exe"));
        write(oldPath, old.f.bytes);
        const auto oldApplied = LayoutPatcher::apply(oldPath, cleanId, true);
        check(oldApplied.ok, QStringLiteral("the clean-up applies to the MSI 5.9 style title bar (%1)")
                                  .arg(oldApplied.msg));
        const auto oldTopbarText = resourceText(read(oldPath), old, QStringLiteral("PlrTopbar.qml"));
        const auto oldPlrText = resourceText(read(oldPath), old, QStringLiteral("plr.qml"));
        check(oldTopbarText.count(QLatin1String("\"visible\" : false /* BSI CUSTOMIZER: clean player UI visible:")) == 4
                  && oldTopbarText.contains(QLatin1String("\"id\" : iTitleLayout,       \"visible\" : true"))
                  && oldTopbarText.contains(QLatin1String("\"id\" : iBackBtn,           \"visible\" : true"))
                  && oldTopbarText.contains(QLatin1String("id: iQuestbtn; visible: false /* BSI CUSTOMIZER")),
              "the ListModel entries for the menu/help buttons are switched off while the title and Back stay");
        check(oldPlrText == oldPlr,
              "the layout-child sidebar keeps its stock Layout.preferredWidth");
        check(LayoutPatcher::apply(oldPath, cleanId, false).ok
                  && resourceText(read(oldPath), old, QStringLiteral("PlrTopbar.qml")) == oldTopbar
                  && resourceText(read(oldPath), old, QStringLiteral("plr.qml")) == oldPlr,
              "the MSI 5.9 style clean-up also restores byte-for-byte");
    }

    std::printf("[about] Settings About page offers every stock link and the About text\n");
    {
        const QString about = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "import QtQuick.Layouts 1.15\r\n"
            "Item {\r\n"
            "    objectName: \"aboutSettings\"\r\n"
            "    UiColumnLayout {\r\n"
            "        anchors.fill: parent\r\n"
            "        UiScrollView {\r\n"
            "            id: iScrollView\r\n"
            "            contentHeight: iContentLayout.height + 10\r\n"
            "            UiColumnLayout {\r\n"
            "                id: iContentLayout\r\n"
            "                width: 548\r\n"
            "                spacing: 16\r\n"
            "                UiRowLayout {\r\n"
            "                    Layout.fillWidth: true\r\n"
            "                    UiImage { asset: \"BlueStacks5Beta\" }\r\n"
            "                    Text { text: qsTranslate(\"QObject\", \"%1\").arg(uiBackend.productName) }\r\n"
            "                }\r\n"
            "                UiColumnLayout {\r\n"
            "                    Layout.fillWidth: true\r\n"
            "                    spacing: 2\r\n"
            "                    UiTextUrl {\r\n"
            "                        text: pUpdateVersion + \" available . <a href=\\\"javascript:void(0)\\\">More info</a>\"\r\n"
            "                        visible: pUpdateAvailable\r\n"
            "                        onLinkActivated: Qt.openUrlExternally(pMoreInfoURL)\r\n"
            "                    }\r\n"
            "                    UiButton {\r\n"
            "                        Layout.preferredWidth: 124\r\n"
            "                        text: pUpdateAvailable ? \"Download Update\" : \"Check for updates\"\r\n"
            "                        visible: !iLoader.visible\r\n"
            "                        onClicked: plrBackend.checkForUpdate()\r\n"
            "                    }\r\n"
            "                }\r\n"
            "                UiColumnLayout {\r\n"
            "                    Layout.fillWidth: true\r\n"
            "                    visible: uiBackend.oem !== \"msi5\"\r\n"
            "                    Text { text: qsTranslate(\"QObject\", \"Important links\") }\r\n"
            "                    UiColumnLayout {\r\n"
            "                        Layout.fillWidth: true\r\n"
            "                        UiTextUrl {\r\n"
            "                            id: iSupportEmailText\r\n"
            "                            text: \"Support email - <a href=\\\"javascript:void(0)\\\">support@bluestacks.com</a>\"\r\n"
            "                        }\r\n"
            "                        UiTextUrl {\r\n"
            "                            id: iSupportText\r\n"
            "                            text: \"Support - <a href=\\\"javascript:void(0)\\\">support.bluestacks.com</a>\"\r\n"
            "                        }\r\n"
            "                        UiTextUrl {\r\n"
            "                            id: iWebsiteText\r\n"
            "                            text: \"Website - <a href=\\\"javascript:void(0)\\\">www.bluestacks.com</a>\"\r\n"
            "                            onLinkActivated: Qt.openUrlExternally(\"http://www.bluestacks.com\")\r\n"
            "                        }\r\n"
            "                    }\r\n"
            "                    UiRowLayout {\r\n"
            "                        visible: uiBackend.oem === \"nxt_cn\"\r\n"
            "                        Text { text: \"联系我们:\" }\r\n"
            "                        UiRowLayout {\r\n"
            "                            UiButton { text: \"QQ群\"; onClicked: Qt.openUrlExternally(\"https://jq.qq.com/?_wv=1027&k=6x617Pxu\") }\r\n"
            "                        }\r\n"
            "                    }\r\n"
            "                }\r\n"
            "                Rectangle {\r\n"
            "                    Layout.preferredHeight: 1\r\n"
            "                    color: UiTheme.colors.seperator\r\n"
            "                    visible: uiBackend.oem !== \"msi5\"\r\n"
            "                }\r\n"
            "                UiColumnLayout {\r\n"
            "                    Layout.fillWidth: true\r\n"
            "                    visible: uiBackend.oem !== \"msi5\"\r\n"
            "                    Text { text: qsTranslate(\"QObject\", \"About and other info\") }\r\n"
            "                    UiTextUrl {\r\n"
            "                        id: iTermsOfUseText\r\n"
            "                        text: qsTranslate(\"QObject\", \"Copyright 2026. By using it you agree to the \") + \"<a href=\\\"javascript:void(0)\\\">\" + qsTranslate(\"QObject\", \"Terms of use\") + \"</a>\"\r\n"
            "                        onLinkActivated: uiBackend.openHelpArticle(\"terms_of_use\")\r\n"
            "                    }\r\n"
            "                }\r\n"
            "            }\r\n"
            "        }\r\n"
            "    }\r\n"
            "}\r\n");
        auto r = resources({ { QStringLiteral("PlrAboutSetting.qml"), about } }, true, {});
        addRegistration(r, false);
        const QByteArray original = r.f.bytes;
        const auto stock = AboutPatcher::analyze(original);
        check(stock.supported && !stock.applied, "the stock About page is recognized");
        check(stock.sectionsHidden && stock.config.showSections,
              "the oem-gated sections are reported as hidden, yet the editor offers them shown: "
              "`visible: uiBackend.oem !== \"msi5\"` is false on every MSI install, so leaving it "
              "alone is what made Apply look like it did nothing there");
        check(stock.config.text
                  == QStringLiteral("Copyright 2026. By using it you agree to the Terms of use"),
              QStringLiteral("the About text editor is prefilled with the stock text (%1)")
                  .arg(stock.config.text));
        check(stock.config.links.size() == 3,
              QStringLiteral("every stock link row is offered to the editor (%1)")
                  .arg(stock.config.links.size()));
        check(stock.config.links.value(0).name == QStringLiteral("Support email")
                  && stock.config.links.value(0).url == QStringLiteral("support@bluestacks.com"),
              QStringLiteral("the e-mail row keeps its name and address (%1 -> %2)")
                  .arg(stock.config.links.value(0).name, stock.config.links.value(0).url));
        check(stock.config.links.value(2).name == QStringLiteral("Website")
                  && stock.config.links.value(2).url == QStringLiteral("http://www.bluestacks.com"),
              QStringLiteral("the website row keeps its name and handler URL (%1 -> %2)")
                  .arg(stock.config.links.value(2).name, stock.config.links.value(2).url));

        const QString aboutPath = temp.filePath(QStringLiteral("about-page.exe"));
        write(aboutPath, original);
        AboutPatcher::Config config = stock.config;
        config.showSections = true; // the Unhide button was pressed
        config.text = QStringLiteral("Modded by Mr.Pc Gamer");
        config.links[0].name = QStringLiteral("Telegram");
        config.links[0].url = QStringLiteral("https://t.me/mrpcgamer");
        config.links[1].name = QStringLiteral("Discord");
        config.links[1].url = QStringLiteral("https://discord.gg/example");
        config.links[2].name = QStringLiteral("Channel");
        config.links[2].url = QStringLiteral("https://youtube.com/@MrPcGamer");
        const auto applied = AboutPatcher::apply(aboutPath, true, config);
        check(applied.ok, QStringLiteral("the About page is updated (%1)").arg(applied.msg));
        QString patched = resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"));
        if (!patched.contains(QLatin1String("t.me/mrpcgamer"))) {
            // Only a failing run needs this, and a failing Windows run is
            // otherwise invisible: print the rows the patch actually wrote.
            QStringList rows;
            for (const QString &line : patched.split(QLatin1Char('\n')))
                if (line.contains(QLatin1String("href="))) rows << line.trimmed();
            ciNotice(QStringLiteral("about rows after apply: %1")
                         .arg(rows.mid(0, 6).join(QStringLiteral(" | "))));
        }
        check(patched.contains(QLatin1String("Modded by Mr.Pc Gamer"))
                  && patched.contains(QLatin1String("Telegram - <a href=\\\"https://t.me/mrpcgamer\\\">t.me/mrpcgamer</a>"))
                  && patched.contains(QLatin1String("Discord - <a href=\\\"https://discord.gg/example\\\">discord.gg/example</a>"))
                  && patched.contains(QLatin1String("Channel - <a href=\\\"https://youtube.com/@MrPcGamer\\\">youtube.com/@MrPcGamer</a>")),
              "all three stock links are rewritten with the edited names and addresses");
        check(patched.contains(QLatin1String("onLinkActivated: Qt.openUrlExternally(\"https://t.me/mrpcgamer\")"))
                  && patched.count(QLatin1String("// BSI CUSTOMIZER: about handler")) == 2,
              "rows without a stock handler get one, and it is tagged so the undo removes it");

        // The address and the words that print it are two fields: a row can
        // open https://t.me/mrpcgamer and still print "Join the channel".
        AboutPatcher::Config named = config;
        named.links[0].label = QStringLiteral("Join the channel");
        check(AboutPatcher::apply(aboutPath, true, named).ok,
              "a link's displayed text can be set separately from its address");
        patched = resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"));
        check(patched.contains(QLatin1String("Telegram - <a href=\\\"https://t.me/mrpcgamer\\\">Join the channel</a>")),
              "the row prints the edited text while the href stays the edited address");
        auto namedState = AboutPatcher::analyze(read(aboutPath));
        check(namedState.applied && namedState.config.links.size() == 3
                  && namedState.config.links.value(0).label == QStringLiteral("Join the channel")
                  && namedState.config.links.value(0).url == QStringLiteral("https://t.me/mrpcgamer"),
              "the displayed text and the address both read back, independently");
        // A row whose text is left empty keeps printing the address, the way
        // the stock page does, and reading that back changes nothing.
        AboutPatcher::Config plain = config;
        plain.links[2].label = QString();
        check(AboutPatcher::apply(aboutPath, true, plain).ok,
              "a row without an edited text still applies");
        patched = resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"));
        check(patched.contains(QLatin1String("Channel - <a href=\\\"https://youtube.com/@MrPcGamer\\\">youtube.com/@MrPcGamer</a>")),
              "an empty display text falls back to printing the address");
        const auto plainState = AboutPatcher::analyze(read(aboutPath));
        check(plainState.config.links.value(2).label == QStringLiteral("youtube.com/@MrPcGamer"),
              "that fallback is what the editor is then prefilled with");
        check(patched.contains(QLatin1String("Important links"))
                  && patched.contains(QLatin1String("About and other info"))
                  && patched.count(QLatin1String("visible: true /* BSI CUSTOMIZER: about shown:")) == 2,
              "the page keeps its layout and the hidden MSI sections are switched back on");
        auto state = AboutPatcher::analyze(read(aboutPath));
        check(state.applied && !state.sectionsHidden && state.config.links.size() == 3
                  && state.config.links.value(1).url == QStringLiteral("https://discord.gg/example"),
              "the applied text and every link read back from the page");

        // Per-link hide: one row is switched off, the others stay.
        AboutPatcher::Config perLink = config;
        perLink.links[1].hidden = true;
        check(AboutPatcher::apply(aboutPath, true, perLink).ok,
              "a single link can be hidden without touching the rest");
        patched = resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"));
        check(patched.contains(QLatin1String("visible: false // BSI CUSTOMIZER: about hidden"))
                  && !patched.contains(QLatin1String("Discord - <a"))
                  && patched.contains(QLatin1String("Telegram - <a"))
                  && patched.contains(QLatin1String("Channel - <a")),
              "the hidden row disappears and the other two links stay");
        state = AboutPatcher::analyze(read(aboutPath));
        check(state.config.links.value(1).hidden && !state.config.links.value(0).hidden,
              "the per-link hide state reads back");

        // The sections can also be left exactly as the build draws them.
        AboutPatcher::Config gated = perLink;
        gated.showSections = false;
        check(AboutPatcher::apply(aboutPath, true, gated).ok
                  && !resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"))
                          .contains(QLatin1String("BSI CUSTOMIZER: about shown:"))
                  && resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"))
                         .contains(QLatin1String("visible: uiBackend.oem !== \"msi5\"")),
              "the sections can be left hidden on the builds that hide them");

        check(AboutPatcher::apply(aboutPath, true, config).ok
                  && resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"))
                         .count(QLatin1String("BSI CUSTOMIZER: about-config ")) == 1,
              "applying different content updates the same page instead of stacking edits");
        const auto restored = AboutPatcher::apply(aboutPath, false, {});
        check(restored.ok, QStringLiteral("the stock About page restores (%1)").arg(restored.msg));
        check(resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml")) == about,
              "restoring the About page is byte-for-byte identical to stock");

        // The values prefill hands the editors can be applied unchanged: the
        // page then shows the same words through the custom rows, the e-mail
        // row keeps a working mailto: target, and the undo is still byte-exact.
        const QString prefillPath = temp.filePath(QStringLiteral("about-prefill.exe"));
        write(prefillPath, original);
        const auto prefilled = AboutPatcher::apply(prefillPath, true, stock.config);
        const auto prefilledText = resourceText(read(prefillPath), r,
                                                QStringLiteral("PlrAboutSetting.qml"));
        check(prefilled.ok
                  && prefilledText.contains(QLatin1String("Copyright 2026. By using it you agree to the Terms of use"))
                  && prefilledText.contains(QLatin1String("Website - <a href=\\\"http://www.bluestacks.com\\\">www.bluestacks.com</a>"))
                  && prefilledText.contains(QLatin1String("Support email - <a href=\\\"mailto:support@bluestacks.com\\\">support@bluestacks.com</a>")),
              QStringLiteral("the prefilled stock values can be applied unchanged (%1)").arg(prefilled.msg));
        const auto prefilledState = AboutPatcher::analyze(read(prefillPath));
        check(prefilledState.config.links.value(0).url
                  == QStringLiteral("mailto:support@bluestacks.com"),
              "the e-mail row reads back as the mailto: target it now uses");
        // The editor offers both sections shown (that is the only value that is
        // right on MSI, where the stock binding hides them, and on BlueStacks,
        // where they are already visible), so applying the prefilled values
        // carries that Unhide step with it. Turning the toggle off keeps the
        // build's own binding - that is the check above.
        check(prefilledText.count(QLatin1String("visible: true /* BSI CUSTOMIZER: about shown:"))
                  == 2,
              "the prefilled values carry the Unhide step the editor offers by default");
        check(AboutPatcher::apply(prefillPath, true, stock.config).msg
                  == QStringLiteral("This About page content is already applied."),
              "applying the same prefill twice is a no-op rather than a rewrite");
        check(AboutPatcher::apply(prefillPath, false, {}).ok
                  && resourceText(read(prefillPath), r, QStringLiteral("PlrAboutSetting.qml")) == about,
              "and that prefill-only patch also restores byte-for-byte");

        // Text only: every stock link row is hidden and the page still shows
        // the custom text.
        const QString textPath = temp.filePath(QStringLiteral("about-text-only.exe"));
        write(textPath, original);
        AboutPatcher::Config textOnly;
        textOnly.text = QStringLiteral("Just my text");
        textOnly.showSections = true;
        const auto textOnlyApplied = AboutPatcher::apply(textPath, true, textOnly);
        const auto textOnlyState = AboutPatcher::analyze(read(textPath));
        check(textOnlyApplied.ok
                  && resourceText(read(textPath), r, QStringLiteral("PlrAboutSetting.qml"))
                         .contains(QLatin1String("Just my text"))
                  && textOnlyState.applied && textOnlyState.config.links.isEmpty()
                  && AboutPatcher::apply(textPath, false, {}).ok
                  && resourceText(read(textPath), r, QStringLiteral("PlrAboutSetting.qml")) == about,
              "a text-only About patch applies and restores byte-for-byte");

        // A build whose link rows are shaped differently (renamed ids) must
        // still get the text editor instead of losing the whole card.
        QString odd = about;
        odd.replace(QStringLiteral("iSupportEmailText"), QStringLiteral("iMailRow"));
        odd.replace(QStringLiteral("iSupportText"), QStringLiteral("iHelpRow"));
        odd.replace(QStringLiteral("iWebsiteText"), QStringLiteral("iSiteRow"));
        auto oddResources = resources({ { QStringLiteral("PlrAboutSetting.qml"), odd } }, true, {});
        addRegistration(oddResources, false);
        const QString oddPath = temp.filePath(QStringLiteral("about-odd-rows.exe"));
        write(oddPath, oddResources.f.bytes);
        const auto oddState = AboutPatcher::analyze(oddResources.f.bytes);
        check(oddState.supported && oddState.config.links.isEmpty()
                  && !oddState.config.text.isEmpty(),
              "an About page whose rows use other ids still reports the text it prints");
        AboutPatcher::Config oddConfig;
        oddConfig.text = QStringLiteral("Only the sentence is edited here");
        oddConfig.showSections = true;
        check(AboutPatcher::apply(oddPath, true, oddConfig).ok
                  && resourceText(read(oddPath), oddResources,
                                  QStringLiteral("PlrAboutSetting.qml"))
                         .contains(QLatin1String("Only the sentence is edited here"))
                  && AboutPatcher::apply(oddPath, false, {}).ok
                  && resourceText(read(oddPath), oddResources,
                                  QStringLiteral("PlrAboutSetting.qml")) == odd,
              "such a build gets a working text editor and restores byte-for-byte");
    }

    std::printf("[images] embedded pictures are listed, previewed and replaced\n");
    {
        // 1x1 red and 2x2 red/green RGBA PNGs, byte-exact fixtures.
        const QByteArray logo = QByteArray::fromHex(
            "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d4944"
            "415478da63f8cfc0f01f00050001ff56c72f0d0000000049454e44ae426082");
        const QByteArray bigger = QByteArray::fromHex(
            "89504e470d0a1a0a0000000d494844520000000200000002080600000072b60d24000000114944"
            "415478da63f8cfc0f01f8441080c0145cc07f9b7c886d10000000049454e44ae426082");
        auto r = nestedResources({ { QStringLiteral("images/logo.png"), logo },
                                   { QStringLiteral("ui/notes.txt"), QByteArray("not a picture\n") } });
        addRegistration(r, false);

        const auto images = EmbeddedImages::list(r.f.bytes, false);
        check(images.size() == 1 && images.first().path == QLatin1String("images/logo.png"),
              QStringLiteral("only the picture is listed by default (%1 entries)").arg(images.size()));
        const QString entry = images.isEmpty() ? QString() : images.first().path;
        check(!images.isEmpty() && images.first().image
                  && images.first().family == QLatin1String("png")
                  && images.first().width == 1 && images.first().height == 1
                  && images.first().size == logo.size(),
              QStringLiteral("the PNG entry carries its size and 1x1 dimensions (%1)")
                  .arg(entry));

        const auto everything = EmbeddedImages::list(r.f.bytes, true);
        check(everything.size() == 2
                  && std::any_of(everything.cbegin(), everything.cend(),
                                 [](const EmbeddedImages::Asset &asset) {
                                     return asset.path == QLatin1String("ui/notes.txt")
                                         && !asset.image;
                                 }),
              "the other embedded files are listed too, marked as not replaceable here");

        check(EmbeddedImages::sniffFormat(logo) == QLatin1String("png")
                  && EmbeddedImages::sniffFormat(QByteArray("GIF89a not really a gif")) == QLatin1String("gif")
                  && EmbeddedImages::sniffFormat(QByteArray("plain text")).isEmpty(),
              "a replacement's real format is read from its bytes, not its name");
        const QString preview = EmbeddedImages::previewDataUrl(logo, QStringLiteral("png"), 64);
        check(preview.startsWith(QLatin1String("data:image/png;base64,")) && preview.size() > 40,
              "the list gets a renderable preview of the embedded picture");

        // The same picture through a real file, for the builds where QML will
        // not take a data: URL. The suffix comes from the entry's own format.
        const QString previewFile = EmbeddedImages::previewFileUrl(logo, QStringLiteral("png"));
        check(previewFile.startsWith(QLatin1String("file://"))
                  && previewFile.endsWith(QLatin1String(".png"))
                  && QFileInfo(QUrl(previewFile).toLocalFile()).size() == logo.size(),
              "a picture also previews through a temp file when a data URL is refused");

        // Older players carry pictures under names that do not say what they
        // hold: the bytes decide, not the extension.
        const QByteArray svgIcon =
            "<svg width=\"16\" height=\"16\" xmlns=\"http://www.w3.org/2000/svg\">"
            "<rect width=\"16\" height=\"16\" fill=\"#123456\"/></svg>";
        const QString svgPreview = EmbeddedImages::previewDataUrl(svgIcon, QStringLiteral("dat"), 64);
        check(svgPreview.startsWith(QLatin1String("data:image/svg+xml;base64,")),
              "markup stored under another name still previews as SVG");
        const QString svgFile = EmbeddedImages::previewFileUrl(svgIcon, QStringLiteral("bin"));
        check(svgFile.endsWith(QLatin1String(".svg")),
              "the temp-file fallback names SVG by what the bytes are, not by the entry name");
        check(EmbeddedImages::previewDataUrl(QByteArray("not a picture"), QStringLiteral("png"), 64).isEmpty()
                  && EmbeddedImages::previewFileUrl(QByteArray(), QStringLiteral("png")).isEmpty(),
              "an unreadable entry reports no preview instead of inventing one");

        const QString exePath = temp.filePath(QStringLiteral("images.exe"));
        const QString newLogo = temp.filePath(QStringLiteral("new-logo.png"));
        write(exePath, r.f.bytes);
        write(newLogo, bigger);
        const auto replaced = EmbeddedImages::replace(exePath, QStringLiteral("images/logo.png"), newLogo);
        check(replaced.ok, QStringLiteral("the embedded PNG is replaced (%1)").arg(replaced.msg));
        check(QFileInfo::exists(exePath + QStringLiteral(".bak")),
              "the original EXE is backed up before the first image write");

        const auto after = EmbeddedImages::list(read(exePath), false);
        check(after.size() == 1 && after.first().width == 2 && after.first().height == 2
                  && EmbeddedImages::readEntry(exePath, after.first().entryOff, after.first().flags)
                         == bigger,
              "the replacement reads back at its own size, byte for byte");

        const QString wrong = temp.filePath(QStringLiteral("wrong.gif"));
        write(wrong, QByteArray("GIF89a") + QByteArray(40, char(0)));
        const auto refused = EmbeddedImages::replace(exePath, QStringLiteral("images/logo.png"), wrong);
        check(!refused.ok && refused.msg.contains(QLatin1String("PNG")),
              QStringLiteral("a different image format is refused instead of written (%1)").arg(refused.msg));
        check(EmbeddedImages::replace(exePath, QStringLiteral("images/missing.png"), newLogo).ok == false,
              "an entry that is not in the EXE is refused");

        const QString back = temp.filePath(QStringLiteral("logo-back.png"));
        write(back, logo);
        const auto restored = EmbeddedImages::replace(exePath, QStringLiteral("images/logo.png"), back);
        const auto finalList = EmbeddedImages::list(read(exePath), false);
        check(restored.ok && finalList.size() == 1 && finalList.first().width == 1
                  && EmbeddedImages::readEntry(exePath, finalList.first().entryOff, finalList.first().flags)
                         == logo,
              "the stock picture can be written back over the replacement");
    }

    std::printf("[resources] every embedded file is listed, edited and never grown\n");
    {
        const QByteArray logo = QByteArray::fromHex(
            "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d4944"
            "415478da63f8cfc0f01f00050001ff56c72f0d0000000049454e44ae426082");
        const QString qmlSource = QStringLiteral(
            "import QtQuick\n"
            "Item {\n"
            "    width: 100\n"
            "    height: 100\n"
            "    Text { text: qsTr(\"stock\") }\n"
            "}\n");
        const QString svgSource = QStringLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
            "<path fill=\"#ffffff\" d=\"M0 0h16v16H0z\"/></svg>");
        auto r = nestedResources({ { QStringLiteral("ui/panel.qml"), qmlSource.toUtf8() },
                                   { QStringLiteral("icons/star.svg"), svgSource.toUtf8() },
                                   { QStringLiteral("images/logo.png"), logo },
                                   { QStringLiteral("cache/panel.qmlc"), QByteArray("qv4cdata\0\0\0\0", 12) } });
        addRegistration(r, false);

        const auto entries = EmbeddedResources::list(r.f.bytes, true);
        const auto find = [&entries](const QString &path) {
            for (const auto &entry : entries)
                if (entry.path == path) return entry;
            return EmbeddedResources::Entry();
        };
        const auto qmlEntry = find(QStringLiteral("ui/panel.qml"));
        const auto svgEntry = find(QStringLiteral("icons/star.svg"));
        const auto pngEntry = find(QStringLiteral("images/logo.png"));
        const auto cacheEntry = find(QStringLiteral("cache/panel.qmlc"));
        check(entries.size() == 4, QStringLiteral("every embedded file is listed (%1)").arg(entries.size()));
        check(qmlEntry.kind == EmbeddedResources::Kind::Text && qmlEntry.text
                  && qmlEntry.folder == QLatin1String("ui")
                  && qmlEntry.format == QLatin1String("qml"),
              "QML is listed as an editable text resource");
        check(svgEntry.kind == EmbeddedResources::Kind::Svg && svgEntry.text
                  && svgEntry.width == 16 && svgEntry.height == 16,
              "SVG is listed as code, with its viewport size");
        check(pngEntry.kind == EmbeddedResources::Kind::Image && pngEntry.width == 1
                  && pngEntry.height == 1,
              "pictures keep the preview path the Images tab used");
        check(cacheEntry.kind == EmbeddedResources::Kind::Binary && !cacheEntry.text,
              "compiled caches are listed but never offered as code");

        check(EmbeddedResources::decodeText(qmlSource.toUtf8()) == qmlSource
                  && EmbeddedResources::decodeText(QByteArray("\x00\x01\x02", 3)).isEmpty(),
              "only real UTF-8 is handed to the code editor");

        const QByteArray original = r.f.bytes;
        const qint64 originalSize = original.size();
        const auto room = EmbeddedResources::budget(original, qmlEntry.entryOff, qmlEntry.leaf);
        check(room.known && room.capacity == room.used && room.used > 0,
              QStringLiteral("the entry's in-place space is measured (%1)").arg(room.detail));

        // The rule the whole tab is built on: nothing may need the file to grow.
        QString noise;
        noise.reserve(8000);
        quint32 seed = 12345;
        for (int i = 0; i < 8000; ++i) {
            seed = seed * 1103515245u + 12345u;
            noise += QChar(ushort('a' + (seed >> 16) % 26));
        }
        const QString longer = qmlSource + noise;
        check(EmbeddedResources::payloadSizeFor(longer.toUtf8(), qmlEntry.flags) > room.capacity,
              "a longer version of the same file is over the entry's space");

        const QString exePath = temp.filePath(QStringLiteral("resources.exe"));
        write(exePath, original);
        const auto refusedText = EmbeddedResources::replaceText(exePath, qmlEntry.path, longer);
        check(!refusedText.ok && refusedText.msg.contains(QLatin1String("never made bigger"))
                  && read(exePath) == original,
              QStringLiteral("a QML edit that grows the embedded file is refused outright (%1)")
                  .arg(refusedText.msg));
        check(!QFileInfo::exists(exePath + QStringLiteral(".bak")),
              "a refused edit does not even create a backup: the EXE was never opened for writing");

        const QString shorter = QStringLiteral("import QtQuick\nItem { width: 10 }\n");
        const auto saved = EmbeddedResources::replaceText(exePath, qmlEntry.path, shorter);
        check(saved.ok, QStringLiteral("a QML file that fits in its own space is saved (%1)").arg(saved.msg));
        const QByteArray afterSave = read(exePath);
        check(afterSave.size() == originalSize,
              "the EXE is exactly the same size after the write - nothing was appended");
        check(EmbeddedResources::readEntry(exePath, qmlEntry.entryOff, qmlEntry.flags)
                  == shorter.toUtf8(),
              "the edited QML reads back byte for byte");

        const auto savedList = EmbeddedResources::list(afterSave, true);
        bool textReadBack = false;
        for (const auto &entry : savedList)
            if (entry.path == QLatin1String("ui/panel.qml") && entry.text) textReadBack = true;
        check(textReadBack, "the list still offers the edited file as code");

        // Same rule for pictures and binary entries.
        QImage wider(32, 32, QImage::Format_ARGB32);
        for (int y = 0; y < wider.height(); ++y)
            for (int x = 0; x < wider.width(); ++x)
                wider.setPixelColor(x, y, QColor((x * 37) % 256, (y * 53) % 256, (x * y) % 256));
        QByteArray widerPng;
        QBuffer widerBuffer(&widerPng);
        widerBuffer.open(QIODevice::WriteOnly);
        wider.save(&widerBuffer, "PNG");
        widerBuffer.close();
        const QString biggerPng = temp.filePath(QStringLiteral("bigger.png"));
        write(biggerPng, widerPng);
        const auto refusedPicture = EmbeddedResources::replacePicture(
            exePath, pngEntry.path, biggerPng);
        check(!refusedPicture.ok && read(exePath) == afterSave,
              QStringLiteral("a bigger picture is refused, not grown into the file (%1)")
                  .arg(refusedPicture.msg));

        const auto refusedBinary = EmbeddedResources::replaceBytes(
            exePath, cacheEntry.path, QByteArray(4096, char(0x42)));
        check(!refusedBinary.ok && read(exePath) == afterSave,
              QStringLiteral("an oversized binary resource is refused (%1)").arg(refusedBinary.msg));

        const auto svgEdit = EmbeddedResources::replaceText(
            exePath, svgEntry.path, QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                                                  "width=\"16\" height=\"16\"/>"));
        check(svgEdit.ok && read(exePath).size() == originalSize,
              QStringLiteral("SVG markup is editable in place (%1)").arg(svgEdit.msg));
        check(EmbeddedResources::readEntry(exePath, svgEntry.entryOff, svgEntry.flags)
                  .contains("<svg"),
              "the edited SVG reads back as markup");
    }

    // A compressed entry whose new payload no longer fits its own slot, while the
    // file it decodes to is EXACTLY the same size: the hard rule allows the edit,
    // so the record is rebuilt in free space the file already has and the tree
    // node is re-pointed at it. Nothing is appended and the EXE size is unchanged.
    {
        // Long and extremely repetitive on purpose: zlib turns it into a few
        // dozen bytes, so the same-length incompressible edit below cannot
        // possibly fit the slot it occupies.
        QString qmlSource = QStringLiteral("import QtQuick\nItem {\n");
        for (int line = 0; line < 120; ++line)
            qmlSource += QStringLiteral("    property string field%1: \"%2\"\n")
                             .arg(line).arg(QStringLiteral("stock"));
        qmlSource += QStringLiteral("}\n");
        auto r = nestedResources({ { QStringLiteral("ui/panel.qml"), qmlSource.toUtf8() },
                                   { QStringLiteral("icons/star.svg"),
                                     QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                                                "width=\"16\" height=\"16\"/>") } });
        addRegistration(r, false);

        const auto entries = EmbeddedResources::list(r.f.bytes, true);
        EmbeddedResources::Entry qmlEntry;
        for (const auto &entry : entries)
            if (entry.path == QLatin1String("ui/panel.qml")) qmlEntry = entry;
        const auto room = EmbeddedResources::budget(r.f.bytes, qmlEntry.entryOff, qmlEntry.leaf);
        check(qmlEntry.flags == 1 && room.known && room.capacity == room.used,
              QStringLiteral("the compressed QML slot is measured (%1)").arg(room.detail));

        // Same length, incompressible: the decoded file is the same size, the
        // zlib payload is not.
        QString scrambled = qmlSource;
        quint32 seed = 987654321u;
        for (int i = 0; i < scrambled.size(); ++i) {
            if (scrambled.at(i) == QLatin1Char('\n'))
                continue;
            seed = seed * 1664525u + 1013904223u;
            scrambled[i] = QChar(ushort('A' + (seed >> 24) % 26));
        }
        const QByteArray scrambledBytes = scrambled.toUtf8();
        const qint64 needed = EmbeddedResources::payloadSizeFor(scrambledBytes, qmlEntry.flags);
        check(scrambledBytes.size() == qmlEntry.size && needed > room.capacity,
              QStringLiteral("a same-size edit can still overrun the compressed slot (%1 > %2)")
                  .arg(needed).arg(room.capacity));

        const QString exePath = temp.filePath(QStringLiteral("resources-move.exe"));
        write(exePath, r.f.bytes);
        const auto saved = EmbeddedResources::replaceText(exePath, qmlEntry.path, scrambled);
        check(saved.ok && saved.msg.contains(QLatin1String("moved into free space")),
              QStringLiteral("the payload is rebuilt in free space instead of being refused (%1)")
                  .arg(saved.msg));
        const QByteArray afterMove = read(exePath);
        check(afterMove.size() == r.f.bytes.size(),
              "relocating a resource never changes the EXE size");

        const auto movedList = EmbeddedResources::list(afterMove, true);
        check(movedList.size() == entries.size(),
              QStringLiteral("the moved file is still listed exactly once (%1 entries)")
                  .arg(movedList.size()));
        bool found = false;
        for (const auto &entry : movedList) {
            if (entry.path != QLatin1String("ui/panel.qml")) continue;
            found = true;
            check(entry.entryOff != qmlEntry.entryOff,
                  "the tree node now points at the new record");
            check(EmbeddedResources::readEntry(exePath, entry.entryOff, entry.flags)
                      == scrambledBytes,
                  "the moved QML reads back byte for byte through the tree");
        }
        check(found, "the moved QML is still addressable by its resource path");

        // A second edit on the relocated entry still obeys both size rules.
        const auto again = EmbeddedResources::replaceText(
            exePath, qmlEntry.path, QStringLiteral("import QtQuick\nItem {}\n"));
        check(again.ok && read(exePath).size() == r.f.bytes.size(),
              QStringLiteral("the relocated entry stays editable (%1)").arg(again.msg));
    }

    // ------------------------------------------------------------------
    // Self-update through the Discord releases bot (docs/UPDATER.md)
    //
    // Everything the updater decides before a single byte reaches the disk is
    // a pure function, so it is checked here against fixed vectors - including
    // one signature produced by updater-bot/publish.mjs, which keeps the Node
    // and C++ implementations of the signed text in sync.
    // ------------------------------------------------------------------
    std::printf("[updater] manifest validation, version order, signature, file checks\n");
    {
        check(UpdateChecker::compareVersions(QStringLiteral("2.0.1"), QStringLiteral("2.0.0")) > 0,
              "2.0.1 is newer than 2.0.0");
        check(UpdateChecker::compareVersions(QStringLiteral("2.0.0"), QStringLiteral("2.0.0")) == 0,
              "the same version compares equal");
        check(UpdateChecker::compareVersions(QStringLiteral("v2.0.1"), QStringLiteral("2.0.1")) == 0,
              "a leading v is packaging, not a version difference");
        check(UpdateChecker::compareVersions(QStringLiteral("2.0"), QStringLiteral("2.0.0")) == 0,
              "2.0 and 2.0.0 are the same build");
        check(UpdateChecker::compareVersions(QStringLiteral("2.0.10"), QStringLiteral("2.0.9")) > 0,
              "2.0.10 is newer than 2.0.9 (numeric, not textual)");
        check(UpdateChecker::compareVersions(QStringLiteral("2.0.1-beta"), QStringLiteral("2.0.1")) < 0,
              "a pre-release sorts before its release");
        check(UpdateChecker::compareVersions(QStringLiteral("1.9.9"), QStringLiteral("2.0.0")) < 0,
              "a major bump wins");
        check(UpdateChecker::normalizedVersion(QStringLiteral("  v2.0.1 ")) == QLatin1String("2.0.1"),
              "normalizedVersion trims and drops the v");

        const QByteArray sha = QByteArrayLiteral(
            "4de96eb5f3d5260c89e2dc2f21c2076be68ba4511e8399ce695bdf542a8354ac");
        QJsonObject good{
            { QStringLiteral("schema"), 1 },
            { QStringLiteral("app"), QStringLiteral("BSI Customizer") },
            { QStringLiteral("version"), QStringLiteral("v2.0.1") },
            { QStringLiteral("file"), QStringLiteral("BSI-Customizer-2.0.1-win64.exe") },
            { QStringLiteral("size"), 1200000 },
            { QStringLiteral("sha256"), QString::fromLatin1(sha) },
            { QStringLiteral("notes"), QStringLiteral("Fixes the About editor.") },
        };
        QString manifestError;
        const QJsonObject parsed = UpdateChecker::parseManifest(
            QJsonDocument(good).toJson(QJsonDocument::Compact), &manifestError);
        check(!parsed.isEmpty()
                  && parsed.value(QStringLiteral("version")).toString() == QLatin1String("2.0.1"),
              QStringLiteral("a valid manifest parses and its version is normalized (%1)").arg(manifestError));
        check(parsed.value(QStringLiteral("sha256")).toString().toLower() == QString::fromLatin1(sha),
              "the SHA-256 from the manifest survives parsing");

        const auto rejected = [](const QJsonObject &manifest, const QString &why) {
            QString error;
            const QJsonObject out = UpdateChecker::parseManifest(
                QJsonDocument(manifest).toJson(QJsonDocument::Compact), &error);
            check(out.isEmpty() && !error.isEmpty(),
                  QStringLiteral("%1 is refused (%2)").arg(why, error));
        };
        QJsonObject bad = good;
        bad.insert(QStringLiteral("schema"), 2);
        rejected(bad, "a future schema");
        bad = good;
        bad.remove(QStringLiteral("sha256"));
        rejected(bad, "a manifest without a SHA-256");
        bad = good;
        bad.insert(QStringLiteral("sha256"), QStringLiteral("abc"));
        rejected(bad, "a truncated SHA-256");
        bad = good;
        bad.insert(QStringLiteral("file"), QStringLiteral("..\\evil.exe"));
        rejected(bad, "a file name with a path");
        bad = good;
        bad.insert(QStringLiteral("file"), QStringLiteral("notes.txt"));
        rejected(bad, "a file that is not an .exe");
        bad = good;
        bad.insert(QStringLiteral("version"), QStringLiteral("latest"));
        rejected(bad, "a version that is not a number");
        bad = good;
        bad.insert(QStringLiteral("size"), 0);
        rejected(bad, "a zero size");

        const QJsonObject newest{
            { QStringLiteral("id"), QStringLiteral("222") },
            { QStringLiteral("attachments"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("id"), QStringLiteral("9") },
                  { QStringLiteral("filename"), QStringLiteral("BSI-Customizer-2.0.1-win64.exe") },
                  { QStringLiteral("url"), QStringLiteral("https://cdn.discordapp.com/a/x.exe") },
                  { QStringLiteral("size"), 1200000 } } } },
        };
        const QJsonObject older{
            { QStringLiteral("id"), QStringLiteral("111") },
            { QStringLiteral("attachments"),
              QJsonArray{
                  QJsonObject{
                      { QStringLiteral("id"), QStringLiteral("1") },
                      { QStringLiteral("filename"), QStringLiteral("manifest.json") },
                      { QStringLiteral("url"), QStringLiteral("https://cdn.discordapp.com/a/m.json") } },
                  QJsonObject{
                      { QStringLiteral("id"), QStringLiteral("2") },
                      { QStringLiteral("filename"), QStringLiteral("BSI-Customizer-2.0.1-win64.exe") },
                      { QStringLiteral("url"), QStringLiteral("https://cdn.discordapp.com/a/x.exe") } } } },
        };
        const auto found = UpdateChecker::findManifestMessage(
            QJsonArray{ newest, older }, QStringLiteral("manifest.json"));
        check(found.found && found.messageId == QLatin1String("111")
                  && found.manifestUrl.endsWith(QLatin1String("/m.json")),
              "the newest message with a manifest.json attachment is the release");
        check(!UpdateChecker::findManifestMessage(QJsonArray{ newest },
                                                  QStringLiteral("manifest.json")).found,
              "a channel with only the EXE and no manifest is refused");
        check(UpdateChecker::attachmentUrl(older, QStringLiteral("bsi-customizer-2.0.1-WIN64.exe"))
                  == QLatin1String("https://cdn.discordapp.com/a/x.exe"),
              "the EXE attachment is found case-insensitively by its manifest name");

        check(UpdateChecker::signedManifestText(parsed)
                  == QByteArrayLiteral("bsi-update-v1\n2.0.1\nBSI-Customizer-2.0.1-win64.exe\n"
                                       "1200000\n4de96eb5f3d5260c89e2dc2f21c2076be68ba4511e8399ce"
                                       "695bdf542a8354ac\n"),
              "the signed manifest text is version/file/size/sha256, one per line");
        const QString key = QStringLiteral("test-key-2026");
        const QString expectedSignature = QStringLiteral("fBbRNx4qNHcswibUpxO656TM0B7ehswUBDQD+e5Bgd4=");
        check(UpdateChecker::manifestSignature(parsed, key) == expectedSignature,
              "the C++ signature matches the one publish.mjs computes");
        QJsonObject signedManifest = parsed;
        signedManifest.insert(QStringLiteral("signature"), expectedSignature);
        QString signatureError;
        check(UpdateChecker::verifyManifestSignature(signedManifest, key, &signatureError),
              "a correctly signed manifest verifies");
        QJsonObject tampered = signedManifest;
        tampered.insert(QStringLiteral("size"), 1200001);
        check(!UpdateChecker::verifyManifestSignature(tampered, key, &signatureError)
                  && signatureError.contains(QLatin1String("signature")),
              "changing one field of a signed manifest breaks the signature");
        check(!UpdateChecker::verifyManifestSignature(parsed, key, &signatureError),
              "an unsigned manifest is refused when a key is configured");
        check(UpdateChecker::verifyManifestSignature(parsed, QString(), &signatureError),
              "without a key the channel itself is the trust boundary");

        QByteArray pe(0x400, '\0');
        pe[0] = 'M';
        pe[1] = 'Z';
        const auto put16 = [&pe](int off, int value) {
            pe[off] = char(value & 0xff);
            pe[off + 1] = char((value >> 8) & 0xff);
        };
        const auto put32 = [&pe](int off, quint32 value) {
            for (int i = 0; i < 4; ++i)
                pe[off + i] = char((value >> (8 * i)) & 0xff);
        };
        const quint32 peOffset = 0x80;
        put32(0x3c, peOffset);
        pe[peOffset] = 'P';
        pe[peOffset + 1] = 'E';
        put16(peOffset + 4, 0x8664); // machine: x86-64
        put16(peOffset + 24, 0x20b); // optional header: PE32+
        const QString pePath = temp.filePath(QStringLiteral("fake-portable.exe"));
        check(write(pePath, pe), "write a minimal 64-bit PE for the updater checks");
        const QString peHash = UpdateChecker::fileSha256(pePath);
        check(peHash == QString::fromLatin1(
                            QCryptographicHash::hash(pe, QCryptographicHash::Sha256).toHex()),
              "fileSha256 hashes the whole file");
        QString stageError;
        check(UpdateChecker::verifyStagedFile(pePath, pe.size(), peHash, &stageError),
              QStringLiteral("a download that matches size + SHA-256 + PE verifies (%1)")
                  .arg(stageError));
        check(!UpdateChecker::verifyStagedFile(pePath, pe.size() + 1, peHash, &stageError)
                  && stageError.contains(QLatin1String("size mismatch")),
              "a truncated download is refused");
        check(!UpdateChecker::verifyStagedFile(pePath, pe.size(),
                                               QString(64, QLatin1Char('0')), &stageError)
                  && stageError.contains(QLatin1String("SHA-256")),
              "a download whose bytes changed is refused");
        const QString notPe = temp.filePath(QStringLiteral("not-a-program.exe"));
        write(notPe, QByteArrayLiteral("MZ but nothing else after it"));
        check(!UpdateChecker::looksLikeExecutable(notPe, &stageError),
              QStringLiteral("a file that only starts with MZ is refused (%1)").arg(stageError));
        QByteArray wrongMachine = pe;
        wrongMachine[peOffset + 4] = char(0x4c);
        wrongMachine[peOffset + 5] = char(0x01); // 0x014c = i386
        const QString i386Path = temp.filePath(QStringLiteral("thirty-two-bit.exe"));
        write(i386Path, wrongMachine);
        check(!UpdateChecker::looksLikeExecutable(i386Path, &stageError)
                  && stageError.contains(QLatin1String("64-bit")),
              "a 32-bit program is refused before it can replace a 64-bit build");

        check(UpdateChecker::describeHttpError(401, "{}").contains(QLatin1String("token")),
              "401 names the token");
        check(UpdateChecker::describeHttpError(403, "{}").contains(QLatin1String("View Channel")),
              "403 names the missing channel permissions");
        check(UpdateChecker::describeHttpError(404, "{}").contains(QLatin1String("channelId")),
              "404 names the channel id");
        check(UpdateChecker::describeHttpError(429, "{\"retry_after\": 12.5}")
                  .contains(QLatin1String("rate-limiting")),
              "429 explains the rate limit");
        check(UpdateChecker::describeHttpError(500, "{\"message\": \"boom\"}")
                  .contains(QLatin1String("boom")),
              "a Discord message is passed through for diagnosis");
        check(UpdateChecker::readableSize(1200000) == QLatin1String("1.14 MB"),
              QStringLiteral("readableSize formats a download (%1)")
                  .arg(UpdateChecker::readableSize(1200000)));

        // A 30 MB EXE cannot ride on a Discord message (10 MB per file on an
        // unboosted server), so the manifest may name a `url`. Hosts like
        // pCloud answer that URL with a web page, and the page carries the real
        // download link - resolved here without guessing.
        const QUrl pcloudPage(QStringLiteral("https://u.pcloud.link/publink/show?code=XZ123"));
        const QByteArray pcloudHtml =
            "<html><script>var publinkData={\"code\":\"XZ123\",\"downloadlink\":"
            "\"https:\\/\\/p-def2.pcloud.com\\/cfZabc\\/BSI-Customizer-2.0.1-win64.exe"
            "?download=1\",\"size\":30000000}</script></html>";
        check(UpdateChecker::looksLikeHtml(pcloudHtml), "a share page is recognised as HTML");
        check(UpdateChecker::looksLikeHtml(QByteArrayLiteral("\xEF\xBB\xBF  <!DOCTYPE html>")),
              "a BOM and leading spaces do not hide an HTML page");
        check(!UpdateChecker::looksLikeHtml(pe), "a real program is not mistaken for a page");
        check(UpdateChecker::resolveDownloadLink(pcloudHtml, pcloudPage)
                  == QLatin1String("https://p-def2.pcloud.com/cfZabc/"
                                   "BSI-Customizer-2.0.1-win64.exe?download=1"),
              QStringLiteral("the pCloud page's escaped download link is unescaped (%1)")
                  .arg(UpdateChecker::resolveDownloadLink(pcloudHtml, pcloudPage)));
        QString pageDetail;
        check(UpdateChecker::resolveDownloadLink(
                  QByteArrayLiteral("<html>{\"downloadlink\":\"http:\\/\\/p-def2.pcloud.com"
                                    "\\/x.exe\"}</html>"),
                  pcloudPage, &pageDetail).isEmpty()
                  && pageDetail.contains(QLatin1String("pCloud")),
              "an http-only download link is refused with a pCloud-specific hint");
        check(UpdateChecker::resolveDownloadLink(
                  QByteArrayLiteral("<html><a href=\"/download/file.exe?token=1\">Download</a>"
                                    "</html>"),
                  pcloudPage)
                  == QLatin1String("https://u.pcloud.link/download/file.exe?token=1"),
              "a relative download button resolves against the page");
        check(UpdateChecker::resolveDownloadLink(
                  QByteArrayLiteral("<html><a href=\"https://cdn.example.com/BSI.exe\">get</a>"
                                    "</html>"),
                  pcloudPage)
                  == QLatin1String("https://cdn.example.com/BSI.exe"),
              "an absolute download button works on any host");
        check(UpdateChecker::resolveDownloadLink(
                  QByteArrayLiteral("nothing but https://files.example.org/BSI-Customizer.exe?x=1 "
                                    "in here"),
                  pcloudPage)
                  == QLatin1String("https://files.example.org/BSI-Customizer.exe?x=1"),
              "a bare .exe link in the page source is the last resort");
        check(UpdateChecker::resolveDownloadLink(
                  QByteArrayLiteral("<html><body>nothing here</body></html>"), pcloudPage,
                  &pageDetail).isEmpty()
                  && pageDetail.contains(QLatin1String("no https download link")),
              "a page without a link says so instead of downloading garbage");

        // ---- posting a build is just a message -------------------------
        // "Write the version, paste the pCloud link, send it in the private
        // channel": no tool, no manifest. The message is the release.
        const auto msg = [](const QString &id, const QString &content) {
            return QJsonObject{
                { QStringLiteral("id"), id },
                { QStringLiteral("content"), content },
                { QStringLiteral("attachments"), QJsonArray{} },
            };
        };
        QJsonObject release;
        QString releaseError;
        const QString plain = QStringLiteral(
            "BSI Customizer v2.0.1\n"
            "https://u.pcloud.link/publink/show?code=XZ123\n"
            "\n"
            "Fixed the About editor.");
        check(UpdateChecker::parseMessageRelease(msg(QStringLiteral("10"), plain), &release,
                                                 &releaseError),
              QStringLiteral("a plain version + pCloud link message is a release (%1)")
                  .arg(releaseError));
        check(release.value(QStringLiteral("version")).toString() == QLatin1String("2.0.1")
                  && release.value(QStringLiteral("url")).toString()
                         == QLatin1String("https://u.pcloud.link/publink/show?code=XZ123"),
              "the version and the link are read out of the message");
        check(release.value(QStringLiteral("notes")).toString() == QLatin1String("Fixed the About editor."),
              QStringLiteral("the remaining text becomes the release notes (%1)")
                  .arg(release.value(QStringLiteral("notes")).toString()));
        check(release.value(QStringLiteral("file")).toString()
                  == QLatin1String("BSI-Customizer-2.0.1-win64.exe"),
              "a link without an .exe name still yields a sane file name");
        check(release.value(QStringLiteral("sha256")).toString().isEmpty(),
              "a message without a hash is still accepted (first-use trust)");

        // The absolute minimum the user asked for: a version line and a link.
        const auto twoLines = [](const QString &a, const QString &b) {
            return a + QLatin1Char('\n') + b;
        };
        check(UpdateChecker::parseMessageRelease(
                  msg(QStringLiteral("11"),
                      twoLines(QStringLiteral("v2.0.2"),
                               QStringLiteral("https://example.com/files/BSI.exe"))),
                  &release, &releaseError),
              QStringLiteral("just \"v2.0.2\" and a link is enough (%1)").arg(releaseError));
        check(release.value(QStringLiteral("file")).toString() == QLatin1String("BSI.exe"),
              "an .exe link keeps its own file name");

        // Markdown, a hash, a size and a stray non-download link.
        const QString rich = QStringLiteral(
            "**BSI Customizer v2.0.3**\n"
            "https://youtu.be/dQw4w9WgXcQ\n"
            "download: https://p-def2.pcloud.com/cfZ/BSI-Customizer-2.0.3-win64.exe?download=1\n"
            "sha256: 4de96eb5f3d5260c89e2dc2f21c2076be68ba4511e8399ce695bdf542a8354ac\n"
            "size: 30000000\n"
            "Smaller download, faster start.");
        check(UpdateChecker::parseMessageRelease(msg(QStringLiteral("12"), rich), &release,
                                                 &releaseError),
              QStringLiteral("a formatted message parses too (%1)").arg(releaseError));
        check(release.value(QStringLiteral("url")).toString()
                  == QLatin1String("https://p-def2.pcloud.com/cfZ/"
                                   "BSI-Customizer-2.0.3-win64.exe?download=1"),
              QStringLiteral("the .exe link wins over a YouTube link (%1)")
                  .arg(release.value(QStringLiteral("url")).toString()));
        check(release.value(QStringLiteral("sha256")).toString()
                  == QLatin1String("4de96eb5f3d5260c89e2dc2f21c2076be68ba4511e8399ce695bdf542a8354ac")
                  && qint64(release.value(QStringLiteral("size")).toDouble(0)) == 30000000,
              "sha256: and size: lines pin the release");
        check(!release.value(QStringLiteral("notes")).toString().contains(QLatin1String("sha256"))
                  && release.value(QStringLiteral("notes")).toString()
                         .contains(QLatin1String("Smaller download")),
              QStringLiteral("the notes keep the prose and drop the bookkeeping (%1)")
                  .arg(release.value(QStringLiteral("notes")).toString()));

        // The same announcement written as an embed (a webhook, a link preview,
        // a message the bot itself posted): the text is empty, the embed holds
        // the version and the link.
        const QJsonObject embedded{
            { QStringLiteral("id"), QStringLiteral("14") },
            { QStringLiteral("content"), QString() },
            { QStringLiteral("attachments"), QJsonArray{} },
            { QStringLiteral("embeds"),
              QJsonArray{ QJsonObject{
                  { QStringLiteral("title"), QStringLiteral("BSI Customizer v2.0.6") },
                  { QStringLiteral("description"),
                    QStringLiteral("https://u.pcloud.link/publink/show?code=EMBED") },
              } } },
        };
        check(UpdateChecker::parseMessageRelease(embedded, &release, &releaseError),
              QStringLiteral("an announcement carried by an embed is read too (%1)")
                  .arg(releaseError));
        check(release.value(QStringLiteral("version")).toString() == QLatin1String("2.0.6")
                  && release.value(QStringLiteral("url")).toString()
                         == QLatin1String("https://u.pcloud.link/publink/show?code=EMBED"),
              QStringLiteral("the embed's title and description are the release (%1 / %2)")
                  .arg(release.value(QStringLiteral("version")).toString(),
                       release.value(QStringLiteral("url")).toString()));

        // A link copied out of a browser's address bar has no https:// in front
        // of it. It is still the download link, not "a message without a link".
        check(UpdateChecker::parseMessageRelease(
                  msg(QStringLiteral("16"),
                      QStringLiteral("BSI Customizer v2.0.8\n"
                                     "u.pcloud.link/publink/show?code=NOSCHEME")),
                  &release, &releaseError),
              QStringLiteral("a link pasted without its scheme is still the release (%1)")
                  .arg(releaseError));
        check(release.value(QStringLiteral("url")).toString()
                  == QLatin1String("https://u.pcloud.link/publink/show?code=NOSCHEME"),
              QStringLiteral("the scheme is restored as https (%1)")
                  .arg(release.value(QStringLiteral("url")).toString()));
        // ...but a sentence must never turn into a release.
        check(!UpdateChecker::parseMessageRelease(
                  msg(QStringLiteral("17"),
                      QStringLiteral("BSI Customizer v2.0.9\nLooks good, thanks! (see notes.txt)")),
                  &release, &releaseError),
              "a bare word with a slash is not a download link");

        // Exactly what publish.mjs writes now: version, notes, the link on its
        // own line, and the bot mention that makes the text readable without
        // the Message Content intent. The mention is plumbing, not notes.
        const QString published = QStringLiteral(
            "**BSI Customizer v2.0.7**\n"
            "\n"
            "Fixed the About editor.\n"
            "\n"
            "https://u.pcloud.link/publink/show?code=PUB2\n"
            "\n"
            "(posted by <@1234567890> for its own updater)");
        check(UpdateChecker::parseMessageRelease(msg(QStringLiteral("15"), published), &release,
                                                 &releaseError),
              QStringLiteral("the published message shape parses (%1)").arg(releaseError));
        check(release.value(QStringLiteral("version")).toString() == QLatin1String("2.0.7")
                  && release.value(QStringLiteral("url")).toString()
                         == QLatin1String("https://u.pcloud.link/publink/show?code=PUB2"),
              QStringLiteral("version and link come out of the published message (%1 / %2)")
                  .arg(release.value(QStringLiteral("version")).toString(),
                       release.value(QStringLiteral("url")).toString()));
        check(release.value(QStringLiteral("notes")).toString()
                  == QLatin1String("Fixed the About editor."),
              QStringLiteral("the bot mention is dropped from the notes (%1)")
                  .arg(release.value(QStringLiteral("notes")).toString()));

        // The token carries the application ID, which is the only way to name
        // the exact Developer Portal page of the Message Content toggle.
        check(UpdateChecker::applicationIdFromToken(
                  QStringLiteral("MTIzNDU2Nzg5MDEyMzQ1Njc4.GaBcDe.signature-part"))
                  == QLatin1String("123456789012345678"),
              QStringLiteral("the application ID is read out of the bot token (%1)")
                  .arg(UpdateChecker::applicationIdFromToken(
                      QStringLiteral("MTIzNDU2Nzg5MDEyMzQ1Njc4.GaBcDe.signature-part"))));
        check(UpdateChecker::applicationIdFromToken(QStringLiteral("not-a-token")).isEmpty()
                  && UpdateChecker::applicationIdFromToken(QString()).isEmpty(),
              "a token that is not shaped like one yields no application ID");

        // What Discord sends when the app has no Message Content intent: the
        // message and its author, with the text and the attachments stripped.
        // The check has to say that instead of reporting an empty channel.
        const QJsonArray stripped{
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("21") },
                         { QStringLiteral("content"), QString() },
                         { QStringLiteral("attachments"), QJsonArray{} },
                         { QStringLiteral("author"),
                           QJsonObject{ { QStringLiteral("username"), QStringLiteral("MrPcGamer") } } } },
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("20") },
                         { QStringLiteral("content"), QString() },
                         { QStringLiteral("embeds"), QJsonArray{} },
                         { QStringLiteral("attachments"), QJsonArray{} } },
        };
        const QString blind = UpdateChecker::explainChannel(stripped,
                                                           QStringLiteral("manifest.json"), true);
        check(blind.contains(QLatin1String("Message Content intent"))
                  && blind.contains(QLatin1String("withheld")),
              QStringLiteral("messages with their text withheld are named as the intent (%1)")
                  .arg(blind.left(160)));
        const QString blindLinked =
            UpdateChecker::explainChannel(stripped, QStringLiteral("manifest.json"), true,
                                          QStringLiteral("123456789012345678"));
        check(blindLinked.contains(
                  QLatin1String("https://discord.com/developers/applications/"
                                "123456789012345678/bot")),
              QStringLiteral("the fix links straight to the toggle (%1)")
                  .arg(blindLinked.section(QLatin1Char('\n'), 3, 3)));
        const QString empty = UpdateChecker::explainChannel(QJsonArray{},
                                                           QStringLiteral("manifest.json"), true);
        check(empty.contains(QLatin1String("channel is empty")),
              QStringLiteral("an empty channel is reported as empty, not as an intent problem (%1)")
                  .arg(empty.left(120)));
        // One readable message proves the intent is on; the rest is a parsing
        // question and the report has to say which message failed and why.
        const QJsonArray mixed{
            msg(QStringLiteral("31"), QStringLiteral("v2.0.1 without a link")),
            msg(QStringLiteral("30"), QStringLiteral("hello")),
        };
        const QString report = UpdateChecker::explainChannel(mixed,
                                                            QStringLiteral("manifest.json"), true);
        check(!report.contains(QLatin1String("Message Content intent"))
                  && report.contains(QLatin1String("#31"))
                  && report.contains(QLatin1String("no download link")),
              QStringLiteral("readable messages are explained one by one (%1)")
                  .arg(report.left(200)));
        // A signed channel refuses text releases; the report must say so
        // instead of claiming the messages are unreadable.
        const QJsonArray oneRelease{ msg(QStringLiteral("41"),
                                        QStringLiteral("v2.0.2\nhttps://x.pcloud.link/a.exe")) };
        check(UpdateChecker::explainChannel(oneRelease, QStringLiteral("manifest.json"), false)
                  .contains(QLatin1String("only accepts manifest.json")),
              "the signature-protected channel explains why a text message is ignored");

        // Things that are not announcements.
        const auto notRelease = [&msg](const QString &content, const QString &why) {
            QJsonObject out;
            QString error;
            check(!UpdateChecker::parseMessageRelease(msg(QStringLiteral("13"), content), &out,
                                                      &error)
                      && !error.isEmpty(),
                  QStringLiteral("%1 is not a release (%2)").arg(why, error));
        };
        notRelease(QStringLiteral("hey, did you see this? https://example.com/x.exe"), "a chat line");
        notRelease(QStringLiteral("v2.0.4 is coming soon"), "a version with no link");
        notRelease(QStringLiteral("v2.0.5\nhttp://example.com/BSI.exe"), "an http link");
        notRelease(QStringLiteral("<:cat:1> https://tenor.com/view/cat-gif-1"), "a gif");

        // Which message in the channel counts.
        const QJsonArray channel{
            msg(QStringLiteral("20"), QStringLiteral("v2.0.4\nhttps://u.pcloud.link/publink/show?code=NEW")),
            msg(QStringLiteral("19"), QStringLiteral("v2.0.3\nhttps://u.pcloud.link/publink/show?code=OLD")),
            msg(QStringLiteral("18"), QStringLiteral("hello")),
        };
        const auto picked = UpdateChecker::findRelease(channel, QStringLiteral("manifest.json"), true);
        check(picked.found && !picked.fromManifest
                  && picked.manifest.value(QStringLiteral("version")).toString()
                         == QLatin1String("2.0.4"),
              "the highest version in the channel is the release");
        const QJsonArray rePosted{
            msg(QStringLiteral("31"), QStringLiteral("v2.0.4\nhttps://u.pcloud.link/publink/show?code=FIXED")),
            msg(QStringLiteral("30"), QStringLiteral("v2.0.4\nhttps://u.pcloud.link/publink/show?code=OLD")),
        };
        const auto rePostedPick = UpdateChecker::findRelease(rePosted, QStringLiteral("manifest.json"), true);
        check(rePostedPick.found && rePostedPick.manifest.value(QStringLiteral("url")).toString()
                                  .endsWith(QLatin1String("FIXED")),
              "re-posting the same version with a fixed link replaces the old one");
        check(!UpdateChecker::findRelease(channel, QStringLiteral("manifest.json"), false).found,
              "a signature-protected channel refuses text releases");
        // A manifest attachment outranks a text message in the same channel.
        const QJsonArray withManifest{
            msg(QStringLiteral("41"), QStringLiteral("v2.0.9\nhttps://u.pcloud.link/publink/show?code=X")),
            QJsonObject{
                { QStringLiteral("id"), QStringLiteral("40") },
                { QStringLiteral("content"), QStringLiteral("latest build") },
                { QStringLiteral("attachments"),
                  QJsonArray{ QJsonObject{
                      { QStringLiteral("filename"), QStringLiteral("manifest.json") },
                      { QStringLiteral("url"),
                        QStringLiteral("https://cdn.discordapp.com/attachments/1/2/manifest.json") } } } },
            },
        };
        const auto manifestWins = UpdateChecker::findRelease(withManifest,
                                                             QStringLiteral("manifest.json"), true);
        check(manifestWins.found && manifestWins.fromManifest,
              "a published manifest wins over a text announcement");
    }


    return failures;
}
