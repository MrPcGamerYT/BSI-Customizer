#pragma once

#include <QString>
#include <QVariantMap>

// Native, opt-in PE startup dependency. Only the EXE is changed. The trusted
// AMD64 DLL must already be beside it and must stay there for normal launches.
namespace DllStartup
{
QVariantMap inspect(const QString &exePath, const QString &dllName,
                    const QString &requiredExport = QStringLiteral("zlag_init"));
QVariantMap apply(const QString &exePath, const QString &dllName, const QString &expectedExeSha256,
                  const QString &expectedDllSha256, bool riskAcknowledged,
                  const QString &requiredExport = QStringLiteral("zlag_init"));
QVariantMap restore(const QString &exePath);

#ifdef BSI_DLLSTARTUP_TESTING
// Compiled ONLY into the disposable file-transaction test, never the app.
enum class Fault { None, BeforeCommit, AfterCommitVerification, Rollback };
void setTestFault(Fault fault);
#endif
} // namespace DllStartup
