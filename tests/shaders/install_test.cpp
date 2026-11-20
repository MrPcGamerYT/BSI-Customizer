#include "ShaderInstall.h"
#include "ShaderActions.h"
#include "ShaderSettingsImage.h"
#include "QtResources.h"
#include "package_fixture.h"
#include "settings_image_fixture.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

using namespace ByteUtils;
using ShaderInstall::Checkpoint;
namespace {
int assertions = 0;
void require(bool yes, const QString &message)
{
    ++assertions;
    if (!yes) { std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); std::exit(1); }
}
void write(const QString &path, const QByteArray &bytes)
{
    QFile f(path); require(f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size(), QStringLiteral("Write disposable fixture: ") + path);
}
QByteArray read(const QString &path)
{
    QFile f(path); require(f.open(QIODevice::ReadOnly), QStringLiteral("Read disposable fixture: ") + path); return f.readAll();
}
ShaderInstall::Result recoverReviewed(const QString &exe)
{
    const QString path = exe + QStringLiteral(".bsi-shaders-transaction.json");
    return ShaderInstall::recover(exe, QFileInfo::exists(path) ? ShaderPackage::sha256(read(path)) : QString(), true);
}
QMap<QString, QByteArray> snapshot(const QString &root)
{
    QMap<QString, QByteArray> result;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) { const auto path = it.next(); result.insert(QDir(root).relativeFilePath(path), read(path)); }
    return result;
}
QMap<QString, QByteArray> resources(const QByteArray &image)
{
    QMap<QString, QByteArray> result;
    for (const auto &file : QtResources::registeredFiles(image)) result.insert(file.name, QtResources::readFile(image, file));
    return result;
}
QByteArray player()
{
    auto b = ShaderSettingsImageFixture::fixture();
    putLe16(b, 0x96, 0x22); putLe32(b, 0x98 + 16, 0x18000);
    // Keep the qRegisterResourceData import/call site used by the real resource
    // scanner, adding the actual Qt Gui/Qml family for strict package checks.
    putLe32(b, 0x98 + 124, 80);
    for (int i = 0; i < 2; ++i) {
        const int descriptor = 0x1014 + i * 20, table = 0x1140 + i * 0x40, name = 0x1260 + i * 0x60;
        putLe32(b, descriptor, table); putLe32(b, descriptor + 12, name); putLe32(b, descriptor + 16, table + 0x20);
        putLe32(b, table, name + 0x20); putLe32(b, table + 0x20, name + 0x20);
        const QByteArray module = i ? QByteArray("Qt6Qml.dll\0", 11) : QByteArray("Qt6Gui.dll\0", 11);
        b.replace(name, module.size(), module);
        const auto symbol = QByteArray("?bsi_fixture@@YAXXZ\0", 20); b.replace(name + 0x22, symbol.size(), symbol);
    }
    return b;
}
struct Fixture {
    QTemporaryDir dir;
    QString exe, module, journal;
    QByteArray original;
    ShaderPackage::Payload first, second;
    Fixture(const QString &runtimeVersion = QStringLiteral("6.8.3"), const QString &baseline = {}, bool publicQtApi = false)
    {
        require(dir.isValid(), QStringLiteral("Disposable transaction directory"));
        exe = dir.filePath(QStringLiteral("HD-Player.exe")); module = dir.filePath(QStringLiteral("BSIShaders"));
        journal = exe + QStringLiteral(".bsi-shaders-transaction.json");
        original = player();
        if (runtimeVersion.startsWith(QLatin1Char('5'))) {
            for (const auto &name : {QByteArray("Core"), QByteArray("Gui"), QByteArray("Qml")})
                original.replace(QByteArray("Qt6") + name + ".dll", QByteArray("Qt5") + name + ".dll");
        }
        write(exe, original);
        first = ShaderPackageFixture::payload(baseline.isEmpty() ? runtimeVersion : baseline, {}, publicQtApi);
        second = first; second.dll[0x1001] = char(0x90);
        const auto r = ShaderPackageFixture::runtime(runtimeVersion);
        for (auto i = r.cbegin(); i != r.cend(); ++i) write(dir.filePath(i.key()), i.value());
        ShaderInstall::setTestFault(Checkpoint::None);
    }
    ShaderInstall::Result apply(const ShaderPackage::Payload &p)
    {
        return ShaderInstall::apply(exe, p, ShaderPackage::sha256(read(exe)), ShaderPackage::payloadHash(p), true);
    }
    void install()
    {
        const auto r = apply(first); require(r.ok && r.changed && !r.recoveryRequired && !r.cleanupPending, QStringLiteral("Initial disposable installation: ") + r.error);
    }
    void after(int action)
    {
        const auto image = ShaderSettingsImage::prepare(read(exe), action != 2);
        require(image.ok && !image.changed, QStringLiteral("After-goal native source state: ") + image.error);
        if (action == 2) require(!QFileInfo::exists(module), QStringLiteral("Removed module directory"));
        else require(read(QDir(module).filePath(QStringLiteral("BSIShaders.dll"))) == (action == 0 ? first.dll : second.dll), QStringLiteral("After-goal module bytes"));
        require(!QFileInfo::exists(journal), QStringLiteral("Completed transaction has no blocking journal"));
    }
};
ShaderInstall::Result operation(Fixture &f, int action)
{
    return action == 2 ? ShaderInstall::remove(f.exe, ShaderPackage::sha256(read(f.exe)), true)
                       : f.apply(action == 0 ? f.first : f.second);
}
void faultCycles()
{
    int cases = 0;
    for (int action = 0; action < 3; ++action) {
        const QList<Checkpoint> checkpoints = action == 0
            ? QList<Checkpoint>{Checkpoint::Journal, Checkpoint::Published, Checkpoint::Exe, Checkpoint::Committed}
            : action == 1 ? QList<Checkpoint>{Checkpoint::Journal, Checkpoint::Parked, Checkpoint::Published, Checkpoint::Committed}
            : QList<Checkpoint>{Checkpoint::Journal, Checkpoint::Exe, Checkpoint::Parked, Checkpoint::Published, Checkpoint::Committed};
        for (const auto checkpoint : checkpoints) for (bool interrupt : {false, true}) {
            Fixture f; if (action) f.install();
            const auto before = snapshot(f.dir.path());
            ShaderInstall::setTestFault(checkpoint, interrupt);
            const auto result = operation(f, action);
            require(!result.error.isEmpty(), QStringLiteral("The requested I/O fault must actually fire"));
            if (interrupt) {
                require(!result.ok && result.recoveryRequired && QFileInfo::exists(f.journal), QStringLiteral("Interruption retains verified recovery journal/snapshots"));
                const auto paused = snapshot(f.dir.path());
                ShaderInstall::setTestFault(Checkpoint::None);
                const auto blocked = operation(f, action);
                require(!blocked.ok && snapshot(f.dir.path()) == paused, QStringLiteral("Pending recovery blocks all new writes"));
                const auto recovered = recoverReviewed(f.exe);
                require(recovered.ok && !recovered.recoveryRequired && !recovered.cleanupPending, QStringLiteral("Explicit interrupted-transaction recovery: ") + recovered.error);
                require(recovered.rolledBack == (checkpoint != Checkpoint::Committed), QStringLiteral("Commit marker controls rollback vs cleanup"));
            } else {
                require(!result.recoveryRequired && !result.cleanupPending && result.ok == (checkpoint == Checkpoint::Committed)
                            && result.rolledBack == (checkpoint != Checkpoint::Committed), QStringLiteral("Automatic ordinary I/O failure recovery: ") + result.error);
            }
            if (checkpoint == Checkpoint::Committed) f.after(action);
            else require(snapshot(f.dir.path()) == before, QStringLiteral("Rollback restores the exact whole previous EXE/module/runtime file set"));
            require(recoverReviewed(f.exe).ok, QStringLiteral("Recovery repeated without journal is a no-op"));
            ++cases;
        }
    }
    std::printf("PASS: %d install/update/removal pre/post-rename, EXE and commit-marker failure/interruption cycles\n", cases);
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    {
        Fixture f;
        const auto before = snapshot(f.dir.path());
        const auto review = ShaderInstall::inspect(f.exe, &f.first);
        require(review.ok && review.playerClosed && review.canApply && !review.canRemove && !review.installed
                && review.qtVersion == QLatin1String("6.8.3") && review.runtimeSha256.size() == 3
                && review.applyReviewSha256.size() == 64 && review.stateSha256.size() == 64,
                QStringLiteral("Read-only complete first-install compatibility review: ") + review.error + review.applyError + review.runtimeError);
        require(snapshot(f.dir.path()) == before, QStringLiteral("Inspection creates no lock, backups, staging or target changes"));
        const auto bad = ShaderInstall::apply(f.exe, f.first, review.exeSha256, review.packageSha256, true, QString(64, QLatin1Char('0')));
        require(!bad.ok && snapshot(f.dir.path()) == before, QStringLiteral("Stale complete review is a whole-file-set no-op"));
        const auto gui = f.dir.filePath(QStringLiteral("qt6gui.dll")); const auto oldGui = read(gui);
        auto newerGui = oldGui; newerGui[2] = 1; write(gui, newerGui);
        const auto runtimeChanged = snapshot(f.dir.path());
        require(!ShaderInstall::apply(f.exe, f.first, review.exeSha256, review.packageSha256, true, review.applyReviewSha256).ok
                && snapshot(f.dir.path()) == runtimeChanged, QStringLiteral("Even ABI-compatible runtime byte changes invalidate a reviewed install"));
        write(gui, oldGui);
        require(ShaderInstall::apply(f.exe, f.first, review.exeSha256, review.packageSha256, true, review.applyReviewSha256).ok,
                QStringLiteral("Install accepts the exact reviewed complete set"));
        const auto current = ShaderInstall::inspect(f.exe, &f.first);
        require(current.ok && current.installed && current.canApply && current.canRemove && !current.updateAvailable,
                QStringLiteral("Installed pair remains reviewable/removable and exact reapply is distinguished from an update"));
        const auto installed = snapshot(f.dir.path());
        ShaderBundle::Catalog catalog; catalog.ok = true;
        catalog.entries << ShaderBundle::Entry{QStringLiteral("qt-5.15.2"), QStringLiteral("5.15.2"),
            ShaderPackage::payloadHash(ShaderPackageFixture::payload(QStringLiteral("5.15.2"))), ShaderPackageFixture::payload(QStringLiteral("5.15.2"))};
        const auto wrongBundle = ShaderActions::inspect(f.exe, catalog);
        require(!wrongBundle.value("canApply").toBool() && wrongBundle.value("canRemove").toBool()
                && wrongBundle.value("msg").toString().contains(QLatin1String("No bundled module matches")),
                QStringLiteral("An exact-version mismatch disables install but not safe owned removal"));
        catalog.entries << ShaderBundle::Entry{QStringLiteral("qt-6.8.3"), QStringLiteral("6.8.3"), ShaderPackage::payloadHash(f.first), f.first};
        const auto facade = ShaderActions::inspect(f.exe, catalog);
        require(facade.value("canApply").toBool() && facade.value("bundleId").toString() == QLatin1String("qt-6.8.3")
                && facade.value("bundledQtVersions").toStringList().size() == 2, QStringLiteral("Facade selects only the exact matching bundled module and lists every version"));
        require(!ShaderActions::apply(f.exe, QStringLiteral("../user.dll"), current.exeSha256, current.packageSha256, {}, true).value("ok").toBool()
                && !ShaderActions::remove(f.exe, current.exeSha256, {}, true).value("ok").toBool()
                && !ShaderActions::recover(f.exe, {}, true).value("ok").toBool()
                && snapshot(f.dir.path()) == installed, QStringLiteral("UI service never accepts missing review tokens or an arbitrary DLL path"));
        require(f.apply(f.second).ok, QStringLiteral("Stage a separately verified owned module update"));
        const auto updated = snapshot(f.dir.path());
        require(!ShaderInstall::remove(f.exe, current.exeSha256, true, current.stateSha256).ok
                && !ShaderInstall::apply(f.exe, f.first, current.exeSha256, current.packageSha256, true, current.applyReviewSha256).ok
                && snapshot(f.dir.path()) == updated, QStringLiteral("DLL-only updates invalidate old install AND remove reviews even when EXE bytes are unchanged"));
        const auto removeReview = ShaderInstall::inspect(f.exe);
        require(removeReview.canRemove && ShaderActions::remove(f.exe, removeReview.exeSha256, removeReview.stateSha256, true).value("ok").toBool(),
                QStringLiteral("Reviewed shader-only removal works independently of the app's bundled payload"));
        require(resources(read(f.exe)) == resources(f.original), QStringLiteral("Reviewed remove restores original decoded native resources"));
    }
    for (const auto &target : {QStringLiteral("5.15.4"), QStringLiteral("5.15.19"), QStringLiteral("6.7.0"), QStringLiteral("6.8.3")}) {
        const QString baseline = target.startsWith(QLatin1Char('5')) ? QStringLiteral("5.15.2") : QStringLiteral("6.2.4");
        Fixture f(target, baseline, true);
        const auto before = snapshot(f.dir.path());
        const auto review = ShaderInstall::inspect(f.exe, &f.first);
        require(review.ok && review.canApply && review.qtVersion == target && review.moduleQtVersion == baseline
                    && review.publicQtApi && snapshot(f.dir.path()) == before,
                QStringLiteral("Read-only public bridge preflight keeps the actual player Qt version: ") + target + review.applyError);
        ShaderBundle::Catalog catalog; catalog.ok = true;
        catalog.entries << ShaderBundle::Entry{QStringLiteral("qt-") + baseline, baseline, ShaderPackage::payloadHash(f.first), f.first};
        const auto facade = ShaderActions::inspect(f.exe, catalog);
        require(facade.value("canApply").toBool() && facade.value("qtVersion").toString() == target
                    && facade.value("moduleQtVersion").toString() == baseline && facade.value("publicQtApi").toBool()
                    && facade.value("bundleId").toString() == QStringLiteral("qt-") + baseline,
                QStringLiteral("Facade selects the audited same-major baseline, NOT an exact-version spoof: ") + target + facade.value("msg").toString());
        const auto legacy = ShaderPackageFixture::payload(baseline);
        catalog.entries[0].payload = legacy; catalog.entries[0].packageSha256 = ShaderPackage::payloadHash(legacy);
        require(!ShaderActions::inspect(f.exe, catalog).value("canApply").toBool() && snapshot(f.dir.path()) == before,
                QStringLiteral("An older V2 bundle is NOT given the audited V3 compatibility contract"));
        f.install();
        const auto installed = snapshot(f.dir.path());
        require(!f.apply(f.first).changed && snapshot(f.dir.path()) == installed, QStringLiteral("Public ABI installation remains byte-idempotent"));
        require(ShaderInstall::remove(f.exe, ShaderPackage::sha256(read(f.exe)), true).ok
                    && resources(read(f.exe)) == resources(f.original), QStringLiteral("Public ABI installation has exact source-only removal"));
        // A different version contract must retain the SAME recovery discipline.
        const auto restored = snapshot(f.dir.path());
        ShaderInstall::setTestFault(Checkpoint::Exe, true);
        require(f.apply(f.first).recoveryRequired, QStringLiteral("Stage public-ABI interrupted apply"));
        ShaderInstall::setTestFault(Checkpoint::None);
        require(recoverReviewed(f.exe).ok && snapshot(f.dir.path()) == restored, QStringLiteral("Public ABI interruption restores the exact previous file set"));
    }
    {
        Fixture f(QStringLiteral("6.7.3"), QStringLiteral("6.2.4"), true);
        ShaderBundle::Catalog catalog; catalog.ok = true;
        const auto newer = ShaderPackageFixture::payload(QStringLiteral("6.5.3"), {}, true);
        catalog.entries << ShaderBundle::Entry{QStringLiteral("qt-6.5.3"), QStringLiteral("6.5.3"), ShaderPackage::payloadHash(newer), newer};
        catalog.entries << ShaderBundle::Entry{QStringLiteral("qt-6.2.4"), QStringLiteral("6.2.4"), ShaderPackage::payloadHash(f.first), f.first};
        const auto before = snapshot(f.dir.path());
        auto facade = ShaderActions::inspect(f.exe, catalog);
        require(facade.value("canApply").toBool() && facade.value("bundleId").toString() == QLatin1String("qt-6.5.3")
                    && snapshot(f.dir.path()) == before, QStringLiteral("Newest eligible audited baseline is deterministic, not list-order dependent"));
        const auto missing = ShaderPackageFixture::payload(QStringLiteral("6.5.3"), {{QByteArray("vendor.dll"), {QByteArray("work")}, false}}, true);
        catalog.entries[0].payload = missing; catalog.entries[0].packageSha256 = ShaderPackage::payloadHash(missing);
        facade = ShaderActions::inspect(f.exe, catalog);
        require(!facade.value("canApply").toBool() && facade.value("bundleId").toString() == QLatin1String("qt-6.5.3")
                    && !facade.value("applyError").toString().isEmpty() && snapshot(f.dir.path()) == before,
                QStringLiteral("A missing provider is not hidden by silently falling back to another candidate"));
    }
    {
        Fixture f;
        const auto before = snapshot(f.dir.path());
        ShaderInstall::setTestFault(Checkpoint::Exe, true);
        require(f.apply(f.first).recoveryRequired, QStringLiteral("Stage the user-facing recovery review"));
        ShaderInstall::setTestFault(Checkpoint::None);
        const auto paused = snapshot(f.dir.path());
        const auto review = ShaderInstall::inspect(f.exe);
        require(review.recoveryRequired && review.canRecover && !review.canApply && !review.canRemove
                && review.journalSha256.size() == 64 && !review.recoveryDirectory.isEmpty()
                && snapshot(f.dir.path()) == paused, QStringLiteral("Recovery inspection is read-only, validates snapshots and blocks other actions"));
        require(!ShaderInstall::recover(f.exe, review.journalSha256, false).ok
                && !ShaderInstall::recover(f.exe, QString(64, QLatin1Char('0')), true).ok
                && snapshot(f.dir.path()) == paused, QStringLiteral("Recovery requires acknowledgment and the exact reviewed journal"));
        require(ShaderActions::recover(f.exe, review.journalSha256, true).value("ok").toBool()
                && snapshot(f.dir.path()) == before, QStringLiteral("Authorized-style reviewed recovery restores the previous whole set"));
    }
    {
        Fixture f;
        const auto before = snapshot(f.dir.path());
        const auto hash = ShaderPackage::sha256(f.original), package = ShaderPackage::payloadHash(f.first);
        require(!ShaderInstall::apply(f.exe, f.first, hash, package, false).ok && snapshot(f.dir.path()) == before, QStringLiteral("Unacknowledged install is disk-no-op"));
        require(!ShaderInstall::apply(f.exe, f.first, QString(64, QLatin1Char('0')), package, true).ok && snapshot(f.dir.path()) == before, QStringLiteral("Stale EXE hash is disk-no-op"));
        require(!ShaderInstall::apply(f.exe, f.first, hash, QString(64, QLatin1Char('0')), true).ok && snapshot(f.dir.path()) == before, QStringLiteral("Stale package hash is disk-no-op"));
        require(ShaderInstall::remove(f.exe, hash, true).ok && snapshot(f.dir.path()) == before, QStringLiteral("Absent removal is an exact file-set no-op"));
        QLockFile lock(f.exe + QStringLiteral(".bsi-shaders.lock")); require(lock.tryLock(0), QStringLiteral("Hold test lock"));
        const auto locked = snapshot(f.dir.path());
        require(!f.apply(f.first).ok && snapshot(f.dir.path()) == locked, QStringLiteral("Concurrent operation lock cannot be stolen")); lock.unlock();
        auto bad = f.first; bad.dll.replace("ABI=msvc", "ABI=xxxx");
        require(!f.apply(bad).ok && snapshot(f.dir.path()) == before, QStringLiteral("Incompatible DLL cannot change the player"));
        const auto missing = f.dir.filePath(QStringLiteral("qt6gui.dll")); const auto bytes = read(missing); require(QFile::remove(missing), QStringLiteral("Stage missing runtime"));
        const auto absent = snapshot(f.dir.path()); require(!f.apply(f.first).ok && snapshot(f.dir.path()) == absent, QStringLiteral("Missing Qt dependency is never installed/overwritten")); write(missing, bytes);
        f.install(); f.after(0);
        const auto installed = snapshot(f.dir.path()); const auto exe = read(f.exe);
        const auto repeat = f.apply(f.first);
        require(repeat.ok && !repeat.changed && snapshot(f.dir.path()) == installed, QStringLiteral("Repeat installation is byte-idempotent without journal churn"));
        const auto update = f.apply(f.second);
        require(update.ok && update.changed && read(f.exe) == exe, QStringLiteral("Module update preserves the entire already-integrated EXE: ") + update.error); f.after(1);
        auto edited = read(f.exe); QString error;
        const QByteArray userEdit("import QtQuick 2.15\nItem{property string untouched:'later user change'}\n");
        require(ShaderSettingsImage::replaceSource(edited, QStringLiteral("Unrelated.qml"), userEdit, &error), error); write(f.exe, edited);
        const auto removal = ShaderInstall::remove(f.exe, ShaderPackage::sha256(edited), true);
        require(removal.ok && removal.changed && !removal.cleanupPending, QStringLiteral("Owned package removal: ") + removal.error); f.after(2);
        auto expected = resources(f.original); expected[QStringLiteral("Unrelated.qml")] = userEdit;
        require(resources(read(f.exe)) == expected, QStringLiteral("Remove restores all native source bytes and preserves later unrelated customizations"));
        for (auto i = before.cbegin(); i != before.cend(); ++i) if (i.key() != QLatin1String("HD-Player.exe"))
            require(read(f.dir.filePath(i.key())) == i.value(), QStringLiteral("No Qt/VC/system runtime file changed"));
    }
    for (bool missingModule : {false, true}) {
        Fixture f; f.install();
        if (missingModule) {
            for (const auto &name : QDir(f.module).entryList(QDir::Files))
                require(QFile::remove(QDir(f.module).filePath(name)), QStringLiteral("Stage an entirely absent owned module"));
            require(QDir().rmdir(f.module), QStringLiteral("Remove disposable empty module directory"));
        } else write(f.exe, f.original); // module-only orphan, not an installation to claim
        const auto before = snapshot(f.dir.path());
        ShaderBundle::Catalog catalog; catalog.ok = true;
        catalog.entries << ShaderBundle::Entry{QStringLiteral("qt-6.8.3"), QStringLiteral("6.8.3"), ShaderPackage::payloadHash(f.first), f.first};
        const auto report = ShaderActions::inspect(f.exe, catalog);
        require(!report.value("canApply").toBool() && !report.value("installed").toBool()
                && report.value("canRemove").toBool() == missingModule && snapshot(f.dir.path()) == before,
                QStringLiteral("Orphan review is read-only; only exactly owned source-only removal is available"));
        if (missingModule) {
            require(ShaderActions::remove(f.exe, report.value("exeSha256").toString(), report.value("stateSha256").toString(), true).value("ok").toBool(),
                    QStringLiteral("Missing whole module directory does not trap the owned native sources"));
        } else require(report.value("msg").toString().contains(QLatin1String("without its matching native source"))
                          && !report.value("applyError").toString().contains(QLatin1String("No bundled module matches")),
                          QStringLiteral("Report retains the orphan-pair refusal instead of misreporting an unknown Qt version"));
    }
    faultCycles();
    for (int conflict = 0; conflict < 5; ++conflict) {
        Fixture f; f.install();
        const auto before = snapshot(f.dir.path());
        ShaderInstall::setTestFault(Checkpoint::Published, true);
        const auto interrupted = f.apply(f.second); require(interrupted.recoveryRequired, QStringLiteral("Stage update conflict"));
        ShaderInstall::setTestFault(Checkpoint::None);
        QString path; QByteArray original;
        if (conflict == 0) { path = f.exe; original = read(path); auto changed = original; changed[2] = 1; write(path, changed); }
        if (conflict == 1) { path = QDir(f.module).filePath(QStringLiteral("BSIShaders.dll")); original = read(path); auto changed = original; changed[2] = 1; write(path, changed); }
        if (conflict == 2) { path = QDir(interrupted.recoveryDirectory).filePath(QStringLiteral("before.exe")); original = read(path); auto changed = original; changed[2] = 1; write(path, changed); }
        if (conflict == 3) { path = f.journal; original = read(path); auto o = QJsonDocument::fromJson(original).object(); o[QStringLiteral("recoveryDir")] = QStringLiteral("../unowned"); write(path, QJsonDocument(o).toJson(QJsonDocument::Compact)); }
        if (conflict == 4) { path = QDir(f.module).filePath(QStringLiteral("user-owned.txt")); write(path, QByteArray("must not be deleted")); }
        const auto conflicted = snapshot(f.dir.path());
        const auto recovered = recoverReviewed(f.exe);
        require(!recovered.ok && recovered.recoveryRequired && snapshot(f.dir.path()) == conflicted, QStringLiteral("Conflicting EXE/module/backup/journal/extra-file recovery must change nothing"));
        if (conflict == 4) require(QFile::remove(path), QStringLiteral("Clear disposable extra file")); else write(path, original);
        require(recoverReviewed(f.exe).ok && snapshot(f.dir.path()) == before, QStringLiteral("Reconciled conflict can recover exact prior file set"));
    }
    {
        Fixture f; const auto before = snapshot(f.dir.path());
        ShaderInstall::setTestFault(Checkpoint::Exe, false, true);
        const auto failed = f.apply(f.first);
        require(!failed.ok && failed.recoveryRequired && !failed.rolledBack && QFileInfo::exists(f.journal), QStringLiteral("Rollback failure retains both verified snapshots and does not claim success"));
        ShaderInstall::setTestFault(Checkpoint::None);
        require(recoverReviewed(f.exe).ok && snapshot(f.dir.path()) == before, QStringLiteral("Rollback-failure recovery is restartable"));
    }
    for (const auto recoveryPoint : {Checkpoint::RecoveryParked, Checkpoint::RecoveryPublished}) {
        Fixture f; f.install(); const auto before = snapshot(f.dir.path());
        ShaderInstall::setTestFault(Checkpoint::Published, true); require(f.apply(f.second).recoveryRequired, QStringLiteral("Prepare nested recovery interruption"));
        ShaderInstall::setTestFault(recoveryPoint, true);
        const auto fail = recoverReviewed(f.exe);
        require(!fail.ok && fail.recoveryRequired && fail.error.contains(QLatin1String("Injected")), QStringLiteral("Recovery interruption itself must fire"));
        ShaderInstall::setTestFault(Checkpoint::None);
        require(recoverReviewed(f.exe).ok && snapshot(f.dir.path()) == before, QStringLiteral("An interrupted recovery can itself recover exact bytes"));
    }
    {
        Fixture f;
        require(QDir().mkdir(f.module), QStringLiteral("Stage unowned directory")); write(QDir(f.module).filePath(QStringLiteral("user.txt")), QByteArray("user data"));
        const auto before = snapshot(f.dir.path()); require(!f.apply(f.first).ok && snapshot(f.dir.path()) == before, QStringLiteral("Never adopt/overwrite an unowned module directory"));
    }
#ifndef Q_OS_WIN
    {
        Fixture f;
        const auto link = f.dir.filePath(QStringLiteral("linked-player.exe")); require(QFile::link(f.exe, link), QStringLiteral("Stage symlink fixture"));
        const auto before = snapshot(f.dir.path());
        require(!ShaderInstall::apply(link, f.first, ShaderPackage::sha256(f.original), ShaderPackage::payloadHash(f.first), true).ok && snapshot(f.dir.path()) == before, QStringLiteral("Symlink EXE is refused"));
    }
#else
    {
        const auto running = ShaderInstall::apply(app.applicationFilePath(), ShaderPackage::Payload{}, QString(64, QLatin1Char('0')), QString(64, QLatin1Char('0')), true);
        require(!running.ok && running.error.contains(QLatin1String("Close every player")), QStringLiteral("A running EXE is refused before parsing/patching"));
    }
#endif
    std::printf("PASS: %d disposable-file ownership/hash/atomic-write/recovery assertions; no reference player executed or installed\n", assertions);
    std::puts("LIMIT: disposable-file fixtures are not installed-player/game-frame or physical GPU/driver certification");
    return 0;
}
