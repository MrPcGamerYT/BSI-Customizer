#include "ShaderInstall.h"
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
    Fixture()
    {
        require(dir.isValid(), QStringLiteral("Disposable transaction directory"));
        exe = dir.filePath(QStringLiteral("HD-Player.exe")); module = dir.filePath(QStringLiteral("BSIShaders"));
        journal = exe + QStringLiteral(".bsi-shaders-transaction.json");
        original = player(); write(exe, original);
        first = ShaderPackageFixture::payload(); second = first; second.dll[0x1001] = char(0x90);
        const auto r = ShaderPackageFixture::runtime();
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
                const auto recovered = ShaderInstall::recover(f.exe);
                require(recovered.ok && !recovered.recoveryRequired && !recovered.cleanupPending, QStringLiteral("Explicit interrupted-transaction recovery: ") + recovered.error);
                require(recovered.rolledBack == (checkpoint != Checkpoint::Committed), QStringLiteral("Commit marker controls rollback vs cleanup"));
            } else {
                require(!result.recoveryRequired && !result.cleanupPending && result.ok == (checkpoint == Checkpoint::Committed)
                            && result.rolledBack == (checkpoint != Checkpoint::Committed), QStringLiteral("Automatic ordinary I/O failure recovery: ") + result.error);
            }
            if (checkpoint == Checkpoint::Committed) f.after(action);
            else require(snapshot(f.dir.path()) == before, QStringLiteral("Rollback restores the exact whole previous EXE/module/runtime file set"));
            require(ShaderInstall::recover(f.exe).ok, QStringLiteral("Recovery repeated without journal is a no-op"));
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
        const auto recovered = ShaderInstall::recover(f.exe);
        require(!recovered.ok && recovered.recoveryRequired && snapshot(f.dir.path()) == conflicted, QStringLiteral("Conflicting EXE/module/backup/journal/extra-file recovery must change nothing"));
        if (conflict == 4) require(QFile::remove(path), QStringLiteral("Clear disposable extra file")); else write(path, original);
        require(ShaderInstall::recover(f.exe).ok && snapshot(f.dir.path()) == before, QStringLiteral("Reconciled conflict can recover exact prior file set"));
    }
    {
        Fixture f; const auto before = snapshot(f.dir.path());
        ShaderInstall::setTestFault(Checkpoint::Exe, false, true);
        const auto failed = f.apply(f.first);
        require(!failed.ok && failed.recoveryRequired && !failed.rolledBack && QFileInfo::exists(f.journal), QStringLiteral("Rollback failure retains both verified snapshots and does not claim success"));
        ShaderInstall::setTestFault(Checkpoint::None);
        require(ShaderInstall::recover(f.exe).ok && snapshot(f.dir.path()) == before, QStringLiteral("Rollback-failure recovery is restartable"));
    }
    for (const auto recoveryPoint : {Checkpoint::RecoveryParked, Checkpoint::RecoveryPublished}) {
        Fixture f; f.install(); const auto before = snapshot(f.dir.path());
        ShaderInstall::setTestFault(Checkpoint::Published, true); require(f.apply(f.second).recoveryRequired, QStringLiteral("Prepare nested recovery interruption"));
        ShaderInstall::setTestFault(recoveryPoint, true);
        const auto fail = ShaderInstall::recover(f.exe);
        require(!fail.ok && fail.recoveryRequired && fail.error.contains(QLatin1String("Injected")), QStringLiteral("Recovery interruption itself must fire"));
        ShaderInstall::setTestFault(Checkpoint::None);
        require(ShaderInstall::recover(f.exe).ok && snapshot(f.dir.path()) == before, QStringLiteral("An interrupted recovery can itself recover exact bytes"));
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
    std::puts("LIMIT: experimental backend only; no Customizer install action/release payload or installed-player/game-frame/hardware certification");
    return 0;
}
