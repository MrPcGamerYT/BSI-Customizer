#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/**
 * Debloat — the BlueStacks Debloat feature set, natively in the Customizer.
 *
 * Everything here edits what the player already owns:
 *
 *  * **Settings** — existing `bluestacks.conf` keys whose value is a boolean
 *    get flipped (ads, telemetry, Smart Downloads, cloud/AI features). Keys the
 *    build does not declare are skipped, never created.
 *  * **Tuning** — ADB access, quiet notifications, dedicated GPU preference
 *    and the player's own high-frame-rate mode.
 *  * **Hosts** — one tagged block of two BlueStacks ad domains in the Windows
 *    hosts file, with everything else in that file left byte-for-byte alone.
 *  * **Cloud companions** — BlueStacks X / Store and BlueStacks Services are
 *    moved aside on their own volume (a rename, not a delete), never the local
 *    player, its disks or its instances.
 *
 * Every write records the values it replaced, so Restore puts each one back.
 * The record lives in the application data folder, not in the player.
 */
namespace Debloat {

// One switch the UI offers. `id` is what apply()/restore() get back.
struct Option
{
    QString id;
    QString group;
    QString label;
    QString hint;
    bool needsInstance = false;
    bool windowsOnly = false;
};

const QList<Option> &options();
QString optionLabel(const QString &id);

// What the user asked for: a set of option ids plus the instance the
// per-instance switches apply to.
struct Request
{
    QStringList options;
    QString instance;
};

// One value written into bluestacks.conf, with the value it replaced.
struct Change
{
    QString key;
    QString before;
    QString after;
};

struct Plan
{
    bool ok = false;
    QString error;
    QList<Change> changes;
    QStringList notes;    // what the user should know about a switch
    QStringList skipped;  // "<key>: <reason>" for settings left alone
    bool hosts = false;
    QStringList cloudIds;
    bool gpu = false;
};

// Pure: works on the bytes of bluestacks.conf, writes nothing.
Plan planFor(const QByteArray &config, const Request &request);

// Which settings each switch owns.
QStringList adKeys();
QStringList telemetryKeys();
QStringList downloadKeys();
QStringList cloudKeys();

// ---- hosts file (pure) ----
// Marker lines that delimit everything this tool owns in the hosts file.
QString hostsStartMarker();
QString hostsEndMarker();
QStringList hostsDomains();
// Adds, refreshes or removes the tagged block. Everything outside the block
// keeps its bytes; a half-deleted block is refused instead of repaired.
bool hostsEdits(const QByteArray &input, bool install, QByteArray *out, QString *error);

// ---- cloud companions ----
struct CloudProduct
{
    QString id;
    QString name;
    QString path;     // where it lives now (or used to, when `moved`)
    bool moved = false;
};
// Folders found under the given roots, so tests never need Program Files.
QList<CloudProduct> cloudProductsAt(const QStringList &roots);
QList<CloudProduct> cloudProducts();
// "<folder>.bsi-disabled" next to the folder itself: same volume, instant,
// and one rename away from coming back.
QString cloudParkedPath(const QString &path);

// ---- status / apply / restore ----
struct Status
{
    bool supported = false;
    bool applied = false;
    QString conf;
    QString detail;
    QStringList instances;
    QString instance;         // the instance the per-instance switches would use
    QStringList already;      // option ids that are already at their target
    bool hostsInstalled = false;
    QVariantList cloud;       // [{id,name,path,moved}]
    bool anyCloudMoved = false;
    bool anyRecord = false;
    int pendingChanges = 0;
};

QString gpuPreferenceValue();   // "GpuPreference=2;" — Windows per-app preference
Status status(const QString &confPath);
QVariantMap apply(const QString &confPath, const QString &exePath, const Request &request);
QVariantMap restore(const QString &confPath, const QString &exePath);

// The revert record, kept in the application data folder.
QString backupFolder();
QStringList backups();          // newest last

} // namespace Debloat
