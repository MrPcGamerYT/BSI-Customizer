#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/**
 * Android — the inside of the instance: the preinstalled apps that ship with
 * the BlueStacks Android image and the guest's own storage.
 *
 * This is deliberately *not* an editor for `Root.vhd`. That file is the shared,
 * read-only Android system image (an ext4 filesystem inside a VHD): every
 * instance cloned from the same base boots from it, so writing to it while the
 * player runs would corrupt the image for all of them, and the next player
 * update would replace it anyway. What makes an app disappear is the package
 * state inside the running instance, and that is what this module changes
 * through the player's own `HD-Adb.exe`:
 *
 *  * **Preinstalled apps** — a reviewed list, removed for Android user 0
 *    (`pm uninstall --user 0`, or `pm disable-user --user 0` when the package
 *    belongs to the system partition). The original state is recorded per
 *    instance, so Restore puts each one back (`cmd package install-existing`).
 *  * **Guest storage** — the contents of `/sdcard/Download` and, if asked for,
 *    of `/sdcard/Android/data` and `/sdcard/Android/obb`, which live in the
 *    instance's own `Data.vhdx`. The Download folder can be copied to this PC
 *    first; that copy is what Restore points at.
 *
 * Nothing here needs root, Magisk or an image editor.
 */
namespace Android {

// One app the tool knows how to put back.
struct AppRule
{
    QString package;
    QString label;
    QString note;
    bool recommended = false;
};

const QList<AppRule> &appRules();
QStringList knownPackages();
QString appLabel(const QString &package);

// A package name safe to interpolate into a shell command: letters, digits,
// dots, underscores; at least two segments. Everything else is refused.
bool validPackage(const QString &package);
// Parses `pm list packages --user 0` output into the complete sorted unique
// package set; exposed so the parser can be regression-tested without a device.
QStringList installedPackagesFromOutput(const QString &output);

// ---- commands (pure, so they can be tested without a device) ----
QString listCommand();                                   // packages installed for user 0
QString uninstallCommand(const QString &package);
QString disableCommand(const QString &package);
QString installExistingCommand(const QString &package);
QString enableCommand(const QString &package);
QString downloadListCommand();
QString downloadSizeCommand();
QString appDataListCommand(const QString &folder);       // "data" or "obb"
QStringList downloadWipeCommands();
QStringList appDataWipeCommands();

// ---- the undo record ----
struct RemovedApp
{
    QString package;
    QString action;     // "uninstall" or "disable"
    QString instance;
};

struct WipedFolder
{
    QString instance;
    QStringList entries;
    QString backup;     // the copy on this PC, when one was kept
};

struct Record
{
    QList<RemovedApp> removed;
    QList<WipedFolder> downloads;
    QStringList appData;   // instances whose Android/data and Android/obb were emptied
};

bool isEmpty(const Record &record);
QString recordPath();
// The record's bytes, and a record read back from them. Both are pure; the
// file in the application data folder is only where they are stored.
QByteArray recordToJson(const Record &record);
Record recordFromJson(const QByteArray &bytes);
Record loadRecord();
bool saveRecord(const Record &record);

// The record as the UI shows it, without talking to a device.
QVariantMap state();

// ---- live, through the instance's own ADB ----
// What the reviewed list and the guest storage look like right now.
QVariantMap scan(const QString &exePath, const QString &confPath, const QString &instance);

struct Request
{
    QStringList packages;
    bool wipeDownloads = false;
    bool keepCopy = true;       // copy the Download folder to this PC before wiping
    bool clearAppData = false;  // /sdcard/Android/data and /sdcard/Android/obb
};

// Applies the request. Nothing is deleted until every pre-flight check passes.
QVariantMap apply(const QString &exePath, const QString &confPath, const QString &instance,
                  const Request &request);
// Puts the recorded apps back. An empty instance restores every recorded one
// that is running; anything that is not running keeps its record.
QVariantMap restore(const QString &exePath, const QString &confPath, const QString &instance);

// The largest Download folder the automatic copy is allowed to pull.
qint64 copyLimitBytes();

} // namespace Android
