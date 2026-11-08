#include "core/Backend.h"
#include "core/BootVideoPatcher.h"
#include "core/PlayerCache.h"
#include "core/GifRpc.h"
#include "../plugins/gif-rpc/RpcPayload.h"
#include "core/BsDetector.h"
#include "core/EmbeddedImages.h"
#include "core/EmbeddedResources.h"
#include "core/ExtrasPatcher.h"
#include "core/AboutPatcher.h"
#include "core/SettingsPanelPatcher.h"
#include "core/AndroidCleanup.h"
#include "core/rootdisk/RootDisk.h"
#include "core/AdbClient.h"
#include "core/DebloatConfig.h"
#include "core/DebloatPatcher.h"
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
#include "core/RootVhd.h"
#include "core/SevenZipArchive.h"
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
#include <QElapsedTimer>
#include <QThread>
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

#define BSI_WORKSPACE_INTEGRATED
#include "../tests/workspace_core_test.cpp"
#undef BSI_WORKSPACE_INTEGRATED

using namespace ByteUtils;

// Implemented in cli.cpp: the scriptable Rich Presence/name command line.
int runRpcIdCli(const QStringList &arguments);

namespace {
int failures = 0;
void check(bool ok, const char *message)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", message);
    // stdout is a file when CI redirects it, so it is block-buffered: a run that
    // dies stops leaving its last lines behind in the buffer, and the log then
    // ends somewhere before the statement that killed it.
    std::fflush(stdout);
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

// A byte comparison that fails says nothing about where: show the first lines
// that differ so one CI annotation is enough to diagnose it.
// One line, because a GitHub annotation stops at the first line break.
QString firstDifference(const QString &expected, const QString &actual)
{
    const QStringList left = expected.split(QLatin1Char('\n'));
    const QStringList right = actual.split(QLatin1Char('\n'));
    QStringList lines;
    for (int i = 0; i < left.size() || i < right.size(); ++i) {
        const QString a = i < left.size() ? left.at(i) : QStringLiteral("<end of file>");
        const QString b = i < right.size() ? right.at(i) : QStringLiteral("<end of file>");
        if (a == b) continue;
        QString shown = a.trimmed();
        QString got = b.trimmed();
        lines << QStringLiteral("line %1 expected [%2] got [%3]")
                     .arg(i + 1).arg(shown.left(150), got.left(150));
        if (lines.size() == 3) break;
    }
    if (lines.isEmpty())
        return QStringLiteral("no line differs: lengths %1 vs %2").arg(expected.size()).arg(actual.size());
    return QStringLiteral("sizes %1 vs %2: ").arg(expected.size()).arg(actual.size())
        + lines.join(QLatin1String(" || "));
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
QByteArray utf16be(const QString &text)
{
    QByteArray bytes(text.size() * 2, '\0');
    for (qsizetype i = 0; i < text.size(); ++i)
        putBe16(bytes, i * 2, text.at(i).unicode());
    return bytes;
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
// Opt in only fixtures whose runtime consumer is explicitly given these bases.
// Unanchored fixtures still exercise inference and genuine ambiguity refusal.
void declarePoolBases(Fixture &f)
{
    const qint64 header = 0x1100;
    f.bytes.replace(header, 4, "qres");
    putBe32(f.bytes, header + 4, quint32(f.version));
    putBe32(f.bytes, header + 8, quint32(f.tree - header));
    putBe32(f.bytes, header + 12, quint32(f.payloads - header));
    putBe32(f.bytes, header + 16, quint32(f.names - header));
}

// QtWebEngine types can already be process-globally registered when the
// executable links Chromium, even in a fresh test subprocess. Namespace the
// temporary synthetic WebEngine provider so this test verifies the HTML URL
// without starting Chromium or selecting a GPU-backed real WebEngine view.
bool prepareSyntheticBootPlayer(const QString &path)
{
    auto source = read(path);
    if (!source.contains("import QtWebEngine"))
        return false;
    source.replace("import QtWebEngine", "import BsiFixtureWebEngine");
    return write(path, source);
}

int bootRuntimeInChild(const QString &qml, const QString &imports,
                       const QString &provider, const QUrl &media)
{
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
    env.insert(QStringLiteral("QML_DISABLE_DISK_CACHE"), QStringLiteral("1"));
    process.setProcessEnvironment(env);
    process.start(QCoreApplication::applicationFilePath(), {
        QStringLiteral("--boot-runtime-test"), qml, imports, provider,
        media.toString(QUrl::FullyEncoded)
    });
    if (!process.waitForStarted(10000) || !process.waitForFinished(30000)) {
        process.kill();
        process.waitForFinished();
        std::printf("    boot runtime child: %s\n", qPrintable(process.errorString()));
        return -1;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        std::printf("    boot runtime %s: %s\n%s\n", qPrintable(provider),
                    process.readAllStandardOutput().constData(),
                    process.readAllStandardError().constData());
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
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
                               const QSet<QString> &rawNames = {}, int recordSlack = 0)
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
        // Left-over zeros are the slack a real pool has behind each record; the
        // dense fixture deliberately has none so growth cannot hide there.
        cursor += bytes.size() + recordSlack;
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
                          const QSet<QString> &rawNames = {}, int recordSlack = 0)
{
    QList<QPair<QString, QByteArray>> payloads;
    payloads.reserve(sources.size());
    for (const auto &source : sources)
        payloads << qMakePair(source.first, source.second.toUtf8());
    return resourcesBytes(payloads, dense, rawNames, recordSlack);
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

QString linkedResourceText(const QByteArray &image, const QString &leaf)
{
    QString result;
    bool found = false;
    for (const auto &file : QtResources::registeredFiles(image)) {
        if (QFileInfo(file.path.isEmpty() ? file.name : file.path).fileName().compare(leaf, Qt::CaseInsensitive) != 0)
            continue;
        if (found) return {};
        found = true;
        result = EmbeddedResources::decodeText(QtResources::readFile(image, file, 8 * 1024 * 1024));
    }
    return result;
}

bool registeredQmlContains(const QByteArray &image, const QString &marker)
{
    for (const auto &file : QtResources::registeredFiles(image)) {
        const QString path = file.path.isEmpty() ? file.name : file.path;
        if (!path.endsWith(QLatin1String(".qml"), Qt::CaseInsensitive)) continue;
        const QString source = EmbeddedResources::decodeText(QtResources::readFile(image, file, 8 * 1024 * 1024));
        if (source.contains(marker)) return true;
    }
    return false;
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

// The stock About page exactly as the players store it, shared by the tests
// that need a page the editor recognises as one.
static QString aboutPageFixture()
{
    return QStringLiteral(
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
}

// Internal self-test subprocess: only the synthetic provider fixture is loaded.
// Like the theme runtime probe, this runs before the application UI is created.
int runBootPlayerRuntimeTest(const QStringList &args)
{
    const int at = args.indexOf(QStringLiteral("--boot-runtime-test"));
    if (at < 0 || args.size() != at + 5) return 3;
    const QString provider = args.at(at + 3);
    if (provider != QLatin1String("web") && provider != QLatin1String("fallback")
        && provider != QLatin1String("controller") && provider != QLatin1String("controller-error")) return 3;
    QQmlEngine engine;
    engine.addImportPath(args.at(at + 2));
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError> &warnings) {
        for (const auto &warning : warnings)
            std::fprintf(stderr, "Boot fixture QML: %s\n", qPrintable(warning.toString()));
    });
    QQmlComponent component(&engine, QUrl::fromLocalFile(args.at(at + 1)));
    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        std::fprintf(stderr, "Boot fixture compile error: %s\n", qPrintable(component.errorString()));
        return 2;
    }
    object->setProperty("width", 1280);
    object->setProperty("height", 720);
    // The controller's Loader can complete asynchronously. Keep assertions on
    // the actual generated player rather than assuming one event pump suffices.
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents();
        if (provider == QLatin1String("web")) {
            const auto player = object->findChild<QObject *>(QStringLiteral("bsiBootHtmlView"));
            if (player && player->property("url").toUrl() == QUrl(args.at(at + 4))) return 0;
        } else if (provider == QLatin1String("fallback")) {
            const auto ring = object->findChild<QObject *>(QStringLiteral("bsiLoadingRing"));
            const auto status = object->findChild<QObject *>(QStringLiteral("bsiBootStatus"));
            if (ring && ring->property("visible").toBool() && status
                && status->property("text").toString() == QStringLiteral("Emulator is starting…")) return 0;
        } else if (provider == QLatin1String("controller")) {
            const auto player = object->findChild<QObject *>(QStringLiteral("bsiBootHtmlView"));
            const auto ring = object->findChild<QObject *>(QStringLiteral("bsiLoadingRing"));
            const auto status = object->findChild<QObject *>(QStringLiteral("bsiBootStatus"));
            const auto stock = object->findChild<QObject *>(QStringLiteral("bootImage"));
            if (player && player->property("url").toUrl() == QUrl(args.at(at + 4))
                && ring && ring->property("visible").toBool() && status
                && status->property("visible").toBool() && stock && !stock->property("visible").toBool()) return 0;
        } else {
            const auto fallback = object->findChild<QObject *>(QStringLiteral("bsiNativeBootFallback"));
            const auto status = object->findChild<QObject *>(QStringLiteral("bsiNativeBootStatus"));
            const auto stock = object->findChild<QObject *>(QStringLiteral("bootImage"));
            if (fallback && fallback->property("visible").toBool() && status
                && status->property("text").toString() == QStringLiteral("Emulator is starting…")
                && stock && !stock->property("visible").toBool()) return 0;
        }
        QThread::msleep(10);
    } while (timer.elapsed() < 5000);
    std::fprintf(stderr, "Boot fixture %s did not start with its expected URL\n", qPrintable(provider));
    return 1;
}

// ---- BlueStacks Debloat -------------------------------------------------
// The tab edits a file BlueStacks owns and rewrites itself, so the tests pin
// the two properties that make it safe: every byte outside a changed value
// survives, and a plan never invents a setting the build does not declare.
void checkAndroid()
{
    std::printf("[android] reviewed apps, commands and the undo record\n");
    {
        const auto &rules = Android::appRules();
        check(rules.size() >= 5, "the reviewed app list is populated");
        bool allValid = true, unique = true, anyRecommended = false;
        QStringList names;
        for (const auto &rule : rules) {
            if (!Android::validPackage(rule.package)) allValid = false;
            if (names.contains(rule.package)) unique = false;
            names << rule.package;
            if (rule.recommended) anyRecommended = true;
            if (rule.label.isEmpty() || rule.note.isEmpty()) allValid = false;
        }
        check(allValid, "every reviewed package and its label are well formed");
        check(unique, "no package is on the reviewed list twice");
        check(anyRecommended, "the deceptive apps are the ones pre-ticked");
        check(Android::knownPackages() == names, "the lookup list is the reviewed list");
        check(Android::appLabel(QStringLiteral("gg.now.ads.service")).contains(QLatin1String("now.gg")),
              "a package is described by its label, not by its identifier");
        // Root.vhd is deliberately absent from this module: the system image is
        // shared and read-only, so nothing here addresses a file on disk.
        check(!Android::recordPath().isEmpty()
                  && Android::recordPath().endsWith(QLatin1String("/debloat/android.json")),
              "the undo record sits next to the configuration record");
    }
    {
        // A package name is interpolated into a shell command, so only the
        // shape Android itself allows gets through.
        check(Android::validPackage(QStringLiteral("com.android.egg")),
              "a normal package name is accepted");
        check(!Android::validPackage(QString()),
              "an empty package is refused");
        check(!Android::validPackage(QStringLiteral("com.android.egg; rm -rf /sdcard")),
              "a package carrying a second command is refused");
        check(!Android::validPackage(QStringLiteral("com.android.egg && reboot")),
              "a package carrying a shell operator is refused");
        check(!Android::validPackage(QStringLiteral("$(reboot)")),
              "a package carrying command substitution is refused");
        check(!Android::validPackage(QStringLiteral("no_dots")),
              "a name without a package segment is refused");
        check(!Android::validPackage(QStringLiteral("../etc/passwd")),
              "a path is not a package name");

        const QString pkg = QStringLiteral("com.android.traceur");
        check(Android::uninstallCommand(pkg)
                  == QLatin1String("pm uninstall --user 0 com.android.traceur"),
              "uninstall targets user 0 only, so the system image is never written");
        check(Android::disableCommand(pkg).contains(QLatin1String("disable-user"))
                  && Android::disableCommand(pkg).endsWith(pkg),
              "the fallback for a system package disables it instead");
        check(Android::installExistingCommand(pkg).startsWith(QLatin1String("cmd package install-existing"))
                  && Android::installExistingCommand(pkg).endsWith(pkg),
              "restore reinstalls the package the player shipped");
        check(Android::enableCommand(pkg).startsWith(QLatin1String("pm enable"))
                  && Android::enableCommand(pkg).endsWith(pkg),
              "a disabled package is enabled again");
        check(Android::listCommand().contains(QLatin1String("--user 0")),
              "the package list asked for is the one the remove step is verified against");
        check(Adb::remoteShellCommand(Android::listCommand())
                  == QLatin1String("pm list packages --user 0; echo \"BSI-EXIT:$?\"")
                  && !Adb::remoteShellCommand(Android::listCommand()).contains(QLatin1Char('(')),
              "HD-Adb receives one unwrapped shell command and an exit marker, not unsupported nested sh syntax");
        const QStringList installed = Android::installedPackagesFromOutput(
            QStringLiteral("package:com.android.systemui\npackage:org.example.game\n"
                           "package:com.android.systemui\nnot-a-package\n"));
        check(installed == (QStringList { QStringLiteral("com.android.systemui"),
                                          QStringLiteral("org.example.game") }),
              "the live debloater parses all installed packages, deduplicates and sorts them");

        const QStringList wipe = Android::downloadWipeCommands();
        bool removes = false, recreates = false;
        for (const QString &command : wipe) {
            if (command.contains(QLatin1String("rm -rf /sdcard/Download"))) removes = true;
            if (command.contains(QLatin1String("mkdir -p /sdcard/Download"))) recreates = true;
        }
        check(removes && recreates,
              "the whole Download folder goes, then comes back empty, so nothing is left behind");
        const QStringList appData = Android::appDataWipeCommands();
        bool data = false, obb = false;
        for (const QString &command : appData) {
            if (command.contains(QLatin1String("/sdcard/Android/data"))) data = true;
            if (command.contains(QLatin1String("/sdcard/Android/obb"))) obb = true;
        }
        check(data && obb, "clearing app data covers both the data and the obb folders");
        check(Android::copyLimitBytes() > 0, "the automatic copy has a size ceiling");
    }
    {
        // ---- the instance's ADB port, from the configuration it publishes ----
        QTemporaryDir temp;
        check(temp.isValid(), "a temporary folder holds the fake bluestacks.conf");
        const QString current = temp.filePath(QStringLiteral("current.conf"));
        const QString legacy = temp.filePath(QStringLiteral("legacy.conf"));
        const QString none = temp.filePath(QStringLiteral("none.conf"));
        const auto write = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return false;
            file.write(bytes);
            file.close();
            return true;
        };
        check(write(current, "bst.instance.Pie64.status.adb_port=\"5565\"\r\n"
                             "bst.instance.Nougat32.status.adb_port=\"5567\"\r\n"),
              "the fixture publishes two instance ports");
        check(write(legacy, "bst.instance.Pie64.adb_port=\"5575\"\n"),
              "the fixture uses the older key as well");
        check(write(none, "bst.instance.Pie64.cpus=\"4\"\n"),
              "the fixture without a port is written");
        check(Adb::instancePort(current, QStringLiteral("Pie64")) == 5565,
              "the running instance's published port is the one used");
        check(Adb::instancePort(current, QStringLiteral("Nougat32")) == 5567,
              "each instance has its own port");
        check(Adb::instancePort(legacy, QStringLiteral("Pie64")) == 5575,
              "the older key spelling is understood");
        check(Adb::instancePort(none, QStringLiteral("Pie64")) == 0,
              "an instance without a published port is reported as such, never guessed");
        check(Adb::instancePort(temp.filePath(QStringLiteral("missing.conf")),
                                QStringLiteral("Pie64")) == 0,
              "a missing configuration is not an error, just no port");
        check(Adb::findAdb(temp.filePath(QStringLiteral("HD-Player.exe"))).isEmpty(),
              "a folder without the player's adb reports no adb instead of inventing one");
    }
    {
        // ---- the undo record survives a round trip, per instance ----
        Android::Record record;
        check(Android::isEmpty(record), "a fresh record is empty");
        record.removed << Android::RemovedApp { QStringLiteral("gg.now.ads.service"),
                                                QStringLiteral("uninstall"),
                                                QStringLiteral("Pie64") };
        record.removed << Android::RemovedApp { QStringLiteral("com.android.egg"),
                                                QStringLiteral("disable"),
                                                QStringLiteral("Nougat32") };
        record.downloads << Android::WipedFolder { QStringLiteral("Pie64"),
                                                   { QStringLiteral("apps"), QStringLiteral("game.apk") },
                                                   QStringLiteral("C:/backups/download-1") };
        record.appData << QStringLiteral("Pie64");
        check(!Android::isEmpty(record), "a record with work in it is not empty");
        const auto back = Android::recordFromJson(Android::recordToJson(record));
        check(back.removed.size() == 2 && back.removed.first().instance == QLatin1String("Pie64")
                  && back.removed.last().action == QLatin1String("disable"),
              "each removed app keeps the instance it was removed from");
        check(back.downloads.size() == 1 && back.downloads.first().entries.size() == 2
                  && back.downloads.first().entries.first() == QLatin1String("apps"),
              "what was in the Download folder, and where its copy is, is kept");
        check(back.downloads.first().backup == QLatin1String("C:/backups/download-1"),
              "the copy on this PC is part of the record");
        check(back.appData == (QStringList { QStringLiteral("Pie64") }),
              "the instances whose app data was cleared are kept");
        check(Android::isEmpty(Android::recordFromJson(QByteArray("not json"))),
              "a damaged record reads as empty instead of pretending to have work");
    }
}

void checkDebloat()
{
    std::printf("[debloat] config values, hosts block and cloud folders\n");
    {
        const QByteArray config =
            "\xEF\xBB\xBF" "bst.enable_programmatic_ads=\"1\"\r\n"
            "bst.feature.usage_stats=\"true\"\r\n"
            "bst.instance.Pie64.cpus=\"4\"\r\n"
            "bst.instance.Pie64.enable_high_fps=\"0\"\r\n"
            "bst.instance.Pie64.max_fps=\"60\"\r\n"
            "bst.instance.Pie64.enable_vsync=\"1\"\r\n"
            "bst.enable_adb_access=\"0\"\r\n"
            "# a comment the player keeps\r\n"
            "bst.not_a_switch=raw value\r\n";
        const auto file = DebloatConfig::File::parse(config);
        check(file.isValid(), "bluestacks.conf is parsed with its BOM, quotes and CRLF kept");
        check(file.value(QStringLiteral("bst.enable_programmatic_ads")) == QLatin1String("1"),
              "a quoted flag reads back without its quotes");
        check(file.instances() == (QStringList { QStringLiteral("Pie64") }),
              "the instance names are read from the settings that declare them");

        Debloat::Request request;
        request.options = { QStringLiteral("ads"), QStringLiteral("telemetry"),
                            QStringLiteral("downloads") };
        const auto plan = Debloat::planFor(config, request);
        check(plan.ok && plan.changes.size() == 2,
              "only the two declared flags in this fixture change; undeclared keys are skipped");
        bool skippedMissing = false;
        for (const auto &entry : plan.skipped)
            if (entry.startsWith(QLatin1String("bst.enable_boot_banner"))) skippedMissing = true;
        check(skippedMissing, "a setting this build does not declare is reported, never created");

        QMap<QString, QString> wanted;
        for (const auto &change : plan.changes) wanted.insert(change.key, change.after);
        QByteArray edited;
        QString editError;
        check(DebloatConfig::File::parse(config).applyEdits(wanted, &edited, &editError),
              "the planned values are written into the bytes the player owns");
        check(edited.contains("bst.enable_programmatic_ads=\"0\""),
              "the ad flag is written as the same quoted zero the player uses");
        check(edited.contains("bst.feature.usage_stats=\"false\""),
              "a flag spelled true becomes false, not 0");
        // Everything the plan did not touch has to survive byte for byte; the
        // comparison stays in bytes so a text round trip cannot hide a change.
        QByteArray stripped = edited;
        stripped.replace("bst.enable_programmatic_ads=\"0\"",
                         "bst.enable_programmatic_ads=\"1\"");
        stripped.replace("bst.feature.usage_stats=\"false\"",
                         "bst.feature.usage_stats=\"true\"");
        check(stripped == config,
              "the edited document is the original plus exactly the changed values");

        Debloat::Request tuning;
        tuning.options = { QStringLiteral("highfps") };
        tuning.instance = QStringLiteral("Pie64");
        const auto tuned = Debloat::planFor(config, tuning);
        bool fps = false, limit = false, vsync = false;
        for (const auto &change : tuned.changes) {
            if (change.key == QLatin1String("bst.instance.Pie64.enable_high_fps")) fps = true;
            if (change.key == QLatin1String("bst.instance.Pie64.max_fps")) limit = true;
            if (change.key == QLatin1String("bst.instance.Pie64.enable_vsync")) vsync = true;
        }
        check(fps && limit && vsync && tuned.changes.size() == 3,
              "high frame-rate mode writes the instance's own three settings");
        // It is the player's setting: the binary patch on the FPS page is a
        // separate edit and must not be implied here.
        check(!tuned.changes.isEmpty() && !config.contains("max_fps=\"120\""),
              "no unrelated FPS value is invented");
    }
    {
        // A value the player spells differently is left alone instead of being
        // forced into a shape it would not understand.
        const QByteArray odd = "bst.enable_smart_downloads=\"2\"\n";
        Debloat::Request request;
        request.options = { QStringLiteral("downloads") };
        const auto plan = Debloat::planFor(odd, request);
        check(plan.ok && plan.changes.isEmpty() && plan.skipped.size() == 2,
              "an unexpected value is reported and left untouched (the feature key is missing)");
    }
    {
        // ---- hosts file ----
        const QByteArray hosts = "127.0.0.1 localhost\r\n10.0.0.5 internal.example\r\n";
        QByteArray withBlock, removed;
        QString error;
        check(Debloat::hostsEdits(hosts, true, &withBlock, &error),
              "the ad domains are added to a clean hosts file");
        check(withBlock.startsWith(hosts),
              "the existing hosts content is untouched, byte for byte");
        check(withBlock.contains("0.0.0.0 ads.bluestacks.com")
                  && withBlock.contains("0.0.0.0 adsdk.bluestacks.com")
                  && withBlock.contains(Debloat::hostsStartMarker().toUtf8()),
              "exactly the two BlueStacks ad domains are blocked");
        check(!withBlock.contains("googleads") && !withBlock.contains("doubleclick"),
              "no Google or game domain is blocked");
        QByteArray again;
        check(Debloat::hostsEdits(withBlock, true, &again, &error) && again == withBlock,
              "applying the block twice does not stack a second copy");
        check(Debloat::hostsEdits(withBlock, false, &removed, &error) && removed == hosts,
              "removing the block restores the original hosts file exactly");
        QByteArray untouched;
        check(Debloat::hostsEdits(hosts, false, &untouched, &error) && untouched == hosts,
              "removing a block that is not there changes nothing");
        // A half-deleted block is refused, never repaired by guesswork.
        const QByteArray broken = hosts + Debloat::hostsStartMarker().toUtf8()
            + "\r\n0.0.0.0 ads.bluestacks.com\r\n";
        QByteArray out;
        check(!Debloat::hostsEdits(broken, true, &out, &error),
              "an incomplete block is refused instead of rewritten");
        QByteArray utf16;
        check(!Debloat::hostsEdits(QByteArray("\xff\xfe" "1\x00"), true, &utf16, &error),
              "a UTF-16 hosts file is refused so its encoding cannot be damaged");
    }
    {
        // ---- cloud companions ----
        QTemporaryDir temp;
        check(temp.isValid(), "a temporary folder holds the fake cloud installs");
        const QString rootA = temp.filePath(QStringLiteral("a"));
        const QString rootB = temp.filePath(QStringLiteral("b"));
        QDir().mkpath(rootA + QStringLiteral("/BlueStacks X"));
        QDir().mkpath(rootB + QStringLiteral("/Programs/bluestacks-services"));
        QFile x(rootA + QStringLiteral("/BlueStacks X/BlueStacks X.exe"));
        x.open(QIODevice::WriteOnly);
        x.write("x");
        x.close();
        QFile services(rootB + QStringLiteral("/Programs/bluestacks-services/BlueStacksServices.exe"));
        services.open(QIODevice::WriteOnly);
        services.write("s");
        services.close();
        const auto found = Debloat::cloudProductsAt({ rootA, rootB });
        check(found.size() == 2 && !found.first().moved,
              "both cloud companions are found and reported as installed");
        // The park name is what makes the move reversible in one rename: no
        // delete, no registry edit, no change on another volume.
        const QString parked = Debloat::cloudParkedPath(rootA + QStringLiteral("/BlueStacks X"));
        check(parked.endsWith(QLatin1String("BlueStacks X.bsi-disabled"))
                  && QFileInfo(parked).absolutePath() == QDir(rootA).absolutePath(),
              "a removed companion is parked next to itself, on the same drive");
        check(QDir().rename(rootA + QStringLiteral("/BlueStacks X"), parked),
              "parking the folder is a single rename");
        const auto afterMove = Debloat::cloudProductsAt({ rootA });
        check(afterMove.size() == 1 && afterMove.first().moved && afterMove.first().id == QLatin1String("x"),
              "a parked companion is still reported, flagged as moved");
        check(QDir().rename(parked, rootA + QStringLiteral("/BlueStacks X")),
              "restore renames the folder back");
        check(!Debloat::cloudProductsAt({ temp.filePath(QStringLiteral("empty")) }).size(),
              "a folder without the companion executable is not treated as an install");
    }
}

// The hardcode debloater: open a real VHD, read the ext4 inside it, write
// files and folders into it, delete them again, and prove one Undo puts the
// image back byte for byte.
void checkRootDisk()
{
    std::printf("[rootdisk] Root.vhd as a file manager\n");
#ifdef BSI_ROOT_VHD_FIXTURE
    const QString fixture = QString::fromUtf8(BSI_ROOT_VHD_FIXTURE);
    check(QFileInfo::exists(fixture), "the Root.vhd fixture is in the tree");
    if (!QFileInfo::exists(fixture))
        return;

    QTemporaryDir temp;
    check(temp.isValid(), "a scratch folder is available for the image");
    if (!temp.isValid())
        return;
    const QString work = temp.filePath(QStringLiteral("Root.vhd"));
    check(QFile::copy(fixture, work), "the fixture is copied so the tree is never written to");
    const QByteArray pristine = [&work] {
        QFile file(work);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }();
    check(pristine.size() > 512, "the working copy really is a disk image");

#ifdef Q_OS_WIN
    // BlueStacks images may carry the Windows Read-only attribute. Verify the
    // explicit recovery action clears it and reopens the ext4 volume writable.
    const QString readOnlyCopy = temp.filePath(QStringLiteral("Root-readonly.vhd"));
    const bool copiedReadOnly = QFile::copy(fixture, readOnlyCopy);
    check(copiedReadOnly, "a separate copy is available for the Windows read-only test");
    if (copiedReadOnly) {
        const QFileDevice::Permissions originalPermissions = QFile::permissions(readOnlyCopy);
        const QFileDevice::Permissions readOnlyPermissions =
            originalPermissions & ~(QFileDevice::WriteOwner | QFileDevice::WriteUser
                                    | QFileDevice::WriteGroup | QFileDevice::WriteOther);
        const bool markedReadOnly = QFile::setPermissions(readOnlyCopy, readOnlyPermissions);
        check(markedReadOnly, "the test image can be marked read-only on Windows");
        if (markedReadOnly) {
            const auto beforeEnable = RootVhd::info(readOnlyCopy);
            check(beforeEnable.value(QStringLiteral("ok")).toBool()
                      && !beforeEnable.value(QStringLiteral("writable")).toBool(),
                  "a read-only host file is reported as not writable before user action");
            const auto enabled = RootVhd::enableWriteAccess(readOnlyCopy);
            check(enabled.value(QStringLiteral("ok")).toBool(),
                  "Enable editing clears the Windows read-only attribute when safe");
            const auto afterEnable = RootVhd::info(readOnlyCopy);
            const bool writable = afterEnable.value(QStringLiteral("writable")).toBool();
            check(writable,
                  "the image reopens as writable after enabling editing");
            if (writable) {
                const auto created = RootVhd::makeFolder(readOnlyCopy, QStringLiteral("/"),
                                                         QStringLiteral("readonly-test"));
                check(created.value(QStringLiteral("ok")).toBool(),
                      "a Windows read-only VHD becomes editable after enabling write access");
                const auto undone = RootVhd::undo(readOnlyCopy);
                check(undone.value(QStringLiteral("ok")).toBool(),
                      "the read-only VHD edit can be undone after the host attribute is cleared");
            }
        }
        QFile::setPermissions(readOnlyCopy, originalPermissions | QFileDevice::WriteOwner);
    }
#endif

#ifdef BSI_ROOT_VHDX_FIXED_FIXTURE
    {
        const QString fixedFixture = QString::fromUtf8(BSI_ROOT_VHDX_FIXED_FIXTURE);
        const QString fixedWork = temp.filePath(QStringLiteral("Root-fixed-test.vhdx"));
        const bool copiedFixed = QFile::copy(fixedFixture, fixedWork);
        check(copiedFixed, "the synthetic fixed-VHDX fixture is copied before testing");
        if (copiedFixed) {
            const auto info = RootVhd::info(fixedWork);
            check(info.value(QStringLiteral("ok")).toBool()
                      && info.value(QStringLiteral("kind")).toString() == QStringLiteral("fixed VHDX")
                      && info.value(QStringLiteral("writable")).toBool(),
                  "the fixed VHDX reader mounts its ext4 volume as writable");
            std::string fixedError;
            auto image = RootDisk::openImage(QFile::encodeName(fixedWork).toStdString(),
                                              &fixedError);
            std::vector<std::uint8_t> pattern(512, 0x5A), readBack(512);
            const bool fixedWrite = image && image->kind() == "fixed VHDX"
                && image->write(1024 * 1024 + 512, pattern.data(), pattern.size())
                && image->flush()
                && image->read(1024 * 1024 + 512, readBack.data(), readBack.size())
                && readBack == pattern;
            check(fixedWrite, "fixed VHDX payload blocks can be edited in place");
        }
    }
#endif

#ifdef BSI_ROOT_VHDX_FIXTURE
    {
        const QString vhdxFixture = QString::fromUtf8(BSI_ROOT_VHDX_FIXTURE);
        const QString vhdxWork = temp.filePath(QStringLiteral("Root-test.vhdx"));
        const bool copiedVhdx = QFile::copy(vhdxFixture, vhdxWork);
        check(copiedVhdx, "the synthetic VHDX fixture is copied before testing");
        if (copiedVhdx) {
            // Standard required metadata (including physical sector size and
            // virtual disk ID) must open; an unknown required GUID must be
            // refused with its identifier so unsupported formats are visible.
            const QString unknownMetadata = temp.filePath(QStringLiteral("Root-unknown-metadata.vhdx"));
            if (QFile::copy(vhdxFixture, unknownMetadata)) {
                QFile metadataFile(unknownMetadata);
                if (metadataFile.open(QIODevice::ReadWrite)
                    && metadataFile.seek(2 * 1024 * 1024 + 32 + 4 * 32)) {
                    QByteArray guid = metadataFile.read(16);
                    if (guid.size() == 16) {
                        guid[0] = char(guid[0] ^ 1);
                        metadataFile.seek(2 * 1024 * 1024 + 32 + 4 * 32);
                        metadataFile.write(guid);
                    }
                }
                metadataFile.close();
                std::string unsupportedError;
                auto unsupported = RootDisk::openImage(
                    QFile::encodeName(unknownMetadata).toStdString(), &unsupportedError);
                check(!unsupported
                          && unsupportedError.find("unsupported metadata GUID") != std::string::npos,
                      "unknown required VHDX metadata is refused with its GUID reported");
            } else {
                check(false, "a copy is available for the unknown-metadata refusal test");
            }

            const auto info = RootVhd::info(vhdxWork);
            check(info.value(QStringLiteral("ok")).toBool()
                      && info.value(QStringLiteral("kind")).toString() == QStringLiteral("dynamic VHDX")
                      && info.value(QStringLiteral("writable")).toBool(),
                  "the VHDX reader mounts its ext4 volume as writable");
            const auto rootBefore = RootVhd::listing(vhdxWork, QStringLiteral("/"));
            check(rootBefore.value(QStringLiteral("ok")).toBool(),
                  "the file manager lists the VHDX ext4 root directory");

            std::string vhdxError;
            auto image = RootDisk::openImage(QFile::encodeName(vhdxWork).toStdString(), &vhdxError);
            check(image && image->kind() == "dynamic VHDX"
                      && image->size() == 4097ull * 1024 * 1024,
                  "the VHDX wrapper exposes its virtual size and dynamic type");
            if (image) {
                std::vector<std::uint8_t> sparse(512, 0xFF);
                check(image->read(1024 * 1024, sparse.data(), sparse.size())
                          && std::all_of(sparse.begin(), sparse.end(),
                                         [](std::uint8_t value) { return value == 0; }),
                      "unallocated VHDX blocks read as zeroes");
                std::vector<std::uint8_t> pattern(512);
                for (std::size_t i = 0; i < pattern.size(); ++i)
                    pattern[i] = std::uint8_t(i ^ 0xA5);
                const bool wroteSparse = image->write(1024 * 1024 + 512,
                                                       pattern.data(), pattern.size())
                    && image->flush();
                check(wroteSparse, "writing into a sparse VHDX block allocates and stores it");
                const std::uint64_t afterFirstChunk = 4096ull * 1024 * 1024 + 64;
                std::vector<std::uint8_t> chunkPattern(512, 0x37);
                const bool wroteAfterChunk = image->write(afterFirstChunk,
                                                           chunkPattern.data(),
                                                           chunkPattern.size())
                    && image->flush();
                check(wroteAfterChunk,
                      "VHDX BAT chunk interleaving is followed across a bitmap boundary");
                image.reset();
                auto reopened = RootDisk::openImage(QFile::encodeName(vhdxWork).toStdString(),
                                                    &vhdxError);
                std::vector<std::uint8_t> readBack(pattern.size()), chunkReadBack(chunkPattern.size());
                check(reopened && reopened->read(1024 * 1024 + 512,
                                                  readBack.data(), readBack.size())
                          && readBack == pattern,
                      "the allocated VHDX block persists after closing and reopening");
                check(reopened && reopened->read(afterFirstChunk, chunkReadBack.data(),
                                                  chunkReadBack.size())
                          && chunkReadBack == chunkPattern,
                      "the payload after a VHDX BAT chunk boundary persists on reopen");
                reopened.reset();
            }

#ifdef Q_OS_WIN
            // Exercise the exact recovery path on a VHDX, including a Unicode
            // filename: the Windows probe and the engine's read/write handle
            // must resolve to the same file before an edit is allowed.
            const QString unicodeReadOnlyVhdx = temp.filePath(QStringLiteral("Root-\u8def\u5f84.vhdx"));
            const bool copiedReadOnlyVhdx = QFile::copy(vhdxFixture, unicodeReadOnlyVhdx);
            check(copiedReadOnlyVhdx,
                  "a separate Unicode-named VHDX copy is available for the Windows read-only test");
            if (copiedReadOnlyVhdx) {
                const QFileDevice::Permissions originalVhdxPermissions =
                    QFile::permissions(unicodeReadOnlyVhdx);
                const QFileDevice::Permissions readOnlyVhdxPermissions =
                    originalVhdxPermissions & ~(QFileDevice::WriteOwner | QFileDevice::WriteUser
                                                | QFileDevice::WriteGroup | QFileDevice::WriteOther);
                const bool markedVhdxReadOnly =
                    QFile::setPermissions(unicodeReadOnlyVhdx, readOnlyVhdxPermissions);
                check(markedVhdxReadOnly, "the VHDX test copy can be marked read-only on Windows");
                if (markedVhdxReadOnly) {
                    const auto beforeVhdxEnable = RootVhd::info(unicodeReadOnlyVhdx);
                    check(beforeVhdxEnable.value(QStringLiteral("ok")).toBool()
                              && !beforeVhdxEnable.value(QStringLiteral("writable")).toBool(),
                          "a read-only VHDX is reported as not writable before user action");
                    const auto enableVhdx = RootVhd::enableWriteAccess(unicodeReadOnlyVhdx);
                    check(enableVhdx.value(QStringLiteral("ok")).toBool(),
                          "Enable editing clears the Windows Read-only attribute on a VHDX");
                    const auto afterVhdxEnable = RootVhd::info(unicodeReadOnlyVhdx);
                    const bool vhdxWritable = afterVhdxEnable.value(QStringLiteral("writable")).toBool();
                    check(vhdxWritable,
                          "a Unicode-named VHDX reopens writable after enabling editing");
                    if (vhdxWritable) {
                        std::string directWriteError;
                        auto directWriteImage = RootDisk::openImage(
                            QDir::toNativeSeparators(unicodeReadOnlyVhdx).toStdWString(),
                            &directWriteError);
                        std::vector<std::uint8_t> sectorBefore(512), sectorAfter(512);
                        const bool readBefore = directWriteImage
                            && directWriteImage->read(0, sectorBefore.data(), sectorBefore.size());
                        const bool directWrite = readBefore
                            && directWriteImage->write(0, sectorBefore.data(), sectorBefore.size())
                            && directWriteImage->flush()
                            && directWriteImage->read(0, sectorAfter.data(), sectorAfter.size())
                            && sectorBefore == sectorAfter;
                        check(directWrite,
                              QStringLiteral("the writable VHDX accepts a direct sector write: %1")
                                  .arg(QString::fromStdString(directWriteError)));
                        directWriteImage.reset();
                        const auto created = RootVhd::makeFolder(unicodeReadOnlyVhdx,
                                                                  QStringLiteral("/"),
                                                                  QStringLiteral("readonly-vhdx-test"));
                        check(created.value(QStringLiteral("ok")).toBool(),
                              QStringLiteral("the read-only VHDX accepts an actual folder edit after enabling access: %1")
                                  .arg(created.value(QStringLiteral("msg")).toString()));
                        const auto undone = RootVhd::undo(unicodeReadOnlyVhdx);
                        check(undone.value(QStringLiteral("ok")).toBool(),
                              QStringLiteral("the VHDX edit can be undone after clearing the host Read-only attribute: %1")
                                  .arg(undone.value(QStringLiteral("msg")).toString()));
                    }
                }
                QFile::setPermissions(unicodeReadOnlyVhdx, originalVhdxPermissions);
            }
#endif

            const QString localFile = temp.filePath(QStringLiteral("vhdx-added.txt"));
            {
                QFile file(localFile);
                file.open(QIODevice::WriteOnly);
                file.write("VHDX journal write\n");
            }
            const auto added = RootVhd::addFile(vhdxWork, QStringLiteral("/"), localFile);
            check(added.value(QStringLiteral("ok")).toBool(),
                  "the journalled file manager writes a file into VHDX ext4");
            const auto rootAfterAdd = RootVhd::listing(vhdxWork, QStringLiteral("/"));
            bool foundAdded = false;
            for (const QVariant &entry : rootAfterAdd.value(QStringLiteral("entries")).toList())
                foundAdded = foundAdded
                    || entry.toMap().value(QStringLiteral("name")).toString()
                           == QStringLiteral("vhdx-added.txt");
            check(foundAdded, "the VHDX file manager can read back the newly added file");
            const auto undone = RootVhd::undo(vhdxWork);
            check(undone.value(QStringLiteral("ok")).toBool(),
                  "journalled VHDX edits can be undone");
            const auto rootAfterUndo = RootVhd::listing(vhdxWork, QStringLiteral("/"));
            bool stillFoundAdded = false;
            for (const QVariant &entry : rootAfterUndo.value(QStringLiteral("entries")).toList())
                stillFoundAdded = stillFoundAdded
                    || entry.toMap().value(QStringLiteral("name")).toString()
                           == QStringLiteral("vhdx-added.txt");
            check(!stillFoundAdded, "undo removes the added file from the VHDX filesystem");
        }
    }
#endif

#ifdef BSI_ROOT_7Z_FIXTURE
    // Opening a plain archive and a synthetic SFX both exercises the bundled
    // decoder: no WinRAR/7-Zip installation is part of the test machine.
    const auto extractedArchive = SevenZipArchive::extractRootVhd(
        QString::fromUtf8(BSI_ROOT_7Z_FIXTURE), temp.filePath(QStringLiteral("archive-cache")));
    check(extractedArchive.value(QStringLiteral("ok")).toBool(),
          "the built-in decoder extracts Root.vhd from a plain 7z package");
    if (extractedArchive.value(QStringLiteral("ok")).toBool())
        check(read(extractedArchive.value(QStringLiteral("path")).toString()) == pristine,
              "the plain-archive Root.vhd is byte-identical to the source fixture");

    QFile archiveFixture(QString::fromUtf8(BSI_ROOT_7Z_FIXTURE));
    QByteArray archiveBytes;
    if (archiveFixture.open(QIODevice::ReadOnly)) archiveBytes = archiveFixture.readAll();
    const QString sfxPath = temp.filePath(QStringLiteral("engine-package.exe"));
    check(write(sfxPath, QByteArray("MZ synthetic SFX stub\r\n") + archiveBytes),
          "write a synthetic engine self-extractor around the 7z fixture");
    const auto extractedSfx = SevenZipArchive::extractRootVhd(
        sfxPath, temp.filePath(QStringLiteral("sfx-cache")));
    check(extractedSfx.value(QStringLiteral("ok")).toBool(),
          "the built-in decoder finds and extracts Root.vhd inside an EXE");
    if (extractedSfx.value(QStringLiteral("ok")).toBool())
        check(read(extractedSfx.value(QStringLiteral("path")).toString()) == pristine,
              "the SFX Root.vhd is byte-identical to the source fixture");
#endif

    // Some engine images are GPT-partitioned rather than raw ext4 or MBR. Put
    // the fixture behind a minimal GPT partition and verify mount locates it.
    {
        constexpr int partitionOffset = 1024 * 1024;
        constexpr int filesystemBytes = 256 * 1024;
        QByteArray gpt(partitionOffset + filesystemBytes, '\0');
        gpt.replace(partitionOffset, filesystemBytes, pristine.left(filesystemBytes));
        auto put32 = [&gpt](int at, quint32 value) {
            for (int i = 0; i < 4; ++i) gpt[at + i] = char((value >> (8 * i)) & 0xff);
        };
        auto put64 = [&gpt](int at, quint64 value) {
            for (int i = 0; i < 8; ++i) gpt[at + i] = char((value >> (8 * i)) & 0xff);
        };
        gpt[510] = char(0x55); gpt[511] = char(0xaa);
        gpt[446 + 4] = char(0xee);
        put32(446 + 8, 1);
        gpt.replace(512, 8, QByteArray("EFI PART", 8));
        put32(512 + 8, 0x00010000);
        put32(512 + 12, 92);
        put64(512 + 24, 1);
        put64(512 + 32, quint64(gpt.size() / 512) - 1);
        put64(512 + 40, 34);
        put64(512 + 48, quint64(gpt.size() / 512) - 34);
        put64(512 + 72, 2);
        put32(512 + 80, 128);
        put32(512 + 84, 128);
        // A non-zero partition type GUID and the starting/ending LBAs.
        gpt[1024] = char(0x28);
        put64(1024 + 32, partitionOffset / 512);
        put64(1024 + 40, (partitionOffset + filesystemBytes) / 512 - 1);
        gpt[510] = char(0x55); gpt[511] = char(0xaa);
        const QString gptPath = temp.filePath(QStringLiteral("gpt-root.img"));
        check(write(gptPath, gpt), "write a synthetic GPT-partitioned image");
        std::string gptError;
        auto gptImage = RootDisk::openImage(QFile::encodeName(gptPath).toStdString(), &gptError);
        auto gptVolume = RootDisk::Volume::mount(std::move(gptImage), &gptError);
        check(gptVolume != nullptr, "ext4 mounts from a GPT partition");
        if (gptVolume)
            check(gptVolume->list("/", &gptError).inode != 0,
                  "the filesystem inside the GPT partition can be browsed");

        // Also cover vendor images whose ext4 starts at a sector-aligned
        // offset but whose partition map is absent or unusable.
        QByteArray unlabelled(partitionOffset + filesystemBytes, '\0');
        unlabelled.replace(partitionOffset, filesystemBytes, pristine.left(filesystemBytes));
        const QString unlabelledPath = temp.filePath(QStringLiteral("offset-root.img"));
        check(write(unlabelledPath, unlabelled), "write an ext4 image with no partition map");
        std::string offsetError;
        auto offsetImage = RootDisk::openImage(QFile::encodeName(unlabelledPath).toStdString(),
                                               &offsetError);
        auto offsetVolume = RootDisk::Volume::mount(std::move(offsetImage), &offsetError);
        check(offsetVolume != nullptr, "the bounded signature scan finds an unlabelled ext4 volume");
        if (offsetVolume)
            check(offsetVolume->list("/", &offsetError).inode != 0,
                  "the unlabelled ext4 volume can be browsed");
    }

    // ---- the engine ----
    {
        std::string error;
        auto image = RootDisk::openImage(QFile::encodeName(work).toStdString(), &error);
        check(image != nullptr, "the VHD opens");
        if (image) {
            check(image->kind() == "fixed VHD",
                  "the footer is recognised as a fixed (not dynamic) disk");
            auto volume = RootDisk::Volume::mount(std::move(image), &error);
            check(volume != nullptr, "the ext4 inside the VHD mounts");
            if (volume) {
                check(volume->writable(), "a clean filesystem is writable");
                check(volume->blockSize() == 1024, "the 1 KiB block size is read from the superblock");

                const RootDisk::Listing root = volume->list("/", &error);
                bool hasHello = false;
                for (const auto &entry : root.entries)
                    if (entry.name == "hello.txt") hasHello = true;
                check(root.inode != 0 && hasHello, "the root folder lists the file that is in it");
                check(root.inode != 0 && root.entries.size() >= 2,
                      "the root folder lists more than one entry");

                const std::uint32_t file = volume->resolve("/hello.txt", &error);
                check(file != 0, "/hello.txt resolves to an inode");
                const auto data = volume->readFile(file, &error);
                check(std::string(data.begin(), data.end()) == "hello world\n",
                      "the file reads back with its bytes intact");
            }
        }
    }

    // ---- symbolic links (fast inline and extent-backed) ----
    {
        const QString symlinkImage = temp.filePath(QStringLiteral("symlink.vhd"));
        check(QFile::copy(fixture, symlinkImage), "a copy of the ext4 fixture is made for symlink tests");
        const std::vector<std::uint8_t> shortTarget { 's', 'y', 's', '/', 'b', 'i', 'n' };
        std::string error;
        auto image = RootDisk::openImage(QFile::encodeName(symlinkImage).toStdString(), &error);
        auto volume = image ? RootDisk::Volume::mount(std::move(image), &error) : nullptr;
        check(volume != nullptr, "the symlink test image mounts");
        if (volume) {
            const auto root = volume->resolve("/", &error);
            check(volume->putSymlink(root, "fast-link", shortTarget, &error),
                  "a short symbolic link is created inline");
            const auto fastInode = volume->resolve("/fast-link", &error);
            const auto fastStat = volume->stat(fastInode, &error);
            check(fastStat.symlink() && volume->readFile(fastInode, &error) == shortTarget,
                  "reading a short symbolic link returns its target, not block pointers");

            const QByteArray longTarget(100, 'x');
            const std::vector<std::uint8_t> longBytes(longTarget.begin(), longTarget.end());
            check(volume->putSymlink(root, "long-link", longBytes, &error),
                  "a long symbolic link is stored using extents");
            const auto longInode = volume->resolve("/long-link", &error);
            check(volume->stat(longInode, &error).symlink()
                      && volume->readFile(longInode, &error) == longBytes,
                  "an extent-backed symbolic link returns its full target");
            check(volume->makeDir(root, "links-copy", 0755, &error),
                  "a destination folder is created for symbolic-link copy tests");
        }
        const auto copiedLink = RootVhd::copy(symlinkImage, QStringLiteral("/fast-link"),
                                               QStringLiteral("/links-copy"));
        check(copiedLink.value(QStringLiteral("ok")).toBool(),
              "symbolic links can be copied without dereferencing their target");
        const QString linkTargetFile = temp.filePath(QStringLiteral("link-target.txt"));
        const auto linkExtract = RootVhd::extract(symlinkImage,
                                                  QStringLiteral("/links-copy/fast-link"),
                                                  linkTargetFile);
        QFile linkTarget(linkTargetFile);
        linkTarget.open(QIODevice::ReadOnly);
        check(linkExtract.value(QStringLiteral("ok")).toBool()
                  && linkTarget.readAll() == QByteArray("sys/bin"),
              "saving a symbolic link writes its target text");
    }

    // ---- the file manager: add, list, delete ----
    int originalEntries = 0;                  // the root folder as the image shipped it
    {
        const auto info = RootVhd::info(work);
        check(info.value(QStringLiteral("ok")).toBool(), "RootVhd::info opens the image");
        check(info.value(QStringLiteral("writable")).toBool(), "RootVhd reports it writable");

        const auto before = RootVhd::listing(work, QStringLiteral("/"));
        check(before.value(QStringLiteral("ok")).toBool(), "the root folder is listed");
        const int entries = before.value(QStringLiteral("entries")).toList().size();
        originalEntries = entries;
        check(entries >= 2, "the listing carries the folder's entries");

        const QString added = temp.filePath(QStringLiteral("added.txt"));
        {
            QFile file(added);
            file.open(QIODevice::WriteOnly);
            file.write("added by the regression test\n");
        }
        const auto add = RootVhd::addFile(work, QStringLiteral("/"), added);
        check(add.value(QStringLiteral("ok")).toBool(), "a local file is written into the image");
        const auto afterAdd = RootVhd::listing(work, QStringLiteral("/"));
        check(afterAdd.value(QStringLiteral("entries")).toList().size() == entries + 1,
              "the folder lists one entry more");
        check(RootVhd::hasUndo(work), "the write left an undo record behind");
        const auto duplicate = RootVhd::addFile(work, QStringLiteral("/"), added);
        check(!duplicate.value(QStringLiteral("ok")).toBool()
                  && duplicate.value(QStringLiteral("msg")).toString().contains(QLatin1String("Replace")),
              "Add refuses an existing name and directs the user to Replace");

        const auto renamed = RootVhd::rename(work, QStringLiteral("/added.txt"),
                                              QStringLiteral("renamed.txt"));
        check(renamed.value(QStringLiteral("ok")).toBool(),
              "a file can be renamed without changing its contents");
        const auto collision = RootVhd::rename(work, QStringLiteral("/renamed.txt"),
                                                QStringLiteral("hello.txt"));
        check(!collision.value(QStringLiteral("ok")).toBool(),
              "rename refuses to overwrite an existing entry");

        const QString replacementPath = temp.filePath(QStringLiteral("replacement.bin"));
        const QByteArray replacementBytes("new bytes replace the old contents\n");
        {
            QFile replacement(replacementPath);
            replacement.open(QIODevice::WriteOnly);
            replacement.write(replacementBytes);
        }
        const auto replaced = RootVhd::replaceFile(work, QStringLiteral("/renamed.txt"),
                                                   replacementPath);
        check(replaced.value(QStringLiteral("ok")).toBool(),
              "an existing image file can be replaced from a differently named local file");

        const QString out = temp.filePath(QStringLiteral("out.txt"));
        const auto extract = RootVhd::extract(work, QStringLiteral("/renamed.txt"), out);
        check(extract.value(QStringLiteral("ok")).toBool(), "the replacement can be extracted");
        {
            QFile file(out);
            file.open(QIODevice::ReadOnly);
            check(file.readAll() == replacementBytes,
                  "Replace keeps the image path and writes the selected local file's bytes");
        }

        const auto mkdir = RootVhd::makeFolder(work, QStringLiteral("/"), QStringLiteral("debloat"));
        check(mkdir.value(QStringLiteral("ok")).toBool(), "a folder is created in the image");

        // Adding at the image root builds "/name", not "//name": cleanPath keeps
        // a double leading slash on Windows, and the path then does not resolve.
        const QString treeRoot = temp.filePath(QStringLiteral("tree"));
        QDir().mkpath(treeRoot + QStringLiteral("/nested"));
        {
            QFile file(treeRoot + QStringLiteral("/nested/leaf.txt"));
            file.open(QIODevice::WriteOnly);
            file.write("a leaf\n");
            QFile top(treeRoot + QStringLiteral("/top.txt"));
            top.open(QIODevice::WriteOnly);
            top.write("top\n");
        }
        const auto addTree = RootVhd::addFolder(work, QStringLiteral("/"), treeRoot);
        check(addTree.value(QStringLiteral("ok")).toBool(),
              "a folder tree is added at the image root");
        const auto nested = RootVhd::listing(work, QStringLiteral("/tree/nested"));
        check(nested.value(QStringLiteral("ok")).toBool()
                  && nested.value(QStringLiteral("entries")).toList().size() == 1,
              "the tree keeps its structure, sub-folder included");
        const auto moveTree = RootVhd::move(work, QStringLiteral("/tree"),
                                            QStringLiteral("/debloat"));
        check(moveTree.value(QStringLiteral("ok")).toBool()
                  && RootVhd::listing(work, QStringLiteral("/debloat/tree/nested"))
                         .value(QStringLiteral("ok")).toBool(),
              "a folder tree can be moved within the image without losing its children");
        const auto cycleMove = RootVhd::move(work, QStringLiteral("/debloat/tree"),
                                             QStringLiteral("/debloat/tree/nested"));
        check(!cycleMove.value(QStringLiteral("ok")).toBool(),
              "a folder cannot be moved into one of its own descendants");
        const auto moveTreeBack = RootVhd::move(work, QStringLiteral("/debloat/tree"),
                                                QStringLiteral("/"));
        check(moveTreeBack.value(QStringLiteral("ok")).toBool()
                  && RootVhd::listing(work, QStringLiteral("/tree/nested"))
                         .value(QStringLiteral("ok")).toBool(),
              "a moved folder can be moved back to its original parent");
        const auto copyTree = RootVhd::copy(work, QStringLiteral("/tree"),
                                            QStringLiteral("/debloat"));
        const auto copiedNested = RootVhd::listing(work, QStringLiteral("/debloat/tree/nested"));
        check(copyTree.value(QStringLiteral("ok")).toBool()
                  && copiedNested.value(QStringLiteral("ok")).toBool(),
              "a folder tree can be copied within the image");
        const QVariantMap sourceLeafInfo = RootVhd::listing(work, QStringLiteral("/tree/nested"))
                                               .value(QStringLiteral("entries")).toList().value(0).toMap();
        const QVariantMap copiedLeafInfo = copiedNested.value(QStringLiteral("entries"))
                                               .toList().value(0).toMap();
        check(copiedLeafInfo.value(QStringLiteral("mode")) == sourceLeafInfo.value(QStringLiteral("mode"))
                  && copiedLeafInfo.value(QStringLiteral("uid")) == sourceLeafInfo.value(QStringLiteral("uid"))
                  && copiedLeafInfo.value(QStringLiteral("gid")) == sourceLeafInfo.value(QStringLiteral("gid"))
                  && copiedLeafInfo.value(QStringLiteral("mtime")) == sourceLeafInfo.value(QStringLiteral("mtime")),
              "a folder copy retains file permissions, owner, group, and modification time");
        const QString copiedLeaf = temp.filePath(QStringLiteral("copied-leaf.txt"));
        const auto copyExtract = RootVhd::extract(work, QStringLiteral("/debloat/tree/nested/leaf.txt"),
                                                  copiedLeaf);
        QFile copiedLeafFile(copiedLeaf);
        copiedLeafFile.open(QIODevice::ReadOnly);
        check(copyExtract.value(QStringLiteral("ok")).toBool()
                  && copiedLeafFile.readAll() == QByteArray("a leaf\n"),
              "a copied tree contains the original file bytes");
        const auto removeCopiedTree = RootVhd::remove(work, QStringLiteral("/debloat/tree"));
        check(removeCopiedTree.value(QStringLiteral("ok")).toBool(),
              "the copied folder tree can be removed recursively");
        const auto copyRenamedFile = RootVhd::copy(work, QStringLiteral("/renamed.txt"),
                                                   QStringLiteral("/debloat"),
                                                   QStringLiteral("renamed-copy.txt"));
        const QString copiedFilePath = temp.filePath(QStringLiteral("renamed-copy.txt"));
        const auto copyFileExtract = RootVhd::extract(work,
                                                      QStringLiteral("/debloat/renamed-copy.txt"),
                                                      copiedFilePath);
        QFile copiedFile(copiedFilePath);
        copiedFile.open(QIODevice::ReadOnly);
        check(copyRenamedFile.value(QStringLiteral("ok")).toBool()
                  && copyFileExtract.value(QStringLiteral("ok")).toBool()
                  && copiedFile.readAll() == replacementBytes,
              "a file can be copied under a new name with its bytes intact");
        check(RootVhd::remove(work, QStringLiteral("/debloat/renamed-copy.txt"))
                  .value(QStringLiteral("ok")).toBool(),
              "the copied file can be removed independently");

        // A file and a folder are both accepted by one addPath call.
        const QString pathFile = temp.filePath(QStringLiteral("path-added.txt"));
        {
            QFile file(pathFile);
            file.open(QIODevice::WriteOnly);
            file.write("added through addPath\\n");
        }
        const auto addPathFile = RootVhd::addPath(work, QStringLiteral("/"), pathFile);
        check(addPathFile.value(QStringLiteral("ok")).toBool(), "addPath takes a file");
        const auto addPathDir = RootVhd::addPath(work, QStringLiteral("/debloat"), treeRoot);
        check(addPathDir.value(QStringLiteral("ok")).toBool(), "addPath takes a folder");

        // Search walks the whole image, not just the open folder.
        const auto search = RootVhd::search(work, QStringLiteral("leaf"));
        bool foundLeaf = false;
        for (const auto &value : search.value(QStringLiteral("entries")).toList()) {
            const QString path = value.toMap().value(QStringLiteral("path")).toString();
            if (path == QLatin1String("/tree/nested/leaf.txt")) foundLeaf = true;
        }
        check(search.value(QStringLiteral("ok")).toBool() && foundLeaf,
              "search finds a file by name anywhere in the image");
        const auto removeTree = RootVhd::remove(work, QStringLiteral("/tree"));
        check(removeTree.value(QStringLiteral("ok")).toBool()
                  && !RootVhd::listing(work, QStringLiteral("/tree")).value(QStringLiteral("ok")).toBool(),
              "deleting a folder removes its nested files and subfolders");
        const auto noMatch = RootVhd::search(work, QStringLiteral("zzz-nothing-zzz"));
        check(noMatch.value(QStringLiteral("ok")).toBool()
                  && noMatch.value(QStringLiteral("entries")).toList().isEmpty(),
              "a search with no match says so instead of failing");
        const auto inside = RootVhd::listing(work, QStringLiteral("/debloat"));
        bool hasCopiedTree = false;
        for (const auto &value : inside.value(QStringLiteral("entries")).toList())
            if (value.toMap().value(QStringLiteral("name")).toString() == QLatin1String("tree"))
                hasCopiedTree = true;
        check(inside.value(QStringLiteral("ok")).toBool() && hasCopiedTree,
              "the folder added by addPath is in the folder it was told to go to");

        const int beforeRemove =
            RootVhd::listing(work, QStringLiteral("/")).value(QStringLiteral("entries")).toList().size();
        const auto remove = RootVhd::remove(work, QStringLiteral("/renamed.txt"));
        check(remove.value(QStringLiteral("ok")).toBool(), "a file is removed from the image");
        check(RootVhd::listing(work, QStringLiteral("/")).value(QStringLiteral("entries")).toList().size()
                  == beforeRemove - 1,
              "the folder lists one entry fewer after the delete");
        const auto removePathFile = RootVhd::remove(work, QStringLiteral("/path-added.txt"));
        check(removePathFile.value(QStringLiteral("ok")).toBool(),
              "a file added through addPath can be deleted");
        const auto refused = RootVhd::remove(work, QStringLiteral("/not-there.txt"));
        check(!refused.value(QStringLiteral("ok")).toBool(),
              "removing something that is not there is refused, not guessed at");
    }

    // ---- undo ----
    {
        check(RootVhd::hasUndo(work), "the image still has an undo record");
        const auto undone = RootVhd::undo(work);
        check(undone.value(QStringLiteral("ok")).toBool(), "undo runs");
        QFile file(work);
        file.open(QIODevice::ReadOnly);
        const QByteArray restored = file.readAll();
        file.close();
        check(restored == pristine, "undo restores the image byte for byte");
        check(!RootVhd::hasUndo(work), "the undo record is gone once it has been replayed");
        const auto reopened = RootVhd::listing(work, QStringLiteral("/"));
        check(reopened.value(QStringLiteral("ok")).toBool()
                  && reopened.value(QStringLiteral("entries")).toList().size() == originalEntries,
              "the restored image opens again and holds its original entries");
        // The folder added at the root and everything below it are gone again:
        // the inode table, the bitmaps and the group counts all went back.
        const auto gone = RootVhd::listing(work, QStringLiteral("/tree"));
        check(!gone.value(QStringLiteral("ok")).toBool(),
              "the folder tree added during the test is gone after the undo");
        const auto goneFile = RootVhd::listing(work, QStringLiteral("/debloat"));
        check(!goneFile.value(QStringLiteral("ok")).toBool(),
              "the folder created during the test is gone after the undo");
    }

    // ---- refusals ----
    {
        const auto missing = RootVhd::info(temp.filePath(QStringLiteral("nope.vhd")));
        check(!missing.value(QStringLiteral("ok")).toBool(), "a missing image is refused");
        const QString junk = temp.filePath(QStringLiteral("junk.vhd"));
        {
            QFile file(junk);
            file.open(QIODevice::WriteOnly);
            file.write(QByteArray(4096, 'x'));
        }
        const auto bad = RootVhd::info(junk);
        check(!bad.value(QStringLiteral("ok")).toBool(), "a file with no ext4 in it is refused");
        check(RootVhd::findImage(QString()).isEmpty(), "no installation means no engine folder guess");
    }
#else
    check(true, "no Root.vhd fixture in this checkout; the file manager is not exercised");
#endif
}

int runPatchRegressionTests()
{
    failures = WorkspaceTests::run();
    QTemporaryDir temp;
    check(temp.isValid(), "regression temp directory available");
    if (!temp.isValid())
        return failures;
    std::printf("[theme-probe] binary previews must not truncate diagnostics\n");
    {
        auto f = fixture(2, QStringLiteral("#112233"));
        // Reproduce a filename marker beside a compressed image/binary stream.
        // The real user's report stopped at the NUL following its first byte.
        const QByteArray binary = QByteArray::fromHex("0100504e470d0a097f5cff") + QByteArray(512, '\0');
        f.bytes += utf16be(QStringLiteral("UiThemeObject.qml"));
        f.bytes += qCompress(binary, 9);
        const QString path = temp.filePath(QStringLiteral("binary-preview.exe"));
        check(write(path, f.bytes), "write binary-preview diagnostic fixture");
        const QString report = ThemeEngine::probeReport(path);
        check(report.contains(QStringLiteral("\\x01\\x00PNG\\x0d\\x0a\\x09\\x7f\\\\\\xff")),
              "probe escapes NUL, newlines, other controls, backslash and high bytes");
        check(!report.contains(QChar(0)) && report.contains(QStringLiteral("[4]"))
                  && report.contains(QStringLiteral("qrc mapping"))
                  && report.contains(QStringLiteral("[6] patch feasibility"))
                  && report.endsWith(QStringLiteral("Probe complete.\n")),
              "probe includes final detection, resource mapping and feasibility after binary preview");
        const QByteArray text = report.toUtf8();
        check(size_t(text.size()) == std::strlen(text.constData()),
              "diagnostic text remains safe for C-string consumers");
        QProcess probe;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
#ifdef Q_OS_WIN
        env.insert(QStringLiteral("__COMPAT_LAYER"), QStringLiteral("RUNASINVOKER"));
#endif
        probe.setProcessEnvironment(env);
        probe.start(QCoreApplication::applicationFilePath(), { QStringLiteral("--theme-probe"), path });
        const bool finished = probe.waitForStarted(10000) && probe.waitForFinished(30000);
        if (!finished) {
            probe.kill();
            probe.waitForFinished();
        }
        const QByteArray stdoutText = probe.readAllStandardOutput().replace("\r\n", "\n");
        check(finished && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0
                  && stdoutText == text,
              "actual theme-probe command exports the entire UTF-8 report through redirected stdout");
        check(read(path) == f.bytes && !QFileInfo::exists(path + QStringLiteral(".bak")),
              "collecting theme diagnostics never changes or backs up the emulator file");
    }
    std::printf("[theme-runtime] real Qt resources, disk caches and restarts\n");
    QByteArray diskUnit;
    for (const int version : { 1, 2, 3 }) {
        auto f = fixture(version, QStringLiteral("#112233"));
        // The runtime child is explicitly given these bases. Represent that
        // same ground truth in the PE, rather than asking detection to invent
        // a base from the retained orphan after a verified relocation.
        declarePoolBases(f);
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
            check(patched.ok, QStringLiteral("patch works without running another theme changer first (rcc %1): %2").arg(version).arg(patched.msg));
            const auto after = read(path);
            check(after.size() == before.size(), "ordinary palette edits use verified existing storage without PE growth");
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
        auto f = fixture(2, QStringLiteral("#556677"), diskUnit);
        declarePoolBases(f);
        const QString path = temp.filePath(QStringLiteral("compiled.exe"));
        const QString cache = temp.filePath(QStringLiteral("embedded-cache"));
        write(path, f.bytes);
        check(loadInQt(path, f, cache, QStringLiteral("#112233")),
              "reproduce unchanged colors: embedded cached QML overrides different source colors");
        const auto info = ThemeEngine::detectTheme(f.bytes, QStringLiteral("qml"));
        const auto r = ThemeEngine::patchTheme(path, info, ThemeEngine::parseQmlEntries(info.qml),
                                              { { QStringLiteral("primary100"), QStringLiteral("#cf50ab") } });
        if (!r.ok) std::printf("    compiled theme patch: %s\n", qPrintable(r.msg));
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
        auto sidebarUnit = unit;
        const QString sidebarId = QStringLiteral("fGetMacroBtnEnabled");
        putLe32(sidebarUnit, 304, quint32(sidebarId.size()));
        for (int i = 0; i < sidebarId.size(); ++i)
            putLe16(sidebarUnit, 324 + i * 2, sidebarId.at(i).unicode());
        check(QtResources::activeUnits(sidebarUnit, { QStringLiteral("PlrSidebar.qml") }) == 1
                  && QtResources::invalidateUnits(sidebarUnit, { QStringLiteral("PlrSidebar.qml") }) == 1
                  && QtResources::activeUnits(sidebarUnit, { QStringLiteral("PlrSidebar.qml") }) == 0,
              "the MSI sidebar QV4 unit is selected by its structural function IDs despite having no filename or stem");
        auto topbarUnit = unit;
        const QString topbarId = QStringLiteral("fReloadTopbarElements");
        putLe32(topbarUnit, 304, quint32(topbarId.size()));
        for (int i = 0; i < topbarId.size(); ++i)
            putLe16(topbarUnit, 324 + i * 2, topbarId.at(i).unicode());
        check(QtResources::activeUnits(topbarUnit, { QStringLiteral("PlrTopbar.qml") }) == 1
                  && QtResources::invalidateUnits(topbarUnit, { QStringLiteral("PlrTopbar.qml") }) == 1
                  && QtResources::activeUnits(topbarUnit, { QStringLiteral("PlrTopbar.qml") }) == 0,
              "the MSI topbar QV4 unit is selected by its structural function IDs despite having no filename or stem");
        auto notificationUnit = unit;
        const QString notificationId = QStringLiteral("iNotificationBarLayout");
        putLe32(notificationUnit, 304, quint32(notificationId.size()));
        for (int i = 0; i < notificationId.size(); ++i)
            putLe16(notificationUnit, 324 + i * 2, notificationId.at(i).unicode());
        check(QtResources::activeUnits(notificationUnit, { QStringLiteral("PlrNotificationBarWindow.qml") }) == 1
                  && QtResources::invalidateUnits(notificationUnit, { QStringLiteral("PlrNotificationBarWindow.qml") }) == 1
                  && QtResources::activeUnits(notificationUnit, { QStringLiteral("PlrNotificationBarWindow.qml") }) == 0,
              "notification-bar QV4 units used by gradient surfaces are selected by a structural identifier");
        auto buttonUnit = unit;
        const QString buttonId = QStringLiteral("defaultComp");
        putLe32(buttonUnit, 304, quint32(buttonId.size()));
        for (int i = 0; i < buttonId.size(); ++i)
            putLe16(buttonUnit, 324 + i * 2, buttonId.at(i).unicode());
        check(QtResources::activeUnits(buttonUnit, { QStringLiteral("UiButton.qml") }) == 1
                  && QtResources::invalidateUnits(buttonUnit, { QStringLiteral("UiButton.qml") }) == 1
                  && QtResources::activeUnits(buttonUnit, { QStringLiteral("UiButton.qml") }) == 0,
              "shared UiButton QV4 bytecode used by gradient surfaces is selected structurally");
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

        auto settingsWindow = unit;
        const QString settingsId = QStringLiteral("iSettingsWindowShadowContainer");
        putLe32(settingsWindow, 304, quint32(settingsId.size()));
        for (int i = 0; i < settingsId.size(); ++i)
            putLe16(settingsWindow, 324 + i * 2, settingsId.at(i).unicode());
        check(QtResources::matchingUnits(settingsWindow, { QStringLiteral("UiSettingsWindow.qml") }) == 1
                  && QtResources::activeUnits(settingsWindow, { QStringLiteral("UiSettingsWindow.qml") }) == 1
                  && QtResources::invalidateUnits(settingsWindow, { QStringLiteral("UiSettingsWindow.qml") }) == 1
                  && QtResources::matchingUnits(settingsWindow, { QStringLiteral("UiSettingsWindow.qml") }) == 1
                  && QtResources::activeUnits(settingsWindow, { QStringLiteral("UiSettingsWindow.qml") }) == 0,
              "MSI Settings-window QV4 IDs invalidate even when the compiled unit omits its source filename");

        auto aboutUnit = unit;
        const QString aboutId = QStringLiteral("Important links");
        putLe32(aboutUnit, 304, quint32(aboutId.size()));
        for (int i = 0; i < aboutId.size(); ++i)
            putLe16(aboutUnit, 324 + i * 2, aboutId.at(i).unicode());
        check(QtResources::activeUnits(aboutUnit, { QStringLiteral("PlrAboutSetting.qml") }) == 1
                  && QtResources::invalidateUnits(aboutUnit, { QStringLiteral("PlrAboutSetting.qml") }) == 1
                  && QtResources::activeUnits(aboutUnit, { QStringLiteral("PlrAboutSetting.qml") }) == 0,
              "MSI About-page QV4 IDs invalidate the active compiled copy so new developer links can render");

        auto bootControllerUnit = unit;
        const QString bootControllerId = QStringLiteral("iBootProgressContainer");
        putLe32(bootControllerUnit, 304, quint32(bootControllerId.size()));
        for (int i = 0; i < bootControllerId.size(); ++i)
            putLe16(bootControllerUnit, 324 + i * 2, bootControllerId.at(i).unicode());
        check(QtResources::activeUnits(bootControllerUnit, {}, { QStringLiteral("iBootProgressContainer") }) == 1
                  && QtResources::invalidateUnits(bootControllerUnit, {}, { QStringLiteral("iBootProgressContainer") }) == 1
                  && QtResources::activeUnits(bootControllerUnit, {}, { QStringLiteral("iBootProgressContainer") }) == 0,
              "boot controller compiled QML can be selected by an explicit structural identifier");
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

    std::printf("[settings-model] scope page discovery to the live Settings model\n");
    {
        const QStringList pages {
            QStringLiteral("General.qml"), QStringLiteral("Display.qml"),
            QStringLiteral("Graphics.qml"), QStringLiteral("Audio.qml"),
            QStringLiteral("Keyboard.qml"), QStringLiteral("Preferences.qml"),
            QStringLiteral("Advanced.qml"), QStringLiteral("PlrAboutSetting.qml")
        };
        const auto makePlayer = [&](bool duplicateLivePage) {
            QString rows;
            for (const QString &page : pages)
                rows += QStringLiteral("{ pQmlSource: \"%1\" },\n").arg(page);
            if (duplicateLivePage)
                rows += QStringLiteral("{ pQmlSource: \"General.qml\" },\n");
            return QStringLiteral(
                "import QtQuick\nItem {\n"
                " property SettingElementModelList pSettingsItems: SettingElementModelList {\n"
                "  pSettingElements: [\n%1  ]\n"
                " }\n"
                " property SettingElementModelList pPreviewItems: SettingElementModelList { pSettingElements: [{ pQmlSource: \"Preview.qml\" }] }\n"
                " Component { id: unrelatedPreview; Item { property string pQmlSource: \"General.qml\" } }\n"
                "}\n").arg(rows);
        };
        const QString settingsWindow = QStringLiteral(
            "import QtQuick\nUiShadowContainer { id: iSettingsWindowShadowContainer;\n"
            " property var model: pSettingsItems.pSettingElements\n"
            " function fChangeSelectedSetting(index) { setSource(model.modelData.pQmlSource); model.modelData.pComponentObj = item }\n"
            "}\n");
        QList<QPair<QString, QByteArray>> files {
            { QStringLiteral("plr.qml"), makePlayer(false).toUtf8() },
            { QStringLiteral("UiSettingsWindow.qml"), settingsWindow.toUtf8() },
            { QStringLiteral("PlrAboutSetting.qml"), aboutPageFixture().toUtf8() },
            { QStringLiteral("General.qml"), QByteArray("Item { }\n") },
            { QStringLiteral("Display.qml"), QByteArray("Item { }\n") },
            { QStringLiteral("Graphics.qml"), QByteArray("Item { }\n") },
            { QStringLiteral("Audio.qml"), QByteArray("Item { }\n") },
            { QStringLiteral("Keyboard.qml"), QByteArray("Item { }\n") },
            { QStringLiteral("Preferences.qml"), QByteArray("Item { }\n") },
            { QStringLiteral("Advanced.qml"), QByteArray("Item { }\n") },
            { QStringLiteral("qrcAssets/BlueStacks5Beta.svg"), QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\"><circle cx=\"32\" cy=\"32\" r=\"24\"/></svg>") },
            { QStringLiteral("qrcAssets/Loader_bright.svg"), QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 16 16\"><circle cx=\"8\" cy=\"8\" r=\"6\"/></svg>") }
        };
        auto validModel = nestedResources(files);
        addRegistration(validModel, false);
        const QString validPath = temp.filePath(QStringLiteral("settings-model-decoy.exe"));
        write(validPath, validModel.f.bytes);
        const auto scoped = SettingsPanelPatcher::analyze(validPath);
        check(scoped.supported && scoped.settingsPageCount == pages.size(),
              QStringLiteral("duplicate pQmlSource outside the live model does not create a false Settings-tab duplicate (%1)")
                  .arg(scoped.detail));

        QString componentRows;
        for (const QString &page : pages)
            componentRows += QStringLiteral("{ pQmlSource: \"%1\" },\n").arg(page);
        const QString componentPlayer = QStringLiteral(
            "import QtQuick\nItem {\n"
            " property var pSettingsWindow: settingsWindowFactory.createObject(iMainWindow, {pSettingsItems: settingsItemsFactory.createObject(iMainWindow)})\n"
            " Component { id: settingsWindowFactory; Item { } }\n"
            " Component { id: settingsItemsFactory\n"
            "  SettingElementModelList { id: pSettingsItems\n"
            "   pSettingElements: [\n%1   ]\n"
            "   SettingElementModelList { id: iNestedDecoy; pSettingElements: [\n%2   ] }\n"
            "  }\n"
            " }\n"
            "}\n").arg(componentRows, componentRows);
        files[0].second = componentPlayer.toUtf8();
        auto componentModel = nestedResources(files);
        addRegistration(componentModel, false);
        const QString componentPath = temp.filePath(QStringLiteral("settings-model-component.exe"));
        write(componentPath, componentModel.f.bytes);
        const auto componentScoped = SettingsPanelPatcher::analyze(componentPath);
        check(componentScoped.supported && componentScoped.settingsPageCount == pages.size(),
              QStringLiteral("the root model passed to the live Settings window wins over nested complete model decoys (%1)")
                  .arg(componentScoped.detail));

        QString assignedComponentPlayer = componentPlayer;
        assignedComponentPlayer.replace(
            QStringLiteral(" property var pSettingsWindow: settingsWindowFactory.createObject(iMainWindow, {pSettingsItems: settingsItemsFactory.createObject(iMainWindow)})\n"),
            QStringLiteral(" property var pSettingsWindow\n"
                           " function makeSettings() { pSettingsWindow = settingsWindowFactory.createObject(iMainWindow); pSettingsWindow.pSettingsItems = settingsItemsFactory.createObject(iMainWindow); }\n"));
        files[0].second = assignedComponentPlayer.toUtf8();
        auto assignedComponentModel = nestedResources(files);
        addRegistration(assignedComponentModel, false);
        const QString assignedComponentPath = temp.filePath(QStringLiteral("settings-model-late-binding.exe"));
        write(assignedComponentPath, assignedComponentModel.f.bytes);
        const auto assignedComponent = SettingsPanelPatcher::analyze(assignedComponentPath);
        check(assignedComponent.supported && assignedComponent.settingsPageCount == pages.size(),
              QStringLiteral("an MSI-style post-create pSettingsItems assignment resolves the active component model (%1)")
                  .arg(assignedComponent.detail));

        files[0].second = makePlayer(true).toUtf8();
        auto duplicateModel = nestedResources(files);
        addRegistration(duplicateModel, false);
        const QString duplicatePath = temp.filePath(QStringLiteral("settings-model-duplicate.exe"));
        write(duplicatePath, duplicateModel.f.bytes);
        const auto duplicate = SettingsPanelPatcher::analyze(duplicatePath);
        check(!duplicate.supported && duplicate.detail.contains(QStringLiteral("duplicate QML page path")),
              QStringLiteral("a repeated page in the live Settings model is still refused safely (%1)")
                  .arg(duplicate.detail));
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

    std::printf("[extras-titlebar] the version and engine labels are hidden, never deleted\n");
    {
        // "Titlebar Version" used to null every UTF-16 "version" in the file.
        // That also hit the compiled QML caches and the translator tables, and
        // the latest player then refused to start (0x…05). The label is hidden
        // in PlrTopbar.qml instead: the same two Text elements exist in 5.9,
        // 5.10 and the current build.
        const QString topbar = QStringLiteral(
            "import QtQuick 2.15\r\n"
            "UiRowLayout {\r\n"
            "    UiRowLayout{\r\n"
            "        Text {\r\n"
            "            id: iVersionTxt\r\n"
            "            text: plrBackend.version\r\n"
            "            opacity: 0.7\r\n"
            "        }\r\n"
            "        Text {\r\n"
            "            id: iImageTxt\r\n"
            "            visible: uiBackend.oem === \"msi5\"\r\n"
            "            text: \"N32\"\r\n"
            "            opacity: 0.7\r\n"
            "        }\r\n"
            "    }\r\n"
            "}\r\n");
        // A translator table: the same word, UTF-16LE, exactly what the old
        // removal silently zeroed.
        const QByteArray german = QByteArray("v\0e\0r\0s\0i\0o\0n\0", 14);
        // Stored raw, not zlib'd: the point of the test is that those bytes come
        // out of the patch byte-for-byte, which a compressed record would hide.
        QSet<QString> raw;
        raw.insert(QStringLiteral("i18n.de-DE.qm"));
        // The QML comes first and the table is packed straight behind it, so the
        // record has no slack - on a real player every slot is exactly full too,
        // and that is why a growing edit used to end in "needs more bytes".
        auto r = resourcesBytes({ qMakePair(QStringLiteral("PlrTopbar.qml"), topbar.toUtf8()),
                                  qMakePair(QStringLiteral("i18n.de-DE.qm"), german) },
                                false, raw);
        addRegistration(r, false);
        const QString path = temp.filePath(QStringLiteral("titlebar-removals.exe"));
        write(path, r.f.bytes);

        check(ExtrasPatcher::removalNames().contains(QStringLiteral("Titlebar Version"))
                  && ExtrasPatcher::removalNames().contains(QStringLiteral("Titlebar Instance Label")),
              "the title bar offers both the version and the engine label as removals");

        const auto applied = ExtrasPatcher::applyRemovals(
            path, { QStringLiteral("Titlebar Version") });
        check(applied.ok && applied.msg.contains(QLatin1String("Hid 1 title-bar label")),
              QStringLiteral("the version label is hidden in the QML (%1)").arg(applied.msg));
        const QByteArray after = read(path);
        check(after.contains(german),
              "the UTF-16 string 'version' in the translator table is untouched");
        const QByteArray qmlAfter = resourceText(after, r, QStringLiteral("PlrTopbar.qml")).toUtf8();
        // The record is exactly full, like every slot in a real player, and the
        // hide still has to work: the edit empties the text binding instead of
        // adding a member, so nothing grows and no free space is needed. This is
        // the fix for "it says it wants more bytes to hide".
        check(be32(after, r.nodes.value(QStringLiteral("PlrTopbar.qml")) + 10)
                  == quint32(r.entries.value(QStringLiteral("PlrTopbar.qml")) - r.f.payloads),
              "hiding a label keeps its record where it was - no growth, no relocation");
        check(qmlAfter.size() <= topbar.toUtf8().size(),
              "and the document is not one byte larger than the stock one");
        const QString text = QString::fromUtf8(qmlAfter);
        check(text.contains(QStringLiteral("text: \"\""))
                  && !text.contains(QStringLiteral("text: plrBackend.version")),
              "the version Text prints nothing; the binding, not the element, was emptied");
        check(text.contains(QStringLiteral("text: \"N32\"")),
              "the engine label is a separate removal and is still there");

        const auto engine = ExtrasPatcher::applyRemovals(
            path, { QStringLiteral("Titlebar Instance Label") });
        check(engine.ok && engine.msg.contains(QLatin1String("Hid 1 title-bar label")),
              QStringLiteral("the engine label is hidden by its own row (%1)").arg(engine.msg));
        const QString both = resourceText(read(path), r, QStringLiteral("PlrTopbar.qml"));
        check(both.contains(QStringLiteral("visible: false")),
              "an existing visible: expression is switched off in place");
        check(!both.contains(QLatin1String("titlebar hide")),
              "no marker comment is added: a comment would be bytes the record does not have");
        check(both.size() <= topbar.size(),
              "and the whole document still did not grow");
        check(read(path).contains(german),
              "and the string table is still intact after both removals");

        const auto repeat = ExtrasPatcher::applyRemovals(
            path, { QStringLiteral("Titlebar Version") });
        check(repeat.ok && repeat.msg.contains(QLatin1String("already hidden")),
              QStringLiteral("a repeated removal reports the label as already hidden (%1)").arg(repeat.msg));
        check(resourceText(read(path), r, QStringLiteral("PlrTopbar.qml"))
                      .count(QStringLiteral("text: \"\"")) == 1,
              "a repeated removal does not edit the label a second time");
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
            "        UiImageButton {\r\n"
            "            id: iHomeBtn\r\n"
            "        }\r\n"
            "        UiImageButton {\r\n"
            "            id: iRecentBtn\r\n"
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
        check(patchedPlr.contains(
                  QStringLiteral("anchors.leftMargin: Math.min(144, parent.width / 4)"))
                  && patchedPlr.contains(
                      QStringLiteral("anchors.rightMargin: Math.min(144, parent.width / 4)"))
                  && !patchedPlr.contains(QLatin1String("pBSISidebarX")),
              "the moved row keeps the same margins on both sides, so it stays in the middle");

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
                  && patchedSidebar.contains(QLatin1String("anchors.centerIn: parent"))
                  && !patchedSidebar.contains(QLatin1String("anchors.fill: parent")),
              "the button column becomes a row centred in the title bar, as it was before");
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
        Rectangle { id: iSettingsBtn; objectName: "settingsGear"; width: 32; height: 32 }
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
                    // The order editor untick writes the row's hidden list.
                    // The player's own fResetSidebar switches the Settings
                    // gear back on line by line, so a helper that assigns
                    // visibility without reading that list undoes the hide -
                    // the row then carried the strip's gear *and* the model
                    // entry the move adds, i.e. two Settings buttons.
                    auto *gear = window->findChild<QQuickItem *>(QStringLiteral("settingsGear"));
                    bar->setProperty("pBSIHiddenButtons", QStringLiteral("|iSettingsBtn|"));
                    QMetaObject::invokeMethod(bar, "fBSIUpdateOverflow");
                    QCoreApplication::processEvents();
                    check(gear && !gear->isVisible() && !dots->isVisible()
                              && bar->property("pVisibleSidebarElementsCount").toInt() == 6,
                          "a button unticked in the order editor stays off when the row recomputes");
                    bar->setProperty("pBSIHiddenButtons", QString());
                    QMetaObject::invokeMethod(bar, "fBSIUpdateOverflow");
                    QCoreApplication::processEvents();
                    check(gear && gear->isVisible() && !dots->isVisible(),
                          "clearing the hidden list brings that button back with the row");
                } else check(false, "the responsive QML exposes its panel, buttons and three-dot menu");
            }
        }
        const auto patchedTopbar = resourceText(read(path), r, QStringLiteral("PlrTopbar.qml"));
        check(patchedTopbar.count(QStringLiteral("visible: false")) == 1
                  && !patchedTopbar.contains(QLatin1String("iBackBtn.visible: false")),
              "the show-sidebar button is disabled without touching the toggled Back entry");
        check(patchedTopbar.count(QStringLiteral("pVisible: false")) == 3
                  && patchedTopbar.count(QStringLiteral("pVisible: true")) == 0,
              "the title bar's own Back/Home/Recent step aside so the moved row is the only copy");
        // An older responsive helper was shipped before the fixed-button
        // model relationship and unmodelled Discord were accounted for. The
        // version marker upgrades it in place without changing the ownership
        // of an independently selected stock-sidebar option.
        const auto olderResponsive = [](QString qml) {
            qml.replace(QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin [titlebar] [v3]"),
                        QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin [titlebar]"));
            qml.replace(QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin [v3]"),
                        QStringLiteral("// BSI CUSTOMIZER: responsive sidebar begin"));
            // The release before this one wrote the marker without the hidden
            // list; both shapes are older helpers that must upgrade in place.
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
        const auto titlebarUpgrade = LayoutPatcher::apply(earlierTitlebarPath, id, true);
        const QString upgradedTitlebar =
            resourceText(read(earlierTitlebarPath), earlierTitlebar, QStringLiteral("PlrSidebar.qml"));
        check(titlebarUpgrade.ok && upgradedTitlebar == patchedSidebar,
              QStringLiteral("Update installs the current helper without moving the sidebar twice%1")
                  .arg(titlebarUpgrade.ok && upgradedTitlebar == patchedSidebar
                           ? QString()
                           : QStringLiteral(" -- apply: %1 -- %2").arg(titlebarUpgrade.msg,
                                 firstDifference(patchedSidebar, upgradedTitlebar))));
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
                  && manualMoved.contains(QLatin1String("responsive sidebar begin [v3]"))
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
        const auto singleUpgrade = LayoutPatcher::apply(oldSinglePath, id, true);
        const QString upgradedSingle =
            resourceText(read(oldSinglePath), oldSingle, QStringLiteral("PlrSidebar.qml"));
        const bool singleDone =
            singleUpgrade.ok && upgradedSingle == patchedSidebar
            && !LayoutPatcher::analyze(read(oldSinglePath), id).needsUpdate;
        check(singleDone,
              QStringLiteral("Update adds smart overflow and anchored panels without changing the existing title bar%1")
                  .arg(singleDone ? QString()
                                  : QStringLiteral(" -- apply: %1 -- %2").arg(singleUpgrade.msg,
                                        firstDifference(patchedSidebar, upgradedSingle))));
        check(LayoutPatcher::apply(oldSinglePath, id, false).ok
                  && resourceText(read(oldSinglePath), oldSingle, QStringLiteral("PlrSidebar.qml")) == sidebar,
              "an updated earlier title bar still restores the stock sidebar byte-for-byte");
        // A release of this tool started the row right after the title bar's
        // own navigation instead of centring it. That shape offers Update, and
        // the update writes exactly the centred row a fresh apply produces.
        {
            QString leftPlr = patchedPlr;
            QString leftTopbar = patchedTopbar;
            QString leftSidebar = patchedSidebar;
            const QString centredMargin =
                QStringLiteral("            anchors.leftMargin: Math.min(144, parent.width / 4)");
            // The strip is the first UiRowLayout, so its own anchor is the
            // occurrence that follows it - other members may be centred too.
            const int centredAt = leftSidebar.indexOf(QLatin1String("anchors.centerIn: parent"),
                                                      leftSidebar.indexOf(QLatin1String("UiRowLayout")));
            // Round 57 hid the navigation in the same two-line block a fresh
            // apply writes, with the three ids added. Only that block's code
            // line may change: the player's own `iSettingsBtn.visible = false`
            // earlier in fResetSidebar is stock text and has to survive.
            const QString markerText = QStringLiteral("// BSI CUSTOMIZER: hidden row buttons=");
            const int markerAt = leftSidebar.indexOf(markerText);
            const int blockNl = markerAt < 0 ? -1 : leftSidebar.indexOf(QLatin1Char('\n'), markerAt);
            const int blockStart = blockNl < 0 ? -1 : blockNl + 1;
            const int blockEnd = blockStart < 0 ? -1 : leftSidebar.indexOf(QLatin1Char('\n'), blockStart);
            const QString settingsLine = QStringLiteral("iSettingsBtn.visible = false");
            const int settingsAt = blockEnd < 0 ? -1 : leftSidebar.indexOf(settingsLine, blockStart);
            const bool legacyBlock = settingsAt > blockStart && settingsAt < blockEnd
                && settingsAt + settingsLine.size() <= blockEnd;
            check(leftPlr.contains(centredMargin) && leftTopbar.count(QStringLiteral("pVisible: false")) == 3
                      && centredAt > 0 && legacyBlock,
                  "the centred fixtures contain what the left-anchored shape is built from");
            leftPlr.replace(centredMargin, QStringLiteral("            anchors.leftMargin: parent.pBSISidebarX"));
            leftTopbar.replace(QStringLiteral("pVisible: false"), QStringLiteral("pVisible: true"));
            const int itemsAt = leftTopbar.indexOf(QStringLiteral("    property list<QtObject> pNonFixedTopbarItems"));
            if (itemsAt > 0)
                leftTopbar.insert(itemsAt, QStringLiteral("    property real pBSISidebarX: 144"
                                                          " // BSI CUSTOMIZER: sidebar row position\r\n"));
            if (centredAt > 0)
                leftSidebar.replace(centredAt, int(QLatin1String("anchors.centerIn: parent").size()),
                                    QStringLiteral("anchors.left: parent.left"));
            if (legacyBlock) {
                // The code line first: the marker sits above it and keeps its offset.
                leftSidebar.replace(settingsAt, int(settingsLine.size()),
                                    QStringLiteral("iBackBtn.visible = false; iHomeBtn.visible = false; "
                                                   "iRecentBtn.visible = false; iSettingsBtn.visible = false"));
                leftSidebar.replace(markerAt, int(markerText.size()),
                                    markerText + QStringLiteral("iBackBtn,iHomeBtn,iRecentBtn,"));
            }
            auto earlierLeft = resources({ { QStringLiteral("plr.qml"), leftPlr },
                                           { QStringLiteral("PlrSidebar.qml"), leftSidebar },
                                           { QStringLiteral("PlrTopbar.qml"), leftTopbar } }, true, {});
            addRegistration(earlierLeft, false);
            const QString leftPath = temp.filePath(QStringLiteral("layout-left-anchored.exe"));
            write(leftPath, earlierLeft.f.bytes);
            const auto leftState = LayoutPatcher::analyze(read(leftPath), id);
            check(leftState.supported && leftState.applied && leftState.needsUpdate,
                  "a row that starts after the navigation offers an Update to centre it");
            const auto leftUpgrade = LayoutPatcher::apply(leftPath, id, true);
            const QString leftPlrNow = resourceText(read(leftPath), earlierLeft, QStringLiteral("plr.qml"));
            const QString leftSidebarNow =
                resourceText(read(leftPath), earlierLeft, QStringLiteral("PlrSidebar.qml"));
            const QString leftTopbarNow =
                resourceText(read(leftPath), earlierLeft, QStringLiteral("PlrTopbar.qml"));
            check(leftUpgrade.ok && leftPlrNow == patchedPlr && leftSidebarNow == patchedSidebar
                      && leftTopbarNow == patchedTopbar,
                  QStringLiteral("Update centres that row and drops the anchor helper%1")
                      .arg(leftUpgrade.ok ? (leftPlrNow == patchedPlr && leftSidebarNow == patchedSidebar
                                                 && leftTopbarNow == patchedTopbar
                                             ? QString()
                                             : QStringLiteral(" -- plr %1 -- sidebar %2 -- topbar %3")
                                                   .arg(firstDifference(patchedPlr, leftPlrNow),
                                                        firstDifference(patchedSidebar, leftSidebarNow),
                                                        firstDifference(patchedTopbar, leftTopbarNow)))
                                         : QStringLiteral(" -- apply: %1").arg(leftUpgrade.msg)));
            check(LayoutPatcher::apply(leftPath, id, false).ok
                      && resourceText(read(leftPath), earlierLeft, QStringLiteral("plr.qml")) == anchoredPlr
                      && resourceText(read(leftPath), earlierLeft, QStringLiteral("PlrSidebar.qml")) == sidebar
                      && resourceText(read(leftPath), earlierLeft, QStringLiteral("PlrTopbar.qml")) == topbar,
                  "the centred update still restores every original document exactly");
        }
        const QByteArray afterFirst = read(path);
        const auto again = LayoutPatcher::apply(path, id, true);
        check(again.ok && read(path) == afterFirst
                  && resourceText(read(path), r, QStringLiteral("plr.qml")) == patchedPlr,
              "applying an already-applied tweak is a no-op");
        const auto patchedState = LayoutPatcher::analyze(read(path), id);
        check(patchedState.supported && patchedState.applied, "the patched player reports the tweak as applied");
        // ---- the row's button order and the buttons it leaves out ----
        // The default row is the sidebar's own strip, centred in the title
        // bar: Back, Home and Recent first, then the player's buttons, with
        // Settings taking a normal place among them instead of sitting after
        // the whole row.
        const auto defaultRows = LayoutPatcher::buttonRows(read(path));
        QStringList defaultKeys;
        for (const auto &button : defaultRows.buttons) defaultKeys << button.key;
        check(defaultRows.supported && defaultRows.applied && defaultKeys.size() >= 5
                  && defaultKeys.mid(0, 3) == (QStringList { QStringLiteral("back"),
                                                             QStringLiteral("home"),
                                                             QStringLiteral("recent") })
                  && defaultRows.buttons.first().canHide == false,
              "the row starts with Back/Home/Recent, which can be moved but never hidden");
        const auto hiddenRowBlock = [](const QString &qml) {
            const int at = qml.indexOf(QLatin1String("// BSI CUSTOMIZER: hidden row buttons="));
            if (at < 0) return QString();
            const int end = qml.indexOf(QLatin1Char('\n'), qml.indexOf(QLatin1Char('\n'), at) + 1);
            return end < 0 ? qml.mid(at) : qml.mid(at, end - at);
        };
        const QString settingsBlock = hiddenRowBlock(patchedSidebar);
        QStringList settingsMissing;
        for (const auto &id : { QStringLiteral("iSettingsBtn") }) {
            if (!settingsBlock.contains(QStringLiteral("%1.visible = false").arg(id)))
                settingsMissing << id;
        }
        // The row is the only copy of the navigation now, so the strip's own
        // Back/Home/Recent must not be switched off with it.
        for (const auto &id : { QStringLiteral("iBackBtn"), QStringLiteral("iHomeBtn"),
                                QStringLiteral("iRecentBtn") }) {
            if (settingsBlock.contains(QStringLiteral("%1.visible = false").arg(id)))
                settingsMissing << QStringLiteral("hides ") + id;
        }
        check(patchedSidebar.contains(QLatin1String("true /* BSI CUSTOMIZER: settings in the row */"))
                  && settingsBlock.contains(QLatin1String("hidden row buttons="))
                  && settingsMissing.isEmpty(),
              QStringLiteral("Settings joins the row as a normal button and its own strip button steps aside%1")
                  .arg(settingsMissing.isEmpty()
                           ? QString()
                           : QStringLiteral(" -- block [%1] missing [%2]")
                                 .arg(settingsBlock.simplified().left(220),
                                      settingsMissing.join(QLatin1Char(',')))));
        check(!patchedPlr.contains(QLatin1String("pBSISidebarX"))
                  && !patchedTopbar.contains(QLatin1String("pBSISidebarX")),
              "no anchor helper is left behind for the centred row");

        const auto reorder = LayoutPatcher::applyOrder(
            path, { QStringLiteral("recent"), QStringLiteral("home"), QStringLiteral("back"),
                    QStringLiteral("screen"), QStringLiteral("sound") },
            { QStringLiteral("sound") });
        if (!reorder.ok) std::printf("    %s\n", qPrintable(reorder.msg));
        check(reorder.ok, "a player with the title bar applied accepts a new button order");
        const auto custom = LayoutPatcher::buttonRows(read(path));
        QStringList customKeys;
        for (const auto &button : custom.buttons) customKeys << button.key;
        check(customKeys.mid(0, 3) == (QStringList { QStringLiteral("recent"),
                                                     QStringLiteral("home"),
                                                     QStringLiteral("back") })
                  && customKeys.indexOf(QStringLiteral("sound"))
                     > customKeys.indexOf(QStringLiteral("screen")),
              "the row follows the requested order");
        bool soundHidden = false;
        for (const auto &button : custom.buttons)
            if (button.key == QLatin1String("sound")) soundHidden = button.hidden;
        check(soundHidden
                  && resourceText(read(path), r, QStringLiteral("PlrSidebar.qml"))
                         .contains(QLatin1String("pVisible: false /* BSI CUSTOMIZER: hidden button */")),
              "an unticked button keeps its place in the source and is switched off");
        check(LayoutPatcher::applyOrder(path, {}, {}).ok,
              "an empty order restores the default row");
        const auto resetRows = LayoutPatcher::buttonRows(read(path));
        check(!resetRows.buttons.isEmpty()
                  && resetRows.buttons.first().key == QLatin1String("back"),
              "the default row comes back after a reset");
        // Put the reordered state back so the restore below proves that a
        // reordered, partly hidden row still restores byte-for-byte.
        check(LayoutPatcher::applyOrder(
                  path, { QStringLiteral("recent"), QStringLiteral("screen"),
                          QStringLiteral("sound") },
                  { QStringLiteral("sound") }).ok,
              "the order can be changed again without stacking markers");
        const QString stockOrderPath = temp.filePath(QStringLiteral("layout-order-stock.exe"));
        write(stockOrderPath, original);
        check(!LayoutPatcher::applyOrder(stockOrderPath, { QStringLiteral("home") }, {}).ok
                  && read(stockOrderPath) == original,
              "a player without the title-bar layout refuses an order instead of writing one");

        check(LayoutPatcher::apply(path, id, false).ok, "turning the option off restores the stock layout");
        const QByteArray restoredBytes = read(path);
        check(resourceText(restoredBytes, r, QStringLiteral("plr.qml")) == anchoredPlr
                  && resourceText(restoredBytes, r, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(restoredBytes, r, QStringLiteral("PlrTopbar.qml")) == topbar,
              "the restore writes every edited document back byte-for-byte");
        const auto restored = LayoutPatcher::analyze(read(path), id);
        check(restored.supported && !restored.applied, "the restored player reports stock again");

        // The patched text is always longer than the record it replaces. When
        // the file has a gap - every real player has one, and this fixture gets
        // exactly one punched into an otherwise solid image - the record is
        // built there and the tree node re-pointed, so the EXE keeps its size
        // instead of gaining a section the way an append would.
        auto gapped = resources({ { QStringLiteral("plr.qml"), anchoredPlr },
                                  { QStringLiteral("PlrSidebar.qml"), sidebar },
                                  { QStringLiteral("PlrTopbar.qml"), topbar } }, true, {});
        addRegistration(gapped, false);
        gapped.f.bytes.replace(0x10000, 0x4000, QByteArray(0x4000, '\0'));
        const qint64 gappedSize = qint64(gapped.f.bytes.size());
        const QString gappedPath = temp.filePath(QStringLiteral("layout-gap.exe"));
        write(gappedPath, gapped.f.bytes);
        const auto gapApplied = LayoutPatcher::apply(gappedPath, id, true);
        if (!gapApplied.ok) std::printf("    %s\n", qPrintable(gapApplied.msg));
        check(gapApplied.ok && QFileInfo(gappedPath).size() == gappedSize
                  && resourceText(read(gappedPath), gapped, QStringLiteral("PlrSidebar.qml"))
                         .contains(QLatin1String("anchors.centerIn: parent")),
              "the title-bar patch writes its longer records into a gap the file already has, "
              "so the EXE keeps its size instead of growing a section");
        check(LayoutPatcher::apply(gappedPath, id, false).ok
                  && QFileInfo(gappedPath).size() == gappedSize
                  && resourceText(read(gappedPath), gapped, QStringLiteral("plr.qml")) == anchoredPlr
                  && resourceText(read(gappedPath), gapped, QStringLiteral("PlrSidebar.qml")) == sidebar
                  && resourceText(read(gappedPath), gapped, QStringLiteral("PlrTopbar.qml")) == topbar,
              "restoring the layout keeps every record in the same gap and restores the stock "
              "documents byte-for-byte without touching the file size");

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
              "the strip becomes a centred row and keeps the build's own overflow code");
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
                  && fixedQml.contains(QLatin1String("var unmodelled = [{ button: iDiscordBtn, allowed: "
                                                     "(iAppwin.pIsBootComplete) && pBSIHiddenButtons.indexOf(\"|iDiscordBtn|\") < 0 }]")),
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

    std::printf("[boot-discovery] renamed controllers, leftover records and refusals\n");
    {
        // The player stores both files under names of its own choosing, and this
        // tool has to find them by what they say. The boot screen also carries
        // the licence banner the real builds put in front of every QML file.
        const QString controller = QStringLiteral(
            "/*\r\n * Copyright (C) 2020-2021 BlueStack Systems, Inc.\r\n */\r\n"
            "import QtQuick\r\n"
            "Item {\r\n"
            "    width: 320; height: 200\r\n"
            "    property bool bootBackgroundImage: true\r\n"
            "    Image { id: iBootImage; objectName: 'bootImage'; visible: true }\r\n"
            "    Text { id: iBootLoadingText; objectName: 'loadingText' }\r\n"
            "    Rectangle { id:iBootProgressContainer; objectName:'stockProgress'; visible:true }\r\n"
            "    function fSetBootPromotion() { iBootImage.visible = true }\r\n"
            "}\r\n");
        const QString host = QStringLiteral(
            "/* (C) BlueStack Systems - the ApplicationWindow of the player */\r\n"
            "import QtQuick\r\n"
            "Item {\r\n"
            "    id: iAppwin\r\n"
            "    property bool pIsBootComplete: false\r\n"
            "    property bool pShowAndroidAd: false\r\n"
            "    Component { id: iBootProgressComponent; RenamedStartup {} }\r\n"
            "    Component { id: iGlContainerComponent; Rectangle { color: '#000000' } }\r\n"
            "    Loader { id: iBootLoader; anchors.fill: parent\r\n"
            "        sourceComponent: pIsBootComplete || pShowAndroidAd ? iGlContainerComponent : iBootProgressComponent }\r\n"
            "}\r\n");
        QString unusedController = controller;
        unusedController.replace(QLatin1String("bootImage"), QLatin1String("unusedBootImage"));
        auto r = resources({ { QStringLiteral("RenamedStartup.qml"), controller },
                             { QStringLiteral("UnusedStartup.qml"), unusedController },
                             { QStringLiteral("Shell.qml"), host },
                             { QStringLiteral("Other.qml"), QStringLiteral("import QtQuick; QtObject {}") },
                             { QStringLiteral("BlueStacks5Beta.svg"), QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\"><circle cx=\"32\" cy=\"32\" r=\"25\" fill=\"#55aaff\"/></svg>") } },
                           false, {}, 256);
        addRegistration(r, false);
        // A record an earlier patch left behind: valid QML, no tree node, so the
        // player never reads it and neither may this tool.
        QByteArray image = r.f.bytes;
        const qint64 leftover = image.size();
        image += record(controller.toUtf8());
        image += QByteArray(64, '\0');
        const auto folder = temp.filePath(QStringLiteral("renamed boot")); QDir().mkpath(folder);
        const QString path = QDir(folder).filePath(QStringLiteral("HD-Player.exe")); write(path, image);

        const auto result = BootVideoPatcher::patch(path);
        check(result.ok, QStringLiteral("a boot screen is found by its content, not by a fixed resource name: %1")
                             .arg(result.msg));
        const auto patched = read(path);
        check(patched.size() == image.size(), "the patched player is exactly as large as the original");
        const auto qml = resourceText(patched, r, QStringLiteral("RenamedStartup.qml"));
        const bool stockProgressWaitsForLoader = QRegularExpression(
            QStringLiteral("id\\s*:\\s*iBootProgressContainer[^}]*visible\\s*:\\s*!pv\\.item"))
                .match(qml).hasMatch();
        check(qml.contains(QLatin1String("objectName:\"bsiBootLoader\""))
                  && qml.contains(QLatin1String("z:10000"))
                  && qml.contains(QLatin1String("BootVideo.qml")) && stockProgressWaitsForLoader
                  && qml.contains(QLatin1String("bsiNativeStartupComponent"))
                  && qml.contains(QLatin1String("bsiBootLoadWatchdog"))
                  && qml.contains(QLatin1String("sourceComponent=bsiNativeStartupComponent"))
                  && qml.contains(QLatin1String("onLoaded:")),
              "the custom loader has an inline native fallback and does not blank the original boot while loading");
        check(resourceText(patched, r, QStringLiteral("UnusedStartup.qml")) == unusedController,
              "a boot-looking QML resource not instantiated by the player root is never patched");
        check(patched.mid(leftover, image.size() - leftover) == image.mid(leftover),
              "a record the tree does not point at is never rewritten");
        const auto again = BootVideoPatcher::patch(path);
        check(again.ok, QStringLiteral("the patched player accepts a second pass: %1").arg(again.msg));

        const QString modernPath = QDir(folder).filePath(QStringLiteral("HD-Player-modern.exe"));
        write(modernPath, image);
        const QString modernAssets = QDir(QFileInfo(modernPath).absolutePath()).filePath(QStringLiteral("Assets"));
        QDir().mkpath(modernAssets);
        QImage iconBitmap(48, 48, QImage::Format_ARGB32_Premultiplied);
        iconBitmap.fill(QColor(QStringLiteral("#41B9EC")));
        QByteArray iconPng;
        QBuffer iconPngBuffer(&iconPng);
        const bool iconPngReady = iconPngBuffer.open(QIODevice::WriteOnly) && iconBitmap.save(&iconPngBuffer, "PNG");
        QByteArray iconFile = QByteArray::fromHex("0000010001000101000001002000");
        const auto appendLe32 = [&iconFile](quint32 value) {
            iconFile.append(char(value & 0xff)); iconFile.append(char((value >> 8) & 0xff));
            iconFile.append(char((value >> 16) & 0xff)); iconFile.append(char((value >> 24) & 0xff));
        };
        appendLe32(quint32(iconPng.size()));
        appendLe32(22);
        iconFile += iconPng;
        check(iconPngReady && write(QDir(modernAssets).filePath(QStringLiteral("BlueStacks.ico")), iconFile),
              "prepare the selected player's Assets/BlueStacks.ico fixture");
        const QString selectedIcon = BootVideoPatcher::logoPreviewDataUrl(modernPath);
        check(selectedIcon.startsWith(QLatin1String("data:image/png;base64,")),
              "the boot screen prefers and decodes Assets/BlueStacks.ico from the selected player");
        const auto modern = BootVideoPatcher::patch(modernPath, 3000, QStringLiteral("modern"));
        const QByteArray modernHelper = read(QDir(modernAssets).filePath(QStringLiteral("BootVideo.qml")));
        const QByteArray modernHtml = read(QDir(modernAssets).filePath(QStringLiteral("BootScreen.html")));
        const QString modernController = resourceText(read(modernPath), r,
                                                          QStringLiteral("RenamedStartup.qml"));
        check(modern.ok && modernHelper.contains("import QtWebEngine")
                  && modernHelper.contains("Qt.resolvedUrl(\"BootScreen.html\")")
                  && modernHtml.contains(selectedIcon.toUtf8())
                  && modernHelper.contains(selectedIcon.toUtf8())
                  && modernController.contains(selectedIcon.toUtf8())
                  && modernHtml.contains("class=\"boot theme-aurora\"")
                  && modernHtml.contains("class=\"ring-track\"")
                  && modernHtml.contains("drop-shadow(0 0 22px")
                  && modernHtml.contains("Emulator is starting…")
                  && modernHelper.contains("bsiAuroraHalo")
                  && modernController.contains("bsiNativeBootGlowCore")
                  && modernHelper.contains("width: Math.max(loadingRingTrack.width * 1.40,")
                  && modernHelper.contains("loadingRingTrack.width / 2 - ctx.lineWidth")
                  && modernController.contains("width: Math.max(bsiNativeRingTrack.width * 1.40,")
                  && modernController.contains("bsiNativeRingTrack.width / 2 - ctx.lineWidth")
                  && modernHelper.contains("objectName: \"bsiAuroraHalo\"")
                  && modernController.contains("objectName: \"bsiNativeBootAura\"")
                  && !modernController.contains("text: \"BS\"")
                  && modernHelper.contains("ctx.shadowBlur")
                  && !modernHtml.contains("Promo.mp4") && !modernHtml.contains("<video")
                  && !modernHtml.contains("CUSTOMIZER")
                  && !QFile::exists(QDir(modernAssets).filePath(QStringLiteral("Promo.mp4"))),
              "built-in mode writes the logo, glowing ring, and neutral status to Assets/BootScreen.html");
        check(read(modernPath + QStringLiteral(".bak")) == image,
              "built-in modern mode preserves the complete original executable backup");

        for (const QString &theme : { QStringLiteral("orbit"), QStringLiteral("pulse"), QStringLiteral("solar") }) {
            const QString themedPath = QDir(folder).filePath(QStringLiteral("HD-Player-%1.exe").arg(theme));
            write(themedPath, image);
            const auto themed = BootVideoPatcher::patch(themedPath, 3000, theme);
            const QString themedAssets = QDir(QFileInfo(themedPath).absolutePath()).filePath(QStringLiteral("Assets"));
            const QByteArray themedHtml = read(QDir(themedAssets).filePath(QStringLiteral("BootScreen.html")));
            const QByteArray themedHelper = read(QDir(themedAssets).filePath(QStringLiteral("BootVideo.qml")));
            const QByteArray visualMarker = theme == QLatin1String("solar")
                ? QByteArrayLiteral("#ffad66") : QByteArrayLiteral("theme-") + theme.toUtf8();
            const QByteArray fallbackMarker = theme == QLatin1String("orbit")
                ? QByteArrayLiteral("bsiFallbackOrbitTilt")
                : theme == QLatin1String("pulse") ? QByteArrayLiteral("bsiFallbackPulse")
                                                   : QByteArrayLiteral("#FFE3BA");
            check(themed.ok && themedHtml.contains(QStringLiteral("class=\"boot theme-%1\"").arg(theme).toUtf8())
                      && themedHtml.contains(visualMarker) && themedHelper.contains(fallbackMarker)
                      && themedHelper.contains(QStringLiteral("property string bootTheme: \"%1\"").arg(theme).toUtf8()),
                  QStringLiteral("the %1 preset is written into the selected player's standalone Assets boot page and its native fallback")
                      .arg(theme));
            check(read(themedPath + QStringLiteral(".bak")) == image,
                  QStringLiteral("the %1 preset keeps the full executable backup").arg(theme));
        }

        const QString switchPath = QDir(folder).filePath(QStringLiteral("HD-Player-switch-theme.exe"));
        write(switchPath, image);
        bool switchSequenceOk = true;
        for (const QString &theme : { QStringLiteral("aurora"), QStringLiteral("solar"),
                                      QStringLiteral("orbit"), QStringLiteral("pulse"),
                                      QStringLiteral("aurora") }) {
            const auto switched = BootVideoPatcher::patch(switchPath, 3000, theme);
            const QByteArray switchedImage = read(switchPath);
            const QString switchAssets = QDir(QFileInfo(switchPath).absolutePath()).filePath(QStringLiteral("Assets"));
            const QByteArray switchedHtml = read(QDir(switchAssets).filePath(QStringLiteral("BootScreen.html")));
            const QByteArray switchedHelper = read(QDir(switchAssets).filePath(QStringLiteral("BootVideo.qml")));
            const QString switchedController = linkedResourceText(switchedImage, QStringLiteral("RenamedStartup.qml"));
            const auto countMatches = [](const QString &text, const QString &pattern) {
                int count = 0;
                auto matches = QRegularExpression(pattern).globalMatch(text);
                while (matches.hasNext()) { matches.next(); ++count; }
                return count;
            };
            const int fallbackDefinitions = countMatches(switchedController,
                QStringLiteral("\\bid\\s*:\\s*bsiNativeStartupComponent\\b"));
            const int watchdogDefinitions = countMatches(switchedController,
                QStringLiteral("\\bid\\s*:\\s*bsiBootLoadWatchdog\\b"));
            const bool intact = !PeUtils::sections(switchedImage).isEmpty()
                && !QtResources::registeredFiles(switchedImage).isEmpty()
                && switchedController.contains(QLatin1String("bsiBootLoader"))
                && fallbackDefinitions == 1 && watchdogDefinitions == 1
                && switchedController.contains(QStringLiteral("property string bootTheme: \"%1\"").arg(theme))
                && switchedHtml.contains(QStringLiteral("class=\"boot theme-%1\"").arg(theme).toUtf8())
                && switchedHelper.contains(QStringLiteral("property string bootTheme: \"%1\"").arg(theme).toUtf8())
                && read(switchPath + QStringLiteral(".bak")) == image;
            switchSequenceOk = switchSequenceOk && switched.ok && intact;
        }
        check(switchSequenceOk,
              "switching boot UI styles repeatedly patches intact PE/resource structures and preserves the original backup");

        const QString customPath = QDir(folder).filePath(QStringLiteral("HD-Player-custom.exe"));
        const QString customSource = QDir(folder).filePath(QStringLiteral("selected-startup.mp4"));
        const QString customAssets = QDir(QFileInfo(customPath).absolutePath()).filePath(QStringLiteral("Assets"));
        const QByteArray selectedMp4(128, 'M');
        const QByteArray previousMp4(96, 'P');
        write(customPath, image);
        write(customSource, selectedMp4);
        QDir().mkpath(customAssets);
        write(QDir(customAssets).filePath(QStringLiteral("Promo.mp4")), previousMp4);
        const auto custom = BootVideoPatcher::patch(customPath, 2500, QStringLiteral("video"), customSource);
        const QByteArray customHelper = read(QDir(customAssets).filePath(QStringLiteral("BootVideo.qml")));
        const QByteArray customHtml = read(QDir(customAssets).filePath(QStringLiteral("BootScreen.html")));
        check(custom.ok && read(QDir(customAssets).filePath(QStringLiteral("Promo.mp4"))) == selectedMp4
                  && read(QDir(customAssets).filePath(QStringLiteral("Promo.mp4.bak"))) == previousMp4,
              "custom mode installs the selected MP4 and preserves the previous startup video");
        check(customHelper.contains("QtWebEngine")
                  && customHelper.contains("Qt.resolvedUrl(\"BootScreen.html\")")
                  && customHtml.contains(selectedIcon.toUtf8())
                  && customHtml.contains("class=\"ring-track\"")
                  && customHtml.contains("Emulator is starting…")
                  && customHtml.contains("src=\"Promo.mp4\"")
                  && customHtml.contains("<video")
                  && !customHtml.contains("BSI  /  STARTUP")
                  && !customHtml.contains("CUSTOMIZER"),
              "custom mode loads its HTML boot page with the logo, glowing ring, status, and selected MP4 backdrop");
        check(read(customPath + QStringLiteral(".bak")) == image,
              "custom mode preserves the complete original executable backup");
        const auto customToOrbit = BootVideoPatcher::patch(customPath, 2800, QStringLiteral("orbit"));
        const QByteArray orbitAfterCustom = read(QDir(customAssets).filePath(QStringLiteral("BootScreen.html")));
        check(customToOrbit.ok && !PeUtils::sections(read(customPath)).isEmpty()
                  && orbitAfterCustom.contains("class=\"boot theme-orbit\"")
                  && !orbitAfterCustom.contains("<video")
                  && read(QDir(customAssets).filePath(QStringLiteral("Promo.mp4"))) == selectedMp4
                  && read(customPath + QStringLiteral(".bak")) == image,
              "switching from custom video to a built-in boot UI keeps the executable and selected video intact");
        const auto orbitToVideo = BootVideoPatcher::patch(customPath, 3000, QStringLiteral("video"));
        const QByteArray videoAfterOrbit = read(QDir(customAssets).filePath(QStringLiteral("BootScreen.html")));
        check(orbitToVideo.ok && !PeUtils::sections(read(customPath)).isEmpty()
                  && videoAfterOrbit.contains("<video") && videoAfterOrbit.contains("src=\"Promo.mp4\"")
                  && read(QDir(customAssets).filePath(QStringLiteral("Promo.mp4"))) == selectedMp4
                  && read(customPath + QStringLiteral(".bak")) == image,
              "switching back to custom-video mode reuses the selected player asset without damaging the EXE");

        const QString refusedPath = QDir(folder).filePath(QStringLiteral("HD-Player-refused.exe"));
        const QString wrongFormat = QDir(folder).filePath(QStringLiteral("not-an-mp4.txt"));
        write(refusedPath, image);
        write(wrongFormat, selectedMp4);
        const auto refusedFormat = BootVideoPatcher::patch(refusedPath, 3000, QStringLiteral("video"), wrongFormat);
        check(!refusedFormat.ok && read(refusedPath) == image
                  && !QFile::exists(refusedPath + QStringLiteral(".bak")),
              "custom mode refuses a non-MP4 before changing the fixture executable");

        QString ambiguousHost = host;
        ambiguousHost.replace(QLatin1String("Component { id: iBootProgressComponent; RenamedStartup {} }"),
            QStringLiteral("Component { id: iBootProgressComponent; BootOne {} }\r\n"
                           "    Component { id: iBootProgressComponent; BootTwo {} }"));
        auto twice = resources({ { QStringLiteral("BootOne.qml"), controller },
                                 { QStringLiteral("BootTwo.qml"), controller },
                                 { QStringLiteral("Shell.qml"), ambiguousHost } }, true);
        addRegistration(twice, false);
        const QString ambiguousPath = temp.filePath(QStringLiteral("two-boots.exe"));
        write(ambiguousPath, twice.f.bytes);
        const auto refused = BootVideoPatcher::patch(ambiguousPath);
        check(!refused.ok && refused.msg.contains(QLatin1String("uniquely instantiate"))
                  && read(ambiguousPath) == twice.f.bytes,
              "two boot controllers instantiated by the registered host are refused instead of guessing");

        auto none = resources({ { QStringLiteral("Shell.qml"), host },
                                { QStringLiteral("Other.qml"), QStringLiteral("import QtQuick; QtObject {}") } }, true);
        addRegistration(none, false);
        const QString nonePath = temp.filePath(QStringLiteral("no-boot.exe"));
        write(nonePath, none.f.bytes);
        check(!BootVideoPatcher::patch(nonePath).ok && read(nonePath) == none.f.bytes,
              "a player without a boot screen is refused and left untouched");
    }

    std::printf("[boot-html] external HTML boot page, local assets, and patch recovery\n");
    {
        const QString boot = QStringLiteral(
            "import QtQuick\nItem { width:320; height:200; property bool pBootImageButtonVisible:true; property bool pBootGridVisible:true;"
            "Image { id:iBootImage; objectName:'bootImage'; visible:true } Text { id:iBootLoadingText; visible:false }"
            "function fSetBootPromotion(){iBootImage.visible=true;iBootLoadingText.visible=false} }\n");
        const QString host = QStringLiteral(
            "import QtQuick\nItem { id:iAppwin; property bool pIsBootComplete:false; property bool pShowAndroidAd:false;"
            "Component { id:iBootProgressComponent; PlrBootProgressBar {} }"
            "Component { id:iGlContainerComponent; Rectangle { color:'#000000' } }"
            "Loader { id:iBootLoader; anchors.fill:parent; sourceComponent:pIsBootComplete||pShowAndroidAd?iGlContainerComponent:iBootProgressComponent } }\n");
        auto r = resources({ { QStringLiteral("PlrBootProgressBar.qml"), boot },
                            { QStringLiteral("plr.qml"), host },
                            { QStringLiteral("BlueStacks5Beta.svg"), QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\"><circle cx=\"32\" cy=\"32\" r=\"24\" fill=\"#55aaff\"/></svg>") } }, false, {}, 256);
        addRegistration(r, false);
        const QString folder = temp.filePath(QStringLiteral("HTML boot Ω & spaces"));
        QDir().mkpath(folder);
        const QString assets = QDir(folder).filePath(QStringLiteral("Assets"));
        QDir().mkpath(assets);
        const QByteArray previousHtml("<html>user startup page</html>");
        write(QDir(assets).filePath(QStringLiteral("BootScreen.html")), previousHtml);
        const QString path = QDir(folder).filePath(QStringLiteral("HD-Player.exe"));
        write(path, r.f.bytes);
        const auto result = BootVideoPatcher::patch(path, 3000, QStringLiteral("modern"));
        if (!result.ok) std::printf("    %s\n", qPrintable(result.msg));
        check(result.ok, "HTML boot page patch succeeds on the linked active controller");
        const QString helperPath = QDir(assets).filePath(QStringLiteral("BootVideo.qml"));
        const QString htmlPath = QDir(assets).filePath(QStringLiteral("BootScreen.html"));
        const QByteArray helper = read(helperPath);
        const QByteArray html = read(htmlPath);
        const auto modified = resourceText(read(path), r, QStringLiteral("PlrBootProgressBar.qml"));
        const QString controllerPath = QDir(assets).filePath(QStringLiteral("BootController.qml"));
        check(write(controllerPath, modified.toUtf8()), "write the patched boot controller as a local runtime fixture");
        check(modified.contains("bsiBootLoader")
                  && modified.contains(QUrl::fromLocalFile(helperPath).toString(QUrl::FullyEncoded))
                  && helper.contains("import QtWebEngine")
                  && helper.contains("Qt.resolvedUrl(\"BootScreen.html\")"),
              "the player boot component loads the external QML wrapper, which opens Assets/BootScreen.html");
        check(html.contains("data:image/svg+xml;base64,")
                  && html.contains("class=\"ring-track\"")
                  && html.contains("Emulator is starting…")
                  && !html.contains("<video") && !html.contains("CUSTOMIZER"),
              "the generated standalone HTML contains the player logo, glowing ring, and neutral status without attribution");
        check(read(QDir(assets).filePath(QStringLiteral("BootScreen.html.bak"))) == previousHtml,
              "an existing user BootScreen.html is backed up before the generated page replaces it");

        const QString importRoot = QDir(folder).filePath(QStringLiteral("imports"));
        const QString webModule = QDir(importRoot).filePath(QStringLiteral("BsiFixtureWebEngine"));
        QDir().mkpath(webModule);
        write(QDir(webModule).filePath(QStringLiteral("qmldir")),
              "module BsiFixtureWebEngine\nWebEngineView 1.0 WebEngineView.qml\n");
        write(QDir(webModule).filePath(QStringLiteral("WebEngineView.qml")),
              "import QtQuick\nItem {\n"
              "    objectName: 'bsiBootHtmlView'\n"
              "    enum LoadStatus { LoadStartedStatus, LoadStoppedStatus, LoadSucceededStatus, LoadFailedStatus }\n"
              "    signal loadingChanged(var request)\n"
              "    signal renderProcessTerminated(var status, int exitCode)\n"
              "    property url url\n"
              "    property color backgroundColor\n"
              "    property QtObject settings: QtObject {\n"
              "        property bool playbackRequiresUserGesture: true\n"
              "        property bool localContentCanAccessFileUrls: false\n"
              "    }\n"
              "}\n");
        check(prepareSyntheticBootPlayer(helperPath), "use a synthetic WebEngine type for the generated HTML-loader test");
        const QUrl htmlUrl = QUrl::fromLocalFile(htmlPath);
        check(bootRuntimeInChild(helperPath, importRoot, QStringLiteral("web"), htmlUrl) == 0,
              "WebEngineView loads the installed local Assets/BootScreen.html URL");
        check(bootRuntimeInChild(controllerPath, importRoot, QStringLiteral("controller"), htmlUrl) == 0,
              "the patched controller loads its external HTML wrapper and keeps the native splash visible until HTML succeeds");
        const QByteArray runtimeHelper = read(helperPath);
        check(QFile::remove(helperPath), "remove the external QML helper to exercise the controller's load-error fallback");
        check(bootRuntimeInChild(controllerPath, importRoot, QStringLiteral("controller-error"), htmlUrl) == 0,
              "a missing external QML helper switches to the inline native splash instead of a black screen");
        check(write(helperPath, runtimeHelper), "restore the external helper after the missing-file regression");
        QString fallbackQml = QString::fromUtf8(read(helperPath));
        fallbackQml.replace(QLatin1String("BsiFixtureWebEngine"), QLatin1String("BsiMissingWebEngine"));
        const QString fallbackPath = QDir(assets).filePath(QStringLiteral("BootVideoFallback.qml"));
        check(write(fallbackPath, fallbackQml.toUtf8()), "write a boot helper with WebEngine unavailable");
        check(bootRuntimeInChild(fallbackPath, importRoot, QStringLiteral("fallback"), htmlUrl) == 0,
              "the native Quick ring and status remain visible on players without WebEngine");
        check(BootVideoPatcher::patch(path).ok, "HTML-based boot patch is safe to repatch");
        check(read(path + QStringLiteral(".bak")) == r.f.bytes,
              "HTML boot repatching preserves the first complete EXE backup");
    }

    std::printf("[boot-hold] HTML player handoff timer remains reversible\n");
    {
        const QString boot = QStringLiteral("import QtQuick\nItem{Image{id:iBootImage;visible:true}Text{id:iBootLoadingText;visible:false}function fSetBootPromotion(){iBootImage.visible=true;iBootLoadingText.visible=false}}\n");
        const QString host = QStringLiteral("import QtQuick\nItem{property bool pIsBootComplete:false;property bool pShowAndroidAd:false;Component{id:iBootProgressComponent;Boot{}}Component{id:iGlContainerComponent;Rectangle{color:'#000000'}}Loader{id:iBootLoader;sourceComponent:pIsBootComplete || pShowAndroidAd ? iGlContainerComponent : iBootProgressComponent}}\n");
        const auto r = resources({{QStringLiteral("Boot.qml"),boot},{QStringLiteral("plr.qml"),host}}, true);
        const auto folder = temp.filePath(QStringLiteral("adaptive boot ' & unicode Ω")); QDir().mkpath(folder);
        const auto path = QDir(folder).filePath(QStringLiteral("HD-Player.exe")); write(path,r.f.bytes);
        check(!BootVideoPatcher::patch(path,-1).ok && read(path)==r.f.bytes,
              "invalid boot holds refuse without changing the executable");
        const auto applied=BootVideoPatcher::patch(path,3000);
        if(!applied.ok) std::printf("    %s\n",qPrintable(applied.msg));
        check(applied.ok,"HTML-backed external player patch supports the active boot component");
        auto text=resourceText(read(path),r,QStringLiteral("plr.qml"));
        check(text.contains(QLatin1String("interval:3000")) && text.contains(QLatin1String("pIsBootComplete&&pBootVideoDone")),
              "the Android loader is held only after boot completion while ads retain their bypass");
        const auto playerPath=QDir(folder).filePath(QStringLiteral("Assets/BootVideo.qml"));
        const auto player=read(playerPath);
        check(player.contains("Qt.resolvedUrl(\"BootScreen.html\")") && player.contains("QtWebEngine")
                  && player.contains("webView.settings.localContentCanAccessFileUrls = true"),
              "boot wrapper loads the HTML asset with local-file access for its adjacent MP4");
        check(BootVideoPatcher::patch(path,4500).ok,"boot hold can be updated on a repatch");
        text=resourceText(read(path),r,QStringLiteral("plr.qml"));
        check(text.count(QLatin1String("id:bvDone"))==1 && text.contains(QLatin1String("interval:4500")),
              "repatching updates the existing timer without duplicating it");
        check(BootVideoPatcher::patch(path,0).ok,"zero hold restores immediate Android handoff");
        text=resourceText(read(path),r,QStringLiteral("plr.qml"));
        check(!text.contains(QLatin1String("pBootVideoDone")) && !text.contains(QLatin1String("bvDone"))
                  && text.contains(QLatin1String("sourceComponent:pIsBootComplete||pShowAndroidAd?")),
              "disabling hold removes both timer and state");
        check(read(path+QStringLiteral(".bak"))==r.f.bytes,"all repatches preserve the first original backup");
    }

    std::printf("[gif-rpc] clean proxy validation, JSON escaping, configuration and restore\n");
    {
        GifRpcPayload::Config payload;
        payload.state="quotes \" \\ \n unicode";payload.details={"one","two"};payload.timestamp=true;
        payload.label1="YouTube";payload.url1="https://youtube.com/@nova_xis20";
        payload.largeImage="https://example.org/presence.gif";
        const auto text=GifRpcPayload::activity(payload,1,123,456,7);
        QJsonParseError parse;
        const auto json=QJsonDocument::fromJson(QByteArray::fromStdString(text),&parse);
        const auto activity=json.object().value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject();
        check(parse.error==QJsonParseError::NoError && activity.value(QStringLiteral("state")).toString().toStdString()==payload.state
                  && activity.value(QStringLiteral("details")).toString()==QLatin1String("two"),
              "GIF RPC serializes real Discord IPC JSON, escaping strings and rotating details");
        QVariantMap config={{QStringLiteral("AppId"),QStringLiteral("1542174289523122330")},
            {QStringLiteral("State"),QStringLiteral("Nova Ω")},{QStringLiteral("Details"),QStringLiteral("one\ntwo")},
            {QStringLiteral("Enabled"),true},{QStringLiteral("Interval"),5},{QStringLiteral("Timestamp"),true}};
        QString error;
        check(GifRpc::validate(config,&error),"valid RPC-only configuration is accepted");
        auto invalid=config;invalid[QStringLiteral("AppId")]=QStringLiteral("not an id");
        check(!GifRpc::validate(invalid,&error),"GIF RPC rejects invalid application IDs");
        invalid=config;invalid[QStringLiteral("LargeImage")]=QStringLiteral("file:///C:/Windows/version.dll");
        check(!GifRpc::validate(invalid,&error),"GIF RPC refuses local/executable image URL schemes");
        invalid=config;invalid[QStringLiteral("Button1Label")]=QStringLiteral("missing URL");
        check(!GifRpc::validate(invalid,&error),"GIF RPC validates paired button labels and HTTPS URLs");
#ifdef Q_OS_WIN
        const auto bytes=GifRpc::bundledProxy();
        check(GifRpc::isOurProxy(bytes,&error),QStringLiteral("embedded RPC-only DLL has a valid x64 PE/DLL header, schema and all forwarding exports: %1").arg(error));
        check(!GifRpc::isOurProxy(bytes.left(128),&error),"truncated RPC DLLs are refused");
        const auto folder=temp.filePath(QStringLiteral("gif-rpc-install"));QDir().mkpath(folder);
        const auto exe=QDir(folder).filePath(QStringLiteral("HD-Player.exe"));write(exe,peFile());
        const auto dll=QDir(folder).filePath(QStringLiteral("version.dll"));
        const auto ini=QDir(folder).filePath(QStringLiteral("bsi-gif-rpc.ini"));
        auto installed=GifRpc::apply(exe,config);
        check(installed.value(QStringLiteral("ok")).toBool(),installed.value(QStringLiteral("msg")).toString());
        const auto inspected=GifRpc::inspect(exe);
        check(inspected.value(QStringLiteral("installed")).toBool()
                  && inspected.value(QStringLiteral("settings")).toMap().value(QStringLiteral("State")).toString()==QStringLiteral("Nova Ω"),
              "installed RPC settings round-trip Unicode without binary string-slot patching");
        // Loading only OUR verified payload in the customizer test process must
        // forward version APIs without starting RPC (host is not HD-Player).
        HMODULE proxy=LoadLibraryW(reinterpret_cast<LPCWSTR>(dll.utf16()));
        check(proxy!=nullptr,"our validated proxy loads without a Discord SDK dependency");
        if(proxy) {
            using Schema=const char *(*)();const auto schema=reinterpret_cast<Schema>(GetProcAddress(proxy,"BsiGifRpcSchema"));
            using Size=DWORD (WINAPI*)(LPCWSTR,LPDWORD);
            const auto size=reinterpret_cast<Size>(GetProcAddress(proxy,"GetFileVersionInfoSizeW"));
            wchar_t windows[MAX_PATH];GetSystemDirectoryW(windows,MAX_PATH);
            const QString kernel=QDir(QString::fromWCharArray(windows)).filePath(QStringLiteral("kernel32.dll"));DWORD handle=0;
            check(schema&&QByteArray(schema())=="BSI_GIF_RPC_PROXY_V1"&&size&&size(reinterpret_cast<LPCWSTR>(kernel.utf16()),&handle)>0,
                  "proxy schema and actual System32 version API forwarding work");
            FreeLibrary(proxy);
        }
        check(GifRpc::restore(exe).value(QStringLiteral("ok")).toBool()&&!QFileInfo::exists(dll),
              "removing a new RPC deployment does not delete saved settings or unrelated files");
        const QByteArray original=peFile();write(dll,original);
        const auto beforeIni=read(ini);
        check(!GifRpc::apply(exe,config).value(QStringLiteral("ok")).toBool()&&read(dll)==original&&read(ini)==beforeIni,
              "a third-party app-local version.dll cannot be overwritten implicitly");
        check(GifRpc::apply(exe,config,true).value(QStringLiteral("ok")).toBool()
                  && read(dll+QStringLiteral(".bsi-gif-rpc.bak"))==original,
              "explicit replacement preserves and verifies the original DLL backup");
        check(GifRpc::restore(exe).value(QStringLiteral("ok")).toBool()&&read(dll)==original,
              "RPC restore recovers the original app-local DLL byte for byte");
#endif
    }

    std::printf("[player-cache] exact AppData cache scope and symlink isolation\n");
    {
        const auto base=temp.filePath(QStringLiteral("appdata"));
        const auto root=QDir(base).filePath(QStringLiteral("HD-Player"));
        QDir().mkpath(QDir(root).filePath(QStringLiteral("cache/qmlcache")));
        QDir().mkpath(QDir(root).filePath(QStringLiteral("GPUCache")));
        QDir().mkpath(QDir(root).filePath(QStringLiteral("Engine")));
        write(QDir(root).filePath(QStringLiteral("cache/qmlcache/unit.qmlc")),"cached");
        write(QDir(root).filePath(QStringLiteral("GPUCache/shader")),"cached");
        write(QDir(root).filePath(QStringLiteral("settings.json")),"keep settings");
        write(QDir(root).filePath(QStringLiteral("Engine/Data.vhdx")),"keep virtual disk");
        QDir().mkpath(QDir(base).filePath(QStringLiteral("Unrelated/cache")));
        write(QDir(base).filePath(QStringLiteral("Unrelated/cache/keep")),"keep");
        const auto purged=PlayerCache::purge({base,base,QString()});
        check(purged.ok && purged.removed==2 && !QFileInfo::exists(QDir(root).filePath(QStringLiteral("cache")))
                  && !QFileInfo::exists(QDir(root).filePath(QStringLiteral("GPUCache"))),
              "manual purge removes only recognized HD-Player cache directories and deduplicates roots");
        check(read(QDir(root).filePath(QStringLiteral("settings.json")))=="keep settings"
                  && read(QDir(root).filePath(QStringLiteral("Engine/Data.vhdx")))=="keep virtual disk"
                  && read(QDir(base).filePath(QStringLiteral("Unrelated/cache/keep")))=="keep",
              "cache purge preserves settings, virtual disks and unrelated application caches");
        check(PlayerCache::purge({base}).ok && PlayerCache::purge({base}).removed==0,
              "purging an already clean cache is an idempotent success");
        const auto outside=temp.filePath(QStringLiteral("outside-cache")); QDir().mkpath(outside);
        write(QDir(outside).filePath(QStringLiteral("keep")),"untouched");
        const auto link=QDir(root).filePath(QStringLiteral("qmlcache"));
#ifdef Q_OS_WIN
        const bool linked = QProcess::execute(QStringLiteral("cmd.exe"),
            {QStringLiteral("/c"),QStringLiteral("mklink"),QStringLiteral("/J"),QDir::toNativeSeparators(link),QDir::toNativeSeparators(outside)}) == 0;
#else
        const bool linked = QFile::link(outside,link);
#endif
        check(linked, "the cache-isolation fixture creates a directory link/junction");
        if(linked) {
            const auto blocked=PlayerCache::purge({base});
            check(!blocked.ok && read(QDir(outside).filePath(QStringLiteral("keep")))=="untouched",
                  "a linked cache is refused without traversing or deleting its target");
#ifdef Q_OS_WIN
            QDir().rmdir(link);
#else
            QFile::remove(link);
#endif
        }
    }

#ifdef BSI_EXE_TEST_FILE
    {
        std::printf("[hdplayer-reference] file parsing and disposable copies only; no uploaded code is executed\n");
        const auto input = QString::fromUtf8(BSI_EXE_TEST_FILE);
        const auto original = read(input);
        std::printf("  Reference bytes: %lld; SHA256: %s\n", static_cast<long long>(original.size()),
                    QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex().constData());
        ciNotice(QStringLiteral("reference EXE %1 (%2 bytes) - solid, gradient, Resources, and boot paths run against it")
                     .arg(QFileInfo(input).fileName()).arg(original.size()));

        // Exercise the exact All files snapshot used by the Resources tab on
        // each uploaded HD-Player reference, without writing to that reference.
        SettingsManager resourceSettings(nullptr, false);
        resourceSettings.setFpsAutoDetect(false);
        resourceSettings.setExePath(input);
        Backend resourceBackend(&resourceSettings);
        const int resourceCount = resourceBackend.scanResources(true);
        const QVariantList resourceRows = resourceBackend.resourceInfos();
        int svgCount = 0, svgThumbCount = 0;
        bool editableSvg = false, selectedSvgPreview = false;
        for (const auto &value : resourceRows) {
            const QVariantMap row = value.toMap();
            if (row.value(QStringLiteral("kind")).toString() != QLatin1String("svg"))
                continue;
            ++svgCount;
            const int index = row.value(QStringLiteral("index")).toInt();
            const QString thumbnail = row.value(QStringLiteral("thumbnail")).toString();
            if (thumbnail.startsWith(QLatin1String("data:image/svg+xml;base64,")))
                ++svgThumbCount;
            if (!editableSvg) {
                editableSvg = !resourceBackend.resourceTextAt(index).trimmed().isEmpty();
                selectedSvgPreview = resourceBackend.resourcePreviewAt(index, 640)
                                         .startsWith(QLatin1String("data:image/svg+xml;base64,"));
            }
        }
        check(resourceCount > 0 && resourceRows.size() == resourceCount,
              "the real HD-Player All files scan publishes every embedded resource");
        if (svgCount > 0) {
            check(svgThumbCount > 0,
                  "the real HD-Player All files scan prepares at least one small SVG thumbnail");
            check(editableSvg && selectedSvgPreview,
                  "a selected real HD-Player SVG supplies editable source and a larger preview");
        }
        ciNotice(QStringLiteral("real All files scan: %1 entries, %2 SVGs, %3 SVG thumbnails")
                     .arg(resourceCount).arg(svgCount).arg(svgThumbCount));
        const QString playerLogo = BootVideoPatcher::logoPreviewDataUrl(input);
        check(playerLogo.startsWith(QLatin1String("data:image/svg+xml;base64,"))
                  || playerLogo.startsWith(QLatin1String("data:image/png;base64,")),
              QStringLiteral("the real %1 resolves its embedded emulator logo, including unregistered MSI resource pools")
                  .arg(QFileInfo(input).fileName()));

        // Exercise the actual Settings-panel rebuild on a disposable copy of
        // every uploaded player version. The source reference is only read;
        // both the native settings window and About source are restored after
        // the apply/update/restore cycle.
        const auto panelState = SettingsPanelPatcher::analyze(input);
        ciNotice(QStringLiteral("real Settings graph: %1 native tabs, %2 QML dependencies; compiled QV4 units: %3 detected, %4 active")
                     .arg(panelState.settingsPageCount).arg(panelState.settingsQmlResourceCount)
                     .arg(panelState.compiledUnitsDetected).arg(panelState.compiledUnitsActive));
        check(panelState.supported && !panelState.applied
                  && panelState.settingsPageCount >= 8 && panelState.settingsQmlResourceCount > 0,
              QStringLiteral("the real %1 exposes a unique editable Settings window and About page (%2)")
                  .arg(QFileInfo(input).fileName(), panelState.detail));
        SettingsManager interfaceSettings(nullptr, false);
        interfaceSettings.setExePath(input);
        Backend interfaceBackend(&interfaceSettings);
        const QVariantMap uiPanelState = interfaceBackend.settingsPanelRebuildState();
        check(uiPanelState.value(QStringLiteral("supported")).toBool()
                  && uiPanelState.value(QStringLiteral("settingsPageCount")).toInt() == panelState.settingsPageCount
                  && uiPanelState.value(QStringLiteral("settingsQmlResourceCount")).toInt() == panelState.settingsQmlResourceCount
                  && uiPanelState.value(QStringLiteral("tabs")).toList().size() == panelState.settingsPageCount
                  && !uiPanelState.value(QStringLiteral("logoStatus")).toString().isEmpty(),
              QStringLiteral("the Customizer interface reports the real %1 Settings model as patchable (%2)")
                  .arg(QFileInfo(input).fileName(), uiPanelState.value(QStringLiteral("detail")).toString()));
        const auto stockAbout = AboutPatcher::analyze(original);
        if (panelState.supported && !panelState.applied && stockAbout.supported
            && stockAbout.sourceEditable) {
            const QString aboutFirstPath = temp.filePath(QStringLiteral("about-first-")
                                                          + QFileInfo(input).completeBaseName()
                                                          + QStringLiteral("/HD-Player.exe"));
            QDir().mkpath(QFileInfo(aboutFirstPath).absolutePath());
            write(aboutFirstPath, original);
            AboutPatcher::Config aboutFirstConfig = stockAbout.config;
            aboutFirstConfig.text = QStringLiteral("About first regression");
            aboutFirstConfig.showSections = true;
            const auto aboutFirst = AboutPatcher::apply(aboutFirstPath, true, aboutFirstConfig);
            if (!aboutFirst.ok) {
                check(read(aboutFirstPath) == original
                          && (aboutFirst.msg.contains(QStringLiteral("free space"), Qt::CaseInsensitive)
                              || aboutFirst.msg.contains(QStringLiteral("room its record"), Qt::CaseInsensitive)),
                      QStringLiteral("the real %1 About editor safely refuses a source rewrite that cannot fit, without changing the EXE (%2)")
                          .arg(QFileInfo(input).fileName(), aboutFirst.msg));
                ciNotice(QStringLiteral("real About-first overlap guard not exercised for %1 because the source record has no spare room; the synthetic overlap regression covers that ordering")
                             .arg(QFileInfo(input).fileName()));
            } else {
                const QByteArray aboutFirstBytes = read(aboutFirstPath);
                const auto settingsAfterAbout = SettingsPanelPatcher::analyze(aboutFirstPath);
                const auto settingsApplyAfterAbout = SettingsPanelPatcher::apply(aboutFirstPath,
                    QStringLiteral("Should be refused"), {}, {}, true);
                check(!settingsAfterAbout.supported && !settingsApplyAfterAbout.value(QStringLiteral("ok")).toBool()
                          && read(aboutFirstPath) == aboutFirstBytes
                          && settingsApplyAfterAbout.value(QStringLiteral("msg")).toString()
                              .contains(QStringLiteral("Restore that page to stock")),
                      QStringLiteral("the real %1 Settings rebuild refuses to overwrite About-editor changes")
                          .arg(QFileInfo(input).fileName()));
            }
        } else {
            ciNotice(QStringLiteral("real About/Settings order regression skipped for %1: About page is not editable by the supported source workflow")
                         .arg(QFileInfo(input).fileName()));
        }
        if (panelState.supported && !panelState.applied) {
            const QString panelDir = temp.filePath(QStringLiteral("settings-panel-")
                                                    + QFileInfo(input).completeBaseName());
            QDir().mkpath(panelDir);
            const QString panelCopy = QDir(panelDir).filePath(QStringLiteral("HD-Player.exe"));
            write(panelCopy, original);
            QVariantList panelLinks {
                QVariantMap{{QStringLiteral("kind"), QStringLiteral("github")},
                            {QStringLiteral("title"), QStringLiteral("Regression source")},
                            {QStringLiteral("url"), QStringLiteral("https://example.com/source")}},
                QVariantMap{{QStringLiteral("kind"), QStringLiteral("discord")},
                            {QStringLiteral("title"), QStringLiteral("Regression community")},
                            {QStringLiteral("url"), QStringLiteral("https://example.com/community")}},
                QVariantMap{{QStringLiteral("kind"), QStringLiteral("youtube")},
                            {QStringLiteral("title"), QStringLiteral("Regression video")},
                            {QStringLiteral("url"), QStringLiteral("https://example.com/video")}},
                QVariantMap{{QStringLiteral("kind"), QStringLiteral("github")},
                            {QStringLiteral("title"), QStringLiteral("Regression extra")},
                            {QStringLiteral("url"), QStringLiteral("https://example.com/extra")}}
            };
            const QByteArray customPanelSvg = QByteArrayLiteral(
                "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><circle cx=\"12\" cy=\"12\" r=\"9\" fill=\"#55aaff\"/></svg>");
            const QString customPanelSvgPath = QDir(panelDir).filePath(QStringLiteral("custom-panel-icon.svg"));
            write(customPanelSvgPath, customPanelSvg);
            const QVariantMap safePanelSvg = SettingsPanelPatcher::readCustomAsset(customPanelSvgPath, QStringLiteral("tab-icon"));
            const QVariantMap safePanelLogo = SettingsPanelPatcher::readCustomAsset(customPanelSvgPath, QStringLiteral("logo"));
            check(safePanelSvg.value(QStringLiteral("ok")).toBool()
                      && safePanelLogo.value(QStringLiteral("ok")).toBool(),
                  QStringLiteral("the Settings rebuild accepts safe self-contained SVGs for icons and logos"));
            const QString unsafePanelSvgPath = QDir(panelDir).filePath(QStringLiteral("unsafe-panel-icon.svg"));
            write(unsafePanelSvgPath, QByteArrayLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\"><script>alert(1)</script></svg>"));
            check(!SettingsPanelPatcher::readCustomAsset(unsafePanelSvgPath, QStringLiteral("tab-icon")).value(QStringLiteral("ok")).toBool(),
                  QStringLiteral("Settings rebuild rejects active-content or unsafe SVG uploads"));
            SettingsManager panelManager(nullptr, false);
            panelManager.setExePath(panelCopy);
            panelManager.setAboutDeveloperName(QStringLiteral("BSI Panel Regression"));
            panelManager.setAboutLinks(panelLinks);
            Backend panelBackend(&panelManager);
            const QVariantMap settingsBeforeApply = panelBackend.settingsPanelRebuildState();
            const QVariantMap appliedPanel = panelBackend.applySettingsPanelRebuild(true);
            const QVariantMap settingsAfterApply = panelBackend.settingsPanelRebuildState();
            check(settingsBeforeApply.value(QStringLiteral("supported")).toBool()
                      && appliedPanel.value(QStringLiteral("ok")).toBool()
                      && settingsAfterApply.value(QStringLiteral("applied")).toBool(),
                  QStringLiteral("the real %1 Settings panel is supported and applies through the Customizer backend (%2)")
                      .arg(QFileInfo(input).fileName(), appliedPanel.value(QStringLiteral("msg")).toString()));
            if (appliedPanel.value(QStringLiteral("ok")).toBool()) {
                const QByteArray rebuiltBeforeAbout = read(panelCopy);
                const auto aboutOnRebuiltSettings = AboutPatcher::analyze(rebuiltBeforeAbout);
                const auto aboutOnRebuiltApply = AboutPatcher::apply(panelCopy, true,
                    AboutPatcher::Config{QStringLiteral("Conflict regression"), {}, true});
                check(!aboutOnRebuiltSettings.supported && !aboutOnRebuiltApply.ok
                          && read(panelCopy) == rebuiltBeforeAbout
                          && aboutOnRebuiltApply.msg.contains(QStringLiteral("restore the Settings panel")),
                      QStringLiteral("the real %1 Settings-owned About page refuses an independent About edit without changing the EXE")
                          .arg(QFileInfo(input).fileName()));
                const auto panelImage = read(panelCopy);
                QString rebuiltWindow;
                for (const auto &file : QtResources::registeredFiles(panelImage)) {
                    if (file.name.compare(QLatin1String("UiSettingsWindow.qml"), Qt::CaseInsensitive) == 0)
                        rebuiltWindow = QString::fromUtf8(QtResources::readFile(panelImage, file));
                }
                check(rebuiltWindow.contains(QStringLiteral("BSI Settings v7"))
                          && rebuiltWindow.contains(QStringLiteral("property var bHidden:"))
                          && rebuiltWindow.contains(QStringLiteral("property var bIcons:"))
                          && rebuiltWindow.contains(QStringLiteral("Rectangle{anchors.fill:parent;color:UiTheme.colors.primary80;radius:12"))
                          && rebuiltWindow.contains(QStringLiteral("Column{anchors.fill:parent;anchors.margins:12;spacing:8"))
                          && rebuiltWindow.contains(QStringLiteral("pShowRedDot:parent.d.pShowRedDot"))
                          && rebuiltWindow.contains(QStringLiteral("Image{source:parent.icon;width:18;height:18"))
                          && rebuiltWindow.contains(QStringLiteral("StackLayout{id:iSelectedSettingControl;anchors.fill:parent;anchors.margins:6"))
                          && rebuiltWindow.contains(QStringLiteral("model.modelData.pComponentObj = item"))
                          && rebuiltWindow.contains(QStringLiteral("fChangeSelectedSetting(pSettingIdx)"))
                          && rebuiltWindow.contains(QStringLiteral("onVisibleChanged: if (visible && transientParent)"))
                          && rebuiltWindow.contains(QStringLiteral("width = transientParent.width * 0.70"))
                          && rebuiltWindow.contains(QStringLiteral("height = transientParent.height * 0.70"))
                          && rebuiltWindow.contains(QStringLiteral("(transientParent.width - width) / 2"))
                          && rebuiltWindow.contains(QStringLiteral("(transientParent.height - height) / 2"))
                          && rebuiltWindow.contains(QStringLiteral("iSettingsWindow.startSystemMove()"))
                          && rebuiltWindow.contains(QStringLiteral("asset:\"CloseIcon\"")),
                      QStringLiteral("the rebuilt %1 Settings panel restores the PR #88 shell and adds customizable tabs while preserving native pages, selection, dragging, and close behavior")
                          .arg(QFileInfo(input).fileName()));
                QString expectedLogoSource;
                for (const auto &entry : EmbeddedResources::list(original, true)) {
                    if (entry.path.compare(QLatin1String("qrcAssets/BlueStacks5Beta.svg"), Qt::CaseInsensitive) == 0)
                        expectedLogoSource = QStringLiteral("qrc:/") + entry.path;
                    else if (entry.path.compare(QLatin1String("qrcAssets/BlueStacks5Beta.png"), Qt::CaseInsensitive) == 0
                             && expectedLogoSource.isEmpty())
                        expectedLogoSource = QStringLiteral("qrc:/") + entry.path;
                }
                QString rebuiltAbout;
                for (const auto &entry : EmbeddedResources::list(panelImage, true)) {
                    if (entry.leaf.compare(QLatin1String("PlrAboutSetting.qml"), Qt::CaseInsensitive) == 0)
                        rebuiltAbout = EmbeddedResources::decodeText(
                            EmbeddedResources::readEntry(panelCopy, entry.entryOff, entry.flags, 8 * 1024 * 1024));
                }
                const bool logoSourceVerified = expectedLogoSource.isEmpty()
                    ? rebuiltAbout.contains(QStringLiteral("source: \"data:image/svg+xml;base64,"))
                    : rebuiltAbout.contains(QStringLiteral("source: \"%1\"").arg(expectedLogoSource));
                check(logoSourceVerified
                          && rebuiltAbout.contains(QStringLiteral("objectName: \"bsiAboutEmulatorLogo\""))
                          && rebuiltAbout.contains(QStringLiteral("objectName: \"bsiAboutDeveloperName\""))
                          && rebuiltAbout.contains(QStringLiteral("text: pDeveloperName"))
                          && rebuiltAbout.contains(QStringLiteral("BSI Panel Regression"))
                          && rebuiltAbout.contains(QStringLiteral("Repeater"))
                          && rebuiltAbout.contains(QStringLiteral("Regression source"))
                          && rebuiltAbout.contains(QStringLiteral("Regression community"))
                          && rebuiltAbout.contains(QStringLiteral("Regression video")),
                      QStringLiteral("the rebuilt %1 About page centers the real emulator logo above its customizable developer name and links")
                          .arg(QFileInfo(input).fileName()));
                const auto activePanel = SettingsPanelPatcher::analyze(panelCopy);
                check(activePanel.supported && activePanel.applied && activePanel.restorable
                          && activePanel.compiledUnitsDetected == panelState.compiledUnitsDetected
                          && activePanel.compiledUnitsActive == 0,
                      QStringLiteral("the rebuilt %1 panel reads back, retains a selective recovery snapshot, and has no active stale compiled QML")
                          .arg(QFileInfo(input).fileName()));
                const QVariantList liveTabs = panelState.tabs;
                const QString hiddenTabId = liveTabs.size() > 1
                    ? liveTabs.at(1).toMap().value(QStringLiteral("id")).toString() : QString();
                const QVariantMap panelOptions {
                    {QStringLiteral("tagline"), QStringLiteral("Regression subtitle")},
                    {QStringLiteral("hiddenTabs"), hiddenTabId.isEmpty() ? QVariantList{} : QVariantList{hiddenTabId}},
                    {QStringLiteral("tabIcons"), liveTabs.isEmpty() ? QVariantMap{} : QVariantMap{{
                        liveTabs.first().toMap().value(QStringLiteral("id")).toString(), safePanelSvg.value(QStringLiteral("asset"))}}},
                    {QStringLiteral("logo"), safePanelLogo.value(QStringLiteral("asset"))}
                };
                const auto updatedPanel = SettingsPanelPatcher::apply(panelCopy,
                    QStringLiteral("BSI Panel Regression Updated"), panelLinks, panelOptions, true);
                check(updatedPanel.value(QStringLiteral("ok")).toBool(),
                      QStringLiteral("the real %1 panel safely accepts custom About profile and tab options (%2)")
                          .arg(QFileInfo(input).fileName(), updatedPanel.value(QStringLiteral("msg")).toString()));
                if (updatedPanel.value(QStringLiteral("ok")).toBool()) {
                    QString updatedWindow, updatedAbout;
                    const QByteArray updatedImage = read(panelCopy);
                    for (const auto &entry : EmbeddedResources::list(updatedImage, true)) {
                        if (entry.leaf.compare(QLatin1String("UiSettingsWindow.qml"), Qt::CaseInsensitive) == 0)
                            updatedWindow = EmbeddedResources::decodeText(EmbeddedResources::readEntry(panelCopy, entry.entryOff, entry.flags, 8 * 1024 * 1024));
                        if (entry.leaf.compare(QLatin1String("PlrAboutSetting.qml"), Qt::CaseInsensitive) == 0)
                            updatedAbout = EmbeddedResources::decodeText(EmbeddedResources::readEntry(panelCopy, entry.entryOff, entry.flags, 8 * 1024 * 1024));
                    }
                    check(updatedWindow.contains(QStringLiteral("property var h:"))
                              && (hiddenTabId.isEmpty() || updatedWindow.contains(hiddenTabId))
                              && updatedWindow.contains(QStringLiteral("data:image/svg+xml;base64,"))
                              && updatedAbout.contains(QStringLiteral("data:image/svg+xml;base64,"))
                              && updatedAbout.contains(QStringLiteral("BSI Panel Regression Updated"))
                              && updatedAbout.contains(QStringLiteral("Regression subtitle")),
                          QStringLiteral("the real %1 Settings rebuild writes hidden tabs, custom SVG icons, and a custom About logo into regenerated QML")
                              .arg(QFileInfo(input).fileName()));
                }
                const auto restoredPanel = SettingsPanelPatcher::apply(panelCopy, QString(), {}, {}, false);
                check(restoredPanel.value(QStringLiteral("ok")).toBool(),
                      QStringLiteral("the real %1 panel restores only its two original QML sources (%2)")
                          .arg(QFileInfo(input).fileName(), restoredPanel.value(QStringLiteral("msg")).toString()));
                const auto finalPanel = SettingsPanelPatcher::analyze(panelCopy);
                check(finalPanel.supported && !finalPanel.applied,
                      QStringLiteral("the original %1 Settings window is recognized again after restore")
                          .arg(QFileInfo(input).fileName()));
            }
            check(read(input) == original,
                  QStringLiteral("the uploaded %1 reference remains byte-for-byte untouched")
                      .arg(QFileInfo(input).fileName()));
        }

        const auto gradientCatalog = GradientPatcher::analyze(original);
        const auto selectedGradient = std::find_if(gradientCatalog.targets.cbegin(), gradientCatalog.targets.cend(),
            [](const GradientPatcher::Target &target) { return target.hexGradient; });
        if (gradientCatalog.ok && selectedGradient != gradientCatalog.targets.cend()) {
            QByteArray gradientCopy = original;
            GradientPatcher::Spec realGradient;
            realGradient.first = QStringLiteral("#3B6FFF");
            realGradient.second = QStringLiteral("#22D3A6");
            QString gradientError;
            const bool gradientApplied = GradientPatcher::apply(
                gradientCopy, selectedGradient->key, realGradient, &gradientError);
            check(gradientApplied && gradientCopy != original,
                  QStringLiteral("the real %1 gradient target %2 writes its loaded QML source (%3)")
                      .arg(QFileInfo(input).fileName(), selectedGradient->resource, gradientError));
            if (gradientApplied && QtResources::matchingUnits(original, {selectedGradient->resource}) > 0)
                check(QtResources::activeUnits(gradientCopy, {selectedGradient->resource}) == 0,
                      QStringLiteral("the real %1 gradient target %2 has no active matching compiled QML after its source change")
                          .arg(QFileInfo(input).fileName(), selectedGradient->resource));
        } else {
            ciNotice(QStringLiteral("real gradient target workflow skipped for %1: %2")
                         .arg(QFileInfo(input).fileName(), gradientCatalog.message));
        }

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
            bool bootThemeSwitchesIntact = true;
            for (const QString &theme : { QStringLiteral("solar"), QStringLiteral("orbit"), QStringLiteral("pulse") }) {
                const auto switchedBoot = BootVideoPatcher::patch(path, 3000, theme);
                if (!switchedBoot.ok)
                    std::printf("    Boot style switch %s: %s\n", qPrintable(theme), qPrintable(switchedBoot.msg));
                const QByteArray switchedImage = read(path);
                const QString switchedAssets = QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("Assets"));
                const QByteArray switchedHtml = read(QDir(switchedAssets).filePath(QStringLiteral("BootScreen.html")));
                const auto switchedTheme = ThemeEngine::findThemeZlib(switchedImage);
                bootThemeSwitchesIntact = bootThemeSwitchesIntact && switchedBoot.ok
                    && !PeUtils::sections(switchedImage).isEmpty() && switchedTheme.hasQrcNode
                    && !switchedTheme.ambiguous
                    && switchedHtml.contains(QStringLiteral("class=\"boot theme-%1\"").arg(theme).toUtf8())
                    && read(path + QStringLiteral(".bak")) == original;
            }
            bootThemeSwitchesIntact = bootThemeSwitchesIntact
                && SettingsPanelPatcher::analyze(path).supported;
            check(bootThemeSwitchesIntact,
                  QStringLiteral("switching boot animation styles on the real %1 preserves its PE, Settings model, theme resource and original backup")
                      .arg(QFileInfo(input).fileName()));
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
                                      QStringLiteral("Settings window background"),
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
                if (key.isEmpty()) {
                    const bool optionalThemeBinding = label == QStringLiteral("Theme gradients.gradientButton");
                    if (optionalThemeBinding) {
                        ciNotice(QStringLiteral("optional theme-level gradient binding is absent from this HD-Player build"));
                        continue;
                    }
                    check(false, QStringLiteral("supplied EXE exposes the %1 gradient target").arg(label));
                    continue;
                }
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

        const QString modernPath = QDir(folder).filePath(QStringLiteral("HD-Player-modern.exe"));
        write(modernPath, original);
        const auto modernBoot = BootVideoPatcher::patch(modernPath, 3000, QStringLiteral("modern"));
        const QString modernAssets = QDir(QFileInfo(modernPath).absolutePath()).filePath(QStringLiteral("Assets"));
        const QByteArray modernHelper = read(QDir(modernAssets).filePath(QStringLiteral("BootVideo.qml")));
        const QByteArray modernHtml = read(QDir(modernAssets).filePath(QStringLiteral("BootScreen.html")));
        check(modernBoot.ok && modernHelper.contains("QtWebEngine")
                  && modernHelper.contains("BootScreen.html")
                  && modernHtml.contains("data:image/")
                  && modernHtml.contains("ring-track")
                  && modernHtml.contains("Emulator is starting…")
                  && !modernHtml.contains("Promo.mp4") && !modernHtml.contains("<video")
                  && !QFile::exists(QDir(modernAssets).filePath(QStringLiteral("Promo.mp4"))),
              "built-in modern boot writes the registered logo and requested startup UI into a standalone HTML asset for this real player");
        const int activeBootUnits = QtResources::activeUnits(read(modernPath), {}, {
            QStringLiteral("iBootImage"), QStringLiteral("iBootProgressContainer"),
            QStringLiteral("iBootProgressComponent"), QStringLiteral("iBootLoader") });
        check(modernBoot.ok && activeBootUnits == 0,
              "built-in modern boot leaves no matching active compiled controller or host QML in the real player image");
        check(read(modernPath + QStringLiteral(".bak")) == original,
              "modern boot preserves the complete original of this HD-Player version");

        const QString customPath = QDir(folder).filePath(QStringLiteral("HD-Player-custom.exe"));
        const QString customSource = QDir(folder).filePath(QStringLiteral("synthetic-custom-boot.mp4"));
        const QString customAssets = QDir(QFileInfo(customPath).absolutePath()).filePath(QStringLiteral("Assets"));
        const QByteArray selectedVideo(256, 'V');
        const QByteArray previousVideo(192, 'P');
        write(customPath, original);
        write(customSource, selectedVideo);
        QDir().mkpath(customAssets);
        write(QDir(customAssets).filePath(QStringLiteral("Promo.mp4")), previousVideo);
        const auto customBoot = BootVideoPatcher::patch(customPath, 2500, QStringLiteral("video"), customSource);
        const QByteArray customHelper = read(QDir(customAssets).filePath(QStringLiteral("BootVideo.qml")));
        const QByteArray customHtml = read(QDir(customAssets).filePath(QStringLiteral("BootScreen.html")));
        check(customBoot.ok && read(QDir(customAssets).filePath(QStringLiteral("Promo.mp4"))) == selectedVideo
                  && read(QDir(customAssets).filePath(QStringLiteral("Promo.mp4.bak"))) == previousVideo,
              "custom boot installs a bounded synthetic MP4 and preserves the prior video on this real HD-Player copy");
        check(customHelper.contains("QtWebEngine") && customHelper.contains("BootScreen.html")
                  && customHtml.contains("data:image/") && customHtml.contains("ring-track")
                  && customHtml.contains("Emulator is starting…") && customHtml.contains("Promo.mp4")
                  && !customHtml.contains("BSI  /  STARTUP") && !customHtml.contains("CUSTOMIZER"),
              "custom boot uses the real emulator logo and requested loading UI in its standalone HTML page with the selected video");
        check(read(customPath + QStringLiteral(".bak")) == original,
              "custom boot preserves the complete original of this HD-Player version");
        check(read(input) == original, "the supplied HD-Player reference remains byte-for-byte untouched");
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
        // A resource-pool BootVideo.qml may be unrelated. Keep it untouched;
        // the active boot controller loads the adjacent HTML-backed wrapper.
        const QString boot = QStringLiteral(
            "/*\r\n * Copyright (C) 2020-2021 BlueStack Systems, Inc.\r\n */\r\n"
            "import QtQuick\r\n"
            "Item{width:320;height:200;property bool bootBackgroundImage:true;"
            "property bool pBootImageButtonVisible:true;property bool pBootGridVisible:true;"
            "Image{id:iBootImage;objectName:'bootImage';visible:true}"
            "Text{id:iBootLoadingText;objectName:'loadingText';visible:false}"
            "function fSetBootPromotion(){iBootImage.visible=true;pBootGridVisible=true;iBootLoadingText.visible=false}}\r\n");
        const QString video = QStringLiteral("import QtQuick\nMediaPlayer { source:'x';objectName:'mediaPlayer' }\n");
        const QString host = QStringLiteral("import QtQuick\nItem{property bool pIsBootComplete:false;property bool pShowAndroidAd:false;Component{id:iBootProgressComponent;Boot{}}Component{id:iGlContainerComponent;Rectangle{color:'#000000'}}Loader{id:iBootLoader;sourceComponent:pIsBootComplete || pShowAndroidAd ? iGlContainerComponent : iBootProgressComponent}}\n");
        const auto r = resources({ { QStringLiteral("Boot.qml"), boot }, { QStringLiteral("BootVideo.qml"), video },
                                  { QStringLiteral("plr.qml"), host },
                                  { QStringLiteral("Other.qml"), QStringLiteral("import QtQuick; QtObject {}") } },
                                false, {}, 256);
        const QString folder = temp.filePath(QStringLiteral("boot test with spaces")); QDir().mkpath(folder);
        const QString path = QDir(folder).filePath(QStringLiteral("HD-Player.exe")); write(path, r.f.bytes);
        const auto patch = BootVideoPatcher::patch(path);
        check(patch.ok, QStringLiteral("boot patch on a one-line pool: %1").arg(patch.msg));
        const auto patched = read(path);
        check(patched.size() == r.f.bytes.size(),
              QStringLiteral("the boot screen is rewritten inside the space its record already has "
                             "(%1 bytes written, %2 before)").arg(patched.size()).arg(r.f.bytes.size()));
        const auto qml = resourceText(patched, r, QStringLiteral("Boot.qml"));
        const auto expected = QUrl::fromLocalFile(QDir(folder).filePath(QStringLiteral("Assets/BootVideo.qml")));
        check(qml.contains(QStringLiteral("source:\"%1\"").arg(expected.toString(QUrl::FullyEncoded))),
              "the boot screen loads a QML player, never the MP4 itself");
        check(resourceText(read(path), r, QStringLiteral("BootVideo.qml")) == video,
              "an unrelated stock BootVideo.qml in the pool is left alone");
        check(!QFile::exists(QDir(folder).filePath(QStringLiteral("Assets/Promo.mp4")))
                  && read(QDir(folder).filePath(QStringLiteral("Assets/BootScreen.html"))).contains("Promo.mp4")
                  && read(QDir(folder).filePath(QStringLiteral("Assets/BootVideo.qml")))
                         .contains("Qt.resolvedUrl(\"BootScreen.html\")"),
              "the HTML page and its QML loader are written next to the EXE and resolve adjacent assets");
        check(patch.msg.contains(QLatin1String("Promo.mp4")),
              "the report names the file the user has to supply");
        const auto repatch = BootVideoPatcher::patch(path);
        check(repatch.ok, QStringLiteral("boot repatch on a pool without a player window: %1").arg(repatch.msg));
        check(read(path + QStringLiteral(".bak")) == r.f.bytes,
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
            "import QtQuick\r\n"
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
            "import QtQuick\r\n"
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
            "import QtQuick\r\n"
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
            "import QtQuick\r\n"
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
            "import QtQuick\r\n"
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
            "import QtQuick\r\n"
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
        const QString about = aboutPageFixture();
        auto r = resources({ { QStringLiteral("PlrAboutSetting.qml"), about } }, true, {});
        // The edited page is longer than its record. This fixture provides a
        // verified in-file cave so the resource moves without adding a section;
        // a separate no-slack case below verifies the safe PE-growth fallback.
        // Fill the rest with 0xa5 so the cave is punched deliberately.
        r.f.bytes.replace(0x10000, 0x4000, QByteArray(0x4000, '\0'));
        addRegistration(r, false);
        // A Qt-compiled player carries a QV4 unit for this page next to its
        // source, and that unit wins even with an empty disk cache. The header
        // is magic / format version / Qt version / unit size, and the page's own
        // words live inside the unit, which is how the patcher finds it.
        QByteArray original = r.f.bytes;
        {
            QByteArray unit(248, '\0');
            unit.replace(0, 8, QByteArray("qv4cdata", 8));
            const auto putUnit32 = [&unit](int at, quint32 value) {
                for (int i = 0; i < 4; ++i) unit[at + i] = char((value >> (8 * i)) & 0xff);
            };
            QByteArray words;
            for (const QChar ch : QStringLiteral("About and other info")) {
                words.append(char(ch.unicode() & 0xff));
                words.append(char((ch.unicode() >> 8) & 0xff));
            }
            putUnit32(8, 41);          // QML unit format version (Qt 5.15)
            putUnit32(12, 0x00050f04); // Qt 5.15.4
            putUnit32(24, quint32(248 + words.size()));
            unit += words;
            original += unit;
        }
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

        // MSI executables can have a full About-QML record and no zero-filled
        // mapped cave. The About editor must still succeed by asking the PE
        // resource writer for a safe appended data section, not fail with a
        // generic “no free space” message.
        auto noSlack = resources({ { QStringLiteral("PlrAboutSetting.qml"), about } }, true, {});
        addRegistration(noSlack, false);
        const QString growthPath = temp.filePath(QStringLiteral("about-page-no-slack.exe"));
        const QByteArray growthOriginal = noSlack.f.bytes;
        write(growthPath, growthOriginal);
        AboutPatcher::Config growthConfig = stock.config;
        growthConfig.text = QStringLiteral("Customized About page with links");
        const auto grown = AboutPatcher::apply(growthPath, true, growthConfig);
        const QByteArray growthWritten = read(growthPath);
        const auto growthState = AboutPatcher::analyze(growthWritten);
        check(grown.ok && growthWritten.size() > growthOriginal.size()
                  && PeUtils::sections(growthWritten).size() == PeUtils::sections(growthOriginal).size() + 1,
              QStringLiteral("a full About resource with no mapped slack is patched using a verified PE data section (%1)")
                  .arg(grown.msg));
        check(growthState.supported && growthState.applied
                  && growthState.config.text == growthConfig.text,
              "the appended About QML resource reads back with the requested text");

        // A build that renames the copyright row (or never had the id) still has
        // to prefill: the line is located by the marker it prints, not by its id.
        QString renamed = about;
        renamed.replace(QStringLiteral("id: iTermsOfUseText"), QStringLiteral("id: iTheSmallPrint"));
        auto renamedFixture = resources({ { QStringLiteral("PlrAboutSetting.qml"), renamed } }, true, {});
        addRegistration(renamedFixture, false);
        const auto renamedState = AboutPatcher::analyze(renamedFixture.f.bytes);
        check(renamedState.supported && !renamedState.applied,
              QStringLiteral("an About page whose copyright row lost its id is still recognised (%1)")
                  .arg(renamedState.detail));
        check(renamedState.config.text == stock.config.text,
              QStringLiteral("the renamed row is read by the text it prints (%1)")
                  .arg(renamedState.config.text));

        QString settingsOwnedAbout = about;
        settingsOwnedAbout.prepend(QStringLiteral("// BSI CUSTOMIZER: redesigned About page v1\n"));
        auto settingsOwnedFixture = resources(
            {{QStringLiteral("PlrAboutSetting.qml"), settingsOwnedAbout}}, true, {});
        addRegistration(settingsOwnedFixture, false);
        const auto settingsOwnedState = AboutPatcher::analyze(settingsOwnedFixture.f.bytes);
        check(!settingsOwnedState.supported && settingsOwnedState.settingsPanelConflict
                  && settingsOwnedState.detail.contains(QStringLiteral("restore the Settings panel")),
              "the About editor gives a safe, actionable conflict when the Settings rebuild owns the page");
        const QString conflictPath = temp.filePath(QStringLiteral("settings-about-conflict.exe"));
        write(conflictPath, settingsOwnedFixture.f.bytes);
        const auto conflictApply = AboutPatcher::apply(conflictPath, true, stock.config);
        check(!conflictApply.ok && read(conflictPath) == settingsOwnedFixture.f.bytes
                  && conflictApply.msg.contains(QStringLiteral("restore the Settings panel")),
              "About edits refuse a Settings-owned page without changing the executable");

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
        // The unit is located by its magic: rewriting the page resource can move
        // it, so a fixed offset would be a trap.
        const auto unitVersion = [](const QByteArray &bytes) {
            const int at = int(bytes.indexOf(QByteArray("qv4cdata", 8)));
            if (at < 0) return -1;
            const auto byteAt = [&bytes, at](int i) {
                return quint32(static_cast<unsigned char>(bytes.at(at + i)));
            };
            return int(byteAt(8) | (byteAt(9) << 8) | (byteAt(10) << 16) | (byteAt(11) << 24));
        };
        check(unitVersion(read(aboutPath)) == 0,
              "the player's compiled copy of the page is switched off, so the edited source - with "
              "the rows and the update button really gone - is what the window runs");
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
                  && patched.count(QLatin1String("visible: true/*msi5*/")) == 3
                  && !patched.contains(QLatin1String("uiBackend.oem !== \"msi5\"")),
              "the page keeps its layout, and every MSI visibility check - both sections and the "
              "separator drawn between them - is switched on");
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
        const bool gatedOk = AboutPatcher::apply(aboutPath, true, gated).ok;
        const QString gatedPage = resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"));
        check(gatedOk && !gatedPage.contains(QLatin1String("visible: true/*msi5*/"))
                  && gatedPage.count(QLatin1String("visible: uiBackend.oem !== \"msi5\"")) == 3,
              "the sections can be left hidden on the builds that hide them, and the check the "
              "build ships is left alone");
        check(!gatedOk || AboutPatcher::analyze(read(aboutPath)).sectionsHidden,
              "a page that keeps the build's own check reads back as hidden");

        check(AboutPatcher::apply(aboutPath, true, config).ok
                  && resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"))
                         .count(QLatin1String("BSI CUSTOMIZER: about-config ")) == 1,
              "applying different content updates the same page instead of stacking edits");
        const QString currentAbout = resourceText(read(aboutPath), r,
                                                  QStringLiteral("PlrAboutSetting.qml"));
        const QString configMarker = QStringLiteral("// BSI CUSTOMIZER: about-config ");
        const int configAt = currentAbout.indexOf(configMarker);
        int configEnd = configAt < 0 ? -1 : currentAbout.indexOf(QLatin1Char('\n'), configAt);
        if (configAt >= 0 && configEnd < 0) configEnd = currentAbout.size();
        const QString compactConfig = configAt < 0 || configEnd < 0
            ? QString() : currentAbout.mid(configAt + configMarker.size(),
                                           configEnd - configAt - configMarker.size());
        check(!compactConfig.isEmpty() && compactConfig.startsWith(QLatin1Char('{'))
                  && compactConfig.size() < QString::fromLatin1(
                         compactConfig.toUtf8().toBase64()).size(),
              "About-page state is stored as compact JSON rather than Base64 to reduce MSI resource pressure");
        if (!compactConfig.isEmpty()) {
            QString oldConfigAbout = currentAbout;
            oldConfigAbout.replace(configAt + configMarker.size(), compactConfig.size(),
                                   QString::fromLatin1(compactConfig.toUtf8().toBase64()));
            auto oldConfigFixture = resources(
                {{QStringLiteral("PlrAboutSetting.qml"), oldConfigAbout}}, true, {});
            addRegistration(oldConfigFixture, false);
            const auto oldConfigState = AboutPatcher::analyze(oldConfigFixture.f.bytes);
            check(oldConfigState.supported && oldConfigState.applied
                      && oldConfigState.config.text == config.text
                      && oldConfigState.config.links.value(1).url == config.links.value(1).url,
                  "previous Base64 About config comments remain readable for update and restore");
        }
        check(resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml"))
                      .count(QLatin1String("visible: true/*msi5*/")) == 3,
              "the sections the stock page hides are switched on again after the page was left "
              "with its own check");
        check(!AboutPatcher::analyze(read(aboutPath)).sectionsHidden,
              "and the card no longer reports them as hidden, so a second apply is not an edit");
        const auto restored = AboutPatcher::apply(aboutPath, false, {});
        check(restored.ok, QStringLiteral("the stock About page restores (%1)").arg(restored.msg));
        check(resourceText(read(aboutPath), r, QStringLiteral("PlrAboutSetting.qml")) == about,
              "restoring the About page is byte-for-byte identical to stock");
        check(unitVersion(read(aboutPath)) == 41,
              "and the player's compiled copy is switched back on, so a restored page is the page "
              "the player shipped");

        // The values prefill hands the editors can be applied unchanged: the
        // page then shows the same words through the custom rows, the e-mail
        // row keeps a working mailto: target, and the undo is still byte-exact.
        const QString prefillPath = temp.filePath(QStringLiteral("about-prefill.exe"));
        write(prefillPath, original);
        const auto prefilled = AboutPatcher::apply(prefillPath, true, stock.config);
        const auto prefilledText = resourceText(read(prefillPath), r,
                                                QStringLiteral("PlrAboutSetting.qml"));
        {
            // One line per failing assertion is all a Windows run shows, so the
            // flags say which half of this check went wrong.
            // The three rewritten rows carry the tag; the About line does not,
            // because it already prints the prefilled text.
            const bool tagsOk = prefilledText.count(QLatin1String("BSI CUSTOMIZER: about text:")) == 3;
            const bool stockText = prefilledText.contains(
                QLatin1String("qsTranslate(\"QObject\", \"Copyright 2026. By using it you agree to the \")"));
            const bool website = prefilledText.contains(
                QLatin1String("Website - <a href=\\\"http://www.bluestacks.com\\\">www.bluestacks.com</a>"));
            const bool mail = prefilledText.contains(
                QLatin1String("Support email - <a href=\\\"mailto:support@bluestacks.com\\\">support@bluestacks.com</a>"));
            if (!prefilled.ok || !tagsOk || !stockText || !website || !mail) {
                QStringList rows;
                for (const QString &line : prefilledText.split(QLatin1Char('\n')))
                    if (line.contains(QLatin1String("href=")) || line.contains(QLatin1String("about text:")))
                        rows << line.trimmed();
                ciNotice(QStringLiteral("prefill ok=%1 tags=%2 stockText=%3 website=%4 mailto=%5 :: %6")
                             .arg(int(prefilled.ok)).arg(int(tagsOk)).arg(int(stockText))
                             .arg(int(website)).arg(int(mail))
                             .arg(rows.mid(0, 3).join(QStringLiteral(" | "))));
            }
        }
        check(prefilled.ok
                  // The About line already prints the prefilled text, so it is
                  // not rewritten: every rewrite spends room, and on a player
                  // whose record is full there is none to spend. The stock
                  // expression stays, and only the three rewritten rows carry a
                  // tag - each one holds the words it replaced.
                  && prefilledText.count(QLatin1String("BSI CUSTOMIZER: about text:")) == 3
                  && prefilledText.contains(QLatin1String("qsTranslate(\"QObject\", \"Copyright 2026. By using it you agree to the \")"))
                  && prefilledText.contains(QLatin1String("Website - <a href=\\\"http://www.bluestacks.com\\\">www.bluestacks.com</a>"))
                  && prefilledText.contains(QLatin1String("Support email - <a href=\\\"mailto:support@bluestacks.com\\\">support@bluestacks.com</a>")),
              QStringLiteral("the prefilled stock values apply unchanged, and the About line that "
                             "already says what it should is left as the player wrote it (%1)")
                  .arg(prefilled.msg));
        const auto prefilledState = AboutPatcher::analyze(read(prefillPath));
        check(prefilledState.config.links.value(0).url
                  == QStringLiteral("mailto:support@bluestacks.com"),
              "the e-mail row reads back as the mailto: target it now uses");
        // The editor offers both sections shown (that is the only value that is
        // right on MSI, where the stock binding hides them, and on BlueStacks,
        // where they are already visible), so applying the prefilled values
        // carries that Unhide step with it. Turning the toggle off keeps the
        // build's own binding - that is the check above.
        check(prefilledText.count(QLatin1String("visible: true/*msi5*/")) == 3,
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
        // Same as the fixture above: the edited page is longer than its record,
        // and the file never grows, so it needs somewhere already free to move.
        oddResources.f.bytes.replace(0x10000, 0x4000, QByteArray(0x4000, '\0'));
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

        // The QML-facing list is a scan snapshot, including thumbnails. A
        // metadata query/filter must never re-read or decode the backing EXE.
        {
            const QString cachedPath = temp.filePath(QStringLiteral("resource-snapshot.exe"));
            write(cachedPath, r.f.bytes);
            SettingsManager settings(nullptr, false);
            settings.setFpsAutoDetect(false);
            settings.setExePath(cachedPath);
            Backend backend(&settings);
            int notifications = 0;
            bool notificationsIdle = true;
            QObject::connect(&backend, &Backend::resourcesChanged, [&] {
                ++notifications;
                notificationsIdle = notificationsIdle && !backend.busy();
            });
            check(backend.resourceInfos().isEmpty(), "selecting a target does not scan Resources");
            check(backend.scanResources(true) == 4, "explicit Resources scan publishes the entire snapshot");
            check(notificationsIdle, "Resources publishes after releasing the busy guard so budget reads are allowed");
            const auto snapshot = backend.resourceInfos();
            bool pngThumb = false, svgThumb = false;
            for (const auto &value : snapshot) {
                const auto row = value.toMap();
                if (row.value(QStringLiteral("path")).toString() == QStringLiteral("images/logo.png"))
                    pngThumb = row.value(QStringLiteral("thumbnail")).toString().startsWith(QLatin1String("data:image/png"));
                if (row.value(QStringLiteral("path")).toString() == QStringLiteral("icons/star.svg"))
                    svgThumb = row.value(QStringLiteral("thumbnail")).toString().startsWith(QLatin1String("data:image/svg"));
            }
            check(pngThumb && svgThumb, "scan worker prepares PNG and SVG thumbnails before publishing rows");
            const int before = notifications;
            backend.rpcidNames(cachedPath); // unrelated worker completion
            check(notifications == before, "unrelated operations do not reload the Resources editor");
            QFile::remove(cachedPath);
            check(backend.resourceInfos() == snapshot, "metadata reads use the snapshot, not disk I/O");
            settings.setExePath(temp.filePath(QStringLiteral("different-player.exe")));
            check(backend.resourceInfos().isEmpty(), "a target change drops cached rows and thumbnails without a scan");
        }

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
        const auto grownText = EmbeddedResources::replaceText(exePath, qmlEntry.path, longer);
        check(grownText.ok && read(exePath).size() == originalSize,
              QStringLiteral("larger decoded QML fits verified storage without EXE growth (%1)").arg(grownText.msg));
        write(exePath, original); QFile::remove(exePath + QStringLiteral(".bak"));
        QString huge; huge.reserve(400000);
        for (int i = 0; i < 400000; ++i) { seed = seed * 1103515245u + 12345u; huge += QChar(ushort('a' + (seed >> 16) % 26)); }
        const auto refusedText = EmbeddedResources::replaceText(exePath, qmlEntry.path, qmlSource + huge);
        check(!refusedText.ok && read(exePath) == original,
              QStringLiteral("oversized payload without verified storage is refused (%1)").arg(refusedText.msg));
        check(!QFileInfo::exists(exePath + QStringLiteral(".bak")), "refused growth does not create a backup");

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
        check(refusedPicture.ok && read(exePath).size() == afterSave.size(),
              QStringLiteral("a larger picture uses verified storage without growing the EXE (%1)")
                  .arg(refusedPicture.msg));

        write(exePath, afterSave); // isolate the next case from the image relocation
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

    std::printf("[resources] a build whose pools cannot be mapped is scanned by its bytes\n");
    {
        // The SVG tab has always worked on every player because it never asks
        // the resource tree where the pictures are. This is the same idea for
        // every picture kind: a blob counts when the four bytes in front of it
        // are its own size, which is exactly the shape a raw payload has. The
        // fixture below has no qRegisterResourceData call at all, so the pool
        // walk finds nothing - what the tab lists has to come from the scan.
        const QByteArray logo = QByteArray::fromHex(
            "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d4944"
            "415478da63f8cfc0f01f00050001ff56c72f0d0000000049454e44ae426082");
        const QByteArray star = QByteArray(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
            "<path fill=\"#ffffff\" d=\"M0 0h16v16H0z\"/></svg>");
        QSet<QString> raw;
        raw.insert(QStringLiteral("icons/star.svg"));
        raw.insert(QStringLiteral("images/logo.png"));
        auto r = resourcesBytes({ { QStringLiteral("icons/star.svg"), star },
                                  { QStringLiteral("images/logo.png"), logo } },
                                false, raw);
        // No addRegistration() on purpose: this image registers nothing.
        const auto entries = EmbeddedResources::list(r.f.bytes, true);
        const auto find = [&entries](const QString &format) {
            for (const auto &entry : entries)
                if (entry.format == format) return entry;
            return EmbeddedResources::Entry();
        };
        const auto svgEntry = find(QStringLiteral("svg"));
        const auto pngEntry = find(QStringLiteral("png"));
        check(entries.size() == 2 && svgEntry.scanned && pngEntry.scanned,
              QStringLiteral("both pictures are found without a resource tree (%1)").arg(entries.size()));
        check(svgEntry.kind == EmbeddedResources::Kind::Svg && svgEntry.text
                  && svgEntry.width == 16 && svgEntry.height == 16,
              "the scanned SVG is listed as code with its viewport size");
        check(pngEntry.kind == EmbeddedResources::Kind::Image && pngEntry.width == 1
                  && pngEntry.height == 1,
              "the scanned PNG is listed as a picture with its pixel size");
        check(svgEntry.stored == star.size() && svgEntry.entryOff >= 4
                  && ByteUtils::be32(r.f.bytes, svgEntry.entryOff) == quint32(star.size()),
              "the scanned row points at the real record, not at a byte in the middle of one");

        const QString scannedPath = temp.filePath(QStringLiteral("scanned-resources.exe"));
        write(scannedPath, r.f.bytes);
        check(EmbeddedResources::readEntry(scannedPath, pngEntry.entryOff, pngEntry.flags) == logo,
              "the scanned record reads back as the picture it holds");

        const auto room = EmbeddedResources::budget(r.f.bytes, pngEntry.entryOff, pngEntry.leaf, true);
        check(room.known && room.capacity == pngEntry.stored && room.used == pngEntry.stored,
              QStringLiteral("a scanned record is measured as its own slot (%1)").arg(room.detail));

        // Same size in, same size out: the record is rewritten in place and the
        // EXE does not change size, which is the rule the whole tab is built on.
        const auto rewritten = EmbeddedResources::replaceBytes(scannedPath, pngEntry.path, logo);
        check(rewritten.ok && read(scannedPath).size() == r.f.bytes.size(),
              QStringLiteral("a scanned picture is replaced in place (%1)").arg(rewritten.msg));
        check(read(scannedPath).mid(int(pngEntry.entryOff) + 4, logo.size()) == logo,
              "the replacement is on disk at the same offset");

        // Reach the scanned-record size guard with a real PNG, rather than
        // random bytes that correctly fail picture validation first.
        QImage largerImage(32, 32, QImage::Format_ARGB32);
        for (int y = 0; y < largerImage.height(); ++y)
            for (int x = 0; x < largerImage.width(); ++x)
                largerImage.setPixelColor(x, y, QColor((x * 37) % 256, (y * 53) % 256, (x * y) % 256));
        QByteArray largerPng;
        QBuffer largerBuffer(&largerPng);
        largerBuffer.open(QIODevice::WriteOnly);
        const bool encoded = largerImage.save(&largerBuffer, "PNG");
        largerBuffer.close();
        check(encoded && largerPng.size() > logo.size(),
              "scanned-record overflow fixture is a valid larger PNG");
        const auto beforeRefusal = read(scannedPath);
        const auto tooBig = EmbeddedResources::replaceBytes(scannedPath, pngEntry.path, largerPng);
        check(!tooBig.ok && tooBig.msg.contains(QLatin1String("grow"))
                  && read(scannedPath) == beforeRefusal,
              QStringLiteral("growing a scanned record is refused without writes (%1)").arg(tooBig.msg));

        const auto svgEdit = EmbeddedResources::replaceText(
            scannedPath, svgEntry.path,
            QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
                           "<path fill=\"#112233\" d=\"M0 0h16v16H0z\"/></svg>"));
        check(svgEdit.ok && read(scannedPath).size() == r.f.bytes.size(),
              QStringLiteral("scanned SVG markup is editable in place (%1)").arg(svgEdit.msg));
        check(read(scannedPath).mid(int(svgEntry.entryOff) + 4, star.size())
                  .contains("#112233"),
              "the edited markup is what the record holds now");
    }

    std::printf("[about] the page is still read when the resource tree is not walkable\n");
    {
        // The editor used to be empty on a build whose pools this tool cannot
        // map: findDocs() came back with nothing and analyze() stopped there,
        // even though the page is sitting in the file. The page is now found by
        // the object name it always carries and the record that holds it, so
        // the card is filled and the text is readable - which also shows what
        // the tool read when the structured parse is not what it expected.
        QString page = QStringLiteral(
            "/*\r\n"
            " * Copyright (C) 2020-2021 BlueStack Systems, Inc.\r\n"
            " */\r\n"
            "import QtQuick\r\n"
            "Item {\r\n"
            "    objectName: \"aboutSettings\"\r\n"
            "    UiColumnLayout {\r\n"
            "        UiScrollView {\r\n"
            "            UiColumnLayout {\r\n"
            "                id: iContentLayout\r\n"
            "                UiTextUrl {\r\n"
            "                    id: iTermsOfUseText\r\n"
            "                    text: qsTranslate(\"QObject\", \"\u00a92011 through 2026 now.gg, Inc. All rights reserved. By using BlueStacks you agree to our \") + \"<a href=\\\"javascript:void(0)\\\">\" + qsTranslate(\"QObject\", \"Terms of use\") + \"</a>\"\r\n"
            "                    onLinkActivated: uiBackend.openHelpArticle(\"terms_of_use\")\r\n"
            "                }\r\n"
            "            }\r\n"
            "        }\r\n"
            "    }\r\n"
            "}\r\n");
        page += QStringLiteral("/* ") + QString(4200, QLatin1Char('x')) + QStringLiteral(" */\r\n");
        auto r = resourcesBytes({ { QStringLiteral("PlrAboutSetting.qml"), page.toUtf8() } });
        // No addRegistration(): nothing tells the pool walk where the tree is.
        const auto state = AboutPatcher::analyze(r.f.bytes);
        check(state.supported && state.scanned,
              QStringLiteral("the page is found by its own text (%1)").arg(state.detail));
        check(state.config.text.contains(QLatin1String("2011 through 2026"))
                  && state.config.text.contains(QLatin1String("Terms of use")),
              QStringLiteral("the editable text is the sentence the page prints (%1)")
                  .arg(state.config.text.left(80)));
        check(state.source.startsWith(QLatin1String("/*"))
                  && state.source.contains(QLatin1String("objectName: \"aboutSettings\"")),
              "the card can show the source it actually read");
        check(state.detail.contains(QLatin1String("scanning")),
              QStringLiteral("the card says the page was found by scanning (%1)").arg(state.detail));
    }

    std::printf("[about] the compiled copy of the page is what gets rewritten\n");
    {
        // BlueStacks ships PlrAboutSetting.qml as text and a compiled copy of
        // the same page, and the window shows the compiled one. Its words live
        // in this UTF-16LE table:
        //   FF FF FF FF | be32 chars | be32 0 | be32 0x18 | be32 0x18 | text | pad
        // The .qml edits used to land in a copy nothing displays, so the card
        // said "applied" and the window never changed. The table is now read and
        // rewritten in place: same bytes, same offsets, no growth.
        const auto utf16 = [](const QString &text) {
            QByteArray bytes;
            for (const QChar ch : text) {
                bytes.append(char(ch.unicode() & 0xff));
                bytes.append(char((ch.unicode() >> 8) & 0xff));
            }
            return bytes;
        };
        // Little-endian, and 24 bytes of header: marker, chars, 0, 24, 24, 0.
        const auto le32 = [](int value) {
            QByteArray bytes(4, '\0');
            bytes[0] = char(value & 0xff);
            bytes[1] = char((value >> 8) & 0xff);
            bytes[2] = char((value >> 16) & 0xff);
            bytes[3] = char((value >> 24) & 0xff);
            return bytes;
        };
        const auto record = [&](const QString &text, int padding) {
            QByteArray out = QByteArray::fromHex("ffffffff");
            out += le32(text.size());
            out += le32(0);
            out += le32(0x18);
            out += le32(0x18);
            out += le32(0);
            out += utf16(text);
            out += QByteArray(padding, '\0');
            return out;
        };
        const QString quote = QString(QChar(34));
        const auto row = [&](const QString &label) {
            return QStringLiteral(" - <a href=") + quote + QStringLiteral("javascript:void(0)")
                   + quote + QLatin1Char('>') + label + QStringLiteral("</a>");
        };
        const QString copyright = QString(QChar(0x00A9))
            + QStringLiteral(" BlueStack Systems, Inc. 2011 through 2021, all rights reserved.");

        // The name of a row and its " - <a …>" fragment are separate records on
        // a real player, and the China branch repeats the fragment, so the
        // fixture keeps them apart too.
        QByteArray table;
        table += record(QStringLiteral("More info"), 6);
        table += record(QStringLiteral("Check for updates"), 4);
        table += record(QStringLiteral("Important links"), 6);
        table += record(QStringLiteral("Support email"), 6);
        table += record(row(QStringLiteral("support@bluestacks.com")), 3);
        // Every row on a real player has three records - the name, the anchor
        // that prints the address and the address the anchor opens - and the
        // e-mail row is no exception. A fixture that left the address out made
        // the row look like it had no target at all.
        table += record(QStringLiteral("support@bluestacks.com"), 4);
        table += record(QStringLiteral("Support"), 6);
        table += record(row(QStringLiteral("support.bluestacks.com")), 3);
        table += record(QStringLiteral("http://support.bluestacks.com"), 4);
        table += record(QStringLiteral("About and other info"), 6);
        table += record(copyright, 3);
        table += record(QStringLiteral("<a href=") + quote + QStringLiteral("javascript:void(0)")
                        + quote + QLatin1Char('>'), 4);
        table += record(QStringLiteral("Terms of use"), 3);
        // A real PE with no Qt resources in it: the page is present only as
        // compiled code, which is the build this test is about.
        auto r = resourcesBytes({}, false, {});
        QByteArray compiled = r.f.bytes;
        // A player built with Qt's QML compiler carries a QV4 unit for the page,
        // and that unit wins over the .qml source even with an empty disk cache.
        // The unit header (magic, format version, Qt version, unit size) is what
        // the patcher switches off so the edited source is what runs.
        {
            QByteArray unit(248, '\0');
            unit.replace(0, 8, QByteArray("qv4cdata", 8));
            const auto putUnit32 = [&unit](int at, quint32 value) {
                for (int i = 0; i < 4; ++i) unit[at + i] = char((value >> (8 * i)) & 0xff);
            };
            putUnit32(8, 41);          // QML unit format version (Qt 5.15)
            putUnit32(12, 0x00050f04); // Qt 5.15.4
            putUnit32(24, quint32(248 + table.size()));
            compiled += unit;
        }
        const int tableAt = compiled.size();
        compiled += table;
        compiled += QByteArray(0x200, '\0');
        const QString path = temp.filePath(QStringLiteral("about-compiled.exe"));
        write(path, compiled);

        const auto stock = AboutPatcher::analyze(compiled);
        check(stock.supported && stock.compiledPage,
              QStringLiteral("the compiled page is found without a .qml (%1)").arg(stock.detail));
        const auto listed = [&stock](const QString &word) {
            for (const auto &entry : stock.table)
                if (entry.text == word) return true;
            return false;
        };
        check(listed(copyright) && listed(QStringLiteral("Support"))
                  && listed(QStringLiteral("About and other info"))
                  && listed(QStringLiteral("http://support.bluestacks.com")),
              QStringLiteral("the words the window prints are listed (%1 records)")
                  .arg(stock.table.size()));
        check(stock.config.text == copyright + QStringLiteral("Terms of use"),
              QStringLiteral("the text field shows the sentence and its link, as the page prints "
                             "them (%1)")
                  .arg(stock.config.text.left(60)));
        check(stock.config.links.size() == 2,
              QStringLiteral("both link rows are found by structure (%1)")
                  .arg(stock.config.links.size()));
        // The rows have the shape a real player has: a name, the words the
        // anchor prints and the address it opens. Without this the fixture can
        // pass while testing a page the player never ships.
        bool complete = !stock.config.links.isEmpty();
        for (const auto &link : stock.config.links)
            complete = complete && !link.name.isEmpty() && !link.label.isEmpty()
                       && !link.url.isEmpty();
        check(complete, QStringLiteral("every row has a name, printed words and an address"));
        check(!stock.config.links.isEmpty()
                  && stock.config.links.at(0).name == QLatin1String("Support email")
                  && stock.config.links.at(0).id == QLatin1String("row0"),
              QStringLiteral("the first row is the one the page prints first (%1)")
                  .arg(stock.config.links.isEmpty() ? QString() : stock.config.links.at(0).name));
        check(stock.config.links.size() > 1
                  && stock.config.links.at(1).name == QLatin1String("Support")
                  && stock.config.links.at(1).url == QLatin1String("http://support.bluestacks.com"),
              QStringLiteral("a row's name and the address it opens are read separately"));

        AboutPatcher::Config config;
        config.text = QStringLiteral("Patched by BSI Customizer: support is closed.");
        AboutPatcher::Link support;
        // The row's position is its id on the compiled path.
        support.id = QStringLiteral("row1");
        support.name = QStringLiteral("Support");
        // The e-mail row's address record holds 23 characters on the players we
        // have, so the test uses an address that fits it.
        support.url = QStringLiteral("https://example.com");
        config.links << support;
        const auto applied = AboutPatcher::apply(path, true, config);
        check(applied.ok, QStringLiteral("the compiled page is patched (%1)").arg(applied.msg));
        const QByteArray after = read(path);
        check(after.size() == compiled.size(),
              "the patch never grows or moves the file - every literal stays in its own slot");
        check(after.contains(utf16(config.text)),
              "the About sentence the window prints is the edited one");
        check(!after.contains(utf16(copyright)), "the stock sentence is gone from the compiled page");
        check(after.contains(utf16(QStringLiteral("https://example.com")))
                  && !after.contains(utf16(QStringLiteral("http://support.bluestacks.com"))),
              "the link the row opens is the edited address");
        check(after.contains(utf16(QStringLiteral(">example.com</a>"))),
              "and the row prints the edited address as well");
        check(!after.contains(utf16(QStringLiteral("Check for updates"))),
              "the update button prints nothing, so it cannot offer a download");
        // This build keeps the page only as compiled code: there is no source to
        // compile instead, so its unit is deliberately left alone - switching it
        // off would leave the player with no page at all.
        {
            const int at = int(after.indexOf(QByteArray("qv4cdata", 8)));
            check(at >= 0 && after.mid(at + 8, 4) == QByteArray("\x29\0\0\0", 4),
                  "a page that exists only as compiled code keeps its unit, so the page still loads");
        }
        // The edited words have to land inside the page's own string table - not
        // be appended somewhere the player never reads - and every record the
        // patch did not touch has to keep its exact bytes.
        const QByteArray tableNow = after.mid(tableAt, table.size());
        check(tableNow.contains(utf16(config.text))
                  && tableNow.contains(record(QStringLiteral("About and other info"), 6))
                  && tableNow.contains(record(QStringLiteral("Support"), 6)),
              "and the compiled strings are still written, for a build that has no source to compile");

        const auto reread = AboutPatcher::analyze(after);
        check(reread.compiledPage && reread.config.text == config.text,
              QStringLiteral("the edited page reads back from the file (%1)")
                  .arg(reread.config.text.left(60)));
        // The row it was written into is found again by its position, so a second
        // apply has nothing left to do - this is the "it does not even see the
        // text it already changed" failure.
        const auto again = AboutPatcher::apply(path, true, config);
        check(again.ok && again.msg.contains(QLatin1String("already applied")),
              QStringLiteral("applying the same content twice changes nothing (%1)").arg(again.msg));

        // Hiding a row must leave the row recognizable: its name and printed
        // words go, but the record that holds them stays, so a later apply can
        // still find the same row and switch it back on.
        AboutPatcher::Config hidden = reread.config;
        hidden.links[1].hidden = true;
        const auto hid = AboutPatcher::apply(path, true, hidden);
        check(hid.ok, QStringLiteral("a row can be hidden (%1)").arg(hid.msg));
        const auto afterHide = AboutPatcher::analyze(read(path));
        check(afterHide.config.links.size() == 2 && afterHide.config.links.at(1).hidden,
              QStringLiteral("the hidden row reads back as hidden and keeps its place"));
        // A hidden row has to leave no trace of the link on the page: the
        // anchor is still there (so the row can be found and switched back on)
        // but nothing is printed - no words, and no " - " either. That dash is
        // not part of the link, so blanking only the label left a lone "-" on
        // every hidden row.
        check(read(path).contains(utf16(QStringLiteral("<a href=\"hidden:Support\"></a>"))),
              "a hidden row's anchor prints nothing at all and points at its own name");
        check(!read(path).contains(utf16(QStringLiteral(" - <a href=\"hidden:"))),
              "and no dash is left in front of it");
        check(afterHide.config.links.at(1).name == QLatin1String("Support"),
              QStringLiteral("so hiding does not lose the name (%1)")
                  .arg(afterHide.config.links.at(1).name));
        check(afterHide.config.links.at(1).url == reread.config.links.at(1).url,
              QStringLiteral("and the address it opened is kept (%1), so the row can be "
                             "switched back on")
                  .arg(afterHide.config.links.at(1).url));
        const auto againHidden = AboutPatcher::apply(path, true, hidden);
        check(againHidden.ok && againHidden.msg.contains(QLatin1String("already applied")),
              QStringLiteral("hiding twice is a no-op too (%1)").arg(againHidden.msg));

        AboutPatcher::Config back = afterHide.config;
        back.links[1].hidden = false;
        back.links[1].url = QStringLiteral("http://support.bluestacks.com");
        back.links[1].label = QStringLiteral("support.bluestacks.com");
        const auto shown = AboutPatcher::apply(path, true, back);
        check(shown.ok, QStringLiteral("the row can be switched back on (%1)").arg(shown.msg));
        check(read(path).contains(utf16(QStringLiteral(">support.bluestacks.com</a>"))),
              "and it prints its words again");
        const auto afterShow = AboutPatcher::analyze(read(path));
        check(afterShow.config.links.size() == 2 && !afterShow.config.links.at(1).hidden
                  && afterShow.config.links.at(1).label == QLatin1String("support.bluestacks.com"),
              QStringLiteral("its own anchor points somewhere real again (%1)")
                  .arg(afterShow.config.links.size() > 1 ? afterShow.config.links.at(1).label
                                                         : QString()));

        // A config that names one row - what an older version wrote, and what
        // "replace every stock link with mine" means - has to leave the page
        // saying exactly that: the rows it does not name are hidden, not left
        // printing the stock addresses.
        AboutPatcher::Config one;
        one.text = QStringLiteral("Only this row is mine.");
        AboutPatcher::Link only;
        only.id = QStringLiteral("row0");
        only.name = QStringLiteral("Help");
        only.url = QStringLiteral("https://example.com");
        one.links << only;
        const auto singly = AboutPatcher::apply(path, true, one);
        check(singly.ok, QStringLiteral("a single-row config applies (%1)").arg(singly.msg));
        const auto oneAfter = AboutPatcher::analyze(read(path));
        check(oneAfter.config.links.size() == 2 && !oneAfter.config.links.at(0).hidden
                  && oneAfter.config.links.at(1).hidden,
              QStringLiteral("the rows it does not name are hidden (%1)")
                  .arg(oneAfter.config.links.size() > 1 && oneAfter.config.links.at(1).hidden
                           ? QStringLiteral("row 1 hidden")
                           : QStringLiteral("row 1 still shown")));
        const auto againOne = AboutPatcher::apply(path, true, one);
        check(againOne.ok && againOne.msg.contains(QLatin1String("already applied")),
              QStringLiteral("and applying it twice is still a no-op (%1)").arg(againOne.msg));

        // A record's room is finite and the card is told how much of it is
        // left, so a rename that the player would have to refuse cannot be
        // typed in the first place - and if it is forced, nothing is written.
        const auto limited = AboutPatcher::analyze(read(path));
        check(limited.config.links.size() == 2
                  && limited.config.links.at(1).nameMax
                         >= limited.config.links.at(1).name.size()
                  && limited.config.links.at(1).urlMax
                         >= limited.config.links.at(1).url.size(),
              QStringLiteral("each row reports the room its own record has (name %1, "
                             "address %2)")
                  .arg(limited.config.links.at(1).nameMax)
                  .arg(limited.config.links.at(1).urlMax));
        // Row 0 is the one that is shown: a hidden row's name travels in its
        // anchor's href, which has room to spare, so the tight record is the
        // name of a row the page actually prints.
        AboutPatcher::Config wide = limited.config;
        wide.links[0].name = QString(limited.config.links.at(0).nameMax + 1, QLatin1Char('n'));
        const QByteArray beforeWide = read(path);
        const auto refuseName = AboutPatcher::apply(path, true, wide);
        check(!refuseName.ok && refuseName.msg.contains(QLatin1String("holds")),
              QStringLiteral("a name longer than its record is refused (%1)").arg(refuseName.msg));
        check(read(path) == beforeWide, "and a refused apply writes nothing at all");

        // A text that does not fit its slot is refused instead of overwriting
        // the next record.
        AboutPatcher::Config tooLong;
        tooLong.text = QString(400, QLatin1Char('x'));
        const auto refused = AboutPatcher::apply(path, true, tooLong);
        check(!refused.ok && refused.msg.contains(QLatin1String("holds")),
              QStringLiteral("a text longer than its slot is refused (%1)").arg(refused.msg));

        AboutPatcher::Config stockConfig;
        const auto restored = AboutPatcher::apply(path, false, stockConfig);
        check(restored.ok, QStringLiteral("the stock page comes back (%1)").arg(restored.msg));
        const QByteArray restoredBytes = read(path);
        check(restoredBytes.mid(tableAt, table.size()) == table,
              "every compiled string is put back byte for byte from the backup");
    }

    std::printf("[about] the page is found in whichever file carries it\n");
    {
        // A newer player ships a small HD-Player.exe (a launcher) and keeps its
        // QML in a DLL beside it. Searching only the selected EXE reported
        // "PlrAboutSetting.qml is not embedded in this player" on a build the
        // tool can edit perfectly well - the page was one file over.
        const QString dirPath = temp.filePath(QStringLiteral("player-folder"));
        QDir().mkpath(dirPath);
        const QString stubPath = dirPath + QStringLiteral("/HD-Player.exe");
        write(stubPath, QByteArray("MZ") + QByteArray(8192, '\0'));
        auto payload = resourcesBytes({ { QStringLiteral("PlrAboutSetting.qml"),
                                         aboutPageFixture().toUtf8() } });
        addRegistration(payload, false);
        const QString dllPath = dirPath + QStringLiteral("/BstkUi.dll");
        write(dllPath, payload.f.bytes);

        check(QFileInfo(AboutPatcher::findPageHost(stubPath)).fileName()
                  == QLatin1String("BstkUi.dll"),
              QStringLiteral("the file that carries the page is found next to the launcher (%1)")
                  .arg(AboutPatcher::findPageHost(stubPath)));
        const auto state = AboutPatcher::analyze(read(AboutPatcher::findPageHost(stubPath)));
        check(state.supported,
              QStringLiteral("and the page in that file is recognised (%1)").arg(state.detail));
        AboutPatcher::Config cfg = state.config;
        cfg.text = QStringLiteral("Edited through the launcher");
        const auto applied = AboutPatcher::apply(stubPath, true, cfg);
        check(applied.ok, QStringLiteral("applying it to the launcher edits the file that has "
                                         "the page (%1)").arg(applied.msg));
        check(resourceText(read(dllPath), payload, QStringLiteral("PlrAboutSetting.qml"))
                  .contains(QLatin1String("Edited through the launcher")),
              "the edit landed in the DLL, not in the launcher that has no page");

        // Nothing carries the page: say so, and do not touch anything.
        const QString emptyDir = temp.filePath(QStringLiteral("no-page"));
        QDir().mkpath(emptyDir);
        const QString lonely = emptyDir + QStringLiteral("/HD-Player.exe");
        write(lonely, QByteArray("MZ") + QByteArray(2048, '\0'));
        check(AboutPatcher::findPageHost(lonely).isEmpty(),
              "a folder with no page in it reports no host");
        check(!AboutPatcher::apply(lonely, true, cfg).ok,
              "and applying refuses instead of writing into the wrong file");
    }

    std::printf("[about] a page record is found and edited even when the resource tree has no entry\n");
    {
        // The tree is walkable but names other files: the byte scan has to get
        // its turn, otherwise a build that stores the page as a compressed
        // record is reported as "not embedded" - the message an MSI 5.12 build
        // shows even though it carries the page.
        QString page = aboutPageFixture();
        // The record's decoded size has to be believable, and the extra comment
        // also gives the record spare room to work with.
        while (page.size() < 5200)
            page += QStringLiteral("// padding, so the record has room above its size rule\n");
        const auto be32 = [](quint32 v) {
            QByteArray out(4, '\0');
            out[0] = char((v >> 24) & 0xff);
            out[1] = char((v >> 16) & 0xff);
            out[2] = char((v >> 8) & 0xff);
            out[3] = char(v & 0xff);
            return out;
        };
        // A record is (size)(decoded size)(zlib stream): the four bytes directly
        // in front of the stream are what qUncompress reads as the decoded size,
        // so the size field counts that prefix plus the stream. The room behind
        // the stream is the player's padding; a record with none cannot be
        // edited at all, and this is where the spare room is modelled.
        const QByteArray packed = qCompress(page.toUtf8(), 9);
        const QByteArray stream = packed.mid(4);
        const int spare = 1024;
        QByteArray record;
        record += be32(quint32(4 + stream.size() + spare));
        record += packed;
        record += QByteArray(spare, '\0');
        auto r = resourcesBytes({ { QStringLiteral("SomeOther.qml"), QByteArray("Item { }") } });
        addRegistration(r, false);
        QByteArray image = r.f.bytes;
        image += record;
        image += QByteArray(256, '\0');

        const auto state = AboutPatcher::analyze(image);
        check(state.supported,
              QStringLiteral("the page is found by its bytes when the tree has no entry for it (%1)")
                  .arg(state.detail));
        check(state.scanned && state.sourceEditable,
              "and it is reported as read from the file, and as editable, because its record was found");

        const QString recordPath = temp.filePath(QStringLiteral("about-record.exe"));
        write(recordPath, image);
        AboutPatcher::Config wanted = state.config;
        wanted.text = QStringLiteral("Edited inside the record");
        wanted.links[0].hidden = true;
        const auto wrote = AboutPatcher::apply(recordPath, true, wanted);
        check(wrote.ok, QStringLiteral("the page is written into the record it was read from (%1)")
                            .arg(wrote.msg));
        check(read(recordPath).size() == image.size(),
              "the file does not grow by a single byte");
        const auto edited = AboutPatcher::analyze(read(recordPath));
        check(edited.supported && edited.applied && edited.config.text == wanted.text,
              QStringLiteral("and what the record says now is the edited page (%1)")
                  .arg(edited.config.text.left(60)));
        check(edited.config.links.value(0).hidden,
              "the row that was hidden reads back as hidden");

        const auto undone = AboutPatcher::apply(recordPath, false, {});
        check(undone.ok, QStringLiteral("the stock page goes back into the same record (%1)")
                             .arg(undone.msg));
        const auto restored = AboutPatcher::analyze(read(recordPath));
        check(restored.supported && !restored.applied
                  && restored.config.text == state.config.text,
              QStringLiteral("and it says what the stock page said (%1)")
                  .arg(restored.config.text.left(60)));
        check(read(recordPath).size() == image.size(), "still without growing the file");

        // A record with no spare room cannot take the edit, and that has to be
        // said instead of writing a longer file: this is the hard limit.
        QByteArray tight;
        tight += be32(quint32(4 + stream.size()));
        tight += packed;
        QByteArray tightImage = r.f.bytes;
        tightImage += tight;
        tightImage += QByteArray(256, '\0');
        const QString tightPath = temp.filePath(QStringLiteral("about-record-tight.exe"));
        write(tightPath, tightImage);
        // A text long enough that the edited page cannot compress smaller than
        // the stock one: this is the case where no amount of care helps.
        AboutPatcher::Config big = state.config;
        big.text = QString(400, QLatin1Char('Q'));
        const auto refused = AboutPatcher::apply(tightPath, true, big);
        check(!refused.ok && refused.msg.contains(QLatin1String("room")),
              QStringLiteral("a record with no spare room is refused with its sizes (%1)")
                  .arg(refused.msg));
        check(read(tightPath) == tightImage, "and that file is left exactly as it was");
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

        // A release is shipped as a setup program, and the updater has to see
        // that from the file itself: a name is the publisher's to choose, and a
        // setup that is mistaken for a plain EXE gets copied over the running
        // app - which leaves a program that only installs.
        {
            const auto utf16 = [](const QString &text) {
                QByteArray bytes;
                for (const QChar ch : text) {
                    bytes.append(char(ch.unicode() & 0xff));
                    bytes.append(char((ch.unicode() >> 8) & 0xff));
                }
                return bytes;
            };
            const QString inno = temp.filePath(QStringLiteral("release-download.bin"));
            QByteArray payload(4096, '\0');
            payload.replace(1024, 42, QByteArray("MZ") + QByteArray(1000, '\0'));
            payload += utf16(QStringLiteral("Inno Setup Setup Data (6.2.2)"));
            payload += QByteArray(512, '\0');
            write(inno, payload);
            check(UpdateChecker::looksLikeInstaller(inno),
                  "a setup is recognized by its own Inno markers, whatever it is called");

            const QString plain = temp.filePath(QStringLiteral("BSI-Customizer-9.9.9-setup.exe"));
            write(plain, QByteArray("MZ") + QByteArray(8192, '\x90'));
            check(!UpdateChecker::looksLikeInstaller(plain),
                  "a plain program is not called an installer just because its name says setup");

            const QString nsis = temp.filePath(QStringLiteral("other.bin"));
            write(nsis, QByteArray("MZ") + QByteArray(600, '\0') + QByteArray("NullsoftInst"));
            check(UpdateChecker::looksLikeInstaller(nsis), "an NSIS setup is recognized too");

            // An installed copy is recognised by the uninstaller next to it: that
            // is the difference between "replace this file" and "there is an
            // install record that has to stay in step".
            const QString installedDir = temp.filePath(QStringLiteral("installed"));
            QDir().mkpath(installedDir);
            const QString installedExe = installedDir + QStringLiteral("/BSI-Customizer.exe");
            write(installedExe, QByteArray("MZ"));
            check(!UpdateChecker::isInstalledCopy(installedExe),
                  "a folder without unins000.exe is a portable copy");
            write(installedDir + QStringLiteral("/unins000.exe"), QByteArray("MZ"));
            check(UpdateChecker::isInstalledCopy(installedExe),
                  "an uninstaller next to the program marks an installed copy");
        }

        // A setup is a Windows program, and the processor written in it is not
        // the one in the app: Inno Setup's own loader is always 32-bit x86 even
        // when it installs a 64-bit program. Requiring the app's machine meant
        // every published setup was verified, then deleted as "not executable"
        // after the download had reached 100%.
        {
            const auto peImage = [](quint16 machine, bool pe32Plus, bool dll) {
                QByteArray image(4096, '\0');
                image[0] = 'M';
                image[1] = 'Z';
                const int peOffset = 0x80;
                image[0x3c] = char(peOffset);
                image[peOffset] = 'P';
                image[peOffset + 1] = 'E';
                const auto put16 = [&image, peOffset](int at, quint16 value) {
                    image[peOffset + at] = char(value & 0xff);
                    image[peOffset + at + 1] = char((value >> 8) & 0xff);
                };
                put16(4, machine);                            // Machine
                put16(6, 3);                                  // NumberOfSections
                put16(20, pe32Plus ? 240 : 224);              // SizeOfOptionalHeader
                put16(22, quint16(dll ? 0x2022 : 0x0022));    // Characteristics
                put16(24, pe32Plus ? 0x020b : 0x010b);        // optional header magic
                put16(24 + 68, 2);                            // Subsystem: GUI
                return image;
            };

            const QString setup = temp.filePath(QStringLiteral("release-setup.exe"));
            write(setup, peImage(0x014c, false, false));
            UpdateChecker::PeInfo setupInfo;
            QString peError;
            check(UpdateChecker::readPeInfo(setup, &setupInfo, &peError),
                  QStringLiteral("the 32-bit setup header is read (%1)").arg(peError));
            check(setupInfo.machine == 0x014c && !setupInfo.pe32Plus,
                  "Inno's loader is x86 / PE32, whatever the app it installs is");
            check(setupInfo.runnableHere(), "this PC can start a 32-bit program");
            check(UpdateChecker::looksLikeExecutable(setup, &peError),
                  QStringLiteral("the downloaded setup is executable, not deleted (%1)").arg(peError));
            check(!UpdateChecker::canReplaceThisProgram(setup, &peError),
                  QStringLiteral("but it is never copied over the running program (%1)").arg(peError));
            const auto setupBytes = read(setup);
            const auto setupHash = QString::fromLatin1(QCryptographicHash::hash(setupBytes, QCryptographicHash::Sha256).toHex());
            check(UpdateChecker::verifyStagedFile(setup, setupBytes.size(), setupHash, &peError),
                  "the download verifier keeps an x86 installer instead of rejecting machine 0x014c");
            check(!UpdateChecker::verifyStagedFile(setup, setupBytes.size() + 1, setupHash, &peError)
                      && !UpdateChecker::verifyStagedFile(setup, setupBytes.size(), QString(64, QLatin1Char('0')), &peError),
                  "accepting x86 installers never bypasses size or SHA-256 verification");

            const QString portable = temp.filePath(QStringLiteral("release-portable.exe"));
            write(portable, peImage(0x8664, true, false));
            check(UpdateChecker::looksLikeExecutable(portable, &peError),
                  QStringLiteral("the 64-bit portable build is executable (%1)").arg(peError));
#if defined(_M_X64) || (defined(__x86_64__) && !defined(__aarch64__))
            check(UpdateChecker::canReplaceThisProgram(portable, &peError),
                  QStringLiteral("a 64-bit build may replace this 64-bit program (%1)").arg(peError));
#endif
            QByteArray sameArchSetup = peImage(0x8664, true, false);
            sameArchSetup.replace(1024, 12, QByteArray("NullsoftInst"));
            const QString x64Setup = temp.filePath(QStringLiteral("same-arch-setup.exe"));
            write(x64Setup, sameArchSetup);
            check(UpdateChecker::looksLikeInstaller(x64Setup)
                      && UpdateChecker::looksLikeExecutable(x64Setup, &peError)
                      && !UpdateChecker::canReplaceThisProgram(x64Setup, &peError)
                      && peError.contains(QLatin1String("setup program")),
                  "even a same-architecture setup is launched, never copied over the app");

            const QString library = temp.filePath(QStringLiteral("release.dll"));
            write(library, peImage(0x8664, true, true));
            check(!UpdateChecker::looksLikeExecutable(library, &peError)
                      && !UpdateChecker::canReplaceThisProgram(library, &peError),
                  QStringLiteral("a DLL is a program file but never an update (%1)").arg(peError));

#if !defined(_M_ARM64) && !defined(__aarch64__)
            const QString arm = temp.filePath(QStringLiteral("release-arm64.exe"));
            write(arm, peImage(0xaa64, true, false));
            check(!UpdateChecker::looksLikeExecutable(arm, &peError),
                  QStringLiteral("an ARM64 build is not offered to an x64 PC (%1)").arg(peError));
#endif

            // A header that contradicts itself, and the shape of a broken download.
            const QString mismatch = temp.filePath(QStringLiteral("release-mismatch.exe"));
            QByteArray corrupt = peImage(0x8664, true, false);
            corrupt[0x80 + 24] = char(0x0b);
            corrupt[0x80 + 25] = char(0x00);
            write(mismatch, corrupt);
            check(!UpdateChecker::looksLikeExecutable(mismatch, &peError),
                  QStringLiteral("a header claiming x64 with a 32-bit optional header is refused (%1)")
                      .arg(peError));

            const QString page = temp.filePath(QStringLiteral("release-page.exe"));
            write(page, QByteArray("<!DOCTYPE html><html><body>link expired</body></html>"));
            check(!UpdateChecker::looksLikeExecutable(page, &peError),
                  QStringLiteral("an HTML page saved as .exe is refused (%1)").arg(peError));

            const QString shortFile = temp.filePath(QStringLiteral("release-short.exe"));
            write(shortFile, peImage(0x014c, false, false).left(0x90));
            check(!UpdateChecker::looksLikeExecutable(shortFile, &peError),
                  QStringLiteral("a download cut off inside its header is refused (%1)").arg(peError));
        }

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
        put16(peOffset + 4, 0x8664);  // machine: x86-64
        put16(peOffset + 20, 240);    // SizeOfOptionalHeader: a full PE32+ header
        put16(peOffset + 24, 0x20b);  // optional header: PE32+
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
        // A real 32-bit program: x86 machine, PE32 optional header. It is a
        // program Windows can start (that is what Inno's setup stub is), so it
        // passes the download check - but it may never replace this 64-bit one.
        QByteArray i386Image = pe;
        i386Image[peOffset + 4] = char(0x4c);
        i386Image[peOffset + 5] = char(0x01); // 0x014c = i386
        i386Image[peOffset + 24] = char(0x0b);
        i386Image[peOffset + 25] = char(0x01); // PE32
        const QString i386Path = temp.filePath(QStringLiteral("thirty-two-bit.exe"));
        write(i386Path, i386Image);
        check(UpdateChecker::looksLikeExecutable(i386Path, &stageError),
              QStringLiteral("a 32-bit program is a program this PC can start (%1)").arg(stageError));
        check(!UpdateChecker::canReplaceThisProgram(i386Path, &stageError)
                  && stageError.contains(QLatin1String("x86")),
              QStringLiteral("but it is refused before it can replace a 64-bit build (%1)")
                  .arg(stageError));

        // A setup marker is not permission to skip executable validation.
        // An actual x86 PE32 loader passes above; a contradictory PE32+ header
        // is damaged even when the file contains an authentic-looking marker.
        {
            QByteArray setupImage = i386Image;
            setupImage[peOffset + 24] = char(0x0b);
            setupImage[peOffset + 25] = char(0x02); // x86 machine, PE32+ header: not a startable program
            QByteArray marker;
            for (const char *c = "Inno Setup Setup Data (6.2.2)"; *c; ++c) {
                marker.append(*c);
                marker.append('\0');
            }
            setupImage.replace(512, marker.size(), marker);
            const QString setupPath = temp.filePath(QStringLiteral("setup-odd-header.exe"));
            write(setupPath, setupImage);
            check(UpdateChecker::looksLikeInstaller(setupPath),
                  "a setup is still recognized by its own markers when its header is odd");
            check(!UpdateChecker::looksLikeExecutable(setupPath, &stageError),
                  "and that header is not one a program may be started from");
            const QString setupHash = UpdateChecker::fileSha256(setupPath);
            check(!UpdateChecker::verifyStagedFile(setupPath, setupImage.size(), setupHash, &stageError),
                  QStringLiteral("a marker never bypasses rejection of a contradictory installer header (%1)")
                      .arg(stageError));
            check(!UpdateChecker::canReplaceThisProgram(setupPath, &stageError)
                      && stageError.contains(QLatin1String("setup")),
                  QStringLiteral("a setup is run, never copied over the running program (%1)")
                      .arg(stageError));
        }

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


    checkDebloat();
    checkAndroid();
    checkRootDisk();

    return failures;
}
