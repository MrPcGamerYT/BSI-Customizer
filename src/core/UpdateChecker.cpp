#include "core/UpdateChecker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMessageAuthenticationCode>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#ifndef BSI_VERSION
#  define BSI_VERSION "0.0.0"
#endif

namespace {

// Same cleaning as AuthManager::loadConfig(): GitHub secrets are often pasted
// with a ```json fence or a BOM, so the packaging is stripped before parsing.
QByteArray cleanJsonBlob(QByteArray bytes)
{
    bytes = bytes.trimmed();
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    if (bytes.startsWith("```")) {
        const int firstNewline = bytes.indexOf('\n');
        if (firstNewline >= 0)
            bytes = bytes.mid(firstNewline + 1);
        const int closingFence = bytes.lastIndexOf("```");
        if (closingFence >= 0)
            bytes = bytes.left(closingFence);
        bytes = bytes.trimmed();
    }
    return bytes;
}

// Snowflake IDs pasted unquoted arrive as doubles.
QString jsonText(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble()) {
        const double raw = value.toDouble();
        if (raw > 0 && raw <= 9.2e18)
            return QString::number(qulonglong(raw));
    }
    return {};
}

bool parsesAsObject(const QByteArray &raw)
{
    const QByteArray json = cleanJsonBlob(raw);
    if (json.isEmpty())
        return false;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    return error.error == QJsonParseError::NoError && doc.isObject();
}

// The updater config lives in the same auth.json AuthManager reads, so the
// source order is deliberately identical (see docs/AUTH.md):
//   $ZLP_AUTH_JSON (alias $BSIC_AUTH_JSON) -> $BSIC_AUTH_FILE -> next to the
//   EXE -> app data -> the blob baked in at build time.
QByteArray authJsonBytes(QString *source)
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *name : { "ZLP_AUTH_JSON", "BSIC_AUTH_JSON" }) {
        const QByteArray blob = env.value(QLatin1String(name)).toUtf8();
        if (parsesAsObject(blob)) {
            if (source)
                *source = QStringLiteral("environment (%1)").arg(QLatin1String(name));
            return cleanJsonBlob(blob);
        }
    }

    QStringList candidates;
    const QString envFile = env.value(QStringLiteral("BSIC_AUTH_FILE")).trimmed();
    if (!envFile.isEmpty())
        candidates << envFile;
    if (QCoreApplication::instance())
        candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/auth.json");
    candidates << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/auth.json");

    for (const QString &path : candidates) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        file.close();
        if (parsesAsObject(bytes)) {
            if (source)
                *source = path;
            return cleanJsonBlob(bytes);
        }
    }

    QFile embedded(QStringLiteral(":/bsic_auth/auth.json"));
    if (embedded.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = embedded.readAll();
        if (parsesAsObject(bytes)) {
            if (source)
                *source = QStringLiteral("embedded auth.json (build-time ZLP_AUTH_JSON)");
            return cleanJsonBlob(bytes);
        }
    }
    return {};
}

bool envFlag(const QProcessEnvironment &env, const char *name)
{
    const QString value = env.value(QLatin1String(name)).trimmed().toLower();
    return value == QLatin1String("1") || value == QLatin1String("true")
           || value == QLatin1String("yes") || value == QLatin1String("on");
}

QString powerShellPath()
{
    const QString systemRoot = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    const QString bundled = systemRoot
                            + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
    if (QFileInfo::exists(bundled))
        return bundled;
    const QString found = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
    return found.isEmpty() ? bundled : found;
}

QString singleQuoted(const QString &path)
{
    return QStringLiteral("'") + QString(path).replace(QLatin1Char('\''), QLatin1String("''"))
           + QLatin1Char('\'');
}

} // namespace

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

UpdateChecker::Config UpdateChecker::loadConfig()
{
    Config config;
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    QString source;
    const QByteArray bytes = authJsonBytes(&source);
    config.sourceFile = source;

    QJsonObject updater;
    if (!bytes.isEmpty()) {
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        const QJsonValue value = root.value(QStringLiteral("updater"));
        if (value.isObject())
            updater = value.toObject();
    }

    // Per-field: environment wins over auth.json, auth.json over the default.
    auto text = [&env, &updater](const char *envName, const char *jsonName,
                                 const QString &fallback) {
        const QString fromEnv = env.value(QLatin1String(envName)).trimmed();
        if (!fromEnv.isEmpty())
            return fromEnv;
        const QString fromJson = jsonText(updater.value(QLatin1String(jsonName))).trimmed();
        if (!fromJson.isEmpty())
            return fromJson;
        return fallback;
    };

    config.apiBase = text("BSIC_UPDATE_API", "apiBase", config.apiBase).trimmed();
    while (config.apiBase.endsWith(QLatin1Char('/')))
        config.apiBase.chop(1);
    config.channelId = text("BSIC_UPDATE_CHANNEL_ID", "channelId", QString()).trimmed();
    config.token = text("BSIC_UPDATE_TOKEN", "botToken", QString()).trimmed();
    config.manifestName = text("BSIC_UPDATE_MANIFEST", "manifest", config.manifestName)
                              .trimmed();
    config.hmacKey = text("BSIC_UPDATE_HMAC_KEY", "hmacKey", QString()).trimmed();

    // A token pasted from the Discord docs often carries the "Bot " prefix.
    if (config.token.startsWith(QLatin1String("Bot "), Qt::CaseInsensitive))
        config.token = config.token.mid(4).trimmed();
    while (config.manifestName.startsWith(QLatin1Char('/')))
        config.manifestName.remove(0, 1);
    if (config.manifestName.isEmpty())
        config.manifestName = QStringLiteral("manifest.json");

    if (updater.value(QStringLiteral("enabled")).isBool()
        && !updater.value(QStringLiteral("enabled")).toBool())
        config.enabled = false;
    if (envFlag(env, "BSI_NO_UPDATER"))
        config.enabled = false;

    QStringList missing;
    if (config.channelId.isEmpty())
        missing << QStringLiteral("updater.channelId");
    else if (!config.channelId.contains(QRegularExpression(QStringLiteral("^[0-9]{5,25}$"))))
        missing << QStringLiteral("updater.channelId (must be the numeric channel ID)");
    if (config.token.isEmpty())
        missing << QStringLiteral("updater.botToken");
    if (!config.apiBase.startsWith(QLatin1String("https://")))
        missing << QStringLiteral("updater.apiBase (must be an https:// URL)");
    config.configured = missing.isEmpty();

    if (!config.enabled) {
        config.setupHint = QStringLiteral("Self-update is switched off in this build.");
    } else if (config.configured) {
        config.setupHint = QStringLiteral("Updates come from Discord channel %1%2.")
                               .arg(config.channelId,
                                    config.sourceFile.isEmpty()
                                        ? QString()
                                        : QStringLiteral(" (config: %1)").arg(config.sourceFile));
    } else {
        config.setupHint = QStringLiteral("Self-update needs %1 in auth.json - see docs/UPDATER.md.")
                               .arg(missing.join(QStringLiteral(", ")));
    }
    return config;
}

// ---------------------------------------------------------------------------
// Construction / state
// ---------------------------------------------------------------------------

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_config(loadConfig())
    , m_network(new QNetworkAccessManager(this))
{
    m_autoCheck = QSettings().value(QStringLiteral("updates/autoCheck"), true).toBool();
    m_skippedVersion = QSettings().value(QStringLiteral("updates/skippedVersion")).toString();

    if (!isConfigured())
        setState(QStringLiteral("disabled"), m_config.setupHint,
                 m_config.enabled ? QStringLiteral("bad") : QString());
    else
        startAutoCheck();
}

UpdateChecker::~UpdateChecker()
{
    if (m_sink) {
        m_sink->close();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply = nullptr;
    }
}

QString UpdateChecker::currentVersion() const
{
    return QStringLiteral(BSI_VERSION);
}

bool UpdateChecker::busy() const
{
    return m_state == QLatin1String("checking") || m_state == QLatin1String("downloading")
           || m_state == QLatin1String("applying") || m_state == QLatin1String("installing");
}

QString UpdateChecker::downloadSize() const
{
    const double bytes = m_latest.value(QStringLiteral("size")).toDouble(0);
    return bytes > 0 ? readableSize(qint64(bytes)) : QString();
}

bool UpdateChecker::skipped() const
{
    return !m_latest.isEmpty() && !m_skippedVersion.isEmpty()
           && m_skippedVersion == latestVersion();
}

void UpdateChecker::setAutoCheck(bool enabled)
{
    if (m_autoCheck == enabled)
        return;
    m_autoCheck = enabled;
    QSettings().setValue(QStringLiteral("updates/autoCheck"), enabled);
    emit autoCheckChanged();
    if (enabled)
        startAutoCheck();
}

void UpdateChecker::startAutoCheck()
{
    if (!isConfigured() || !m_autoCheck)
        return;
    // Give the window time to appear first: a self-update must never be what
    // delays the application's own start.
    QTimer::singleShot(4000, this, [this] {
        if (isConfigured() && m_state == QLatin1String("idle") && !m_available)
            check();
    });
}

void UpdateChecker::setState(const QString &state, const QString &note, const QString &kind)
{
    if (m_state == state && m_note == note && m_noteKind == kind)
        return;
    m_state = state;
    m_note = note;
    m_noteKind = kind;
    emit stateChanged();
}

void UpdateChecker::setProgress(qreal value)
{
    const qreal clamped = qBound<qreal>(0.0, value, 1.0);
    if (qFuzzyCompare(m_progress + 1.0, clamped + 1.0))
        return;
    m_progress = clamped;
    emit progressChanged();
}

void UpdateChecker::fail(const QString &note, const QString &detail)
{
    m_lastError = detail.isEmpty() ? note : detail;
    setState(QStringLiteral("error"), note, QStringLiteral("bad"));
}

// ---------------------------------------------------------------------------
// Check
// ---------------------------------------------------------------------------

void UpdateChecker::check()
{
    if (!isConfigured()) {
        setState(QStringLiteral("disabled"), m_config.setupHint, QStringLiteral("bad"));
        return;
    }
    if (busy())
        return;
    m_installAfterDownload = false;
    m_lastError.clear();
    setProgress(0);
    setState(QStringLiteral("checking"), QStringLiteral("Checking the Discord releases channel..."));

    const QUrl url(QStringLiteral("%1/channels/%2/messages?limit=50")
                       .arg(m_config.apiBase, m_config.channelId));
    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QByteArrayLiteral("Bot ") + m_config.token.toUtf8());
    request.setRawHeader("User-Agent", QByteArrayLiteral("BSI-Customizer/") + QByteArray(BSI_VERSION)
                                           + QByteArrayLiteral(" (self-updater)"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(m_config.timeoutSeconds * 1000);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        if (!reply)
            return;
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            networkError(reply, body, "check");
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            fail(describeHttpError(status, body));
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isArray()) {
            fail(QStringLiteral("Discord did not answer with a message list - check that the "
                                "channel ID belongs to a text channel the bot can read."));
            return;
        }
        const ManifestRef ref = findManifestMessage(doc.array(), m_config.manifestName);
        if (!ref.found) {
            fail(QStringLiteral("No '%1' attachment in the last 50 messages of channel %2. "
                                "Publish one with updater-bot/publish.mjs.")
                     .arg(m_config.manifestName, m_config.channelId));
            return;
        }
        if (!ref.manifestUrl.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
            fail(QStringLiteral("The manifest link is not https - refusing to trust it."));
            return;
        }
        m_pendingMessage = ref.message;

        QNetworkRequest manifestRequest(QUrl(ref.manifestUrl));
        manifestRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::NoLessSafeRedirectPolicy);
        manifestRequest.setRawHeader("User-Agent", QByteArrayLiteral("BSI-Customizer/")
                                                       + QByteArray(BSI_VERSION));
        manifestRequest.setTransferTimeout(m_config.timeoutSeconds * 1000);
        m_reply = m_network->get(manifestRequest);
        connect(m_reply, &QNetworkReply::finished, this, [this] {
            QNetworkReply *manifestReply = m_reply;
            m_reply = nullptr;
            if (!manifestReply)
                return;
            manifestReply->deleteLater();
            const QByteArray manifestBody = manifestReply->readAll();
            if (manifestReply->error() != QNetworkReply::NoError) {
                networkError(manifestReply, manifestBody, "manifest");
                return;
            }
            const int status =
                manifestReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status != 200) {
                fail(describeHttpError(status, manifestBody));
                return;
            }
            handleManifest(manifestBody);
        });
    });
}

void UpdateChecker::handleManifest(const QByteArray &bytes)
{
    QString error;
    const QJsonObject manifest = parseManifest(bytes, &error);
    if (manifest.isEmpty()) {
        fail(QStringLiteral("The update manifest is invalid: %1").arg(error), error);
        return;
    }

    if (!m_config.hmacKey.isEmpty()) {
        QString signatureError;
        if (!verifyManifestSignature(manifest, m_config.hmacKey, &signatureError)) {
            fail(QStringLiteral("The update manifest is not signed by this build's key: %1")
                     .arg(signatureError),
                 signatureError);
            return;
        }
    }

    const QString version = manifest.value(QStringLiteral("version")).toString();
    if (compareVersions(version, currentVersion()) <= 0) {
        m_available = false;
        m_latest = QJsonObject();
        emit releaseChanged();
        setState(QStringLiteral("uptodate"),
                 QStringLiteral("You are on the newest build (%1).").arg(currentVersion()),
                 QStringLiteral("good"));
        return;
    }

    m_latest = manifest;
    m_available = true;
    emit releaseChanged();

    // A build older than the manifest's minSupported must not stay put even if
    // the user pressed "Later" on it.
    const QString minSupported = manifest.value(QStringLiteral("minSupported")).toString();
    const bool forced = !minSupported.isEmpty()
                        && compareVersions(currentVersion(), minSupported) < 0;
    if (skipped() && !forced) {
        setState(QStringLiteral("available"),
                 QStringLiteral("Version %1 is available (you skipped it).").arg(version));
        return;
    }
    setState(QStringLiteral("available"),
             QStringLiteral("Version %1 is available - %2.")
                 .arg(version, readableSize(qint64(
                                     m_latest.value(QStringLiteral("size")).toDouble(0)))),
             QStringLiteral("good"));
}

void UpdateChecker::networkError(const QNetworkReply *reply, const QByteArray &body,
                                const char *what)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString detail = QStringLiteral("%1 failed: %2")
                               .arg(QLatin1String(what), reply->errorString());
    if (status > 0)
        fail(describeHttpError(status, body), detail);
    else
        fail(QStringLiteral("Could not reach Discord (%1). Check the connection, then try again.")
                 .arg(reply->errorString()),
             detail);
}

// ---------------------------------------------------------------------------
// Download
// ---------------------------------------------------------------------------

QString UpdateChecker::stagingDirectory() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + QStringLiteral("/BSI-Customizer-update");
    QDir().mkpath(dir);
    return dir;
}

void UpdateChecker::download()
{
    if (!isConfigured()) {
        setState(QStringLiteral("disabled"), m_config.setupHint, QStringLiteral("bad"));
        return;
    }
    if (readyToInstall()) {
        setState(QStringLiteral("ready"),
                 QStringLiteral("Version %1 is downloaded and verified.").arg(latestVersion()),
                 QStringLiteral("good"));
        return;
    }
    if (!m_available || m_latest.isEmpty()) {
        fail(QStringLiteral("There is no announced update to download - check again first."));
        return;
    }
    if (busy())
        return;
    beginDownload();
}

void UpdateChecker::beginDownload()
{
    const QString fileName = m_latest.value(QStringLiteral("file")).toString();
    const QString urlText = attachmentUrl(m_pendingMessage, fileName);
    if (urlText.isEmpty()) {
        fail(QStringLiteral("The release message has no attachment named '%1'. The publisher "
                            "must upload the EXE with exactly the name in the manifest.")
                 .arg(fileName));
        return;
    }
    if (!urlText.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        fail(QStringLiteral("The download link is not https - refusing to trust it."));
        return;
    }

    const QString target = stagingDirectory() + QLatin1Char('/') + QFileInfo(fileName).fileName();
    if (!m_stagedFile.isEmpty() && m_stagedFile != target && QFileInfo::exists(m_stagedFile))
        QFile::remove(m_stagedFile);
    m_stagedFile.clear();
    emit stateChanged();

    m_sink = new QFile(target);
    if (!m_sink->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString why = m_sink->errorString();
        delete m_sink;
        m_sink = nullptr;
        fail(QStringLiteral("The update cannot be written to %1: %2")
                 .arg(QFileInfo(target).absolutePath(), why));
        return;
    }

    setProgress(0);
    setState(QStringLiteral("downloading"),
             QStringLiteral("Downloading version %1...").arg(latestVersion()));

    QNetworkRequest request(QUrl(urlText));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", QByteArrayLiteral("BSI-Customizer/") + QByteArray(BSI_VERSION));
    request.setTransferTimeout(120000);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_sink && m_reply)
            m_sink->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                qint64 expected = total;
                if (expected <= 0)
                    expected = qint64(m_latest.value(QStringLiteral("size")).toDouble(0));
                if (expected > 0)
                    setProgress(qreal(received) / qreal(expected));
            });
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::finishDownload);
}

void UpdateChecker::finishDownload()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    const QByteArray body = reply ? reply->readAll() : QByteArray();
    const int status = reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
    const QNetworkReply::NetworkError error = reply ? reply->error() : QNetworkReply::UnknownNetworkError;
    const QString errorText = reply ? reply->errorString() : QString();
    if (reply)
        reply->deleteLater();

    QString target;
    if (m_sink) {
        if (!body.isEmpty())
            m_sink->write(body);
        m_sink->close();
        target = m_sink->fileName();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (target.isEmpty()) {
        fail(QStringLiteral("The download was interrupted before it started."));
        return;
    }

    if (error != QNetworkReply::NoError) {
        QFile::remove(target);
        if (status > 0)
            fail(describeHttpError(status, body));
        else
            fail(QStringLiteral("The update download failed: %1").arg(errorText));
        return;
    }
    if (status != 0 && status != 200) {
        QFile::remove(target);
        fail(describeHttpError(status, body));
        return;
    }

    const qint64 expectedSize = qint64(m_latest.value(QStringLiteral("size")).toDouble(0));
    const QString expectedSha = m_latest.value(QStringLiteral("sha256")).toString();
    QString verifyError;
    if (!verifyStagedFile(target, expectedSize, expectedSha, &verifyError)) {
        QFile::remove(target);
        fail(QStringLiteral("The downloaded update did not verify, so it was deleted: %1")
                 .arg(verifyError),
             verifyError);
        return;
    }

    m_stagedFile = target;
    setProgress(1);
    setState(QStringLiteral("ready"),
             QStringLiteral("Version %1 is downloaded and verified.").arg(latestVersion()),
             QStringLiteral("good"));
    if (m_installAfterDownload) {
        m_installAfterDownload = false;
        apply();
    }
}

void UpdateChecker::cancel()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_sink) {
        const QString path = m_sink->fileName();
        m_sink->close();
        delete m_sink;
        m_sink = nullptr;
        QFile::remove(path);
    }
    m_installAfterDownload = false;
    setProgress(0);
    if (m_available)
        setState(QStringLiteral("available"),
                 QStringLiteral("Version %1 is available.").arg(latestVersion()));
    else
        setState(QStringLiteral("idle"), m_config.setupHint);
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

void UpdateChecker::installNow()
{
    if (readyToInstall()) {
        apply();
        return;
    }
    if (!m_available || m_latest.isEmpty()) {
        fail(QStringLiteral("There is no update to install - check again first."));
        return;
    }
    m_installAfterDownload = true;
    if (!busy())
        beginDownload();
}

void UpdateChecker::apply()
{
    if (m_stagedFile.isEmpty() || !QFileInfo::exists(m_stagedFile)) {
        fail(QStringLiteral("The update file is missing from the temp folder - download it again."));
        return;
    }

    const QString target = QCoreApplication::applicationFilePath();
    if (target.isEmpty() || !QFileInfo::exists(target)) {
        fail(QStringLiteral("The running program file could not be located, so it cannot be "
                            "replaced. Use 'Show the downloaded file' and copy it by hand."));
        return;
    }

    const QString dirPath = QFileInfo(target).absolutePath();
    // The honest test for "may we write here": create a file. QFileInfo's
    // isWritable() only reflects the DOS read-only attribute on Windows.
    const QString probe = dirPath + QStringLiteral("/.bsi-update-probe");
    QFile probeFile(probe);
    if (!probeFile.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("Windows does not let this app write to %1, so the update cannot be "
                            "installed automatically. Run BSI Customizer as administrator.")
                 .arg(dirPath));
        return;
    }
    probeFile.close();
    QFile::remove(probe);

    // Stage a sibling copy first: the helper then only has to copy inside one
    // folder, which is far less likely to fail than a cross-volume copy.
    const QString sibling = target + QStringLiteral(".update");
    if (QFileInfo::exists(sibling))
        QFile::remove(sibling);
    if (!QFile::copy(m_stagedFile, sibling)) {
        fail(QStringLiteral("The downloaded update could not be prepared next to %1.").arg(target));
        return;
    }

    writeHelper(sibling, target);
}

void UpdateChecker::writeHelper(const QString &sibling, const QString &target)
{
    const QString scriptPath = stagingDirectory() + QStringLiteral("/apply-update.ps1");
    const QString logPath = stagingDirectory() + QStringLiteral("/apply-update.log");
    QFile::remove(logPath);

    // The application is gone by the time any of this runs, so the script is
    // the whole transaction: wait for this PID, copy over the EXE with retries,
    // start it again, delete itself. Everything is logged for the failure case.
    const qint64 pid = QCoreApplication::applicationPid();
    const QString script = QStringLiteral(
        "$ErrorActionPreference = 'Continue'\n"
        "$target = %1\n"
        "$source = %2\n"
        "$log = %3\n"
        "$waitFor = %4\n"
        "function Write-Log([string]$text) {\n"
        "    try { Add-Content -LiteralPath $log -Value (\"{0:yyyy-MM-dd HH:mm:ss} {1}\" -f (Get-Date), $text) } catch {}\n"
        "}\n"
        "Write-Log \"update: waiting for PID $waitFor to exit\"\n"
        "for ($i = 0; $i -lt 900; $i++) {\n"
        "    if (-not (Get-Process -Id $waitFor -ErrorAction SilentlyContinue)) { break }\n"
        "    Start-Sleep -Milliseconds 200\n"
        "}\n"
        "$copied = $false\n"
        "for ($i = 0; $i -lt 30; $i++) {\n"
        "    try {\n"
        "        Copy-Item -LiteralPath $source -Destination $target -Force -ErrorAction Stop\n"
        "        $copied = $true\n"
        "        break\n"
        "    } catch {\n"
        "        Write-Log (\"copy attempt {0} failed: {1}\" -f ($i + 1), $_.Exception.Message)\n"
        "        Start-Sleep -Milliseconds 500\n"
        "    }\n"
        "}\n"
        "if ($copied) {\n"
        "    Write-Log 'update: replaced, restarting'\n"
        "    Start-Process -FilePath $target\n"
        "    Remove-Item -LiteralPath $source -Force -ErrorAction SilentlyContinue\n"
        "    Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue\n"
        "} else {\n"
        "    Write-Log 'update: FAILED - the file could not be replaced. Close every other copy of BSI Customizer and run it as administrator.'\n"
        "}\n"
        "Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n")
                             .arg(singleQuoted(target), singleQuoted(sibling),
                                  singleQuoted(logPath), QString::number(pid));

    QFile scriptFile(scriptPath);
    if (!scriptFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QFile::remove(sibling);
        fail(QStringLiteral("The restart helper could not be written to %1.").arg(scriptPath));
        return;
    }
    scriptFile.write(script.toUtf8());
    scriptFile.close();

    const QString powerShell = powerShellPath();
    const QStringList args{ QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                            QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                            QStringLiteral("-File"), scriptPath };
    const bool started = QProcess::startDetached(powerShell, args, stagingDirectory());
    if (!started) {
        QFile::remove(sibling);
        QFile::remove(scriptPath);
        fail(QStringLiteral("The update was downloaded and verified, but the restart helper "
                            "could not be started. Use 'Show the downloaded file' and copy it "
                            "over %1.")
                 .arg(target));
        return;
    }

    m_installed = true;
    setState(QStringLiteral("applying"),
             QStringLiteral("Restarting into version %1...").arg(latestVersion()),
             QStringLiteral("good"));
    emit restartRequested();
}

QString UpdateChecker::helperLogPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QStringLiteral("/BSI-Customizer-update/apply-update.log");
}

// ---------------------------------------------------------------------------
// User actions
// ---------------------------------------------------------------------------

void UpdateChecker::skip()
{
    if (m_latest.isEmpty())
        return;
    m_skippedVersion = latestVersion();
    QSettings().setValue(QStringLiteral("updates/skippedVersion"), m_skippedVersion);
    emit releaseChanged();
    setState(QStringLiteral("available"),
             QStringLiteral("Version %1 will be ignored until a newer one appears.")
                 .arg(m_skippedVersion),
             QStringLiteral("warn"));
}

void UpdateChecker::unskip()
{
    m_skippedVersion.clear();
    QSettings().remove(QStringLiteral("updates/skippedVersion"));
    emit releaseChanged();
    if (m_available)
        setState(QStringLiteral("available"),
                 QStringLiteral("Version %1 is available.").arg(latestVersion()));
}

void UpdateChecker::openDownloadPage()
{
    const QString url = attachmentUrl(m_pendingMessage,
                                      m_latest.value(QStringLiteral("file")).toString());
    if (url.isEmpty() || !QDesktopServices::openUrl(QUrl(url))) {
        fail(QStringLiteral("The release link could not be opened in the browser."));
        return;
    }
}

void UpdateChecker::showStagedFile()
{
    if (m_stagedFile.isEmpty() || !QFileInfo::exists(m_stagedFile)) {
        fail(QStringLiteral("The update file is missing from the temp folder - download it again."));
        return;
    }
    QProcess::startDetached(QStringLiteral("explorer.exe"),
                            { QStringLiteral("/select,"), QDir::toNativeSeparators(m_stagedFile) });
}

// ---------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------

QString UpdateChecker::normalizedVersion(const QString &version)
{
    QString text = version.trimmed();
    if (text.startsWith(QLatin1String("version "), Qt::CaseInsensitive))
        text = text.mid(8).trimmed();
    if (text.size() > 1 && (text.at(0) == QLatin1Char('v') || text.at(0) == QLatin1Char('V'))
        && text.at(1).isDigit())
        text.remove(0, 1);
    return text;
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    struct Parsed
    {
        QList<qint64> numbers;
        QString preRelease;
    };
    const auto parse = [](const QString &raw) {
        Parsed parsed;
        QString main = normalizedVersion(raw);
        const int dash = main.indexOf(QLatin1Char('-'));
        if (dash >= 0) {
            parsed.preRelease = main.mid(dash + 1);
            main = main.left(dash);
        }
        const int plus = main.indexOf(QLatin1Char('+'));
        if (plus >= 0)
            main = main.left(plus);
        const QStringList pieces = main.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        for (const QString &piece : pieces) {
            bool ok = false;
            const qint64 value = piece.toLongLong(&ok);
            parsed.numbers << (ok ? value : qint64(0));
        }
        if (parsed.numbers.isEmpty())
            parsed.numbers << 0;
        return parsed;
    };

    const Parsed left = parse(a);
    const Parsed right = parse(b);
    const int count = qMax(left.numbers.size(), right.numbers.size());
    for (int i = 0; i < count; ++i) {
        const qint64 l = i < left.numbers.size() ? left.numbers.at(i) : 0;
        const qint64 r = i < right.numbers.size() ? right.numbers.at(i) : 0;
        if (l != r)
            return l < r ? -1 : 1;
    }
    if (left.preRelease == right.preRelease)
        return 0;
    if (left.preRelease.isEmpty())
        return 1; // 2.0.1 outranks 2.0.1-beta
    if (right.preRelease.isEmpty())
        return -1;
    return left.preRelease.compare(right.preRelease, Qt::CaseInsensitive) < 0 ? -1 : 1;
}

QJsonObject UpdateChecker::parseManifest(const QByteArray &json, QString *error)
{
    const auto reject = [error](const QString &text) {
        if (error)
            *error = text;
        return QJsonObject();
    };
    if (error)
        error->clear();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return reject(QStringLiteral("not valid JSON (%1 at byte %2)")
                          .arg(parseError.errorString())
                          .arg(parseError.offset));
    }
    if (!doc.isObject())
        return reject(QStringLiteral("the manifest must be a JSON object"));

    const QJsonObject object = doc.object();
    const int schema = object.value(QStringLiteral("schema")).toInt(0);
    if (schema != 1) {
        return reject(QStringLiteral("unsupported schema %1 (this build understands schema 1)")
                          .arg(schema));
    }

    const QString version = normalizedVersion(object.value(QStringLiteral("version")).toString());
    static const QRegularExpression semver(
        QStringLiteral("^[0-9]+(\\.[0-9]+){1,3}([-+][A-Za-z0-9.]+)?$"));
    if (!semver.match(version).hasMatch()) {
        return reject(QStringLiteral("version '%1' is not a version number like 2.0.1")
                          .arg(object.value(QStringLiteral("version")).toString()));
    }

    const QString file = object.value(QStringLiteral("file")).toString().trimmed();
    if (file.isEmpty())
        return reject(QStringLiteral("file is missing"));
    if (file.contains(QLatin1Char('/')) || file.contains(QLatin1Char('\\'))
        || file == QLatin1String(".."))
        return reject(QStringLiteral("file must be a bare file name"));
    if (!file.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        return reject(QStringLiteral("file must be the .exe the updater replaces"));

    const double size = object.value(QStringLiteral("size")).toDouble(-1);
    if (size <= 0)
        return reject(QStringLiteral("size must be the exact byte count of the EXE"));

    const QString sha = object.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    static const QRegularExpression hex(QStringLiteral("^[0-9a-f]{64}$"));
    if (!hex.match(sha).hasMatch())
        return reject(QStringLiteral("sha256 must be 64 hexadecimal characters"));

    QJsonObject clean = object;
    clean.insert(QStringLiteral("version"), version);
    clean.insert(QStringLiteral("file"), file);
    clean.insert(QStringLiteral("size"), size);
    clean.insert(QStringLiteral("sha256"), sha);
    return clean;
}

UpdateChecker::ManifestRef UpdateChecker::findManifestMessage(const QJsonArray &messages,
                                                             const QString &manifestName)
{
    ManifestRef ref;
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        const QJsonArray attachments = message.value(QStringLiteral("attachments")).toArray();
        for (const QJsonValue &attachmentValue : attachments) {
            const QJsonObject attachment = attachmentValue.toObject();
            const QString name = attachment.value(QStringLiteral("filename")).toString();
            if (name.compare(manifestName, Qt::CaseInsensitive) != 0)
                continue;
            const QString url = attachment.value(QStringLiteral("url")).toString();
            if (url.isEmpty())
                continue;
            ref.found = true;
            ref.messageId = message.value(QStringLiteral("id")).toString();
            ref.manifestUrl = url;
            ref.message = message;
            return ref;
        }
    }
    ref.error = QStringLiteral("no message carries a '%1' attachment").arg(manifestName);
    return ref;
}

QString UpdateChecker::attachmentUrl(const QJsonObject &message, const QString &fileName)
{
    const QJsonArray attachments = message.value(QStringLiteral("attachments")).toArray();
    for (const QJsonValue &value : attachments) {
        const QJsonObject attachment = value.toObject();
        if (attachment.value(QStringLiteral("filename")).toString().compare(
                fileName, Qt::CaseInsensitive)
            == 0)
            return attachment.value(QStringLiteral("url")).toString();
    }
    return {};
}

QByteArray UpdateChecker::signedManifestText(const QJsonObject &manifest)
{
    QByteArray text = QByteArrayLiteral("bsi-update-v1\n");
    text += manifest.value(QStringLiteral("version")).toString().trimmed().toUtf8();
    text += '\n';
    text += manifest.value(QStringLiteral("file")).toString().trimmed().toUtf8();
    text += '\n';
    text += QByteArray::number(qint64(manifest.value(QStringLiteral("size")).toDouble(0)));
    text += '\n';
    text += manifest.value(QStringLiteral("sha256")).toString().trimmed().toLower().toUtf8();
    text += '\n';
    return text;
}

QString UpdateChecker::manifestSignature(const QJsonObject &manifest, const QString &key)
{
    if (key.isEmpty())
        return {};
    return QString::fromLatin1(QMessageAuthenticationCode::hash(
                                   signedManifestText(manifest), key.toUtf8(),
                                   QCryptographicHash::Sha256)
                                   .toBase64());
}

bool UpdateChecker::verifyManifestSignature(const QJsonObject &manifest, const QString &key,
                                            QString *error)
{
    if (key.isEmpty())
        return true;
    const QString provided = manifest.value(QStringLiteral("signature")).toString().trimmed();
    if (provided.isEmpty()) {
        if (error)
            *error = QStringLiteral("the manifest has no signature");
        return false;
    }
    const QString expected = manifestSignature(manifest, key);
    // Compare without a length-dependent early exit.
    const QByteArray left = provided.toUtf8();
    const QByteArray right = expected.toUtf8();
    unsigned char difference = left.size() == right.size() ? 0 : 1;
    const qsizetype count = qMin(left.size(), right.size());
    for (qsizetype i = 0; i < count; ++i)
        difference |= static_cast<unsigned char>(left.at(i) ^ right.at(i));
    if (difference != 0) {
        if (error) {
            *error = QStringLiteral("the signature does not match this build's key "
                                    "(was the manifest published with the same hmacKey?)");
        }
        return false;
    }
    return true;
}

QString UpdateChecker::fileSha256(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool UpdateChecker::looksLikeExecutable(const QString &path, QString *error)
{
    const auto reject = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return reject(QStringLiteral("the downloaded file cannot be opened"));
    const QByteArray head = file.read(4096);
    if (head.size() < 0x40 || !head.startsWith("MZ"))
        return reject(QStringLiteral("it is not a Windows program (no MZ header)"));

    const auto littleEndian = [](const QByteArray &data, int offset, int bytes) {
        quint32 value = 0;
        for (int i = 0; i < bytes; ++i)
            value |= quint32(static_cast<unsigned char>(data.at(offset + i))) << (8 * i);
        return value;
    };

    const qint64 peOffset = qint64(littleEndian(head, 0x3c, 4));
    if (peOffset < 0x40 || peOffset + 0x60 > file.size())
        return reject(QStringLiteral("its PE header offset is outside the file"));
    if (!file.seek(peOffset))
        return reject(QStringLiteral("its PE header cannot be read"));
    const QByteArray coff = file.read(0x60);
    if (coff.size() < 0x60)
        return reject(QStringLiteral("its PE header is truncated"));
    if (!coff.startsWith(QByteArrayLiteral("PE\0\0")))
        return reject(QStringLiteral("it has no PE signature"));
    const quint32 machine = littleEndian(coff, 4, 2);
    if (machine != 0x8664)
        return reject(QStringLiteral("it is not a 64-bit x86 Windows program (machine 0x%1)")
                          .arg(machine, 4, 16, QLatin1Char('0')));
    const quint32 optionalMagic = littleEndian(coff, 24, 2);
    if (optionalMagic != 0x20b)
        return reject(QStringLiteral("its optional header is not PE32+"));
    return true;
}

bool UpdateChecker::verifyStagedFile(const QString &path, qint64 expectedSize,
                                     const QString &expectedSha256, QString *error)
{
    const auto reject = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile())
        return reject(QStringLiteral("the download is missing"));
    if (expectedSize > 0 && info.size() != expectedSize) {
        return reject(QStringLiteral("size mismatch: expected %1 bytes, got %2")
                          .arg(expectedSize)
                          .arg(info.size()));
    }
    if (!expectedSha256.isEmpty()) {
        const QString actual = fileSha256(path);
        if (actual.compare(expectedSha256, Qt::CaseInsensitive) != 0) {
            return reject(QStringLiteral("SHA-256 mismatch: expected %1..., got %2...")
                              .arg(expectedSha256.left(12), actual.left(12)));
        }
    }
    QString peError;
    if (!looksLikeExecutable(path, &peError))
        return reject(peError);
    return true;
}

QString UpdateChecker::describeHttpError(int status, const QByteArray &body)
{
    QString detail;
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    if (!object.isEmpty()) {
        detail = object.value(QStringLiteral("message")).toString().trimmed();
        if (detail.isEmpty() && object.contains(QStringLiteral("retry_after")))
            detail = QStringLiteral("retry after %1 s")
                         .arg(object.value(QStringLiteral("retry_after")).toDouble());
    }
    if (detail.size() > 160)
        detail = detail.left(157) + QStringLiteral("...");

    QString text;
    switch (status) {
    case 401:
        text = QStringLiteral("Discord rejected the updater token (401). Re-paste "
                              "updater.botToken from the developer portal - a regenerated token "
                              "must be updated in auth.json.");
        break;
    case 403:
        text = QStringLiteral("The updater bot cannot read the releases channel (403). Give it "
                              "View Channel and Read Message History on that channel.");
        break;
    case 404:
        text = QStringLiteral("The releases channel was not found (404). Check "
                              "updater.channelId in auth.json and that the bot is in that server.");
        break;
    case 429:
        text = QStringLiteral("Discord is rate-limiting the updater (429). Try again in a minute.");
        break;
    default:
        if (status >= 500)
            text = QStringLiteral("Discord is having trouble (HTTP %1). Try again later.")
                       .arg(status);
        else
            text = QStringLiteral("Discord answered HTTP %1.").arg(status);
        break;
    }
    if (!detail.isEmpty())
        text += QStringLiteral(" Discord said: '%1'.").arg(detail);
    return text;
}

QString UpdateChecker::readableSize(qint64 bytes)
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
}
