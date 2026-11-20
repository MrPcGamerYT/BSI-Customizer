#pragma once
#include "ShaderPackage.h"
#include <QString>

// Experimental backend only, deliberately not wired to the Customizer UI or a
// release payload. A pair of filesystems objects is NOT globally atomic: this
// journals verified snapshots before each rename/atomic EXE write and supports
// recovery after interruption. It never installs Qt/CRT/system replacement DLLs.
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
Result apply(const QString &exePath, const ShaderPackage::Payload &payload,
             const QString &expectedExeSha256, const QString &expectedPackageSha256,
             bool acknowledged);
Result remove(const QString &exePath, const QString &expectedExeSha256, bool acknowledged);
// Prepared transactions roll back. A verified committed transaction only
// finishes cleanup; it must never silently undo a successfully committed update.
Result recover(const QString &exePath);

#ifdef BSI_SHADER_INSTALL_TESTING
// Disposable transaction tests only; never compiled into the application.
enum class Checkpoint { None, Journal, Parked, Published, Exe, Committed, RecoveryParked, RecoveryPublished, RecoveryExe };
void setTestFault(Checkpoint at, bool interrupt = false, bool failRecovery = false);
#endif
} // namespace ShaderInstall
