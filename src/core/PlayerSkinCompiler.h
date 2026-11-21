#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVariantMap>

// Generated QML only: no user-supplied scripts, offsets or replacement sources.
// Common controls propagate to Settings, exit/game/volume popups and editors
// that actually use them. Unknown source contracts/compiled shadows are refused.
namespace PlayerSkinCompiler {
QStringList components();
QVariantMap compile(const QString &leaf, const QByteArray &source, const QVariantMap &profile);
QVariantMap plan(const QByteArray &image, const QVariantMap &profile, const QStringList &manualPaths);
bool replace(QByteArray &image, const QString &path, const QByteArray &source, QString *error);
}
