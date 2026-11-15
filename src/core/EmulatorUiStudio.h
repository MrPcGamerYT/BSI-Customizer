#pragma once
#include <QByteArray>
#include <QMap>
#include <QVariantMap>

// Immutable Assets bundles. Native hooks retain all OEM callbacks/navigation.
namespace EmulatorUiStudio {
QMap<QString,QByteArray> files(const QVariantMap &profile);
QVariantMap publish(const QString &parent, const QVariantMap &profile);
QVariantMap hook(const QString &leaf, const QByteArray &source, const QString &directory);
bool verify(const QString &directory, const QVariantMap &hashes);
}
