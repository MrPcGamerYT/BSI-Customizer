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
    QString qtVersion;
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
// Runtime keys must be unique lowercase basenames. All non-system dependency
// providers (and forwarders) must be present. Qt Core/Gui/Qml must have the same
// exact release VS_FIXEDFILEINFO version as the compiled module, with all used
// imported symbols resolvable. Unknown/debug/mixed-Qt/other-ABI packages refuse.
Result validate(const QByteArray &player, const Payload &payload,
                const QMap<QString, QByteArray> &runtime);
} // namespace ShaderPackage
