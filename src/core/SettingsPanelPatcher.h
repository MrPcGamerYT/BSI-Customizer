#pragma once

#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

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
    int compiledUnitsActive = 0;
    QString detail;
};

State analyze(const QString &exePath);
QVariantMap apply(const QString &exePath, const QString &developerName,
                  const QVariantList &links, bool enabled);

} // namespace SettingsPanelPatcher
