#include "core/Backend.h"
#include "core/ByteUtils.h"
#include "core/FpsPatcher.h"
#include "core/PatchFile.h"
#include "core/PeUtils.h"
#include "core/SettingsManager.h"
#include "core/ThemeEngine.h"
#include "core/ZlibUtils.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>

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
        data.replace(0x1300 + i * 0x100, 6, QByteArray::fromHex("48895c2408c3"));
    }
    return data;
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
            check(FpsPatcher::patch(path, 480).ok, "FPS can be changed again from a clean backup");
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
