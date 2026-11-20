#include "ShaderPackage.h"
#include "package_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <cstdio>
#include <cstdlib>

using namespace ShaderPackageFixture;
namespace {
int assertions = 0;
void require(bool yes, const QString &message)
{
    ++assertions;
    if (!yes) { std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); std::exit(1); }
}
void rejected(const QByteArray &player, const ShaderPackage::Payload &p, const QMap<QString, QByteArray> &r,
              const char *message)
{
    const auto result = ShaderPackage::validate(player, p, r);
    require(!result.ok && result.qtVersion.isEmpty() && result.moduleQtVersion.isEmpty() && !result.publicQtApi
                && result.packageSha256.isEmpty() && result.runtimeSha256.isEmpty() && !result.error.isEmpty(), QString::fromLatin1(message));
}
QByteArray read(const QString &path)
{
    QFile f(path);
    require(f.open(QIODevice::ReadOnly) && f.size() > 0 && f.size() <= 128 * 1024 * 1024, QStringLiteral("Read runtime fixture: ") + path);
    return f.readAll();
}
// Windows CI validates the ACTUAL compiled release plugin against the actual
// matching Qt/VC DLLs, still as data. This never loads an arbitrary DLL.
void actual(const QString &moduleDir, const QString &runtimeDir)
{
    const QDir module(moduleDir), bin(runtimeDir);
    ShaderPackage::Payload p{read(module.filePath(QStringLiteral("BSIShaders.dll"))), read(module.filePath(QStringLiteral("qmldir")))};
    auto dependencies = ShaderPackage::dependencies(p.dll, true);
    require(dependencies.ok, QStringLiteral("Actual plugin imports: ") + dependencies.error);
    QMap<QString, QByteArray> providers;
    QStringList pending = dependencies.modules;
    // VC dependencies may be in System32 rather than Qt/bin. Do not change or
    // redistribute them, and never resolve other modules from PATH/the cwd.
    const QDir system(qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32"));
    while (!pending.isEmpty()) {
        const auto name = pending.takeFirst();
        if (ShaderPackage::systemModule(name) || providers.contains(name)) continue;
        require(providers.size() < 64, QStringLiteral("Bounded actual runtime closure"));
        QString found;
        const bool vc = name.startsWith(QLatin1String("msvcp140")) || name.startsWith(QLatin1String("vcruntime140")) || name == QLatin1String("concrt140.dll");
        const QList<QDir> locations = vc ? QList<QDir>{bin, system} : QList<QDir>{bin};
        for (const auto &dir : locations)
            for (const auto &entry : dir.entryList(QDir::Files))
                if (entry.compare(name, Qt::CaseInsensitive) == 0) { found = dir.filePath(entry); break; }
        require(!found.isEmpty(), QStringLiteral("Actual dependency not found in explicit trusted runtime locations: ") + name);
        const auto bytes = read(found);
        providers.insert(name, bytes);
        const auto deps = ShaderPackage::dependencies(bytes, true);
        require(deps.ok, name + QLatin1String(": ") + deps.error);
        pending += deps.modules;
    }
    // Forwarders may reference an otherwise nonimported VC DLL.
    for (const auto &name : {QStringLiteral("msvcp140_1.dll"), QStringLiteral("msvcp140_2.dll"), QStringLiteral("vcruntime140_1.dll")}) {
        if (providers.contains(name)) continue;
        for (const auto &dir : {bin, system}) if (QFileInfo::exists(dir.filePath(name))) { providers.insert(name, read(dir.filePath(name))); break; }
    }
    const auto description = ShaderPackage::describe(p);
    require(description.ok, QStringLiteral("Actual module descriptor: ") + description.error);
    const int major = description.moduleQtVersion.left(1).toInt();
    const auto fixed = ShaderPackage::runtimeVersion(providers.value(QStringLiteral("qt%1core.dll").arg(major)));
    require(fixed.ok, QStringLiteral("Actual host fixed version: ") + fixed.error);
    const auto result = ShaderPackage::validate(image(false, qtImports(major)), p, providers);
    require(result.ok && result.qtVersion == fixed.qtVersion && result.moduleQtVersion == description.moduleQtVersion,
            QStringLiteral("Actual compiled Windows module/runtime package check: ") + result.error);
    std::printf("PASS: actual module built with Qt %s -> runtime Qt %s, MSVC regular plugin, read-only descriptor/entry points/imports/fixed-version/dependency closure; not an installed-player test\n",
                qPrintable(result.moduleQtVersion), qPrintable(result.qtVersion));
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    for (const auto &version : {QStringLiteral("6.8.3"), QStringLiteral("5.15.2")}) {
        const int major = version.left(1).toInt();
        const auto host = image(false, qtImports(major));
        const auto p = payload(version);
        const auto r = runtime(version);
        const auto good = ShaderPackage::validate(host, p, r);
        require(good.ok && good.qtVersion == version && good.packageSha256 == ShaderPackage::payloadHash(p)
                    && good.runtimeSha256.size() == 3, QStringLiteral("Strict valid Qt package: ") + good.error);
        auto crlf = p; crlf.qmldir.replace("\n", "\r\n");
        require(ShaderPackage::validate(host, crlf, r).ok, QStringLiteral("Windows qmldir newline checkout"));
        auto bad = p; bad.qmldir += "prefer C:/other\n";
        rejected(host, bad, r, "qmldir cannot redirect/execute unowned imports");
        auto missing = r; missing.remove(QStringLiteral("qt%1qml.dll").arg(major));
        rejected(host, p, missing, "Missing dependency cannot be supplied by overwriting the player runtime");
        auto incompatible = r;
        incompatible[QStringLiteral("qt%1gui.dll").arg(major)] = image(true, {}, {{QByteArray("?bsi_fixture@@YAXXZ"), {}}}, major == 6 ? QStringLiteral("6.7.3") : QStringLiteral("5.15.1"));
        rejected(host, p, incompatible, "Qt-major equality alone is insufficient");
        incompatible = r;
        auto &core = incompatible[QStringLiteral("qt%1core.dll").arg(major)]; putLe32(core, 0x8144, 1);
        rejected(host, p, incompatible, "Qt debug fixed flags are refused");
        putLe32(core, 0x8144, 0); putLe32(core, 0x813c, 0);
        rejected(host, p, incompatible, "Conflicting product/file version is refused");
        incompatible = r; putLe32(incompatible[QStringLiteral("qt%1core.dll").arg(major)], 0x98 + 128, 0);
        rejected(host, p, incompatible, "No display-string fallback for broken RT_VERSION");
        incompatible = r; putLe32(incompatible[QStringLiteral("qt%1core.dll").arg(major)], 0x8054, 0x40000000);
        rejected(host, p, incompatible, "Version directory escape is refused");
        incompatible = r; incompatible[QStringLiteral("qt%1qml.dll").arg(major)] = image(true, {}, {{QByteArray("differentSymbol"), {}}}, version);
        rejected(host, p, incompatible, "Matching Qt version with absent imported symbol is refused");
        auto other = p; putLe16(other.dll, 0x84, 0xaa64);
        rejected(host, other, r, "ARM64 DLL cannot be mislabeled x64");
        other = p; putLe16(other.dll, 0x96, 0x22);
        rejected(host, other, r, "Renamed EXE is not a regular DLL");
        other = p; putLe32(other.dll, 0x1b0 + 36, 0xc0000040);
        rejected(host, other, r, "Writable data descriptor is refused");
        other = p; putLe32(other.dll, 0x1b0 + 36, 0x60000040);
        rejected(host, other, r, "Descriptor is data, never an executable function");
        for (const auto &pair : QList<QPair<QByteArray, QByteArray>>{
                 {"ABI=msvc", "ABI=xxxx"}, {"BUILD=release", "BUILD=debugxx"},
                 {"ARCH=x64", "ARCH=x86"}, {"NS=default", "NS=customx"},
                 {"BSI_SHADERS_V2", "BSI_SHADERS_V1"}}) {
            other = p; other.dll.replace(pair.first, pair.second);
            rejected(host, other, r, "Unknown ABI/build/namespace/descriptor is refused");
        }
        other = p; putLe32(other.dll, 0x5100, 0x1000);
        rejected(host, other, r, "Arbitrary descriptor text outside its data export cannot establish compatibility");
        auto wrongHost = host; putLe16(wrongHost, 0x84, 0x14c);
        rejected(wrongHost, p, r, "x86 player cannot receive x64 module");
        const auto mixed = qtImports(major) + qtImports(major == 6 ? 5 : 6);
        rejected(image(false, mixed), p, r, "Mixed Qt host import families are refused");
        auto uppercase = r; uppercase.insert(QStringLiteral("Qt%1Core.dll").arg(major), r.first());
        rejected(host, p, uppercase, "Case-ambiguous dependency keys are refused");
    }
    // This is a new opt-in source contract, not a blanket relaxation of V2.
    for (const auto &baseline : {QStringLiteral("5.15.2"), QStringLiteral("6.2.4")}) {
        const int major = baseline.left(1).toInt();
        const auto portable = payload(baseline, {}, true);
        const auto description = ShaderPackage::describe(portable);
        require(description.ok && description.publicQtApi && description.moduleQtVersion == baseline,
                QStringLiteral("Only the explicit audited V3 descriptor opts in"));
        const auto host = image(false, qtImports(major));
        const QStringList supported = major == 5 ? QStringList{baseline, QStringLiteral("5.15.4"), QStringLiteral("5.15.19")}
            : QStringList{baseline, QStringLiteral("6.5.3"), QStringLiteral("6.7.0"), QStringLiteral("6.7.3"), QStringLiteral("6.8.3"), QStringLiteral("6.10.0")};
        for (const auto &target : supported) {
            const auto result = ShaderPackage::validate(host, portable, runtime(target));
            require(ShaderPackage::canTarget(description, target) && result.ok && result.qtVersion == target
                        && result.moduleQtVersion == baseline && result.publicQtApi && result.runtimeSha256.size() == 3,
                    QStringLiteral("Public API forward preflight preserves actual host and compile versions: ") + target + result.error);
            if (target != baseline) rejected(host, payload(baseline), runtime(target), "Legacy V2 NEVER gains wider version eligibility");
            auto broken = runtime(target);
            broken[QStringLiteral("qt%1qml.dll").arg(major)] = image(true, {}, {{QByteArray("missing"), {}}}, target);
            rejected(host, portable, broken, "A public-API contract never bypasses missing imported symbols");
        }
        const QString older = major == 5 ? QStringLiteral("5.15.1") : QStringLiteral("6.2.3");
        rejected(host, portable, runtime(older), "A newer bridge never runs backwards against an older runtime");
        require(!ShaderPackage::canTarget(description, older), QStringLiteral("Candidate selection also refuses older runtimes"));
        for (const auto &invalid : {QStringLiteral("4.8.7"), QStringLiteral("7.0.0"), QStringLiteral("5.015.4"), QStringLiteral("6.999.0"), QStringLiteral("6.7.3-extra"), QString()})
            require(!ShaderPackage::canTarget(description, invalid), QStringLiteral("Version eligibility has no unknown/string/prefix fallback"));
        const QString otherMajor = major == 5 ? QStringLiteral("6.8.3") : QStringLiteral("5.15.4");
        require(!ShaderPackage::canTarget(description, otherMajor), QStringLiteral("Qt 5 and 6 NEVER share one ABI"));
        rejected(image(false, qtImports(major == 5 ? 6 : 5)), portable, runtime(otherMajor), "Cross-major public bridge is refused");
        auto mixed = runtime(baseline);
        mixed[QStringLiteral("qt%1gui.dll").arg(major)] = runtime(supported.last()).value(QStringLiteral("qt%1gui.dll").arg(major));
        rejected(host, portable, mixed, "All host Qt providers must STILL have identical exact fixed versions");
        auto bad = portable;
        bad.dll.replace("qt_plugin_query_metadata", "qt_plugin_query_metadatb");
        rejected(host, bad, runtime(baseline), "The real Qt 5 / early Qt 6 metadata entry is still mandatory");
        bad = portable; bad.dll.replace("API=public-v1", "API=custom-v1");
        rejected(host, bad, runtime(baseline), "Unknown/private ABI policies cannot request forward compatibility");
        bad = portable; bad.dll.replace("ABI=msvc", "ABI=ming");
        rejected(host, bad, runtime(baseline), "Public-API policy does not bridge a compiler ABI mismatch");
        bad = portable; putLe32(bad.dll, 0x1b0 + 36, 0xc0000040);
        rejected(host, bad, runtime(baseline), "V3 descriptor must still be read-only non-executable data");
        auto debug = runtime(baseline); putLe32(debug[QStringLiteral("qt%1core.dll").arg(major)], 0x8144, 1);
        rejected(host, portable, debug, "V3 still rejects debug runtime builds");
    }
    require(!ShaderPackage::describe(payload(QStringLiteral("5.12.0"), {}, true)).ok
                && !ShaderPackage::describe(payload(QStringLiteral("6.0.0"), {}, true)).ok,
            QStringLiteral("A descriptor cannot claim an unaudited API floor"));
    const auto host = image(false, qtImports(6));
    auto p = payload(QStringLiteral("6.8.3"), {{QByteArray("vendor.dll"), {QByteArray("work")}, true}});
    auto r = runtime();
    rejected(host, p, r, "Missing delay dependency is refused");
    r.insert(QStringLiteral("vendor.dll"), image(true, {{QByteArray("leaf.dll"), {QByteArray("#1")}, false}}, {{QByteArray("work"), {}}}));
    rejected(host, p, r, "Transitive runtime dependency is required");
    r.insert(QStringLiteral("leaf.dll"), image(true, {}, {{QByteArray("ordinal-one"), {}}}));
    require(ShaderPackage::validate(host, p, r).ok, QStringLiteral("Delay imports and ordinal dependency closure resolve"));
    r[QStringLiteral("vendor.dll")] = image(true, {}, {{QByteArray("work"), QByteArray("forward:leaf.#1")}});
    require(ShaderPackage::validate(host, p, r).ok, QStringLiteral("Forwarded non-system ordinal resolves"));
    r[QStringLiteral("leaf.dll")] = image(true, {}, {{QByteArray("ordinal-one"), QByteArray("forward:vendor.work")}});
    rejected(host, p, r, "Forwarder cycle is refused");
    r[QStringLiteral("vendor.dll")] = image(true, {}, {{QByteArray("work"), QByteArray("forward:KERNEL32.Sleep")}});
    require(ShaderPackage::validate(host, p, r).ok, QStringLiteral("Windows-owned forwarder is not mistaken for missing package DLL"));
    auto zeroData = image(true, {}, {{QByteArray("work"), {}}});
    putLe32(zeroData, 0x1b0 + 8, 0x7000); // rdata virtual tail, not file-backed
    putLe32(zeroData, 0x1d8 + 12, 0x9000); putLe32(zeroData, 0x98 + 56, 0xb000);
    putLe32(zeroData, 0x5100, 0x8800);
    r[QStringLiteral("vendor.dll")] = zeroData;
    require(ShaderPackage::validate(host, p, r).ok, QStringLiteral("Legitimate runtime zero-fill data exports resolve without pretending they are executable bytes"));
    putLe32(zeroData, 0x1b0 + 36, 0x60000040);
    r[QStringLiteral("vendor.dll")] = zeroData;
    rejected(host, p, r, "Executable export cannot point into zero-fill storage");
    auto debug = payload(QStringLiteral("6.8.3"), {{QByteArray("vcruntime140d.dll"), {QByteArray("work")}, false}});
    auto debugRuntime = runtime(); debugRuntime.insert(QStringLiteral("vcruntime140d.dll"), image(true, {}, {{QByteArray("work"), {}}}));
    rejected(host, debug, debugRuntime, "Debug CRT cannot satisfy a release package contract");
    auto noEntry = payload(); noEntry.dll.replace("qt_plugin_instance", "qt_plugin_instancf");
    rejected(host, noEntry, runtime(), "A matching data descriptor alone cannot replace Qt's native plugin entry point");
    const auto parsed = ShaderPackage::dependencies(p.dll, true);
    require(parsed.ok && parsed.modules.contains(QStringLiteral("vendor.dll")), QStringLiteral("Actual delay import parsed"));
    auto malformed = p.dll; putLe32(malformed, 0x2200, 2);
    require(!ShaderPackage::dependencies(malformed, true).ok, QStringLiteral("Unknown delay import address mode refuses"));
    malformed = p.dll; putLe32(malformed, 0x98 + 124, 20);
    require(!ShaderPackage::dependencies(malformed, true).ok, QStringLiteral("Unterminated import descriptors refuse"));
    malformed = p.dll; putLe32(malformed, 0x1b0 + 12, 0x1000);
    require(!ShaderPackage::dependencies(malformed, true).ok, QStringLiteral("Overlapping section mapping refuses"));
    for (int length : {0, 2, 64, 128, 256, 512, 4096, 8192, 32768})
        require(!ShaderPackage::dependencies(p.dll.left(length), true).ok, QStringLiteral("Truncated PE refuses"));
    require(ShaderPackage::systemModule(QStringLiteral("api-ms-win-crt-runtime-l1-1-0.dll"))
                && !ShaderPackage::systemModule(QStringLiteral("api-not-really-system.dll"))
                && !ShaderPackage::systemModule(QStringLiteral("vcruntime140.dll"))
                && !ShaderPackage::systemModule(QStringLiteral("qt6core.dll")), QStringLiteral("No wildcard exemption for Qt/VC/vendor libraries"));
    const auto args = app.arguments();
    if (args.size() > 1) {
        require(args.size() == 3, QStringLiteral("Usage: shader-package-test [module-directory Qt-bin-directory]"));
        actual(args.at(1), args.at(2));
    }
    std::printf("PASS: %d package/PE/Qt ABI/dependency ownership assertions; no DLL executed or player installed\n", assertions);
    return 0;
}
