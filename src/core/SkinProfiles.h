#pragma once
#include <QVariantMap>
#include <QString>

// Portable authoring data only. Imports cannot execute QML, scripts or file paths.
namespace SkinProfiles {
QVariantMap validate(const QVariantMap &profile);
QVariantMap read(const QString &file);
QVariantMap save(const QString &file, const QVariantMap &profile);
QVariantMap readIcon(const QString &file);
QByteArray mappedIcon(const QVariantMap &mapping, const QString &style, const QString &color);
QByteArray icon(const QString &glyph, const QString &style, const QString &color);
}
