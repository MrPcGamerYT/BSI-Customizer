#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

// Read-only checks for a trusted, normal Windows QML module. Never loads a DLL
// or calls an export. The descriptor is a compatibility contract, NOT a digital
// signature: a release must separately pin/review the payload's SHA-256.
namespace ShaderPackage {
struct Payload {
    QByteArray dll;
    QByteArray qmldir;
};
struct Result {
    bool ok = false;
    QString error;
    // describe(): compiled baseline. validate(): actual player runtime.
    QString qtVersion;
    QString moduleQtVersion;
    // Only the new, source-audited V3 descriptor opts into Qt's backwards
    // public-API ABI contract. Existing V2 modules remain exact-version only.
    bool publicQtApi = false;
    QString packageSha256;
    QMap<QString, QString> runtimeSha256;
};
struct Dependencies {
    bool ok = false;
    QString error;
    QStringList modules;
};
// Bounded normal/delay import parsing, including symbol thunks, plus DLL export
// forwarder providers so a filesystem caller can collect the complete closure.
Dependencies dependencies(const QByteArray &image, bool dll);
// Only Windows-owned system/API-set names, not Qt, VC runtimes or arbitrary
// third-party DLLs. Those must be supplied and checked, never redistributed or
// overwritten by the shader installer.
bool systemModule(const QString &name);
QString sha256(const QByteArray &bytes);
QString payloadHash(const Payload &payload);
QByteArray moduleDefinition();
// Accompanies the installed module, including binary redistribution notices.
QByteArray licenseNotices();
// Bounded descriptor/entry-point inspection, without loading the module. This
// is compatibility metadata, not a signature or permission to execute a DLL.
Result describe(const Payload &payload);
// Read the fixed release version of a Qt runtime DLL, not editable strings.
Result runtimeVersion(const QByteArray &dll);
// Version eligibility only, for choosing a compiled-in candidate. This is NOT
// a substitute for validate(): all actual imports/providers must still pass.
// V2 stays exact; audited V3 permits a newer/equal runtime of the SAME major.
bool canTarget(const Result &description, const QString &runtimeQtVersion);
// Runtime keys must be unique lowercase basenames. All non-system dependency
// providers (and forwarders) must be present. Every Qt provider must have the
// SAME exact release VS_FIXEDFILEINFO version as the player's Core. Its version
// must satisfy the module's versioned contract, and every used imported symbol
// must resolve. Unknown/debug/mixed-Qt/other-ABI packages still refuse.
Result validate(const QByteArray &player, const Payload &payload,
                const QMap<QString, QByteArray> &runtime);
} // namespace ShaderPackage
