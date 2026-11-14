#pragma once

#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QStringList>

// Rebuilds the BlueStacks/HD-Player in-emulator Settings window while keeping
// its original settings-page components and their native save/discard logic.
// The original QML sources are stored as a feature-local recovery snapshot, so
// restoring this panel never restores unrelated changes to the executable.
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

// Where the feature-local recovery snapshot for this executable lives. Exposed
// so callers can report a damaged snapshot, and so regression tests can stage
// stale state at the exact path the patcher reads.
QString recoveryPathFor(const QString &exePath);
State analyze(const QString &exePath);
QVariantMap readCustomAsset(const QString &path, const QString &kind);
QVariantMap apply(const QString &exePath, const QString &developerName,
                  const QVariantList &links, const QVariantMap &profile, bool enabled);

} // namespace SettingsPanelPatcher
