#include "DllStartup.h"
#include "DllStartupImport.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>

namespace DllStartup
{
namespace
{
constexpr qint64 fileLimit = 128LL * 1024 * 1024;
const QString journalSuffix = QStringLiteral(".bsi-startup-dll.json");
const QString schema = QStringLiteral("bsi.dll-startup.v1");
#ifdef BSI_DLLSTARTUP_TESTING
Fault testFault = Fault::None;
#endif

QString sha(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
bool hashValid(const QString &s)
{
    static const QRegularExpression rx(QStringLiteral("^[0-9a-f]{64}$"));
    return rx.match(s).hasMatch();
}
bool exists(const QString &p)
{
    const QFileInfo f(p);
    return f.exists() || f.isSymLink();
}
QString absolute(const QString &p) { return QDir::cleanPath(QFileInfo(p).absoluteFilePath()); }
bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}
bool readFile(const QString &p, QByteArray &b, QString &error, qint64 max = fileLimit)
{
    const QFileInfo info(p);
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > max ||
        !samePath(absolute(p), info.canonicalFilePath())) {
        error = QStringLiteral("Not a regular, non-symlink file within the size limit: %1").arg(p);
        return false;
    }
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot read %1: %2").arg(p, f.errorString());
        return false;
    }
    b = f.read(max + 1);
    if (f.error() != QFileDevice::NoError || b.size() != info.size() || b.size() > max || !f.atEnd()) {
        error = QStringLiteral("File changed or could not be completely read: %1").arg(p);
        return false;
    }
    return true;
}
QVariantMap fail(const QString &s) { return {{"ok", false}, {"msg", s}, {"changed", false}}; }
DllStartupImport::Bytes bytes(const QByteArray &b)
{
    const auto *p = reinterpret_cast<const std::uint8_t *>(b.constData());
    return {p, p + b.size()};
}
QByteArray byteArray(const DllStartupImport::Bytes &b)
{
    return QByteArray(reinterpret_cast<const char *>(b.data()), qsizetype(b.size()));
}
bool validName(const QString &name) { return DllStartupImport::validName(name.toUtf8().toStdString()); }
bool inputs(const QString &exe, const QString &name, QByteArray &target, QByteArray &dll, QString &error)
{
    if (!validName(name)) {
        error = QStringLiteral(
            "Enter only an ASCII DLL filename, such as zlag.dll; no paths, spaces or reserved device names.");
        return false;
    }
    return readFile(exe, target, error) && readFile(QFileInfo(exe).dir().filePath(name), dll, error);
}
DllStartupImport::Plan prepare(const QByteArray &target, const QByteArray &dll, const QString &name)
{
    return DllStartupImport::prepare(bytes(target), bytes(dll), name.toUtf8().toStdString());
}

// NewOnly is essential: neither a user backup nor an interrupted transaction
// may ever be truncated. All recovery material is read back before EXE commit.
bool exclusiveFile(const QString &p, const QByteArray &data, QString &error)
{
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        error = QStringLiteral("Will not replace an existing backup/journal, or cannot create %1: %2")
                    .arg(p, f.errorString());
        return false;
    }
    if (f.write(data) != data.size() || !f.flush()) {
        error = QStringLiteral("Could not finish recovery file %1: %2. The EXE was not changed.")
                    .arg(p, f.errorString());
        return false;
    }
    f.close();
    QByteArray check;
    if (!readFile(p, check, error, qMax<qint64>(data.size(), 16384)) || check != data) {
        error = QStringLiteral("Recovery file verification failed: %1. The EXE was not changed.").arg(p);
        return false;
    }
    return true;
}

struct Record {
    QString backup, originalSha, patchedSha, dllSha, dllName;
    QByteArray original;
};
bool loadRecord(const QString &exe, Record &r, QString &error)
{
    QByteArray data;
    if (!readFile(exe + journalSuffix, data, error, 16384))
        return false;
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(data, &parseError);
    const auto o = doc.object();
    const QString backup = o.value("backupFile").toString();
    const QString prefix = QFileInfo(exe).fileName() + QStringLiteral(".bsi-startup-dll-");
    const QString id = backup.mid(prefix.size(), 36);
    const QUuid uuid(id);
    r.originalSha = o.value("originalSha256").toString();
    r.patchedSha = o.value("patchedSha256").toString();
    r.dllSha = o.value("dllSha256").toString();
    r.dllName = o.value("dllName").toString();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
        o.value("schema").toString() != schema ||
        o.value("exeFile").toString() != QFileInfo(exe).fileName() || uuid.isNull() ||
        id != uuid.toString(QUuid::WithoutBraces) || backup != prefix + id + QStringLiteral(".backup") ||
        !hashValid(r.originalSha) || !hashValid(r.patchedSha) || r.originalSha == r.patchedSha ||
        !hashValid(r.dllSha) || !validName(r.dllName)) {
        error = QStringLiteral("Invalid startup-DLL recovery journal. No file was changed; preserve it and "
                               "the original backup for manual recovery.");
        return false;
    }
    r.backup = QFileInfo(exe).dir().filePath(backup);
    if (!readFile(r.backup, r.original, error) || sha(r.original) != r.originalSha) {
        error = QStringLiteral(
                    "Startup-DLL backup is missing, changed or unreadable: %1. Nothing was overwritten.")
                    .arg(r.backup);
        return false;
    }
    return true;
}

// Atomic staging only; direct-write fallback is deliberately forbidden. A stale
// target is rejected immediately before commit as well as before preparation.
bool replace(const QString &path, const QByteArray &expected, const QByteArray &replacement, bool &committed,
             QString &error, bool rollback = false)
{
    committed = false;
#ifdef BSI_DLLSTARTUP_TESTING
    if ((testFault == Fault::BeforeCommit && !rollback) || (testFault == Fault::Rollback && rollback)) {
        error = QStringLiteral("Injected pre-commit I/O failure");
        return false;
    }
#else
    Q_UNUSED(rollback);
#endif
    QByteArray current;
    if (!readFile(path, current, error) || current != expected) {
        error =
            QStringLiteral("Target changed or became unreadable before commit; refusing to overwrite it.");
        return false;
    }
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly) || f.write(replacement) != replacement.size() || !f.flush() ||
        !readFile(path, current, error) || current != expected) {
        error = QStringLiteral("Could not stage a complete atomic replacement, or target changed: %1")
                    .arg(f.errorString());
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        error = QStringLiteral("Atomic commit failed (close the emulator and check permissions): %1")
                    .arg(f.errorString());
        return false;
    }
    committed = true;
#ifdef BSI_DLLSTARTUP_TESTING
    if (!rollback && (testFault == Fault::AfterCommitVerification || testFault == Fault::Rollback)) {
        error = QStringLiteral("Injected post-commit verification failure");
        return false;
    }
#endif
    if (!readFile(path, current, error) || current != replacement) {
        error = QStringLiteral("Full EXE read-back verification failed.");
        return false;
    }
    return true;
}
QVariantMap transact(const QString &exe, const QByteArray &before, const QByteArray &after,
                     const QString &backup)
{
    bool committed = false;
    QString error;
    if (replace(exe, before, after, committed, error))
        return {{"ok", true}, {"changed", true}};
    if (!committed)
        return fail(error + QStringLiteral(" The EXE was not committed; recovery material was retained."));
    bool rollbackCommitted = false;
    QString rollbackError;
    const bool recovered = replace(exe, after, before, rollbackCommitted, rollbackError, true);
    auto r =
        fail(recovered ? error + QStringLiteral(" The previous EXE was restored and verified automatically.")
                       : error + QStringLiteral(" Automatic rollback could not be verified. Do not launch "
                                                "the player. Recover from %1. %2")
                                     .arg(backup, rollbackError));
    r.insert("changed", !recovered);
    r.insert("recoveryRequired", !recovered);
    r.insert("rolledBack", recovered);
    r.insert("backup", backup);
    return r;
}

QString warning()
{
    return QStringLiteral(
        "Only patch software you are authorized to modify. Close every player instance first. "
        "Changing the EXE invalidates its Authenticode signature. The named DLL and its compatible "
        "dependencies "
        "must remain available or Windows can refuse startup. This loads the DLL and runs its normal DLL "
        "initialization; "
        "it does NOT call zlag_init or any other exported function. Only use a trusted DLL designed for "
        "startup loading.");
}
} // namespace

QVariantMap inspect(const QString &exePath, const QString &dllName)
{
    const QString exe = absolute(exePath);
    auto result = fail(QString());
    result.insert("warning", warning());
    result.insert("dllName", dllName);
    result.insert("canApply", false);
    result.insert("canRestore", false);
    result.insert("hasJournal", exists(exe + journalSuffix));
    QString error;
    QByteArray target, dll;
    if (exists(exe + journalSuffix)) {
        Record record;
        if (!loadRecord(exe, record, error)) {
            result.insert("msg", error);
            return result;
        }
        result.insert("backup", record.backup);
        if (readFile(exe, target, error)) {
            const auto current = sha(target);
            result.insert("canRestore", current == record.originalSha || current == record.patchedSha);
            result.insert("recordState", current == record.patchedSha    ? "applied"
                                         : current == record.originalSha ? "prepared-or-restored"
                                                                         : "conflict");
        }
    }
    if (!inputs(exe, dllName, target, dll, error)) {
        result.insert("msg", error);
        return result;
    }
    const auto plan = prepare(target, dll, dllName);
    if (!plan.ok) {
        result.insert("msg", QString::fromStdString(plan.error));
        return result;
    }
    result.insert("ok", true);
    result.insert("alreadyApplied", plan.alreadyApplied);
    result.insert("canApply", !plan.alreadyApplied && !result.value("hasJournal").toBool());
    result.insert("exeSha256", sha(target));
    result.insert("dllSha256", sha(dll));
    result.insert("outputSha256", sha(byteArray(plan.output)));
    result.insert("importsBefore", qulonglong(plan.importsBefore));
    result.insert("importsAfter", qulonglong(plan.importsAfter));
    QStringList dependencies;
    for (const auto &name : plan.dependencies)
        dependencies << QString::fromStdString(name);
    result.insert("dllDirectImports", dependencies);
    result.insert(
        "msg",
        plan.alreadyApplied
            ? QStringLiteral(
                  "This EXE already has the startup dependency. No export is called. Keep %1 beside it.")
                  .arg(dllName)
        : result.value("hasJournal").toBool()
            ? QStringLiteral("Recovery material already exists. Restore/reconcile it before applying; "
                             "nothing will be overwritten.")
            : QStringLiteral(
                  "Ready to add %1 as a normal startup dependency. Review the warnings before applying.")
                  .arg(dllName));
    return result;
}

QVariantMap apply(const QString &exePath, const QString &dllName, const QString &expectedExeSha256,
                  const QString &expectedDllSha256, bool riskAcknowledged)
{
    if (!riskAcknowledged)
        return fail(QStringLiteral("Review and acknowledge the startup-DLL/signature warnings first."));
    if (!hashValid(expectedExeSha256) || !hashValid(expectedDllSha256))
        return fail(
            QStringLiteral("Inspect the selected EXE and DLL first; a fresh hash-bound review is required."));
    const QString exe = absolute(exePath);
    QLockFile lock(exe + QStringLiteral(".bsi-startup-dll.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return fail(QStringLiteral("Another startup-DLL operation holds the lock. No files were changed."));
    QByteArray target, dll;
    QString error;
    if (!inputs(exe, dllName, target, dll, error))
        return fail(error);
    if (sha(target) != expectedExeSha256 || sha(dll) != expectedDllSha256)
        return fail(
            QStringLiteral("The EXE or DLL changed since inspection. Inspect again; nothing was written."));
    const auto plan = prepare(target, dll, dllName);
    if (!plan.ok)
        return fail(QString::fromStdString(plan.error));
    if (plan.alreadyApplied) {
        if (exists(exe + journalSuffix)) {
            Record record;
            if (!loadRecord(exe, record, error))
                return fail(error);
            if (record.patchedSha != sha(target) || record.dllSha != sha(dll) || record.dllName != dllName)
                return fail(QStringLiteral("The recorded EXE/DLL no longer matches. Nothing was changed; "
                                           "review recovery before continuing."));
        }
        return {{"ok", true},
                {"changed", false},
                {"alreadyApplied", true},
                {"msg", QStringLiteral("The identical startup dependency is already installed; the EXE and "
                                       "backups were not changed.")}};
    }
    if (exists(exe + journalSuffix))
        return fail(QStringLiteral("A startup-DLL recovery journal already exists. Restore/reconcile it "
                                   "first; backups are never overwritten."));
    const QByteArray output = byteArray(plan.output);
    const QString backup = exe + QStringLiteral(".bsi-startup-dll-") +
                           QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".backup");
    if (!exclusiveFile(backup, target, error))
        return fail(error);
    const QJsonObject record{{"schema", schema},
                             {"exeFile", QFileInfo(exe).fileName()},
                             {"backupFile", QFileInfo(backup).fileName()},
                             {"dllName", dllName},
                             {"originalSha256", sha(target)},
                             {"patchedSha256", sha(output)},
                             {"dllSha256", sha(dll)},
                             {"createdUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    if (!exclusiveFile(exe + journalSuffix, QJsonDocument(record).toJson(), error)) {
        auto r = fail(error + QStringLiteral(" Original backup retained at %1.").arg(backup));
        r.insert("backup", backup);
        return r;
    }
    // Recheck the DLL too: never commit a plan for different dependency bytes.
    QByteArray checkedDll;
    if (!readFile(QFileInfo(exe).dir().filePath(dllName), checkedDll, error) || checkedDll != dll)
        return fail(QStringLiteral(
            "DLL changed before commit. The EXE was not changed; recovery material was retained."));
    auto result = transact(exe, target, output, backup);
    result.insert("backup", backup);
    if (result.value("ok").toBool()) {
        result.insert(
            "msg",
            QStringLiteral(
                "Startup import installed and the complete EXE verified. Normal launches now require %1; "
                "distribute the EXE WITH that DLL and its dependencies. No Python, Node, script, recurring "
                "launcher or proxy is used. "
                "This does not prove your DLL's functionality or call an exported initializer.")
                .arg(dllName));
    }
    return result;
}

QVariantMap restore(const QString &exePath)
{
    const QString exe = absolute(exePath);
    QLockFile lock(exe + QStringLiteral(".bsi-startup-dll.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return fail(QStringLiteral("Another startup-DLL operation holds the lock. No files were changed."));
    Record record;
    QString error;
    if (!loadRecord(exe, record, error))
        return fail(error);
    QByteArray target;
    if (!readFile(exe, target, error))
        return fail(error);
    const auto current = sha(target);
    if (current != record.patchedSha && current != record.originalSha)
        return fail(
            QStringLiteral(
                "EXE changed after the startup-DLL transaction (for example an update or other edit). "
                "Automatic restore would discard those changes, so it was refused. Original backup: %1")
                .arg(record.backup));
    auto result = current == record.originalSha ? QVariantMap{{"ok", true}, {"changed", false}}
                                                : transact(exe, target, record.original, record.backup);
    if (!result.value("ok").toBool())
        return result;
    // Retain both the original and its journal permanently, under a unique
    // snapshot name. A later apply creates a new snapshot, never reuses it.
    const bool archived = QFile::rename(exe + journalSuffix, record.backup + QStringLiteral(".json"));
    result.insert("backup", record.backup);
    result.insert("journalArchived", archived);
    result.insert(
        "msg", archived
                   ? QStringLiteral("Original EXE restored and verified byte-for-byte. The DLL was not "
                                    "modified/deleted, and the unique backup was retained.")
                   : QStringLiteral(
                         "Original EXE is verified restored, but the recovery journal could not be archived. "
                         "It and the backup were retained; resolve the journal before applying again."));
    return result;
}

#ifdef BSI_DLLSTARTUP_TESTING
void setTestFault(Fault fault) { testFault = fault; }
#endif
} // namespace DllStartup
