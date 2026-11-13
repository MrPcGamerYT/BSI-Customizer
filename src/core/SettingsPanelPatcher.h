#pragma once

#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QStringList>

// Rebuilds the BlueStacks/HD-Player in-emulator Settings window, widening the
// fixed-width native tab content while keeping each page's controls and native
// save/discard callbacks. Original QML sources are stored in a feature-local
// recovery snapshot so restore never reverts unrelated executable changes.
namespace SettingsPanelPatcher {

struct State
{
    bool supported = false;
    bool applied = false;
    bool restorable = false;
    int compiledUnitsDetected = 0;
    int compiledUnitsActive = 0;
    int settingsPageCount = 0;
    int settingsQmlResourceCount = 0;
    int embeddedQmlResourceCount = 0;
    QStringList qmlResourceSamples;
    QVariantList tabs;
    bool logoFallback = false;
    QString logoStatus;
    QString detail;
};

State analyze(const QString &exePath);
QVariantMap readCustomAsset(const QString &path, const QString &kind);
QVariantMap apply(const QString &exePath, const QString &developerName,
                  const QVariantList &links, const QVariantMap &profile, bool enabled);

} // namespace SettingsPanelPatcher
