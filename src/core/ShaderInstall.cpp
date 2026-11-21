#include "ShaderInstall.h"
#include "ShaderSettingsImage.h"
#include <QDir>
#include <QDirIterator>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace ShaderInstall {
namespace {
using Tree = QMap<QString, QByteArray>;
const QString moduleName = QStringLiteral("BSIShaders");
const QString ownerName = QStringLiteral("bsi-shaders-owner.json");
const QString noticesName = QStringLiteral("THIRD-PARTY-NOTICES.txt");
const QString journalSuffix = QStringLiteral(".bsi-shaders-transaction.json");
constexpr qint64 fileLimit = 128LL * 1024 * 1024;
#ifdef BSI_SHADER_INSTALL_TESTING
Checkpoint testAt = Checkpoint::None;
bool testInterrupt = false, testRecoveryFailure = false;
void checkpoint(Checkpoint at)
{
    if (testAt == at) { testAt = Checkpoint::None; throw std::runtime_error("Injected shader transaction interruption/I/O failure."); }
}
#define BSI_CHECKPOINT(name) checkpoint(Checkpoint::name)
#else
#define BSI_CHECKPOINT(name) do {} while (false)
#endif
[[noreturn]] void refuse(const QString &s) { throw std::runtime_error(s.toUtf8().constData()); }
void need(bool ok, const QString &s) { if (!ok) refuse(s); }
void need(bool ok, const char *s) { if (!ok) refuse(QString::fromLatin1(s)); }
bool hashValid(const QString &s)
{
    static const QRegularExpression rx(QStringLiteral("^[0-9a-f]{64}$"));
    return rx.match(s).hasMatch();
}
QStringList entries(const QString &path, int limit = 16)
{
    QStringList names;
    QDirIterator it(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        need(names.size() < limit, QStringLiteral("Too many directory entries: %1").arg(path));
        it.next(); names << it.fileName();
    }
    names.sort(); return names;
}
bool exists(const QString &path) { const QFileInfo f(path); return f.exists() || f.isSymLink(); }
QString absolute(const QString &path) { return QDir::cleanPath(QFileInfo(path).absoluteFilePath()); }
bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}
void safePath(const QString &path)
{
    QFileInfo f(absolute(path));
    for (;;) {
        need(!f.isSymLink() && !f.isJunction(), QStringLiteral("Linked/reparse shader paths are refused: %1").arg(path));
        if (f.exists()) need(samePath(f.absoluteFilePath(), f.canonicalFilePath()), QStringLiteral("Noncanonical shader path: %1").arg(path));
        const QFileInfo parent(f.absolutePath());
        if (samePath(parent.absoluteFilePath(), f.absoluteFilePath())) break;
        f = parent;
    }
}
void closedPlayer(const QString &exe)
{
    safePath(exe);
#ifdef Q_OS_WIN
    const HANDLE handle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    need(handle != INVALID_HANDLE_VALUE, "Cannot enumerate running players; installation is refused.");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    bool found = false, ok = Process32FirstW(handle, &entry) != FALSE;
    if (ok) {
        do {
            if (QFileInfo(exe).fileName().compare(QString::fromWCharArray(entry.szExeFile), Qt::CaseInsensitive) == 0) { found = true; break; }
        } while (Process32NextW(handle, &entry));
        if (!found) ok = GetLastError() == ERROR_NO_MORE_FILES;
    }
    CloseHandle(handle);
    need(ok && !found, "Close every player instance before shader installation or recovery; process state could not be verified closed.");
#else
    Q_UNUSED(exe);
#endif
}
QByteArray read(const QString &path, qint64 limit = fileLimit)
{
    safePath(path);
    const QFileInfo info(path);
    need(info.isFile() && info.size() > 0 && info.size() <= limit, QStringLiteral("Not a bounded regular file: %1").arg(path));
    QFile f(path); need(f.open(QIODevice::ReadOnly), QStringLiteral("Cannot read %1: %2").arg(path, f.errorString()));
    const QByteArray bytes = f.read(limit + 1);
    need(f.error() == QFileDevice::NoError && f.atEnd() && bytes.size() == info.size(), QStringLiteral("File changed or read failed: %1").arg(path));
    return bytes;
}
void exclusive(const QString &path, const QByteArray &bytes)
{
    safePath(path);
    QFile f(path);
    need(f.open(QIODevice::WriteOnly | QIODevice::NewOnly), QStringLiteral("Will not replace an existing recovery/staging file: %1").arg(path));
    need(f.write(bytes) == bytes.size() && f.flush(), QStringLiteral("Incomplete recovery/staging write: %1").arg(path));
    f.close(); need(read(path) == bytes, QStringLiteral("Recovery/staging read-back failed: %1").arg(path));
}
void replace(const QString &path, const QByteArray &before, const QByteArray &after)
{
    need(read(path) == before, QStringLiteral("Target changed before atomic commit: %1").arg(path));
    QSaveFile f(path); f.setDirectWriteFallback(false);
    need(f.open(QIODevice::WriteOnly) && f.write(after) == after.size() && f.flush(), QStringLiteral("Cannot stage atomic replacement: %1").arg(path));
    need(read(path) == before, QStringLiteral("Target changed while staging: %1").arg(path));
    need(f.commit() && read(path) == after, QStringLiteral("Atomic replacement or full read-back failed: %1").arg(path));
}
QByteArray json(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }
QJsonObject object(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    need(error.error == QJsonParseError::NoError && doc.isObject(), "Invalid shader ownership/recovery JSON.");
    // Reject duplicate keys, trailing material and alternate encodings rather
    // than letting permissive JSON parsing change an ownership record's meaning.
    need(json(doc.object()) == bytes, "Noncanonical shader ownership/recovery record.");
    return doc.object();
}
QString treeHash(const Tree &tree)
{
    if (tree.isEmpty()) return {};
    QByteArray record("bsi.shader-tree.v1\n");
    for (auto i = tree.cbegin(); i != tree.cend(); ++i) record += i.key().toUtf8() + ':' + ShaderPackage::sha256(i.value()).toLatin1() + '\n';
    return ShaderPackage::sha256(record);
}
Tree readTree(const QString &path, const QString &exeName)
{
    safePath(path);
    if (!exists(path)) return {};
    need(QFileInfo(path).isDir(), QStringLiteral("Not an owned shader module directory: %1").arg(path));
    const QDir dir(path);
    const auto names = entries(path, 4);
    QStringList expected{QStringLiteral("BSIShaders.dll"), QStringLiteral("qmldir"), ownerName, noticesName}; expected.sort();
    need(names.size() == expected.size() && std::equal(names.cbegin(), names.cend(), expected.cbegin()), QStringLiteral("Unowned/extra/missing module files; nothing will be overwritten: %1").arg(path));
    Tree tree;
    for (const auto &name : expected) tree.insert(name, read(dir.filePath(name), name.endsWith(QLatin1String(".dll")) ? fileLimit : name == noticesName ? 32768 : 4096));
    need(tree.value(noticesName) == ShaderPackage::licenseNotices(), "Owned shader license notices were changed; no files will be overwritten.");
    need(QByteArray(tree.value(QStringLiteral("qmldir"))).replace("\r\n", "\n") == ShaderPackage::moduleDefinition(), "Owned qmldir was changed.");
    const auto owner = object(tree.value(ownerName));
    const ShaderPackage::Payload p{tree.value(QStringLiteral("BSIShaders.dll")), tree.value(QStringLiteral("qmldir"))};
    need(owner.size() == 4 && owner.value(QStringLiteral("schema")).toString() == QLatin1String("bsi.shader-module.v2")
             && owner.value(QStringLiteral("exeFile")).toString() == exeName
             && owner.value(QStringLiteral("packageSha256")).toString() == ShaderPackage::payloadHash(p)
             && QRegularExpression(QStringLiteral("^[56]\\.[0-9]{1,3}\\.[0-9]{1,3}$")).match(owner.value(QStringLiteral("qtVersion")).toString()).hasMatch(),
         "Shader module ownership/hash does not match this player; no file was changed.");
    return tree;
}
Tree owned(const ShaderPackage::Payload &p, const QString &exeName, const QString &version)
{
    return {{QStringLiteral("BSIShaders.dll"), p.dll}, {QStringLiteral("qmldir"), p.qmldir},
        {noticesName, ShaderPackage::licenseNotices()},
        {ownerName, json({{QStringLiteral("schema"), QStringLiteral("bsi.shader-module.v2")}, {QStringLiteral("exeFile"), exeName},
            {QStringLiteral("packageSha256"), ShaderPackage::payloadHash(p)}, {QStringLiteral("qtVersion"), version}})}};
}
void writeTree(const QString &path, const Tree &tree)
{
    need(!tree.isEmpty(), "Cannot stage an empty module tree."); safePath(path);
    need(QDir().mkdir(path), QStringLiteral("Cannot exclusively create shader staging directory: %1").arg(path));
    for (auto i = tree.cbegin(); i != tree.cend(); ++i) exclusive(QDir(path).filePath(i.key()), i.value());
}
void sameTree(const QString &path, const Tree &wanted, const QString &exeName)
{
    need(readTree(path, exeName) == wanted, QStringLiteral("Shader module changed unexpectedly: %1").arg(path));
}
void rename(const QString &from, const QString &to)
{
    safePath(from); safePath(to);
    need(!exists(to) && QDir().rename(from, to), QStringLiteral("Cannot safely rename shader module (close the player): %1 -> %2").arg(from, to));
}
// Never recurse through a user directory or use removeRecursively(). Deletion
// only handles a fully verified, exactly four-file owned module tree.
void eraseTree(const QString &path, const Tree &wanted, const QString &exeName)
{
    if (!exists(path)) return;
    sameTree(path, wanted, exeName);
    for (auto i = wanted.cbegin(); i != wanted.cend(); ++i) need(QFile::remove(QDir(path).filePath(i.key())), "Cannot remove verified shader staging file.");
    need(QDir().rmdir(path), "Cannot remove verified shader staging directory.");
}
struct Runtime {
    QMap<QString, QByteArray> bytes;
    QMap<QString, QString> paths;
};
Runtime runtimeFor(const QString &exe, const ShaderPackage::Payload &p)
{
    Runtime runtime;
    const QDir dir = QFileInfo(exe).dir();
    const auto adjacent = entries(dir.absolutePath(), 4096);
    auto deps = ShaderPackage::dependencies(p.dll, true);
    need(deps.ok, deps.error); QStringList pending = deps.modules;
    qint64 total = 0;
    while (!pending.isEmpty()) {
        const auto name = pending.takeFirst();
        if (ShaderPackage::systemModule(name)) {
            // Never accidentally endorse an adjacent system-DLL proxy.
            for (const auto &entry : adjacent)
                need(entry.compare(name, Qt::CaseInsensitive) != 0, QStringLiteral("Adjacent system-DLL override is outside shader package support: %1").arg(name));
            continue;
        }
        if (runtime.bytes.contains(name)) continue;
        need(runtime.bytes.size() < 64, "Too many runtime dependencies.");
        QString path;
        for (const auto &entry : adjacent) {
            if (entry.compare(name, Qt::CaseInsensitive) != 0) continue;
            need(path.isEmpty(), "Case-ambiguous player runtime dependency."); path = dir.filePath(entry);
        }
#ifdef Q_OS_WIN
        if (path.isEmpty() && (name.startsWith(QLatin1String("msvcp140")) || name.startsWith(QLatin1String("vcruntime140")) || name == QLatin1String("concrt140.dll"))) {
            wchar_t system[MAX_PATH + 1]{};
            const UINT length = GetSystemDirectoryW(system, MAX_PATH);
            need(length && length < MAX_PATH, "Cannot identify the trusted Windows system runtime directory.");
            path = QDir(QString::fromWCharArray(system, int(length))).filePath(name);
        }
#endif
        need(!path.isEmpty(), QStringLiteral("Required player runtime dependency is missing: %1. No runtime DLL is installed or replaced.").arg(name));
        const auto bytes = read(path); total += bytes.size(); need(total <= 384LL * 1024 * 1024, "Runtime dependency size limit exceeded.");
        runtime.bytes.insert(name, bytes); runtime.paths.insert(name, path);
        deps = ShaderPackage::dependencies(bytes, true); need(deps.ok, name + QLatin1String(": ") + deps.error); pending += deps.modules;
    }
    return runtime;
}
void unchangedRuntime(const Runtime &runtime)
{
    for (auto i = runtime.paths.cbegin(); i != runtime.paths.cend(); ++i)
        need(read(i.value()) == runtime.bytes.value(i.key()), QStringLiteral("Player runtime changed after validation: %1").arg(i.key()));
}
QString stateHash(const QByteArray &exe, const Tree &tree)
{
    return ShaderPackage::sha256(QByteArray("bsi.shader-state.v1\nexe:") + ShaderPackage::sha256(exe).toLatin1()
        + "\nmodule:" + treeHash(tree).toLatin1() + '\n');
}
QString applyHash(const QByteArray &exe, const Tree &tree, const ShaderPackage::Payload &payload,
                  const QMap<QString, QString> &runtimeHashes)
{
    QByteArray record = QByteArray("bsi.shader-review.v1\nstate:") + stateHash(exe, tree).toLatin1()
        + "\npackage:" + ShaderPackage::payloadHash(payload).toLatin1() + '\n';
    for (auto i = runtimeHashes.cbegin(); i != runtimeHashes.cend(); ++i)
        record += i.key().toLatin1() + ':' + i.value().toLatin1() + '\n';
    return ShaderPackage::sha256(record);
}
QString hostVersion(const QString &exe, const QByteArray &image, int *major)
{
    const auto deps = ShaderPackage::dependencies(image, false); need(deps.ok, deps.error);
    QList<int> families;
    for (int m : {5, 6}) if (deps.modules.contains(QStringLiteral("qt%1core.dll").arg(m))) families << m;
    need(families.size() == 1, "Cannot identify a unique Qt release runtime imported by this player.");
    *major = families.first();
    const QDir dir = QFileInfo(exe).dir(); const auto names = entries(dir.absolutePath(), 4096);
    QString version;
    for (const auto &part : {QStringLiteral("core"), QStringLiteral("gui"), QStringLiteral("qml")}) {
        const QString name = QStringLiteral("qt%1%2.dll").arg(*major).arg(part);
        need(deps.modules.contains(name), "The player must import a complete Qt Core/Gui/Qml family.");
        QString path;
        for (const auto &entry : names) if (entry.compare(name, Qt::CaseInsensitive) == 0) {
            need(path.isEmpty(), "Case-ambiguous adjacent Qt runtime DLLs."); path = dir.filePath(entry);
        }
        need(!path.isEmpty(), QStringLiteral("Required adjacent player runtime is missing: %1. Nothing is installed to supply it.").arg(name));
        const auto found = ShaderPackage::runtimeVersion(read(path)); need(found.ok, name + QLatin1String(": ") + found.error);
        need(found.qtVersion.startsWith(QString::number(*major) + QLatin1Char('.'))
             && (version.isEmpty() || version == found.qtVersion), "Player Qt Core/Gui/Qml release versions do not agree.");
        version = found.qtVersion;
    }
    return version;
}
struct Record {
    QString exe, recovery, state;
    QByteArray before, after, journal;
    Tree oldModule, newModule;
};
QString modulePath(const QString &exe) { return QFileInfo(exe).dir().filePath(moduleName); }
QString part(const Record &r, const char *name) { return QDir(r.recovery).filePath(QString::fromLatin1(name)); }
QByteArray recordBytes(const Record &r)
{
    return json({{QStringLiteral("schema"), QStringLiteral("bsi.shader-transaction.v1")}, {QStringLiteral("exeFile"), QFileInfo(r.exe).fileName()},
        {QStringLiteral("recoveryDir"), QFileInfo(r.recovery).fileName()}, {QStringLiteral("state"), r.state},
        {QStringLiteral("beforeExeSha256"), ShaderPackage::sha256(r.before)}, {QStringLiteral("afterExeSha256"), ShaderPackage::sha256(r.after)},
        {QStringLiteral("beforeModuleSha256"), treeHash(r.oldModule)}, {QStringLiteral("afterModuleSha256"), treeHash(r.newModule)}});
}
Record load(const QString &exe)
{
    Record r; r.exe = exe; r.journal = read(exe + journalSuffix, 16384);
    const auto o = object(r.journal);
    const QString name = o.value(QStringLiteral("recoveryDir")).toString(), prefix = QFileInfo(exe).fileName() + QStringLiteral(".bsi-shaders-");
    const QString id = name.mid(prefix.size(), 36); const QUuid uuid(id);
    need(o.size() == 8 && o.value(QStringLiteral("schema")).toString() == QLatin1String("bsi.shader-transaction.v1")
             && o.value(QStringLiteral("exeFile")).toString() == QFileInfo(exe).fileName() && !uuid.isNull()
             && id == uuid.toString(QUuid::WithoutBraces) && name == prefix + id + QLatin1String(".recovery"), "Invalid shader recovery identity/path.");
    r.recovery = QFileInfo(exe).dir().filePath(name); safePath(r.recovery);
    need(QFileInfo(r.recovery).isDir(), "Shader recovery directory is missing.");
    r.state = o.value(QStringLiteral("state")).toString();
    need(r.state == QLatin1String("prepared") || r.state == QLatin1String("committed"), "Unknown shader recovery state.");
    r.before = read(part(r, "before.exe")); r.after = read(part(r, "after.exe"));
    const QString exeName = QFileInfo(exe).fileName();
    r.oldModule = readTree(part(r, "before-module"), exeName); r.newModule = readTree(part(r, "after-module"), exeName);
    // Reconstruct all hashes and every allowed key, not just a convenient field.
    need(recordBytes(r) == r.journal, "Changed/missing shader recovery snapshots or hashes.");
    const QMap<QString, Tree> allowed{{QStringLiteral("before-module"), r.oldModule}, {QStringLiteral("after-module"), r.newModule},
        {QStringLiteral("next-module"), r.newModule}, {QStringLiteral("restore-module"), r.oldModule}};
    for (const auto &entry : entries(r.recovery)) {
        if (entry == QLatin1String("before.exe") || entry == QLatin1String("after.exe")) continue;
        const auto path = QDir(r.recovery).filePath(entry);
        if (entry == QLatin1String("parked-module") || entry == QLatin1String("replaced-module")) {
            const auto tree = readTree(path, exeName);
            need(!tree.isEmpty() && (tree == r.oldModule || tree == r.newModule), "Unexpected parked module in recovery.");
        } else {
            need(allowed.contains(entry) && !allowed.value(entry).isEmpty(), "Unexpected files in shader recovery directory.");
            sameTree(path, allowed.value(entry), exeName);
        }
    }
    return r;
}
void currentKnown(const Record &r)
{
    const auto exe = read(r.exe);
    const auto tree = readTree(modulePath(r.exe), QFileInfo(r.exe).fileName());
    need(exe == r.before || exe == r.after, "EXE changed outside the transaction; recovery will not overwrite it.");
    need(tree.isEmpty() || tree == r.oldModule || tree == r.newModule, "Module changed outside the transaction; recovery will not overwrite it.");
    if (tree.isEmpty() && !r.oldModule.isEmpty() && !r.newModule.isEmpty())
        need(exists(part(r, "parked-module")) || exists(part(r, "replaced-module")), "An unexplained missing module is not a recognized transaction state.");
}
// Called only after the journal/snapshots and BOTH current objects have passed
// validation. Retain snapshots on any rollback failure; never clobber a conflict.
void setModule(const Record &r, const Tree &wanted)
{
    const QString exeName = QFileInfo(r.exe).fileName(), path = modulePath(r.exe);
    auto now = readTree(path, exeName);
    if (now == wanted) return;
    if (!wanted.isEmpty()) {
        if (exists(part(r, "restore-module"))) sameTree(part(r, "restore-module"), wanted, exeName);
        else writeTree(part(r, "restore-module"), wanted);
    }
    closedPlayer(r.exe); currentKnown(r);
    if (!now.isEmpty()) {
        if (exists(part(r, "replaced-module"))) {
            const auto previous = readTree(part(r, "replaced-module"), exeName);
            need(previous == r.oldModule || previous == r.newModule, "Unexpected recovery parking directory.");
            eraseTree(part(r, "replaced-module"), previous, exeName);
        }
        sameTree(path, now, exeName); rename(path, part(r, "replaced-module")); BSI_CHECKPOINT(RecoveryParked);
    }
    if (!wanted.isEmpty()) { rename(part(r, "restore-module"), path); BSI_CHECKPOINT(RecoveryPublished); }
    sameTree(path, wanted, exeName);
}
bool cleanup(const Record &r)
{
    // Remove the external journal FIRST. Interruption after this point can only
    // leave inert backup files, not an unrecoverable half-deleted journal.
    need(read(r.exe + journalSuffix, 16384) == r.journal, "Recovery journal changed before cleanup.");
    if (!QFile::remove(r.exe + journalSuffix)) return false;
    bool ok = true;
    const QString exeName = QFileInfo(r.exe).fileName();
    for (const auto &entry : entries(r.recovery)) {
        if (entry == QLatin1String("before.exe") || entry == QLatin1String("after.exe")) continue;
        const QString path = QDir(r.recovery).filePath(entry);
        const auto tree = readTree(path, exeName);
        need(tree == r.oldModule || tree == r.newModule, "Unexpected module during verified recovery cleanup.");
        eraseTree(path, tree, exeName);
    }
    for (const auto &name : {"before.exe", "after.exe"}) ok = QFile::remove(part(r, name)) && ok;
    return QDir().rmdir(r.recovery) && ok;
}
Result reconcile(Record r)
{
    Result result; result.recoveryDirectory = r.recovery;
    currentKnown(r);
    const bool committed = r.state == QLatin1String("committed");
    const auto &goal = committed ? r.after : r.before;
    const auto &module = committed ? r.newModule : r.oldModule;
    if (committed) {
        need(read(r.exe) == goal && readTree(modulePath(r.exe), QFileInfo(r.exe).fileName()) == module,
             "A committed installation was modified; recovery refuses to roll it back or overwrite it.");
    } else {
#ifdef BSI_SHADER_INSTALL_TESTING
        need(!testRecoveryFailure, "Injected rollback failure; preserve all recovery snapshots.");
#endif
        closedPlayer(r.exe);
        result.changed = read(r.exe) != goal || readTree(modulePath(r.exe), QFileInfo(r.exe).fileName()) != module;
        // Publish a restored DLL before an active EXE; remove the bootstrap
        // before removing the installed DLL on a rolled-back fresh install.
        if (!module.isEmpty()) setModule(r, module);
        const auto now = read(r.exe);
        need(now == r.before || now == r.after, "EXE changed during recovery; it will not be overwritten.");
        if (now != goal) { replace(r.exe, now, goal); BSI_CHECKPOINT(RecoveryExe); }
        if (module.isEmpty()) setModule(r, module);
        need(read(r.exe) == goal && readTree(modulePath(r.exe), QFileInfo(r.exe).fileName()) == module, "Rollback pair verification failed.");
        result.rolledBack = true;
    }
    result.ok = true;
    try { result.cleanupPending = !cleanup(r); }
    catch (const std::exception &e) { result.cleanupPending = true; result.error = QString::fromUtf8(e.what()); }
    return result;
}
Result operate(const QString &exe, const ShaderPackage::Payload *payload, const QString &expectedExe,
               const QString &expectedPackage, bool acknowledged, const QString &expectedReview)
{
    Result result; Record r; r.exe = exe;
    bool journalReady = false;
    try {
        need(acknowledged && hashValid(expectedExe), "Review/acknowledge the trusted package, closed-player, signature and recovery risks first.");
        safePath(exe); closedPlayer(exe);
        need(!exists(exe + journalSuffix), "A shader transaction journal exists. Recover it before installing/updating/removing.");
        r.before = read(exe); need(ShaderPackage::sha256(r.before) == expectedExe, "EXE changed since inspection; nothing was installed.");
        const auto image = ShaderSettingsImage::prepare(r.before, payload != nullptr); need(image.ok, image.error);
        r.after = image.output;
        const QString exeName = QFileInfo(exe).fileName();
        r.oldModule = readTree(modulePath(exe), exeName);
        Runtime runtime;
        if (payload) {
            need(hashValid(expectedPackage) && ShaderPackage::payloadHash(*payload) == expectedPackage, "Package changed since its trusted SHA-256 was reviewed.");
            runtime = runtimeFor(exe, *payload);
            const auto valid = ShaderPackage::validate(r.before, *payload, runtime.bytes); need(valid.ok, valid.error);
            need(expectedReview.isEmpty() || (hashValid(expectedReview)
                && expectedReview == applyHash(r.before, r.oldModule, *payload, valid.runtimeSha256)),
                "The reviewed EXE/module/runtime set changed. Inspect again; nothing was installed.");
            r.newModule = owned(*payload, exeName, valid.qtVersion);
            // Existing owned source and module must form a complete pair. Never
            // claim/overwrite an adjacent user's directory to repair an orphan.
            const auto absent = ShaderSettingsImage::prepare(r.before, false); need(absent.ok, absent.error);
            need(absent.changed == !r.oldModule.isEmpty(), "Orphan shader source/module; reconcile it before installing or updating.");
        } else {
            need(expectedReview.isEmpty() || (hashValid(expectedReview) && expectedReview == stateHash(r.before, r.oldModule)),
                 "The reviewed EXE/module set changed. Inspect again; nothing was removed.");
            need(image.changed || r.oldModule.isEmpty(), "Owned module exists without its native source overlay; removal is refused.");
        }
        if (r.before == r.after && r.oldModule == r.newModule) { result.ok = true; return result; }
        r.recovery = exe + QStringLiteral(".bsi-shaders-") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".recovery");
        result.recoveryDirectory = r.recovery; safePath(r.recovery);
        need(QDir().mkdir(r.recovery), "Cannot exclusively create shader recovery directory.");
        exclusive(part(r, "before.exe"), r.before); exclusive(part(r, "after.exe"), r.after);
        if (!r.oldModule.isEmpty()) writeTree(part(r, "before-module"), r.oldModule);
        if (!r.newModule.isEmpty()) { writeTree(part(r, "after-module"), r.newModule); writeTree(part(r, "next-module"), r.newModule); }
        r.state = QStringLiteral("prepared"); r.journal = recordBytes(r);
        exclusive(exe + journalSuffix, r.journal); journalReady = true;
        // Re-read the journal and EVERY snapshot before any target mutation.
        r = load(exe); currentKnown(r); unchangedRuntime(runtime); closedPlayer(exe); BSI_CHECKPOINT(Journal);
        if (!payload && r.before != r.after) { replace(exe, r.before, r.after); BSI_CHECKPOINT(Exe); }
        if (!r.oldModule.isEmpty()) {
            sameTree(modulePath(exe), r.oldModule, exeName); closedPlayer(exe);
            rename(modulePath(exe), part(r, "parked-module")); BSI_CHECKPOINT(Parked);
        }
        if (!r.newModule.isEmpty()) { closedPlayer(exe); rename(part(r, "next-module"), modulePath(exe)); }
        BSI_CHECKPOINT(Published);
        if (payload && r.before != r.after) {
            unchangedRuntime(runtime); closedPlayer(exe); replace(exe, r.before, r.after); BSI_CHECKPOINT(Exe);
        }
        need(read(exe) == r.after && readTree(modulePath(exe), exeName) == r.newModule, "Post-installation pair read-back failed.");
        unchangedRuntime(runtime);
        const auto prepared = r.journal; r.state = QStringLiteral("committed"); r.journal = recordBytes(r);
        replace(exe + journalSuffix, prepared, r.journal); BSI_CHECKPOINT(Committed);
        result.ok = true; result.changed = true;
        try { result.cleanupPending = !cleanup(r); } catch (const std::exception &e) { result.cleanupPending = true; result.error = QString::fromUtf8(e.what()); }
        return result;
    } catch (const std::exception &e) {
        result.error = QString::fromUtf8(e.what());
        // Even a partial journal write must be retained. No target mutation is
        // permitted before successful journal read-back, so the user can inspect
        // these pre-commit staging remnants without a falsely installed status.
        if (!journalReady) { result.cleanupPending = !r.recovery.isEmpty(); result.recoveryRequired = exists(exe + journalSuffix); return result; }
#ifdef BSI_SHADER_INSTALL_TESTING
        if (testInterrupt) { result.recoveryRequired = true; result.changed = true; return result; }
#endif
        try {
            const auto recovered = reconcile(load(exe));
            result.rolledBack = recovered.rolledBack; result.cleanupPending = recovered.cleanupPending;
            if (!recovered.rolledBack) { result.ok = true; result.changed = true; }
        } catch (const std::exception &recovery) {
            result.error += QStringLiteral(" Recovery could not be verified; do not launch the player. ") + QString::fromUtf8(recovery.what());
            result.recoveryRequired = true; result.changed = true;
        }
        return result;
    }
}
template<class Operation> Result locked(const QString &exePath, Operation operation)
{
    const QString exe = absolute(exePath);
    try {
        safePath(exe); closedPlayer(exe);
        const QString lockPath = exe + QStringLiteral(".bsi-shaders.lock"); safePath(lockPath);
        QLockFile lock(lockPath); lock.setStaleLockTime(0);
        need(lock.tryLock(0), "Another shader transaction/recovery owns this player lock.");
        return operation(exe);
    } catch (const std::exception &e) { Result r; r.error = QString::fromUtf8(e.what()); return r; }
}
} // namespace
Inspection inspect(const QString &exePath, const ShaderPackage::Payload *payload)
{
    Inspection result;
    try {
        need(!exePath.trimmed().isEmpty(), "Select HD-Player.exe first.");
        const QString exe = absolute(exePath); safePath(exe);
        const auto image = read(exe); result.exeSha256 = ShaderPackage::sha256(image);
        QString processError;
        try { closedPlayer(exe); result.playerClosed = true; }
        catch (const std::exception &e) { processError = QString::fromUtf8(e.what()); }
        if (exists(exe + journalSuffix)) {
            result.recoveryRequired = true;
            try {
                const auto r = load(exe); currentKnown(r);
                if (r.state == QLatin1String("committed"))
                    need(image == r.after && readTree(modulePath(exe), QFileInfo(exe).fileName()) == r.newModule,
                         "Committed shader files changed; recovery will not overwrite them.");
                result.journalSha256 = ShaderPackage::sha256(r.journal);
                result.recoveryDirectory = r.recovery;
                result.canRecover = result.playerClosed;
                result.recoveryError = processError;
            } catch (const std::exception &e) { result.recoveryError = QString::fromUtf8(e.what()); }
            result.error = QStringLiteral("An interrupted shader transaction needs recovery before installation, removal or player launch.");
            return result;
        }
        const auto tree = readTree(modulePath(exe), QFileInfo(exe).fileName());
        result.modulePresent = !tree.isEmpty();
        result.stateSha256 = stateHash(image, tree);
        if (!tree.isEmpty()) result.installedPackageSha256 = ShaderPackage::payloadHash(
            {tree.value(QStringLiteral("BSIShaders.dll")), tree.value(QStringLiteral("qmldir"))});
        const auto removal = ShaderSettingsImage::prepare(image, false); need(removal.ok, removal.error);
        result.qtMajor = removal.qtMajor;
        result.sourcePresent = removal.changed;
        result.installed = result.sourcePresent && result.modulePresent;
        result.ok = true;
        if (result.modulePresent && !result.sourcePresent) {
            result.applyError = result.removeError = QStringLiteral("Owned module exists without its native source overlay. Automatic overwrite/removal is refused; restore a matching saved pair first.");
            return result;
        }
        result.canRemove = result.sourcePresent && result.playerClosed;
        if (!processError.isEmpty()) result.removeError = processError;
        else if (!result.sourcePresent) result.removeError = QStringLiteral("No native shader integration is installed.");
        try { result.qtVersion = hostVersion(exe, image, &result.qtMajor); }
        catch (const std::exception &e) { result.runtimeError = QString::fromUtf8(e.what()); }
        if (!payload) return result;
        result.packageSha256 = ShaderPackage::payloadHash(*payload);
        try {
            need(result.sourcePresent == result.modulePresent,
                 "Native shader sources exist without their owned module. Remove that inactive integration before reinstalling.");
            const auto addition = ShaderSettingsImage::prepare(image, true); need(addition.ok, addition.error);
            const auto runtime = runtimeFor(exe, *payload);
            const auto valid = ShaderPackage::validate(image, *payload, runtime.bytes); need(valid.ok, valid.error);
            result.qtVersion = valid.qtVersion;
            result.moduleQtVersion = valid.moduleQtVersion;
            result.publicQtApi = valid.publicQtApi;
            result.runtimeSha256 = valid.runtimeSha256;
            result.applyReviewSha256 = applyHash(image, tree, *payload, valid.runtimeSha256);
            result.updateAvailable = result.installed && result.packageSha256 != result.installedPackageSha256;
            result.canApply = result.playerClosed;
            result.applyError = processError;
        } catch (const std::exception &e) { result.applyError = QString::fromUtf8(e.what()); }
    } catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); }
    return result;
}
Result apply(const QString &exe, const ShaderPackage::Payload &payload, const QString &expectedExe,
             const QString &expectedPackage, bool acknowledged, const QString &expectedReview)
{
    return locked(exe, [&](const QString &path) { return operate(path, &payload, expectedExe, expectedPackage, acknowledged, expectedReview); });
}
Result remove(const QString &exe, const QString &expectedExe, bool acknowledged, const QString &expectedState)
{
    return locked(exe, [&](const QString &path) { return operate(path, nullptr, expectedExe, {}, acknowledged, expectedState); });
}
Result recover(const QString &exePath, const QString &expectedJournal, bool acknowledged)
{
    return locked(exePath, [&](const QString &exe) {
        Result result;
        try {
            if (!exists(exe + journalSuffix)) { result.ok = true; return result; }
            need(acknowledged && hashValid(expectedJournal), "Inspect and acknowledge this shader recovery first.");
            const auto record = load(exe); result.recoveryDirectory = record.recovery;
            need(ShaderPackage::sha256(record.journal) == expectedJournal,
                 "The recovery journal changed since inspection. Nothing was recovered; inspect again.");
            return reconcile(record);
        } catch (const std::exception &e) { result.error = QString::fromUtf8(e.what()); result.recoveryRequired = true; return result; }
    });
}
#ifdef BSI_SHADER_INSTALL_TESTING
void setTestFault(Checkpoint at, bool interrupt, bool failRecovery)
{
    testAt = at; testInterrupt = interrupt; testRecoveryFailure = failRecovery;
}
#endif
} // namespace ShaderInstall
