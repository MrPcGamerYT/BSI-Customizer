#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// Version spoof, ad/string removal and instance detection.
// Port of find_versions / apply_version / apply_removals / detect_instances.
namespace ExtrasPatcher {

struct Result { bool ok = false; QString msg; };

QList<QVariantMap> findVersions(const QByteArray &data); // [{version, offset}]
Result applyVersion(const QString &exePath, const QString &oldV, const QString &newV);

QStringList removalNames(); // REMOVE_STRINGS keys + contextual removal names
Result applyRemovals(const QString &exePath, const QStringList &enabledNames);

QVariantList detectInstances(const QString &confPath); // [{name, display, conf_key}]

} // namespace ExtrasPatcher
