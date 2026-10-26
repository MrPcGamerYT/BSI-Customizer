#pragma once
#include <QVariantMap>
#include <QString>
#include <QByteArray>
namespace GifRpc {
QByteArray bundledProxy();
bool isOurProxy(const QByteArray &data, QString *error = nullptr);
QVariantMap inspect(const QString &exePath);
QVariantMap apply(const QString &exePath, const QVariantMap &settings, bool replaceExisting = false);
QVariantMap restore(const QString &exePath);
// Public for offline regression tests; rejects invalid IDs, URL schemes and
// oversized text before touching an executable, DLL, config or backup.
bool validate(const QVariantMap &settings, QString *error);
}
