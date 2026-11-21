#pragma once
#include "QmlText.h"
#include <QHash>
#include <QString>

// Select the model actually passed to the native Settings window. Detached
// prototypes and page-local submodels are never guessed from filename strings.
namespace NativeSettingsModel {
bool literalQmlString(const QString &source, int begin, int end, QString *value);
QmlText::Object locate(const QString &playerText, const QHash<QString, int> &pageCounts,
                       QString *error);
}
