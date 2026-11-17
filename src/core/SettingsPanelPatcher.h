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
// Built-in assets keyed by the inspected pages' stable ids, in the same
// MIME/base64 format as custom uploads. These are not saved as user overrides;
// clearing an override therefore restores the pack on the next Apply.
QVariantMap defaultTabIcons(const QVariantList &tabs, QString *error = nullptr);
QVariantMap readCustomAsset(const QString &path, const QString &kind);
QVariantMap apply(const QString &exePath, const QString &developerName,
                  const QVariantList &links, const QVariantMap &profile, bool enabled);

} // namespace SettingsPanelPatcher
