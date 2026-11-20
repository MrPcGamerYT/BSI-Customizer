#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Extra choices in the player's ORIGINAL Phone profile ComboBox. Selection is
// staged in the native config and saved through setCustomDeviceProfile; never
// send invented cloud profile codes or change FPS/driver/root settings.
namespace PhoneProfiles {
struct Profile { QString name, manufacturer, brand, model; };
const QList<Profile> &catalog();
QString resourceName();
struct SourcePlan {
    bool ok = false;
    bool installed = false;
    QString output;
    QString error;
};
SourcePlan prepareSource(const QString &source, bool enabled,
                         const QString &runtimeUrl = QStringLiteral("file:///BSI-PhoneProfiles.js"));
QByteArray runtimeJavascript();
QString compactPreparedSource(const QString &source);
struct State {
    bool supported = false;
    bool applied = false;
    QString detail;
};
struct Plan {
    bool ok = false;
    bool changed = false;
    bool installed = false;
    QByteArray output;
    QByteArray beforeSource, afterSource;
    QString error;
};
// Read-only/in-memory. A successful plan is not a disk write or a game-FPS test.
State inspect(const QByteArray &image);
Plan prepare(const QByteArray &image, bool enabled, const QString &runtimeUrl = {},
             const QByteArray &originalSource = {});
} // namespace PhoneProfiles
