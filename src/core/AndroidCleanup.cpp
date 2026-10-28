#include "AndroidCleanup.h"

#include "AdbClient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <memory>

namespace Android {

namespace {

constexpr int kRecordVersion = 1;
// A copy bigger than this is refused instead of filling the system drive; the
// message tells the user how to delete without a copy.
constexpr qint64 kCopyLimit = qint64(4) * 1024 * 1024 * 1024;

// Same folder the configuration undo record lives in.
QString stateDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/debloat");
}

QString stamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
}

QVariantMap failure(const QString &message)
{
    return { { QStringLiteral("ok"), false }, { QStringLiteral("msg"), message } };
}

// `ls -A` output to a list of names, order kept, no duplicates.
QStringList entriesFrom(const QString &output)
{
    QStringList entries;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QString name = line.trimmed();
        if (!name.isEmpty() && !entries.contains(name))
            entries << name;
    }
    return entries;
}

// "1234\t/sdcard/Download" (du -sk) to bytes.
qint64 sizeFrom(const QString &output)
{
    static const QRegularExpression first(QStringLiteral("^(\\d+)"));
    const auto match = first.match(output.trimmed());
    if (!match.hasMatch())
        return -1;
    bool ok = false;
    const qint64 kb = match.captured(1).toLongLong(&ok);
    return ok ? kb * 1024 : -1;
}

QString readableSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("unknown size");
    if (bytes >= qint64(1024) * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(QString::number(double(bytes) / (1024.0 * 1024 * 1024), 'f', 1));
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(QString::number(double(bytes) / (1024.0 * 1024), 'f', 1));
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024);
    return QStringLiteral("%1 bytes").arg(bytes);
}

bool hasPackage(const QString &output, const QString &package)
{
    const QString needle = QStringLiteral("package:") + package;
    for (const QString &line : output.split(QLatin1Char('\n')))
        if (line.trimmed() == needle)
            return true;
    return false;
}

std::unique_ptr<Adb::Client> openClient(const QString &exePath, const QString &confPath,
                                       const QString &instance, QString *error)
{
    const QString adb = Adb::findAdb(exePath);
    if (adb.isEmpty()) {
        *error = QStringLiteral("HD-Adb.exe was not found next to HD-Player.exe. Point the tool at "
                                "the player's folder first.");
        return nullptr;
    }
    if (instance.isEmpty()) {
        *error = QStringLiteral("Choose an Android instance first.");
        return nullptr;
    }
    const quint16 port = Adb::instancePort(confPath, instance);
    if (port == 0) {
        *error = QStringLiteral("bluestacks.conf publishes no ADB port for “%1”. Turn on "
                                "“Enable Android debugging (ADB)” above, start the instance, then "
                                "try again.")
                     .arg(instance);
        return nullptr;
    }
    QString why;
    auto client = std::make_unique<Adb::Client>(adb, QStringLiteral("127.0.0.1:%1").arg(port), &why);
    if (!client->ok()) {
        *error = QStringLiteral("Could not talk to “%1”: %2 Start the instance, leave it on the "
                                "home screen and make sure Android debugging is enabled.")
                     .arg(instance, why);
        return nullptr;
    }
    return client;
}

} // namespace

// ---------------------------------------------------------------------------
// the reviewed list
// ---------------------------------------------------------------------------

const QList<AppRule> &appRules()
{
    static const QList<AppRule> rules {
        { QStringLiteral("gg.now.ads.service"),
          QStringLiteral("now.gg ad service"),
          QStringLiteral("Presents the now.gg ads shown over games."), true },
        { QStringLiteral("com.uncube.gamevantage"),
          QStringLiteral("Gameplay ads"),
          QStringLiteral("The in-game overlay BlueStacks uses for its own ads."), true },
        { QStringLiteral("com.bluestacks.appmart"),
          QStringLiteral("Legacy App Center"),
          QStringLiteral("The older BlueStacks store app."), true },
        { QStringLiteral("com.bluestacks.appfinder"),
          QStringLiteral("App recommendations"),
          QStringLiteral("Suggests and installs games you did not ask for."), true },
        { QStringLiteral("com.android.egg"),
          QStringLiteral("Android Easter egg"),
          QStringLiteral("A leftover toy app from the Android image."), false },
        { QStringLiteral("com.android.printspooler"),
          QStringLiteral("Android printing"),
          QStringLiteral("Printing from Android stops working."), false },
        { QStringLiteral("com.android.traceur"),
          QStringLiteral("System tracing UI"),
          QStringLiteral("The developer tracing app; nothing else uses it."), false },
    };
    return rules;
}

QStringList knownPackages()
{
    QStringList packages;
    for (const auto &rule : appRules())
        packages << rule.package;
    return packages;
}

QString appLabel(const QString &package)
{
    for (const auto &rule : appRules())
        if (rule.package == package)
            return rule.label;
    return package;
}

bool validPackage(const QString &package)
{
    if (package.isEmpty() || package.size() > 200)
        return false;
    static const QRegularExpression shape(
        QStringLiteral("^[A-Za-z][A-Za-z0-9_]*(\\.[A-Za-z0-9_]+)+$"));
    return shape.match(package).hasMatch();
}

QStringList installedPackagesFromOutput(const QString &output)
{
    QStringList packages;
    for (const QString &line : output.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1String("package:")))
            continue;
        const QString package = trimmed.mid(8).trimmed();
        if (validPackage(package) && !packages.contains(package))
            packages << package;
    }
    packages.sort(Qt::CaseInsensitive);
    return packages;
}

// ---------------------------------------------------------------------------
// commands
// ---------------------------------------------------------------------------

QString listCommand()
{
    return QStringLiteral("pm list packages --user 0");
}

QString uninstallCommand(const QString &package)
{
    return QStringLiteral("pm uninstall --user 0 %1").arg(package);
}

QString disableCommand(const QString &package)
{
    return QStringLiteral("pm disable-user --user 0 %1").arg(package);
}

QString installExistingCommand(const QString &package)
{
    return QStringLiteral("cmd package install-existing --user 0 %1").arg(package);
}

QString enableCommand(const QString &package)
{
    return QStringLiteral("pm enable --user 0 %1").arg(package);
}

QString downloadListCommand()
{
    return QStringLiteral("ls -A /sdcard/Download 2>/dev/null");
}

QString downloadSizeCommand()
{
    return QStringLiteral("du -sk /sdcard/Download 2>/dev/null");
}

QString appDataListCommand(const QString &folder)
{
    return QStringLiteral("ls -A /sdcard/Android/%1 2>/dev/null").arg(folder);
}

// The folder itself goes and comes back empty, so not even an empty "apps"
// sub-folder or a hidden file is left behind.
QStringList downloadWipeCommands()
{
    return { QStringLiteral("rm -rf /sdcard/Download"),
             QStringLiteral("mkdir -p /sdcard/Download") };
}

QStringList appDataWipeCommands()
{
    return { QStringLiteral("find /sdcard/Android/data -mindepth 1 -maxdepth 1 -exec rm -rf {} + "
                            "2>/dev/null"),
             QStringLiteral("find /sdcard/Android/obb -mindepth 1 -maxdepth 1 -exec rm -rf {} + "
                            "2>/dev/null") };
}

qint64 copyLimitBytes()
{
    return kCopyLimit;
}

// ---------------------------------------------------------------------------
// record
// ---------------------------------------------------------------------------

QString recordPath()
{
    return stateDir() + QStringLiteral("/android.json");
}

bool isEmpty(const Record &record)
{
    return record.removed.isEmpty() && record.downloads.isEmpty() && record.appData.isEmpty();
}

Record recordFromJson(const QByteArray &bytes)
{
    Record record;
    const QJsonObject root = QJsonDocument::fromJson(bytes).object();
    for (const auto &value : root.value(QStringLiteral("removed")).toArray()) {
        const QJsonObject object = value.toObject();
        const RemovedApp entry { object.value(QStringLiteral("package")).toString(),
                                 object.value(QStringLiteral("action")).toString(),
                                 object.value(QStringLiteral("instance")).toString() };
        if (entry.package.isEmpty() || entry.action.isEmpty())
            continue;
        record.removed << entry;
    }
    for (const auto &value : root.value(QStringLiteral("downloads")).toArray()) {
        const QJsonObject object = value.toObject();
        WipedFolder folder { object.value(QStringLiteral("instance")).toString(), {},
                             object.value(QStringLiteral("backup")).toString() };
        for (const auto &name : object.value(QStringLiteral("entries")).toArray())
            folder.entries << name.toString();
        record.downloads << folder;
    }
    for (const auto &value : root.value(QStringLiteral("appData")).toArray())
        record.appData << value.toString();
    return record;
}

Record loadRecord()
{
    QFile file(recordPath());
    if (!file.open(QIODevice::ReadOnly))
        return Record();
    const Record record = recordFromJson(file.readAll());
    file.close();
    return record;
}

QByteArray recordToJson(const Record &record)
{
    QJsonArray removed;
    for (const auto &entry : record.removed)
        removed.append(QJsonObject { { QStringLiteral("package"), entry.package },
                                     { QStringLiteral("action"), entry.action },
                                     { QStringLiteral("instance"), entry.instance } });
    QJsonArray downloads;
    for (const auto &folder : record.downloads) {
        QJsonArray entries;
        for (const QString &name : folder.entries)
            entries.append(name);
        downloads.append(QJsonObject { { QStringLiteral("instance"), folder.instance },
                                       { QStringLiteral("backup"), folder.backup },
                                       { QStringLiteral("entries"), entries } });
    }
    QJsonArray appData;
    for (const QString &instance : record.appData)
        appData.append(instance);
    const QJsonObject root { { QStringLiteral("version"), kRecordVersion },
                             { QStringLiteral("removed"), removed },
                             { QStringLiteral("downloads"), downloads },
                             { QStringLiteral("appData"), appData } };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool saveRecord(const Record &record)
{
    if (!QDir().mkpath(stateDir()))
        return false;
    QSaveFile file(recordPath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(recordToJson(record));
    return file.commit();
}

QVariantMap state()
{
    const Record record = loadRecord();
    QVariantList removed;
    for (const auto &entry : record.removed)
        removed << QVariantMap { { QStringLiteral("package"), entry.package },
                                 { QStringLiteral("label"), appLabel(entry.package) },
                                 { QStringLiteral("action"), entry.action },
                                 { QStringLiteral("instance"), entry.instance } };
    QVariantList downloads;
    for (const auto &folder : record.downloads) {
        QVariantList entries;
        for (const QString &name : folder.entries)
            entries << name;
        downloads << QVariantMap { { QStringLiteral("instance"), folder.instance },
                                   { QStringLiteral("entries"), entries },
                                   { QStringLiteral("count"), folder.entries.size() },
                                   { QStringLiteral("backup"),
                                     QDir::toNativeSeparators(folder.backup) } };
    }
    QVariantList appData;
    for (const QString &instance : record.appData)
        appData << instance;
    return { { QStringLiteral("recorded"), !isEmpty(record) },
             { QStringLiteral("removed"), removed },
             { QStringLiteral("downloads"), downloads },
             { QStringLiteral("appData"), appData } };
}

// ---------------------------------------------------------------------------
// scan
// ---------------------------------------------------------------------------

QVariantMap scan(const QString &exePath, const QString &confPath, const QString &instance)
{
    QString error;
    const auto client = openClient(exePath, confPath, instance, &error);
    if (!client)
        return failure(error);

    const Adb::Output listed = client->shell(listCommand(), 30000);
    if (!listed.ok)
        return failure(QStringLiteral("The instance did not list its packages: %1").arg(listed.error));

    const Record record = loadRecord();
    QStringList recorded;
    for (const auto &entry : record.removed)
        if (entry.instance == instance)
            recorded << entry.package;

    // Show every package installed for Android user 0. The reviewed list gets
    // useful labels and the safe-by-default recommendation; every other app is
    // still visible and can be selected deliberately by the user.
    QMap<QString, AppRule> reviewed;
    for (const auto &rule : appRules())
        reviewed.insert(rule.package, rule);
    const QStringList installedPackages = installedPackagesFromOutput(listed.out);

    QVariantList apps;
    for (const QString &package : installedPackages) {
        const bool isReviewed = reviewed.contains(package);
        const AppRule rule = reviewed.value(package);
        apps << QVariantMap { { QStringLiteral("package"), package },
                              { QStringLiteral("label"), isReviewed ? rule.label : package },
                              { QStringLiteral("note"), isReviewed ? rule.note
                                  : QStringLiteral("Not on the reviewed list. Removing this app may "
                                                   "break Android or BlueStacks.") },
                              { QStringLiteral("recommended"), isReviewed && rule.recommended },
                              { QStringLiteral("debloatable"), true },
                              { QStringLiteral("installed"), true },
                              { QStringLiteral("recorded"), recorded.contains(package) } };
    }
    // Keep removed-by-this-tool entries on screen, even though Android no
    // longer reports them as installed. The Undo action can put them back.
    for (const QString &package : recorded) {
        if (installedPackages.contains(package)) continue;
        const bool isReviewed = reviewed.contains(package);
        const AppRule rule = reviewed.value(package);
        apps << QVariantMap { { QStringLiteral("package"), package },
                              { QStringLiteral("label"), isReviewed ? rule.label : package },
                              { QStringLiteral("note"), isReviewed ? rule.note : QString() },
                              { QStringLiteral("recommended"), false },
                              { QStringLiteral("debloatable"), false },
                              { QStringLiteral("installed"), false },
                              { QStringLiteral("recorded"), true } };
    }

    const QStringList entries = entriesFrom(client->shell(downloadListCommand(), 20000).out);
    QVariantList entryList;
    for (const QString &entry : entries)
        entryList << entry;
    const qint64 bytes = sizeFrom(client->shell(downloadSizeCommand(), 20000).out);

    QVariantList appData;
    for (const auto &folder : { QStringLiteral("data"), QStringLiteral("obb") }) {
        const QStringList names = entriesFrom(client->shell(appDataListCommand(folder), 20000).out);
        appData << QVariantMap { { QStringLiteral("folder"), folder },
                                 { QStringLiteral("count"), names.size() } };
    }

    return { { QStringLiteral("ok"), true },
             { QStringLiteral("msg"), QStringLiteral("Connected to “%1”.").arg(instance) },
             { QStringLiteral("apps"), apps },
             { QStringLiteral("others"), QStringList() },
             { QStringLiteral("installedCount"), installedPackages.size() },
             { QStringLiteral("download"), QVariantMap { { QStringLiteral("entries"), entryList },
                                                         { QStringLiteral("bytes"), bytes },
                                                         { QStringLiteral("size"), readableSize(bytes) } } },
             { QStringLiteral("appData"), appData } };
}

// ---------------------------------------------------------------------------
// apply
// ---------------------------------------------------------------------------

QVariantMap apply(const QString &exePath, const QString &confPath, const QString &instance,
                  const Request &request)
{
    QStringList wanted;
    for (const QString &package : request.packages) {
        if (!validPackage(package))
            return failure(QStringLiteral("“%1” is not a package name; nothing was changed.")
                               .arg(package));
        if (!wanted.contains(package))
            wanted << package;
    }
    if (wanted.isEmpty() && !request.wipeDownloads && !request.clearAppData)
        return failure(QStringLiteral("Select at least one app or a storage folder to clean."));

    QString error;
    const auto client = openClient(exePath, confPath, instance, &error);
    if (!client)
        return failure(error);

    const Adb::Output before = client->shell(listCommand(), 30000);
    if (!before.ok)
        return failure(QStringLiteral("The instance did not list its packages: %1").arg(before.error));

    // ---- pre-flight: nothing is touched before these pass ----
    const QStringList downloadEntries = request.wipeDownloads
        ? entriesFrom(client->shell(downloadListCommand(), 20000).out)
        : QStringList();
    qint64 downloadBytes = -1;
    if (request.wipeDownloads && !downloadEntries.isEmpty()) {
        downloadBytes = sizeFrom(client->shell(downloadSizeCommand(), 20000).out);
        if (request.keepCopy && downloadBytes > kCopyLimit)
            return failure(QStringLiteral("The Download folder holds %1, more than the %2 the copy "
                                          "is allowed to take. Turn off “Keep a copy on this PC "
                                          "first”, or copy it yourself, then clean it.")
                               .arg(readableSize(downloadBytes), readableSize(kCopyLimit)));
    }

    // ---- the copy, before anything is deleted ----
    // Pulling changes nothing inside the instance, so a failed copy is still a
    // clean refusal.
    QString backupPath;
    if (request.wipeDownloads && !downloadEntries.isEmpty() && request.keepCopy) {
        backupPath = QDir(stateDir()).filePath(QStringLiteral("backups/download-%1").arg(stamp()));
        if (!QDir().mkpath(backupPath))
            return failure(QStringLiteral("Could not create %1; nothing was deleted.")
                               .arg(QDir::toNativeSeparators(backupPath)));
        const Adb::Output copied = client->pull(QStringLiteral("/sdcard/Download"), backupPath);
        if (!copied.ok)
            return failure(QStringLiteral("The copy failed (%1), so nothing was deleted. Turn off "
                                          "“Keep a copy on this PC first” to delete without one.")
                               .arg(copied.error));
    }

    // ---- apps ----
    Record record = loadRecord();
    QStringList removedNow, alreadyGone, failed;
    for (const QString &package : wanted) {
        if (!hasPackage(before.out, package)) {
            alreadyGone << package;
            continue;
        }
        Adb::Output out = client->shell(uninstallCommand(package), 60000);
        QString action = QStringLiteral("uninstall");
        if (!out.ok || !out.out.contains(QLatin1String("Success"))) {
            out = client->shell(disableCommand(package), 60000);
            action = QStringLiteral("disable");
        }
        if (!out.ok || !out.out.contains(QLatin1String("Success"))) {
            failed << QStringLiteral("%1 (%2)").arg(package,
                                                    out.error.isEmpty() ? out.out : out.error);
            continue;
        }
        removedNow << package;
        bool recorded = false;
        for (const auto &existing : record.removed)
            if (existing.package == package && existing.instance == instance)
                recorded = true;
        if (!recorded)
            record.removed << RemovedApp { package, action, instance };
    }
    // Saved before the storage steps: a later refusal must never lose the
    // record that puts these apps back.
    if (!removedNow.isEmpty() && !saveRecord(record))
        return failure(QStringLiteral("The apps were removed, but their undo record could not be "
                                      "written to %1. Reinstall them from Google Play if you need "
                                      "them.")
                           .arg(QDir::toNativeSeparators(stateDir())));

    // ---- Download folder ----
    bool wiped = false;
    if (request.wipeDownloads) {
        for (const QString &command : downloadWipeCommands())
            client->shell(command, 120000);
        wiped = entriesFrom(client->shell(downloadListCommand(), 20000).out).isEmpty();
        if (wiped) {
            bool recorded = false;
            for (auto &folder : record.downloads) {
                if (folder.instance != instance)
                    continue;
                folder = WipedFolder { instance, downloadEntries, backupPath };
                recorded = true;
            }
            if (!recorded)
                record.downloads << WipedFolder { instance, downloadEntries, backupPath };
            saveRecord(record);
        }
    }

    // ---- app data ----
    bool appDataCleared = false;
    if (request.clearAppData) {
        for (const QString &command : appDataWipeCommands())
            client->shell(command, 180000);
        appDataCleared =
            entriesFrom(client->shell(appDataListCommand(QStringLiteral("data")), 20000).out).isEmpty()
            && entriesFrom(client->shell(appDataListCommand(QStringLiteral("obb")), 20000).out).isEmpty();
        if (appDataCleared) {
            if (!record.appData.contains(instance))
                record.appData << instance;
            saveRecord(record);
        }
    }

    if (!wiped && request.wipeDownloads) {
        QString message = QStringLiteral("The Android apps were cleaned, but the Download folder "
                                         "still has files in it; the player may be holding one "
                                         "open. Close the instance and try again.");
        if (!failed.isEmpty())
            message += QStringLiteral(" Could not remove: %1.")
                           .arg(failed.join(QStringLiteral("; ")));
        return { { QStringLiteral("ok"), false }, { QStringLiteral("msg"), message } };
    }

    QStringList parts;
    if (!removedNow.isEmpty())
        parts << QStringLiteral("%1 app(s)").arg(removedNow.size());
    if (wiped)
        parts << (downloadEntries.isEmpty()
                      ? QStringLiteral("the already empty Download folder")
                      : QStringLiteral("the Download folder (%1 item(s), %2)")
                            .arg(downloadEntries.size()).arg(readableSize(downloadBytes)));
    if (appDataCleared) parts << QStringLiteral("the Android app data");
    QString message = parts.isEmpty()
        ? QStringLiteral("Nothing was changed.")
        : QStringLiteral("Cleaned: %1.").arg(parts.join(QStringLiteral(", ")));
    if (!alreadyGone.isEmpty())
        message += QStringLiteral(" Already gone: %1.").arg(alreadyGone.join(QStringLiteral(", ")));
    if (!failed.isEmpty())
        message += QStringLiteral(" Could not remove: %1.").arg(failed.join(QStringLiteral("; ")));
    if (!backupPath.isEmpty())
        message += QStringLiteral(" A copy of the Download folder is at %1.")
                       .arg(QDir::toNativeSeparators(backupPath));
    if (!appDataCleared && request.clearAppData)
        message += QStringLiteral(" The app data folders could not be emptied; restart the "
                                  "instance and try again.");
    message += QStringLiteral(" Restart the instance to see the launcher without the removed apps.");
    return { { QStringLiteral("ok"), !parts.isEmpty() },
             { QStringLiteral("msg"), message },
             { QStringLiteral("removed"), removedNow.size() },
             { QStringLiteral("backup"), QDir::toNativeSeparators(backupPath) } };
}

// ---------------------------------------------------------------------------
// restore
// ---------------------------------------------------------------------------

QVariantMap restore(const QString &exePath, const QString &confPath, const QString &instance)
{
    Record record = loadRecord();
    if (isEmpty(record))
        return { { QStringLiteral("ok"), true },
                 { QStringLiteral("msg"), QStringLiteral("Nothing to restore inside Android.") } };

    QStringList targets;
    for (const auto &entry : record.removed)
        if ((instance.isEmpty() || entry.instance == instance) && !targets.contains(entry.instance))
            targets << entry.instance;

    QStringList restored, failed, notes;
    QList<int> done;
    bool deviceTrouble = false;
    for (const QString &target : targets) {
        QString error;
        const auto client = openClient(exePath, confPath, target, &error);
        if (!client) {
            deviceTrouble = true;
            notes << error;
            continue;
        }
        for (int i = 0; i < record.removed.size(); ++i) {
            const RemovedApp entry = record.removed.at(i);
            if (entry.instance != target || done.contains(i))
                continue;
            const Adb::Output listed = client->shell(listCommand(), 30000);
            if (!(listed.ok && hasPackage(listed.out, entry.package))) {
                client->shell(entry.action == QLatin1String("disable")
                                  ? enableCommand(entry.package)
                                  : installExistingCommand(entry.package),
                              60000);
                // The package list is the only proof that matters.
                const Adb::Output after = client->shell(listCommand(), 30000);
                if (!(after.ok && hasPackage(after.out, entry.package))) {
                    failed << QStringLiteral("%1 (%2)").arg(entry.package, target);
                    continue;
                }
            }
            restored << entry.package;
            done << i;
        }
    }
    if (!done.isEmpty()) {
        QList<RemovedApp> keep;
        for (int i = 0; i < record.removed.size(); ++i)
            if (!done.contains(i))
                keep << record.removed.at(i);
        record.removed = keep;
    }

    QStringList parts;
    if (!restored.isEmpty())
        parts << QStringLiteral("%1 app(s)").arg(restored.size());
    if (!record.downloads.isEmpty()) {
        QStringList places;
        for (const auto &folder : record.downloads)
            places << (folder.backup.isEmpty()
                           ? QStringLiteral("“%1” was deleted without a copy").arg(folder.instance)
                           : QDir::toNativeSeparators(folder.backup));
        notes << QStringLiteral("Download folder copies: %1 — copy one back through the shared "
                                "folder to put it in Android again.")
                     .arg(places.join(QStringLiteral("; ")));
        record.downloads.clear();
    }
    if (!record.appData.isEmpty()) {
        notes << QStringLiteral("App data was cleared, so each app rebuilds it the next time it "
                                "starts.");
        record.appData.clear();
    }
    saveRecord(record);

    QString message = parts.isEmpty()
        ? QStringLiteral("No Android apps were put back.")
        : QStringLiteral("Restored: %1.").arg(parts.join(QStringLiteral(", ")));
    if (!failed.isEmpty())
        message += QStringLiteral(" Could not put back: %1.").arg(failed.join(QStringLiteral(", ")));
    for (const QString &note : notes)
        message += QLatin1Char(' ') + note;
    const bool ok = failed.isEmpty() && !deviceTrouble;
    return { { QStringLiteral("ok"), ok }, { QStringLiteral("msg"), message } };
}

} // namespace Android
