#include "ShaderBundle.h"
#include "package_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstdlib>

namespace {
int assertions = 0;
void require(bool yes, const QString &message)
{
    ++assertions;
    if (!yes) { std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); std::exit(1); }
}
QByteArray read(const QString &path)
{
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
void write(const QString &path, const QByteArray &bytes)
{
    QFile f(path); require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(), QStringLiteral("Write disposable bundle fixture: ") + path);
}
QJsonObject row(const QString &version, const ShaderPackage::Payload &p)
{
    return {{"id", QStringLiteral("qt-") + version}, {"qtVersion", version},
        {"packageSha256", ShaderPackage::payloadHash(p)}, {"dllSha256", ShaderPackage::sha256(p.dll)},
        {"qmldirSha256", ShaderPackage::sha256(p.qmldir)}};
}
QByteArray index(const QJsonArray &rows)
{
    return QJsonDocument(QJsonObject{{"schema", "bsi.shader-bundle.v1"}, {"entries", rows}}).toJson(QJsonDocument::Compact);
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    QMap<QString, QByteArray> files;
    QJsonArray rows;
    for (const auto &version : {QStringLiteral("5.15.2"), QStringLiteral("6.8.3")}) {
        const auto p = ShaderPackageFixture::payload(version);
        const QString id = QStringLiteral("qt-") + version;
        files.insert(id + QStringLiteral("/BSIShaders.dll"), p.dll);
        files.insert(id + QStringLiteral("/qmldir"), p.qmldir);
        rows << row(version, p);
    }
    if (args.size() == 3 && args[1] == QLatin1String("--write-fixtures")) {
        for (auto i = files.cbegin(); i != files.cend(); ++i) {
            const auto path = QDir(args[2]).filePath(i.key());
            require(QDir().mkpath(QFileInfo(path).absolutePath()), QStringLiteral("Create disposable bundle fixture directory"));
            write(path, i.value());
        }
        return 0;
    }
    if (args.size() == 4 && args[1] == QLatin1String("--check-index")) {
        const auto result = ShaderBundle::decode(read(args[2]), [&](const QString &name) { return read(QDir(args[3]).filePath(name)); });
        require(result.ok && result.entries.size() == 2, QStringLiteral("CMake-produced resource index/hash contract: ") + result.error);
        std::puts("PASS: real CMake-generated shader index, fixed resource aliases, both Qt versions and exact DLL/qmldir/package hashes");
        return 0;
    }
    const auto reader = [&](const QString &name) { require(files.contains(name), QStringLiteral("Decoder requests only known fixed relative paths")); return files.value(name); };
    const auto good = ShaderBundle::decode(index(rows), reader);
    require(good.ok && good.entries.size() == 2 && good.entries[0].qtVersion == QLatin1String("5.15.2")
            && good.entries[1].qtVersion == QLatin1String("6.8.3"), QStringLiteral("Both release MSVC/x64 regular modules decode without being loaded: ") + good.error);
    const auto rejected = [&](const QByteArray &bytes, const std::function<QByteArray(const QString &)> &source, const QString &why) {
        const auto r = ShaderBundle::decode(bytes, source);
        require(!r.ok && r.entries.isEmpty() && !r.error.isEmpty(), why);
    };
    rejected({}, reader, QStringLiteral("An absent bundle cannot enable installation"));
    rejected(QByteArray(8193, ' '), reader, QStringLiteral("Oversized index is refused"));
    rejected(QByteArray("{broken"), reader, QStringLiteral("Invalid JSON is refused"));
    rejected(index({}), reader, QStringLiteral("No empty success"));
    auto bad = QJsonDocument::fromJson(index(rows)).object(); bad[QStringLiteral("extra")] = true;
    rejected(QJsonDocument(bad).toJson(QJsonDocument::Compact), reader, QStringLiteral("Unknown root keys are refused"));
    bad.remove(QStringLiteral("extra")); bad[QStringLiteral("schema")] = QStringLiteral("bsi.shader-bundle.v0");
    rejected(QJsonDocument(bad).toJson(QJsonDocument::Compact), reader, QStringLiteral("Unknown schema is refused"));
    auto changed = rows; changed << rows[0];
    rejected(index(changed), reader, QStringLiteral("Duplicate versions cannot select an ambiguous binary"));
    changed << rows[0] << rows[0];
    rejected(index(changed), reader, QStringLiteral("At most four packages"));
    for (const auto &field : {QStringLiteral("id"), QStringLiteral("qtVersion"), QStringLiteral("packageSha256"), QStringLiteral("dllSha256"), QStringLiteral("qmldirSha256")}) {
        auto object = rows[0].toObject(); object[field] = field == QLatin1String("id") ? QStringLiteral("../../user.dll") : QStringLiteral("invalid");
        changed = rows; changed[0] = object;
        rejected(index(changed), reader, QStringLiteral("Bad identity/hash is refused before file access: ") + field);
    }
    auto object = rows[0].toObject(); object[QStringLiteral("extra")] = QStringLiteral("unrecognized"); changed = rows; changed[0] = object;
    rejected(index(changed), reader, QStringLiteral("Unknown entry keys are refused"));
    rejected(index(rows), [&](const QString &name) { auto bytes = files.value(name); if (name.endsWith(QLatin1String(".dll"))) bytes[2] = 1; return bytes; },
             QStringLiteral("Binary corruption is refused before descriptor inspection"));
    rejected(index(rows), [&](const QString &name) { return name.endsWith(QLatin1String("qmldir")) ? QByteArray("module BSIShaders\nprefer C:/unowned\n") : files.value(name); },
             QStringLiteral("Unowned qmldir redirection is refused"));
    rejected(index(rows), [](const QString &) { return QByteArray(); }, QStringLiteral("Missing resource never becomes an empty success"));
    auto mislabeled = rows[0].toObject(); mislabeled[QStringLiteral("qtVersion")] = QStringLiteral("6.8.3"); mislabeled[QStringLiteral("id")] = QStringLiteral("qt-6.8.3");
    rejected(index(QJsonArray{mislabeled}), [&](const QString &name) { return files.value(QStringLiteral("qt-5.15.2/") + QFileInfo(name).fileName()); },
             QStringLiteral("Pinned bytes still need their actual matching read-only descriptor and Qt entry points"));
    {
        QMap<QString, QByteArray> publicFiles;
        QJsonArray publicRows;
        for (const auto &baseline : {QStringLiteral("5.15.2"), QStringLiteral("6.2.4")}) {
            const auto p = ShaderPackageFixture::payload(baseline, {}, true);
            publicRows << row(baseline, p);
            publicFiles.insert(QStringLiteral("qt-") + baseline + QStringLiteral("/BSIShaders.dll"), p.dll);
            publicFiles.insert(QStringLiteral("qt-") + baseline + QStringLiteral("/qmldir"), p.qmldir);
        }
        const auto decoded = ShaderBundle::decode(index(publicRows), [&](const QString &name) { return publicFiles.value(name); });
        require(decoded.ok && decoded.entries.size() == 2, QStringLiteral("V3 baselines use the SAME pinned bounded bundle format: ") + decoded.error);
        for (const auto &entry : decoded.entries) {
            const auto description = ShaderPackage::describe(entry.payload);
            const auto target = entry.qtVersion.startsWith(QLatin1Char('5')) ? QStringLiteral("5.15.4") : QStringLiteral("6.7.3");
            require(description.ok && description.publicQtApi && description.moduleQtVersion == entry.qtVersion
                        && ShaderPackage::canTarget(description, target),
                    QStringLiteral("The index retains the real build baseline; eligibility never relabels a DLL"));
        }
    }
    require(ShaderPackage::licenseNotices().contains("MinHook") && ShaderPackage::licenseNotices().contains("Apache License"),
            QStringLiteral("Binary redistribution notices accompany owned installations"));
    require(ShaderPackage::licenseNotices().size() == 17709 && ShaderPackage::sha256(ShaderPackage::licenseNotices())
                == QLatin1String("fd42573ba8962381dbf5f27419147cc33f454bc1b97e4134113ef9102554fd9c"),
            QStringLiteral("Compiled notices preserve ALL bytes across MSVC-safe literal splitting; old owned installations stay valid"));
    std::printf("PASS: %d embedded bundle/hash/identity/bounds/descriptor assertions (no DLL execution)\n", assertions);
    return 0;
}
