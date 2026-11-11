#pragma once
#include <QByteArray>
#include <QVariantMap>
#include <QVariantList>
#include <QString>

// Read-only inspection and single-EXE transactional batches. Boot assets and
// GIF RPC sidecars are deliberately not included in an atomic EXE transaction.
namespace CustomizationWorkspace {
QString digest(const QByteArray &bytes);
QVariantMap inspect(const QString &path, const QVariantMap &edits = {});
QVariantMap validateProfile(const QVariantMap &profile);
QVariantMap readProfile(const QString &path);
QVariantMap saveProfile(const QString &path, const QVariantMap &profile);
QVariantMap stageResource(const QString &path, const QString &replacement);
QVariantMap inspectAppearance(const QString &path);
QVariantMap stageAppearance(const QString &path, const QVariantMap &edits);
QVariantMap simulate(const QString &path, const QVariantList &operations, const QString &expectedHash,
                     QByteArray *prepared = nullptr);
QVariantMap apply(const QString &path, const QVariantList &operations, const QString &expectedHash,
                  const QString &expectedOutputHash, const QString &historyRoot = {});
QVariantList history(const QString &historyRoot = {});
QVariantMap restore(const QString &path, const QString &id, const QString &historyRoot = {});
QVariantMap exportDiagnostics(const QString &path, const QString &destination);
QVariantMap prepareImage(const QString &source, const QString &destination, int width, int height,
                         bool crop, int quality);
QVariantMap inspectVideo(const QString &source);
QVariantMap rpcDiagnostics(const QString &exePath, const QVariantMap &settings);
}
