#include "DebloatPatcher.h"

#include "BsDetector.h"
#include "DebloatConfig.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace Debloat {

namespace {

const char kStateVersion = 1;

// ---- the switches -------------------------------------------------------
// Only settings a BlueStacks build really declares are ever touched; a key that
// is missing is the player saying this build has no such feature.
const QList<Option> &optionTable()
{
    static const QList<Option> table {
        { QStringLiteral("ads"), QStringLiteral("ads"),
          QStringLiteral("Block in-app ads and promotions"),
          QStringLiteral("Turns off programmatic ads, the boot banner, the promotion grid and the "
                         "Google Play ad flags on this installation."),
          false, false },
        { QStringLiteral("telemetry"), QStringLiteral("ads"),
          QStringLiteral("Stop usage and statistics reporting"),
          QStringLiteral("Turns off the usage, notification, offer and advertising statistics the "
                         "player sends."),
          false, false },
        { QStringLiteral("downloads"), QStringLiteral("ads"),
          QStringLiteral("Disable Smart Downloads"),
          QStringLiteral("Stops the player from downloading promoted apps in the background."),
          false, false },
        { QStringLiteral("cloud"), QStringLiteral("cloud"),
          QStringLiteral("Disable cloud, nowBux and AI features"),
          QStringLiteral("Turns off BlueStacks X integration, cloud instances, nowBux rewards, "
                         "cloud recording uploads and the AI buttons the sidebar would show."),
          false, false },
        { QStringLiteral("quiet"), QStringLiteral("tuning"),
          QStringLiteral("Silence Android notifications"),
          QStringLiteral("Sets the two notification flags of the selected instance to off."),
          true, false },
        { QStringLiteral("highfps"), QStringLiteral("tuning"),
          QStringLiteral("High frame-rate mode (up to 240 FPS)"),
          QStringLiteral("Enables the player's own high-FPS mode, raises its limit to 240 and "
                         "turns emulator VSync off. The game and the GPU still decide what is "
                         "reached. The FPS page raises the ceiling in the player binary; this is "
                         "the setting the player itself exposes."),
          true, false },
        { QStringLiteral("adb"), QStringLiteral("tuning"),
          QStringLiteral("Enable Android debugging (ADB)"),
          QStringLiteral("Lets this tool (and scrcpy, and your own scripts) talk to a running "
                         "instance. Remote ADB access stays off."),
          false, false },
        { QStringLiteral("gpu"), QStringLiteral("tuning"),
          QStringLiteral("Prefer the dedicated GPU"),
          QStringLiteral("Asks Windows to run HD-Player on the high-performance GPU."),
          false, true },
        { QStringLiteral("hosts"), QStringLiteral("system"),
          QStringLiteral("Block BlueStacks ad domains"),
          QStringLiteral("Adds one tagged block to the Windows hosts file with the two BlueStacks "
                         "advertising domains. Everything else in that file is left exactly as it "
                         "is; rewarded in-game ads can stop working."),
          false, true },
        { QStringLiteral("removex"), QStringLiteral("system"),
          QStringLiteral("Remove BlueStacks X / Store"),
          QStringLiteral("Moves the cloud player folder aside on its own drive. The local player, "
                         "its disks and every instance stay where they are."),
          false, true },
        { QStringLiteral("removeservices"), QStringLiteral("system"),
          QStringLiteral("Remove BlueStacks Services"),
          QStringLiteral("Moves the separate cloud services companion aside the same way."),
          false, true },
    };
    return table;
}

const Option *findOption(const QString &id)
{
    for (const auto &option : optionTable())
        if (option.id == id) return &option;
    return nullptr;
}

// The two shapes a BlueStacks flag takes. Anything else is left alone: a build
// that spells a setting differently keeps its value instead of getting a value
// the player would not understand.
bool offValue(const DebloatConfig::File &config, const QString &key, QString *value)
{
    const QString current = config.value(key);
    if (current == QLatin1String("1")) *value = QStringLiteral("0");
    else if (current == QLatin1String("true")) *value = QStringLiteral("false");
    else return false;
    return true;
}

bool isOff(const QString &value)
{
    return value == QLatin1String("0") || value == QLatin1String("false");
}

// ---- state file ---------------------------------------------------------
QString stateDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/debloat");
}

QString statePath()
{
    return stateDir() + QStringLiteral("/state.json");
}

struct StoredConfig
{
    QString path;
    QList<Change> changes;
};

struct StoredCloud
{
    QString name;
    QString from;
    QString to;
};

struct Record
{
    QList<StoredConfig> configs;
    bool hosts = false;
    QList<StoredCloud> cloud;
    bool gpu = false;
    QString gpuPath;
    QString gpuBefore;
};

Record loadRecord()
{
    Record record;
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly)) return record;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    for (const auto &value : root.value(QStringLiteral("configs")).toArray()) {
        const QJsonObject object = value.toObject();
        StoredConfig stored;
        stored.path = object.value(QStringLiteral("path")).toString();
        for (const auto &change : object.value(QStringLiteral("changes")).toArray()) {
            const QJsonObject entry = change.toObject();
            stored.changes << Change { entry.value(QStringLiteral("key")).toString(),
                                       entry.value(QStringLiteral("before")).toString(),
                                       entry.value(QStringLiteral("after")).toString() };
        }
        if (!stored.path.isEmpty() && !stored.changes.isEmpty()) record.configs << stored;
    }
    record.hosts = root.value(QStringLiteral("hosts")).toBool(false);
    for (const auto &value : root.value(QStringLiteral("cloud")).toArray()) {
        const QJsonObject object = value.toObject();
        const StoredCloud entry { object.value(QStringLiteral("name")).toString(),
                                  object.value(QStringLiteral("from")).toString(),
                                  object.value(QStringLiteral("to")).toString() };
        if (!entry.from.isEmpty() && !entry.to.isEmpty()) record.cloud << entry;
    }
    const QJsonObject gpu = root.value(QStringLiteral("gpu")).toObject();
    record.gpu = gpu.value(QStringLiteral("set")).toBool(false);
    record.gpuPath = gpu.value(QStringLiteral("path")).toString();
    record.gpuBefore = gpu.value(QStringLiteral("before")).toString();
    return record;
}

bool saveRecord(const Record &record)
{
    QJsonArray configs;
    for (const auto &stored : record.configs) {
        QJsonArray changes;
        for (const auto &change : stored.changes)
            changes.append(QJsonObject { { QStringLiteral("key"), change.key },
                                         { QStringLiteral("before"), change.before },
                                         { QStringLiteral("after"), change.after } });
        configs.append(QJsonObject { { QStringLiteral("path"), stored.path },
                                     { QStringLiteral("changes"), changes } });
    }
    QJsonArray cloud;
    for (const auto &entry : record.cloud)
        cloud.append(QJsonObject { { QStringLiteral("name"), entry.name },
                                   { QStringLiteral("from"), entry.from },
                                   { QStringLiteral("to"), entry.to } });
    QJsonObject root { { QStringLiteral("version"), int(kStateVersion) },
                       { QStringLiteral("configs"), configs },
                       { QStringLiteral("hosts"), record.hosts },
                       { QStringLiteral("cloud"), cloud } };
    if (record.gpu || !record.gpuPath.isEmpty())
        root.insert(QStringLiteral("gpu"), QJsonObject { { QStringLiteral("set"), record.gpu },
                                                         { QStringLiteral("path"), record.gpuPath },
                                                         { QStringLiteral("before"), record.gpuBefore } });
    QDir().mkpath(stateDir());
    QSaveFile file(statePath());
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

void setConfigChanges(Record *record, const QString &confPath, const QList<Change> &changes)
{
    for (int i = record->configs.size() - 1; i >= 0; --i)
        if (QDir::cleanPath(record->configs.at(i).path).compare(QDir::cleanPath(confPath),
                                                                Qt::CaseInsensitive) == 0)
            record->configs.removeAt(i);
    if (changes.isEmpty()) return;
    record->configs << StoredConfig { confPath, changes };
}

QList<Change> configChanges(const Record &record, const QString &confPath)
{
    for (const auto &stored : record.configs)
        if (QDir::cleanPath(stored.path).compare(QDir::cleanPath(confPath), Qt::CaseInsensitive) == 0)
            return stored.changes;
    return {};
}

bool writeFileAtomic(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(bytes);
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QByteArray readFileBytes(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot read %1: %2").arg(path, file.errorString());
        return {};
    }
    return file.readAll();
}

// One full copy of a file before it is written. Keeping three per kind means a
// wrong choice is undone by hand even if the per-key record was lost.
void keepBackupOf(const QString &sourcePath, const QString &prefix)
{
    const QByteArray bytes = readFileBytes(sourcePath, nullptr);
    if (bytes.isEmpty()) return;
    const QString dir = backupFolder();
    if (!QDir().mkpath(dir)) return;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    writeFileAtomic(QDir(dir).filePath(QStringLiteral("%1-%2.conf").arg(prefix, stamp)), bytes, nullptr);
    QStringList names = QDir(dir).entryList({ prefix + QStringLiteral("-*.conf") }, QDir::Files,
                                            QDir::Name);
    std::sort(names.begin(), names.end());
    while (names.size() > 3) QFile::remove(QDir(dir).filePath(names.takeFirst()));
}

void keepBackup(const QString &confPath)
{
    keepBackupOf(confPath, QStringLiteral("bluestacks"));
}

#ifdef Q_OS_WIN
const QString kGpuKey = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\DirectX\\UserGpuPreferences");
#endif

} // namespace

// ---------------------------------------------------------------------------
// switches
// ---------------------------------------------------------------------------

QStringList adKeys()
{
    return { QStringLiteral("bst.enable_programmatic_ads"),
             QStringLiteral("bst.enable_boot_banner"),
             QStringLiteral("bst.enable_android_ads_test_app"),
             QStringLiteral("bst.feature.programmatic_ads"),
             QStringLiteral("bst.feature.enable_boot_promotion_grid"),
             QStringLiteral("bst.feature.show_gp_ads") };
}

QStringList telemetryKeys()
{
    return { QStringLiteral("bst.feature.android_ads_stats"),
             QStringLiteral("bst.feature.app_install_stats"),
             QStringLiteral("bst.feature.send_auto_record_stats"),
             QStringLiteral("bst.feature.send_internal_notification_stats"),
             QStringLiteral("bst.feature.send_notification_stats"),
             QStringLiteral("bst.feature.send_nowbux_login_boot_stats"),
             QStringLiteral("bst.feature.send_offer_stats"),
             QStringLiteral("bst.feature.send_programmatic_ads_boot_stats"),
             QStringLiteral("bst.feature.send_programmatic_ads_click_stats"),
             QStringLiteral("bst.feature.send_programmatic_ads_fill_stats"),
             QStringLiteral("bst.feature.send_usage_state_stats"),
             QStringLiteral("bst.feature.usage_stats") };
}

QStringList downloadKeys()
{
    return { QStringLiteral("bst.enable_smart_downloads"),
             QStringLiteral("bst.feature.smart_downloads") };
}

QStringList cloudKeys()
{
    return { QStringLiteral("bst.feature.bluestacksX"),
             QStringLiteral("bst.enable_bsx_app_shortcuts"),
             QStringLiteral("bst.launch_store_on_boot"),
             QStringLiteral("bst.feature.show_cloud_instance"),
             QStringLiteral("bst.feature.nowgg_cloud_upload_enabled"),
             QStringLiteral("bst.feature.auto_upload_nowgg_recording"),
             QStringLiteral("bst.feature.auto_upload_nowgg_moments"),
             QStringLiteral("bst.enable_auto_upload_recording"),
             QStringLiteral("bst.feature.nowgg_login_popup"),
             QStringLiteral("bst.feature.nowbux"),
             QStringLiteral("bst.show_nowbux_rewards_red_dot_onboarding"),
             QStringLiteral("bst.feature.show_moments"),
             QStringLiteral("bst.enable_ai_highlights"),
             QStringLiteral("bst.feature.show_ai_highlights"),
             QStringLiteral("bst.ai.enabled"),
             QStringLiteral("bst.feature.blueai"),
             QStringLiteral("bst.feature.ai_chat"),
             QStringLiteral("bst.feature.popout_ai_chat"),
             QStringLiteral("bst.feature.creator_studio"),
             QStringLiteral("bst.feature.live_stream") };
}

const QList<Option> &options()
{
    return optionTable();
}

QString optionLabel(const QString &id)
{
    const Option *option = findOption(id);
    return option ? option->label : id;
}

QString gpuPreferenceValue()
{
    return QStringLiteral("GpuPreference=2;");
}

// ---------------------------------------------------------------------------
// plan
// ---------------------------------------------------------------------------

Plan planFor(const QByteArray &configBytes, const Request &request)
{
    Plan plan;
    const DebloatConfig::File config = DebloatConfig::File::parse(configBytes);
    if (!config.isValid()) {
        plan.error = config.error();
        return plan;
    }

    QMap<QString, QString> wanted;
    const auto flipAll = [&](const QStringList &keys) {
        for (const QString &key : keys) {
            if (!config.contains(key)) {
                plan.skipped << QStringLiteral("%1: not declared by this player").arg(key);
                continue;
            }
            QString value;
            const QString current = config.value(key);
            if (isOff(current)) continue;
            if (offValue(config, key, &value)) wanted.insert(key, value);
            else plan.skipped << QStringLiteral("%1: unexpected value \"%2\"").arg(key, current);
        }
    };
    const auto setTo = [&](const QString &key, const QString &value) {
        if (!config.contains(key)) {
            plan.skipped << QStringLiteral("%1: not declared by this player").arg(key);
            return;
        }
        if (config.value(key) != value) wanted.insert(key, value);
    };

    const QStringList chosen = request.options;
    const QString instance = request.instance;
    const auto instanceKey = [&](const QString &name) {
        return QStringLiteral("bst.instance.%1.%2").arg(instance, name);
    };
    const bool hasInstance = !instance.isEmpty() && config.instances().contains(instance);

    if (chosen.contains(QStringLiteral("ads"))) {
        flipAll(adKeys());
        if (hasInstance) setTo(instanceKey(QStringLiteral("split_ad_enabled")), QStringLiteral("0"));
    }
    if (chosen.contains(QStringLiteral("telemetry"))) flipAll(telemetryKeys());
    if (chosen.contains(QStringLiteral("downloads"))) flipAll(downloadKeys());
    if (chosen.contains(QStringLiteral("cloud"))) {
        flipAll(cloudKeys());
        plan.notes << QStringLiteral("Disabling the AI flags also removes the Ana AI and live-stream "
                                     "buttons from the sidebar and from the top-bar row.");
    }
    if (chosen.contains(QStringLiteral("quiet"))) {
        if (hasInstance) {
            setTo(instanceKey(QStringLiteral("enable_notifications")), QStringLiteral("0"));
            setTo(instanceKey(QStringLiteral("enable_mobile_notifications")), QStringLiteral("0"));
        } else {
            plan.notes << QStringLiteral("Read and silence notifications: choose an instance first.");
        }
    }
    if (chosen.contains(QStringLiteral("highfps"))) {
        if (hasInstance) {
            setTo(instanceKey(QStringLiteral("enable_high_fps")), QStringLiteral("1"));
            setTo(instanceKey(QStringLiteral("max_fps")), QStringLiteral("240"));
            setTo(instanceKey(QStringLiteral("enable_vsync")), QStringLiteral("0"));
            plan.notes << QStringLiteral("High frame-rate mode is the player's own setting; the game "
                                         "and the GPU still decide what is actually drawn.");
        } else {
            plan.notes << QStringLiteral("High frame-rate mode needs an instance; choose one first.");
        }
    }
    if (chosen.contains(QStringLiteral("adb"))) {
        setTo(QStringLiteral("bst.enable_adb_access"), QStringLiteral("1"));
        setTo(QStringLiteral("bst.enable_adb_remote_access"), QStringLiteral("0"));
    }
    if (chosen.contains(QStringLiteral("gpu"))) plan.gpu = true;
    if (chosen.contains(QStringLiteral("hosts"))) plan.hosts = true;
    if (chosen.contains(QStringLiteral("removex"))) plan.cloudIds << QStringLiteral("x");
    if (chosen.contains(QStringLiteral("removeservices"))) plan.cloudIds << QStringLiteral("services");

    for (auto it = wanted.constBegin(); it != wanted.constEnd(); ++it)
        plan.changes << Change { it.key(), config.value(it.key()), it.value() };

    plan.ok = true;
    return plan;
}

// ---------------------------------------------------------------------------
// hosts file
// ---------------------------------------------------------------------------

QString hostsStartMarker()
{
    return QStringLiteral("# BEGIN BSI CUSTOMIZER DEBLOAT");
}

QString hostsEndMarker()
{
    return QStringLiteral("# END BSI CUSTOMIZER DEBLOAT");
}

QStringList hostsDomains()
{
    return { QStringLiteral("ads.bluestacks.com"), QStringLiteral("adsdk.bluestacks.com") };
}

namespace {

QList<QByteArray> splitLines(const QByteArray &input, QList<QByteArray> *newlines)
{
    QList<QByteArray> lines;
    QList<QByteArray> ends;
    int offset = 0;
    while (offset < input.size()) {
        int next = input.indexOf('\n', offset);
        if (next < 0) {
            lines << input.mid(offset);
            ends << QByteArray();
            break;
        }
        const int bodyEnd = next > offset && input.at(next - 1) == '\r' ? next - 1 : next;
        lines << input.mid(offset, bodyEnd - offset);
        ends << (bodyEnd == next ? QByteArray("\n") : QByteArray("\r\n"));
        offset = next + 1;
    }
    if (newlines) *newlines = ends;
    return lines;
}

bool isComment(const QByteArray &line)
{
    const QByteArray trimmed = line.trimmed();
    return trimmed.isEmpty() || trimmed.startsWith('#');
}

} // namespace

bool hostsEdits(const QByteArray &input, bool install, QByteArray *out, QString *error)
{
    const auto fail = [&](const QString &why) {
        if (error) *error = why;
        return false;
    };
    if (input.startsWith("\xff\xfe") || input.startsWith("\xfe\xff"))
        return fail(QStringLiteral("The hosts file is not UTF-8/ASCII; it was left untouched."));
    for (const char c : input)
        if (static_cast<unsigned char>(c) < 32 && c != '\r' && c != '\n' && c != '\t')
            return fail(QStringLiteral("The hosts file contains control bytes; it was left untouched."));

    QList<QByteArray> newlines;
    QList<QByteArray> lines = splitLines(input, &newlines);
    QByteArray eol = "\r\n";
    for (const QByteArray &end : newlines)
        if (!end.isEmpty()) {
            eol = end;
            break;
        }

    int start = -1, end = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QByteArray trimmed = lines.at(i).trimmed();
        if (trimmed == hostsStartMarker().toUtf8()) {
            if (start >= 0) return fail(QStringLiteral("The hosts file has two begin markers; "
                                                       "it was left untouched."));
            start = i;
        } else if (trimmed == hostsEndMarker().toUtf8()) {
            if (end >= 0) return fail(QStringLiteral("The hosts file has two end markers; "
                                                     "it was left untouched."));
            end = i;
        }
    }
    if ((start >= 0) != (end >= 0) || (start >= 0 && end < start))
        return fail(QStringLiteral("The debloat block in the hosts file is incomplete; "
                                   "fix or delete it by hand first."));

    QStringList block;
    block << hostsStartMarker();
    for (const QString &domain : hostsDomains())
        block << QStringLiteral("0.0.0.0 ") + domain;
    block << hostsEndMarker();

    QByteArray result;
    const auto appendLine = [&](const QByteArray &line, int index, bool last) {
        result += line;
        if (!last) result += newlines.value(index, eol);
    };
    if (install) {
        // The block replaces itself: a later run with a different domain list
        // rewrites only these lines.
        for (int i = 0; i < lines.size(); ++i) {
            if (start >= 0 && i >= start && i <= end) {
                if (i == start)
                    for (int b = 0; b < block.size(); ++b) {
                        result += block.at(b).toUtf8();
                        result += eol;
                    }
                continue;
            }
            appendLine(lines.at(i), i, i == lines.size() - 1);
        }
        if (start < 0) {
            if (!result.isEmpty() && !result.endsWith('\n')) result += eol;
            for (const QString &line : block) {
                result += line.toUtf8();
                result += eol;
            }
        }
    } else {
        if (start < 0) {
            if (out) *out = input;
            return true;
        }
        for (int i = 0; i < lines.size(); ++i) {
            if (i >= start && i <= end) continue;
            appendLine(lines.at(i), i, i == lines.size() - 1);
        }
    }
    if (out) *out = result;
    return true;
}

// ---------------------------------------------------------------------------
// cloud companions
// ---------------------------------------------------------------------------

QString cloudParkedPath(const QString &path)
{
    return path + QStringLiteral(".bsi-disabled");
}

namespace {

struct CloudDefinition
{
    QString id;
    QString name;
    QStringList roots;   // environment variables that may hold it
    QString subfolder;
    QString marker;
};

QList<CloudDefinition> cloudDefinitions()
{
    return {
        { QStringLiteral("x"), QStringLiteral("BlueStacks X / Store"),
          { QStringLiteral("ProgramFiles(x86)"), QStringLiteral("ProgramFiles") },
          QStringLiteral("BlueStacks X"), QStringLiteral("BlueStacks X.exe") },
        { QStringLiteral("services"), QStringLiteral("BlueStacks Services"),
          { QStringLiteral("LOCALAPPDATA") },
          QStringLiteral("Programs/bluestacks-services"), QStringLiteral("BlueStacksServices.exe") },
    };
}

} // namespace

QList<CloudProduct> cloudProductsAt(const QStringList &roots)
{
    QList<CloudProduct> found;
    const auto definitions = cloudDefinitions();
    for (const auto &definition : definitions) {
        for (const QString &root : roots) {
            const QString base = QDir(root).filePath(definition.subfolder);
            const QString parked = cloudParkedPath(base);
            const QFileInfo marker(QDir(base).filePath(definition.marker));
            const QFileInfo parkedMarker(QDir(parked).filePath(definition.marker));
            CloudProduct product;
            product.id = definition.id;
            product.name = definition.name;
            if (marker.isFile()) {
                product.path = QDir::cleanPath(base);
            } else if (parkedMarker.isFile()) {
                product.path = QDir::cleanPath(parked);
                product.moved = true;
            } else {
                continue;
            }
            bool duplicate = false;
            for (const auto &existing : found)
                if (existing.id == product.id && existing.moved == product.moved) duplicate = true;
            if (!duplicate) found << product;
        }
    }
    return found;
}

QList<CloudProduct> cloudProducts()
{
    QStringList roots;
    for (const auto &definition : cloudDefinitions())
        for (const QString &variable : definition.roots) {
            const QString value = qEnvironmentVariable(variable.toUtf8().constData());
            if (!value.isEmpty() && !roots.contains(value)) roots << value;
        }
    return cloudProductsAt(roots);
}

// ---------------------------------------------------------------------------
// status / apply / restore
// ---------------------------------------------------------------------------

QString backupFolder()
{
    return stateDir() + QStringLiteral("/backups");
}

QStringList backups()
{
    return QDir(backupFolder()).entryList({ QStringLiteral("bluestacks-*.conf") }, QDir::Files,
                                          QDir::Name);
}

Status status(const QString &confPath)
{
    Status state;
    state.conf = confPath;
    if (confPath.isEmpty() || !QFileInfo::exists(confPath)) {
        state.detail = QStringLiteral("Select the bluestacks.conf of your installation first.");
        return state;
    }
    const QByteArray bytes = readFileBytes(confPath, nullptr);
    const DebloatConfig::File config = DebloatConfig::File::parse(bytes);
    if (!config.isValid()) {
        state.detail = config.error();
        return state;
    }
    state.supported = true;
    state.instances = config.instances();
    if (!state.instances.isEmpty()) state.instance = state.instances.first();

    const Record record = loadRecord();
    state.hostsInstalled = record.hosts;
    state.anyCloudMoved = !record.cloud.isEmpty();
    state.anyRecord = state.hostsInstalled || state.anyCloudMoved || !record.configs.isEmpty();
    state.applied = !configChanges(record, confPath).isEmpty() || state.hostsInstalled
        || state.anyCloudMoved;

    for (const auto &option : optionTable()) {
        Request request;
        request.options << option.id;
        request.instance = state.instances.value(0);
        const Plan plan = planFor(bytes, request);
        const bool inPlace = plan.ok && plan.changes.isEmpty() && plan.cloudIds.isEmpty()
            && !plan.hosts && !plan.gpu;
        if (inPlace) state.already << option.id;
    }
    // The hosts file, the GPU preference and the two cloud folders are not
    // configuration values, so their own state decides whether their switch is
    // already in place.
    const auto markDone = [&](const QString &id) {
        if (!state.already.contains(id)) state.already << id;
    };
    if (state.hostsInstalled) markDone(QStringLiteral("hosts"));
    for (const auto &product : cloudProducts()) {
        state.cloud << QVariantMap { { QStringLiteral("id"), product.id },
                                     { QStringLiteral("name"), product.name },
                                     { QStringLiteral("path"), QDir::toNativeSeparators(product.path) },
                                     { QStringLiteral("moved"), product.moved } };
        if (!product.moved) continue;
        markDone(product.id == QLatin1String("x") ? QStringLiteral("removex")
                                                  : QStringLiteral("removeservices"));
    }
    state.pendingChanges = configChanges(record, confPath).size() + (state.hostsInstalled ? 1 : 0);
    state.detail = state.applied
        ? QStringLiteral("Debloat changes are recorded for this installation.")
        : QStringLiteral("Ready.");
    return state;
}

QVariantMap apply(const QString &confPath, const QString &exePath, const Request &request)
{
    const auto refuse = [](const QString &message) {
        return QVariantMap { { QStringLiteral("ok"), false }, { QStringLiteral("msg"), message } };
    };
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return refuse(QStringLiteral("Close BlueStacks first: it rewrites bluestacks.conf from memory "
                                     "when it exits, so a change made while it runs is lost."));
    if (confPath.isEmpty() || !QFileInfo::exists(confPath))
        return refuse(QStringLiteral("Select the bluestacks.conf of your installation first."));
    QString error;
    const QByteArray original = readFileBytes(confPath, &error);
    if (original.isEmpty() && !error.isEmpty()) return refuse(error);

    const Plan plan = planFor(original, request);
    if (!plan.ok) return refuse(plan.error);

    // Build every piece before writing anything, so a failure cannot leave half
    // the options applied.
    QByteArray confAfter;
    if (!plan.changes.isEmpty()) {
        QMap<QString, QString> changes;
        for (const auto &change : plan.changes) changes.insert(change.key, change.after);
        const DebloatConfig::File config = DebloatConfig::File::parse(original);
        if (!config.applyEdits(changes, &confAfter, &error)) return refuse(error);
    }

    QStringList cloudMoves;
    QList<StoredCloud> moves;
    for (const auto &product : cloudProducts()) {
        if (product.moved || !plan.cloudIds.contains(product.id)) continue;
        const QFileInfo source(product.path);
        if (source.isSymLink())
            return refuse(QStringLiteral("%1 is a link; it is not removed automatically.")
                              .arg(QDir::toNativeSeparators(product.path)));
        if (!QFileInfo(QDir(source.absolutePath()).filePath(source.fileName())).isDir())
            return refuse(QStringLiteral("%1 is not a folder.").arg(QDir::toNativeSeparators(product.path)));
        const QString parked = cloudParkedPath(product.path);
        if (QFileInfo::exists(parked))
            return refuse(QStringLiteral("%1 already exists; move it away first.")
                              .arg(QDir::toNativeSeparators(parked)));
        moves << StoredCloud { product.name, product.path, parked };
    }

    // The hosts file is read and the new text built now, but nothing is written
    // until every other piece has been validated: a refused plan must leave the
    // machine exactly as it was.
    const QString hostsPath = qEnvironmentVariable("SystemRoot", "C:/Windows")
        + QStringLiteral("/System32/drivers/etc/hosts");
    QString hostsError;
    QByteArray hostsBefore;
    QByteArray hostsAfter;
    bool hostsChanged = false;
    if (plan.hosts) {
        if (QFileInfo::exists(hostsPath)) {
            hostsBefore = readFileBytes(hostsPath, &hostsError);
            if (!hostsError.isEmpty()) return refuse(hostsError);
        }
        if (!hostsEdits(hostsBefore, true, &hostsAfter, &hostsError)) return refuse(hostsError);
        hostsChanged = hostsAfter != hostsBefore;
    }

    if (!plan.changes.isEmpty()) {
        keepBackup(confPath);
        if (!writeFileAtomic(confPath, confAfter, &error)) return refuse(error);
    }
    for (const auto &move : moves) {
        if (!QDir().rename(move.from, move.to))
            return refuse(QStringLiteral("%1 could not be moved aside; the settings were already "
                                         "written and can be restored from the Debloat tab.")
                              .arg(QDir::toNativeSeparators(move.from)));
        cloudMoves << move.name;
    }
    if (hostsChanged) {
        keepBackupOf(hostsPath, QStringLiteral("hosts"));
        QString writeError;
        if (!writeFileAtomic(hostsPath, hostsAfter, &writeError)) return refuse(writeError);
    }

    bool gpuSet = false;
#ifdef Q_OS_WIN
    if (plan.gpu && !exePath.isEmpty()) {
        QSettings preferences(kGpuKey, QSettings::NativeFormat);
        const QString key = QDir::toNativeSeparators(exePath);
        if (preferences.value(key).toString() != gpuPreferenceValue()) {
            preferences.setValue(key, gpuPreferenceValue());
            gpuSet = true;
        }
    }
#else
    Q_UNUSED(exePath)
#endif

    Record record = loadRecord();
    if (!plan.changes.isEmpty()) {
        QList<Change> merged = plan.changes;
        for (const auto &previous : configChanges(record, confPath)) {
            bool present = false;
            for (const auto &change : merged)
                if (change.key == previous.key) present = true;
            if (!present) merged << previous;
        }
        setConfigChanges(&record, confPath, merged);
    }
    if (hostsChanged) record.hosts = true;
    for (const auto &move : moves) record.cloud << move;
#ifdef Q_OS_WIN
    if (gpuSet && !record.gpu) {
        record.gpu = true;
        record.gpuPath = exePath;
        record.gpuBefore.clear();
    }
#endif
    if (!saveRecord(record))
        return refuse(QStringLiteral("The change was written, but its undo record could not be "
                                     "saved. Use a backup from %1 to restore by hand.")
                          .arg(QDir::toNativeSeparators(backupFolder())));

    QStringList parts;
    if (!plan.changes.isEmpty())
        parts << QStringLiteral("%1 setting(s)").arg(plan.changes.size());
    if (hostsChanged) parts << QStringLiteral("the hosts block");
    if (!cloudMoves.isEmpty()) parts << cloudMoves.join(QStringLiteral(", "));
    if (gpuSet) parts << QStringLiteral("the GPU preference");
    QString message = parts.isEmpty() ? QStringLiteral("Everything selected was already in place.")
                                      : QStringLiteral("Applied: %1.").arg(parts.join(QStringLiteral(", ")));
    if (plan.skipped.size() > 0)
        message += QStringLiteral(" %1 setting(s) this build does not declare were left alone.")
                       .arg(plan.skipped.size());
    for (const QString &note : plan.notes) message += QLatin1Char(' ') + note;
    if (!plan.changes.isEmpty())
        message += QStringLiteral(" Start BlueStacks to see the effect; the undo record is kept.");
    return { { QStringLiteral("ok"), true }, { QStringLiteral("msg"), message },
             { QStringLiteral("changes"), plan.changes.size() } };
}

QVariantMap restore(const QString &confPath, const QString &exePath)
{
    Q_UNUSED(exePath);
    const auto refuse = [](const QString &message) {
        return QVariantMap { { QStringLiteral("ok"), false }, { QStringLiteral("msg"), message } };
    };
    Record record = loadRecord();
    const QList<Change> stored = configChanges(record, confPath);
    QStringList reverted, kept;
    QByteArray confBytes;
    bool confChanged = false;
    if (!stored.isEmpty()) {
        QString error;
        confBytes = readFileBytes(confPath, &error);
        if (confBytes.isEmpty() && !error.isEmpty()) return refuse(error);
        const DebloatConfig::File config = DebloatConfig::File::parse(confBytes);
        if (!config.isValid()) return refuse(config.error());
        QMap<QString, QString> changes;
        for (const auto &change : stored) {
            if (!config.contains(change.key)) continue;
            const QString current = config.value(change.key);
            // Only a value this tool wrote is put back; a setting the user
            // changed again since then keeps their value.
            if (current == change.after) {
                changes.insert(change.key, change.before);
                reverted << change.key;
            } else if (current != change.before) {
                kept << change.key;
            } else {
                reverted << change.key;
            }
        }
        if (!changes.isEmpty()) {
            if (!config.applyEdits(changes, &confBytes, &error)) return refuse(error);
            confChanged = true;
        }
    }

    const QString hostsPath = qEnvironmentVariable("SystemRoot", "C:/Windows")
        + QStringLiteral("/System32/drivers/etc/hosts");
    bool hostsRestored = false;
    if (record.hosts && QFileInfo::exists(hostsPath)) {
        QString error;
        const QByteArray before = readFileBytes(hostsPath, &error);
        QByteArray after;
        if (!hostsEdits(before, false, &after, &error))
            kept << QStringLiteral("hosts");
        else if (after != before) {
            keepBackupOf(hostsPath, QStringLiteral("hosts"));
            if (!writeFileAtomic(hostsPath, after, &error)) return refuse(error);
            hostsRestored = true;
        } else {
            hostsRestored = true;
        }
    }

    QStringList restoredCloud;
    for (const auto &move : record.cloud) {
        if (!QFileInfo::exists(move.to)) continue;
        if (QFileInfo::exists(move.from)) continue;
        if (QDir().rename(move.to, move.from)) restoredCloud << move.name;
    }

#ifdef Q_OS_WIN
    if (record.gpu) {
        QSettings preferences(kGpuKey, QSettings::NativeFormat);
        const QString key = QDir::toNativeSeparators(record.gpuPath);
        if (!key.isEmpty()) {
            if (record.gpuBefore.isEmpty()) preferences.remove(key);
            else preferences.setValue(key, record.gpuBefore);
        }
    }
#endif

    if (confChanged) {
        QString error;
        if (!writeFileAtomic(confPath, confBytes, &error)) return refuse(error);
    }
    setConfigChanges(&record, confPath, {});
    if (hostsRestored) record.hosts = false;
    record.cloud.clear();
#ifdef Q_OS_WIN
    record.gpu = false;
    record.gpuPath.clear();
    record.gpuBefore.clear();
#endif
    saveRecord(record);

    QStringList parts;
    if (!reverted.isEmpty()) parts << QStringLiteral("%1 setting(s)").arg(reverted.size());
    if (hostsRestored) parts << QStringLiteral("the hosts block");
    if (!restoredCloud.isEmpty()) parts << restoredCloud.join(QStringLiteral(", "));
    if (parts.isEmpty() && kept.isEmpty())
        return { { QStringLiteral("ok"), true },
                 { QStringLiteral("msg"), QStringLiteral("Nothing to restore for this installation.") } };
    QString message = parts.isEmpty() ? QStringLiteral("Nothing was put back.")
                                      : QStringLiteral("Restored: %1.").arg(parts.join(QStringLiteral(", ")));
    if (!kept.isEmpty())
        message += QStringLiteral(" %1 value(s) were changed after this tool wrote them and were "
                                  "left as they are (%2).").arg(kept.size()).arg(kept.join(QStringLiteral(", ")));
    return { { QStringLiteral("ok"), true }, { QStringLiteral("msg"), message } };
}

} // namespace Debloat
