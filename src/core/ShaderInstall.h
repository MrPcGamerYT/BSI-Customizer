#pragma once
#include "ShaderPackage.h"
#include <QString>

// Regular, matching-Qt native Settings module; never a system-DLL proxy. Two
// filesystem objects cannot be exchanged atomically together. Verified journal
// snapshots and ordered writes support recovery, not power-loss certification.
// No Qt/CRT/system replacement DLL is ever installed.
namespace ShaderInstall {
struct Result {
    bool ok = false;
    bool changed = false;
    bool recoveryRequired = false;
    bool rolledBack = false;
    bool cleanupPending = false;
    QString error;
    QString recoveryDirectory;
};
struct Inspection {
    bool ok = false;
    bool installed = false;
    bool sourcePresent = false;
    bool modulePresent = false;
    bool playerClosed = false;
    bool canApply = false;
    bool canRemove = false;
    bool canRecover = false;
    bool recoveryRequired = false;
    bool updateAvailable = false;
    int qtMajor = 0;
    QString qtVersion; // actual player Core/Gui/Qml release
    QString moduleQtVersion; // real compiled baseline, never relabeled
    bool publicQtApi = false;
    QString exeSha256;
    QString packageSha256;
    QString installedPackageSha256;
    QString stateSha256; // EXE + owned module, including absence
    QString applyReviewSha256; // also binds package + verified dependency bytes
    QString journalSha256;
    QString recoveryDirectory;
    QString error;
    QString runtimeError;
    QString applyError;
    QString removeError;
    QString recoveryError;
    QMap<QString, QString> runtimeSha256;
};
// Read-only: no lock/staging/backup files, DLL loading or player execution.
// A compatible result is a preflight, NOT evidence of in-game rendering.
Inspection inspect(const QString &exePath, const ShaderPackage::Payload *payload = nullptr);
Result apply(const QString &exePath, const ShaderPackage::Payload &payload,
             const QString &expectedExeSha256, const QString &expectedPackageSha256,
             bool acknowledged, const QString &expectedReviewSha256 = QString());
Result remove(const QString &exePath, const QString &expectedExeSha256, bool acknowledged,
              const QString &expectedStateSha256 = QString());
// The UI always supplies the reviewed fingerprints above and for recovery.
// Empty optional fingerprints preserve the internal transaction-test API; they
// are not exposed through Backend. Prepared recovery rolls back; a verified
// committed transaction only finishes cleanup, never silently undoes success.
Result recover(const QString &exePath, const QString &expectedJournalSha256 = QString(),
               bool acknowledged = false);

#ifdef BSI_SHADER_INSTALL_TESTING
// Disposable transaction tests only; never compiled into the application.
enum class Checkpoint { None, Journal, Parked, Published, Exe, Committed, RecoveryParked, RecoveryPublished, RecoveryExe };
void setTestFault(Checkpoint at, bool interrupt = false, bool failRecovery = false);
#endif
} // namespace ShaderInstall
