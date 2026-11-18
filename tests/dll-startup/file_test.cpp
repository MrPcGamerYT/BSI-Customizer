#include "Fixture.h"
#include "core/DllStartup.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>

#include <cstdio>

namespace
{
int failures = 0;
void check(bool pass, const char *what)
{
    std::printf("  %s  %s\n", pass ? "PASS" : "FAIL", what);
    if (!pass) {
        ++failures;
        if (qEnvironmentVariableIsSet("GITHUB_ACTIONS"))
            std::printf("::error::DLL startup: %s\n", what);
    }
}
QByteArray read(const QString &p)
{
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}
bool write(const QString &p, const QByteArray &data)
{
    QFile f(p);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.flush();
}
QString hash(const QByteArray &b)
{
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}
QByteArray array(const DllStartupImport::Bytes &b)
{
    return QByteArray(reinterpret_cast<const char *>(b.data()), qsizetype(b.size()));
}
const QString name = QStringLiteral("zlag.dll");
const QString suffix = QStringLiteral(".bsi-startup-dll.json");
struct Fixture {
    QTemporaryDir temp{QDir::tempPath() + QString::fromUtf8("/bsi-dll-\xCE\x94-XXXXXX")};
    QString exe = temp.filePath(QStringLiteral("HD-Player.exe"));
    QString dll = temp.filePath(name);
    QByteArray original = array(DllStartupFixture::image());
    QByteArray payload = array(DllStartupFixture::image(true));
    Fixture()
    {
        check(temp.isValid() && write(exe, original) && write(dll, payload),
              "create isolated Unicode-path PE-data fixtures");
    }
    QVariantMap inspect() const { return DllStartup::inspect(exe, name); }
    QVariantMap apply(const QVariantMap &review = {}) const
    {
        const auto r = review.isEmpty() ? inspect() : review;
        return DllStartup::apply(exe, name, r.value("exeSha256").toString(), r.value("dllSha256").toString(),
                                 true);
    }
};
void basicTests()
{
    Fixture f;
    const auto review = f.inspect();
    check(review.value("ok").toBool() && review.value("canApply").toBool() &&
              !review.value("canRestore").toBool(),
          "inspection is a read-only hash-bound plan");
    check(QDir(f.temp.path()).entryList(QDir::Files).size() == 2,
          "inspection creates no backup, journal or helper");
    auto r = DllStartup::apply(f.exe, name, review.value("exeSha256").toString(),
                               review.value("dllSha256").toString(), false);
    check(!r.value("ok").toBool() && read(f.exe) == f.original, "explicit risk acknowledgement is mandatory");
    check(!DllStartup::apply(f.exe, name, {}, {}, true).value("ok").toBool(),
          "apply without a reviewed hash is refused");
    const auto sentinel = f.exe + QStringLiteral(".bak");
    check(write(sentinel, "user backup"), "create unrelated user backup");
    r = f.apply(review);
    if (!r.value("ok").toBool())
        std::fprintf(stderr, "%s\n", qPrintable(r.value("msg").toString()));
    check(r.value("ok").toBool() && r.value("changed").toBool(), "native apply commits verified EXE");
    const QString backup = r.value("backup").toString();
    const QByteArray patched = read(f.exe);
    check(hash(patched) == review.value("outputSha256").toString(),
          "entire committed EXE matches the reviewed plan");
    check(read(backup) == f.original && read(sentinel) == "user backup" && read(f.dll) == f.payload,
          "unique original backup, user's existing backup and DLL bytes are preserved");
    const auto installed = f.inspect();
    check(installed.value("alreadyApplied").toBool() && installed.value("canRestore").toBool() &&
              !installed.value("canApply").toBool(),
          "inspect recognizes applied state and verified recovery");
    const auto again = f.apply(installed);
    check(again.value("ok").toBool() && !again.value("changed").toBool() && read(f.exe) == patched &&
              read(backup) == f.original,
          "repeat apply never repatches or clobbers a backup");
    const auto different =
        DllStartup::apply(f.exe, name, hash(patched), hash(f.payload), true, QStringLiteral("other_init"));
    check(
        !different.value("ok").toBool() && read(f.exe) == patched && read(backup) == f.original,
        "another valid requested export on the same installed DLL is refused without changing target/backup");
    check(QFile::remove(f.dll), "remove required DLL in disposable fixture");
    check(f.inspect().value("canRestore").toBool(), "missing DLL does not block recovery inspection");
    r = DllStartup::restore(f.exe);
    check(r.value("ok").toBool() && read(f.exe) == f.original && read(backup) == f.original &&
              !QFileInfo::exists(f.exe + suffix) && QFileInfo::exists(backup + ".json"),
          "restore works without DLL; original and archived journal remain byte-safe");
    check(write(f.dll, f.payload), "replace test DLL for another transaction");
    r = f.apply();
    check(r.value("ok").toBool() && r.value("backup").toString() != backup && read(backup) == f.original,
          "a later apply creates a distinct backup instead of reusing the old one");
    check(DllStartup::restore(f.exe).value("ok").toBool() && read(f.dll) == f.payload,
          "restore does not delete or change the user's DLL");
}
void refusalTests()
{
    {
        Fixture f;
        auto malformed = DllStartupFixture::image(true);
        DllStartupFixture::put(malformed, 0x640, 0);
        write(f.dll, array(malformed));
        const auto r = DllStartup::apply(f.exe, name, hash(f.original), hash(read(f.dll)), true);
        check(!r.value("ok").toBool() && read(f.exe) == f.original && !QFileInfo::exists(f.exe + suffix),
              "null named export is refused before backup/journal/EXE writes");
        write(f.dll, f.payload);
        const auto wrongCase = DllStartup::apply(f.exe, name, hash(f.original), hash(f.payload), true,
                                                 QStringLiteral("ZLAG_INIT"));
        check(!wrongCase.value("ok").toBool() && read(f.exe) == f.original &&
                  !QFileInfo::exists(f.exe + suffix),
              "wrong export case cannot create a startup dependency that Windows cannot resolve");
    }
    {
        Fixture f;
        const auto review = f.inspect();
        auto changed = f.original;
        changed[0x450] = 'x';
        write(f.exe, changed);
        check(!f.apply(review).value("ok").toBool() && read(f.exe) == changed &&
                  !QFileInfo::exists(f.exe + suffix),
              "stale EXE review refuses all writes");
        write(f.exe, f.original);
        changed = f.payload;
        changed[0x450] = 'y';
        write(f.dll, changed);
        check(!f.apply(review).value("ok").toBool() && read(f.exe) == f.original &&
                  !QFileInfo::exists(f.exe + suffix),
              "stale DLL review refuses all writes");
    }
    {
        Fixture f;
        const auto review = f.inspect();
        write(f.exe + suffix, "user journal sentinel");
        check(!f.apply(review).value("ok").toBool() && read(f.exe) == f.original &&
                  read(f.exe + suffix) == "user journal sentinel",
              "existing recovery journal is never overwritten");
        check(!DllStartup::restore(f.exe).value("ok").toBool() && read(f.exe) == f.original,
              "malformed journal cannot drive restore");
    }
    {
        Fixture f;
        const auto review = f.inspect();
        QLockFile lock(f.exe + QStringLiteral(".bsi-startup-dll.lock"));
        check(lock.tryLock(), "hold a real concurrent-operation lock");
        check(!f.apply(review).value("ok").toBool() && read(f.exe) == f.original,
              "concurrent apply is refused, not interleaved");
    }
    {
        Fixture f;
        const auto review = f.inspect();
        for (const auto &badName : {QStringLiteral("../outside.dll"), QString::fromUtf8("zlag-\xC3\xA9.dll"),
                                    QStringLiteral("CON.dll")}) {
            const auto r = DllStartup::apply(f.exe, badName, review.value("exeSha256").toString(),
                                             review.value("dllSha256").toString(), true);
            check(!r.value("ok").toBool() && read(f.exe) == f.original && !QFileInfo::exists(f.exe + suffix),
                  "unsafe/Unicode DLL basename never reaches EXE commit");
        }
    }
    {
        Fixture f;
        const auto applied = f.apply();
        const auto backup = applied.value("backup").toString();
        auto changed = read(f.exe);
        changed[0x450] = 'x';
        write(f.exe, changed);
        check(!DllStartup::restore(f.exe).value("ok").toBool() && read(f.exe) == changed &&
                  read(backup) == f.original,
              "restore refuses unrelated edits/emulator updates without clobbering them");
    }
    {
        Fixture f;
        const auto applied = f.apply();
        const auto patched = read(f.exe);
        const auto backup = applied.value("backup").toString();
        write(backup, "corrupt backup");
        check(!DllStartup::restore(f.exe).value("ok").toBool() && read(f.exe) == patched &&
                  read(backup) == "corrupt backup",
              "corrupt backup is refused, never used or repaired by overwriting it");
    }
    {
        Fixture f;
        f.apply();
        const auto patched = read(f.exe);
        auto record = QJsonDocument::fromJson(read(f.exe + suffix)).object();
        record.insert("backupFile", "../outside.backup");
        write(f.exe + suffix, QJsonDocument(record).toJson());
        check(!DllStartup::restore(f.exe).value("ok").toBool() && read(f.exe) == patched,
              "journal path traversal cannot select a restore source");
    }
#ifndef Q_OS_WIN
    {
        Fixture f;
        const QString link = f.temp.filePath(QStringLiteral("linked.exe"));
        check(QFile::link(f.exe, link), "create POSIX target symlink fixture");
        check(!DllStartup::inspect(link, name).value("ok").toBool() && read(f.exe) == f.original,
              "target symlinks are not followed for patching");
        QFile::rename(f.dll, f.dll + ".real");
        check(QFile::link(f.dll + ".real", f.dll), "create POSIX DLL symlink fixture");
        check(!f.inspect().value("ok").toBool(), "DLL symlinks are refused too");
    }
#endif
}
#ifdef BSI_DLLSTARTUP_TESTING
void faultTests()
{
    for (const auto fault : {DllStartup::Fault::BeforeCommit, DllStartup::Fault::AfterCommitVerification,
                             DllStartup::Fault::Rollback}) {
        Fixture f;
        DllStartup::setTestFault(fault);
        const auto r = f.apply();
        DllStartup::setTestFault(DllStartup::Fault::None);
        check(!r.value("ok").toBool() && read(r.value("backup").toString()) == f.original,
              "injected commit/verification failure is never a success and retains the original snapshot");
        if (fault == DllStartup::Fault::BeforeCommit)
            check(read(f.exe) == f.original, "pre-commit I/O failure leaves original EXE unchanged");
        if (fault == DllStartup::Fault::AfterCommitVerification)
            check(r.value("rolledBack").toBool() && !r.value("recoveryRequired").toBool() &&
                      read(f.exe) == f.original,
                  "post-commit verification failure rolls back and verifies the complete original");
        if (fault == DllStartup::Fault::Rollback)
            check(r.value("recoveryRequired").toBool() && r.value("changed").toBool(),
                  "rollback failure is explicit: do not launch; recovery required");
        check(DllStartup::restore(f.exe).value("ok").toBool() && read(f.exe) == f.original,
              "a retained prepared/applied journal can recover after the I/O fault clears");
    }
    {
        Fixture f;
        f.apply();
        const auto patched = read(f.exe);
        DllStartup::setTestFault(DllStartup::Fault::AfterCommitVerification);
        const auto r = DllStartup::restore(f.exe);
        DllStartup::setTestFault(DllStartup::Fault::None);
        check(!r.value("ok").toBool() && r.value("rolledBack").toBool() && read(f.exe) == patched,
              "failed restore verification also rolls back atomically to its prior state");
        check(DllStartup::restore(f.exe).value("ok").toBool() && read(f.exe) == f.original,
              "restore can be retried after a transient read-back fault");
    }
}
#endif
void referenceTest(const QString &path)
{
    if (path.isEmpty())
        return;
    const QByteArray original = read(path);
    check(!original.isEmpty(), "read optional HD-Player reference as data only");
    if (original.isEmpty())
        return;
    Fixture f;
    check(write(f.exe, original), "copy reference bytes into an isolated directory");
    const auto review = f.inspect();
    const auto r = f.apply(review);
    if (!r.value("ok").toBool())
        std::fprintf(stderr, "%s\n", qPrintable(r.value("msg").toString()));
    check(r.value("ok").toBool() && hash(read(f.exe)) == review.value("outputSha256").toString(),
          "native transaction patches and verifies only the disposable reference copy");
    check(DllStartup::restore(f.exe).value("ok").toBool() && read(f.exe) == original,
          "native transaction restores the disposable real reference exactly");
    check(hash(read(path)) == hash(original),
          "original reference hash is unchanged; no player or supplied DLL code was executed");
}
} // namespace

int runDllStartupFileTests(const QString &reference)
{
    failures = 0;
    std::printf("\nDLL startup native file-transaction tests\n");
    basicTests();
    refusalTests();
#ifdef BSI_DLLSTARTUP_TESTING
    faultTests();
#endif
    referenceTest(reference);
    std::printf("DLL STARTUP FILE TESTS %s\n", failures ? "FAILED" : "PASSED");
    return failures;
}

#ifdef BSI_DLLSTARTUP_STANDALONE_MAIN
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 5 && args.at(1) == QLatin1String("--prepare-deployment")) {
        // CI later copies ONLY these two files into a different directory and
        // launches the host AFTER this patcher process has exited.
        if (!QDir().mkdir(args.at(4)))
            return 2;
        const QDir folder(args.at(4));
        const auto exe = folder.filePath(QStringLiteral("HD-Player.exe"));
        const auto dllName = QStringLiteral("bsi-startup-probe.dll");
        if (!QFile::copy(args.at(2), exe) || !QFile::copy(args.at(3), folder.filePath(dllName)))
            return 2;
        const auto symbol = QStringLiteral("BsiProbeInitialize");
        const auto review = DllStartup::inspect(exe, dllName, symbol);
        const auto r = DllStartup::apply(exe, dllName, review.value("exeSha256").toString(),
                                         review.value("dllSha256").toString(), true, symbol);
        if (!r.value("ok").toBool())
            std::fprintf(stderr, "%s\n", qPrintable(r.value("msg").toString()));
        return r.value("ok").toBool() ? 0 : 1;
    }
    return runDllStartupFileTests(args.size() == 2 ? args.at(1) : QString()) ? 1 : 0;
}
#endif
