#include "UpdateChecker.h"

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
    // Every place an auth.json can come from, in priority order. The first one
    // that actually carries an "updater" object wins; a file that only has the
    // sign-in part (or the other way round) must not hide the build-time
    // config, which is the whole point of baking a blob into the EXE.
    struct Candidate
    {
        QString label;
        QByteArray bytes;
    };
    QList<Candidate> candidates;

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char *name : { "ZLP_AUTH_JSON", "BSIC_AUTH_JSON" }) {
        const QByteArray blob = env.value(QLatin1String(name)).toUtf8();
        if (parsesAsObject(blob))
            candidates << Candidate{ QStringLiteral("environment (%1)").arg(QLatin1String(name)),
                                     cleanJsonBlob(blob) };
    }

    QStringList files;
    const QString envFile = env.value(QStringLiteral("BSIC_AUTH_FILE")).trimmed();
    if (!envFile.isEmpty())
        files << envFile;
    if (QCoreApplication::instance())
        files << QCoreApplication::applicationDirPath() + QStringLiteral("/auth.json");
    files << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                 + QStringLiteral("/auth.json");
    for (const QString &path : files) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = file.readAll();
        file.close();
        if (parsesAsObject(bytes))
            candidates << Candidate{ path, cleanJsonBlob(bytes) };
    }

    QFile embedded(QStringLiteral(":/bsic_auth/auth.json"));
    if (embedded.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = embedded.readAll();
        if (parsesAsObject(bytes)) {
            candidates << Candidate{ QStringLiteral("embedded auth.json (build-time "
                                                    "ZLP_AUTH_JSON)"),
                                     cleanJsonBlob(bytes) };
        }
    }

    const auto holdsUpdater = [](const QByteArray &bytes) {
        return QJsonDocument::fromJson(bytes).object().value(QStringLiteral("updater")).isObject();
    };
    for (const Candidate &candidate : candidates) {
        if (holdsUpdater(candidate.bytes)) {
            if (source)
                *source = candidate.label;
            return candidate.bytes;
        }
    }
    if (!candidates.isEmpty()) {
        if (source)
            *source = candidates.first().label;
        return candidates.first().bytes;
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

// A landing page keeps the real download URL in JSON ("downloadlink") with the
// slashes escaped as \/ , or in an ordinary href. Turn either into a URL.
QString unescapePageValue(QString value)
{
    value.replace(QLatin1String("\\/"), QLatin1String("/"));
    value.replace(QLatin1String("\\u002F"), QLatin1String("/"), Qt::CaseInsensitive);
    value.replace(QLatin1String("\\u002f"), QLatin1String("/"));
    value.replace(QLatin1String("&amp;"), QLatin1String("&"));
    value.replace(QLatin1String("&#x2F;"), QLatin1String("/"), Qt::CaseInsensitive);
    value.replace(QLatin1String("&#47;"), QLatin1String("/"));
    return value.trimmed();
}

// First kilobyte of a downloaded file: does it open a web page instead of being
// the program we asked for?
bool looksLikeHtmlFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(1024);
    file.close();
    QByteArray probe = head;
    if (probe.startsWith("\xEF\xBB\xBF"))
        probe = probe.mid(3);
    probe = probe.trimmed().toLower();
    if (probe.startsWith("mz") || probe.startsWith("pk"))
        return false;
    return probe.startsWith("<!doctype html") || probe.startsWith("<html")
           || probe.startsWith("<?xml") || probe.startsWith("<head")
           || (probe.startsWith("<") && probe.contains("</"));
}

bool isHttpsUrl(const QString &text)
{
    const QUrl url(text);
    return url.isValid() && url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0
           && !url.host().isEmpty();
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

    // A release is published by posting a message, and the app can be open for
    // days. One check at startup meant a message posted afterwards was never
    // seen until the user pressed "Check now" - which reads exactly like "the
    // updater does not detect the message". Look again every few minutes while
    // nothing is pending; the channel is one small JSON read.
    if (m_recheck)
        return;
    m_recheck = new QTimer(this);
    m_recheck->setInterval(10 * 60 * 1000);
    QObject::connect(m_recheck, &QTimer::timeout, this, [this] {
        // Never interrupt a download or replace an update already offered.
        if (!isConfigured() || !m_autoCheck || busy() || m_available)
            return;
        check();
    });
    m_recheck->start();
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

    // 100 is the endpoint's maximum: a busy channel must not push the release
    // message out of the window.
    const QUrl url(QStringLiteral("%1/channels/%2/messages?limit=100")
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
        // A signed channel only accepts manifests (a text message carries no
        // signature, so accepting one would bypass the key).
        const bool allowText = m_config.hmacKey.isEmpty();
        m_lastMessages = doc.array();
        m_lastAllowText = allowText;
        const ReleasePick release = findRelease(m_lastMessages, m_config.manifestName, allowText);
        if (!release.found) {
            fail(QStringLiteral("No release in the last 100 messages of channel %1. Post the "
                                "version and the download link, for example:\n"
                                "    BSI Customizer v2.0.1\n"
                                "    https://u.pcloud.link/publink/show?code=XXXX\n"
                                "or upload a manifest.json with updater-bot/publish.mjs.%2\n\n%3")
                     .arg(m_config.channelId,
                          release.error.isEmpty() ? QString()
                                                  : QStringLiteral("\n(%1)").arg(release.error),
                          explainChannel(m_lastMessages, m_config.manifestName, allowText)));
            return;
        }
        m_pendingMessage = release.message;

        if (!release.fromManifest) {
            // Nothing else to fetch: the message itself is the manifest.
            handleRelease(release.manifest, false);
            return;
        }
        if (!release.manifestUrl.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
            fail(QStringLiteral("The manifest link is not https - refusing to trust it."));
            return;
        }

        QNetworkRequest manifestRequest{QUrl(release.manifestUrl)};
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
    handleRelease(manifest, true);
}

// Everything both announcement styles share: is it newer, is it skipped, what
// does the UI say. `fromManifest` is false for a plain message, which has no
// signature and possibly no hash.
void UpdateChecker::handleRelease(const QJsonObject &manifest, bool fromManifest)
{
    if (fromManifest && !m_config.hmacKey.isEmpty()) {
        QString signatureError;
        if (!verifyManifestSignature(manifest, m_config.hmacKey, &signatureError)) {
            fail(QStringLiteral("The update manifest is not signed by this build's key: %1")
                     .arg(signatureError),
                 signatureError);
            return;
        }
    }

    m_pinnedRelease = !manifest.value(QStringLiteral("sha256")).toString().isEmpty();

    const QString version = manifest.value(QStringLiteral("version")).toString();
    if (compareVersions(version, currentVersion()) <= 0) {
        m_available = false;
        m_latest = QJsonObject();
        emit releaseChanged();
        // Say which message was actually read: "nothing happened" and "the
        // channel offers the version you already run" look identical otherwise.
        setState(QStringLiteral("uptodate"),
                 version.isEmpty() || version == currentVersion()
                     ? QStringLiteral("You are on the newest build (%1).").arg(currentVersion())
                     : QStringLiteral("The channel offers v%1; you run %2, so there is nothing "
                                      "to install.")
                           .arg(version, currentVersion()),
                 QStringLiteral("good"));
        return;
    }

    m_latest = manifest;
    m_downloadUrl.clear();
    m_hops = 0;
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
    const qint64 size = qint64(m_latest.value(QStringLiteral("size")).toDouble(0));
    setState(QStringLiteral("available"),
             size > 0 ? QStringLiteral("Version %1 is available - %2.").arg(version,
                                                                           readableSize(size))
                      : QStringLiteral("Version %1 is available.").arg(version),
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
    m_hops = 0;
    m_downloadUrl.clear();
    beginDownload();
}

void UpdateChecker::beginDownload(const QString &urlText)
{
    const QString fileName = m_latest.value(QStringLiteral("file")).toString();

    // Three places a download link can come from:
    //  1. the URL the previous step resolved from a landing page,
    //  2. `url` in the manifest (pCloud, any https host, a CDN),
    //  3. the attachment of the release message (Discord's own upload).
    QString source = urlText;
    if (source.isEmpty())
        source = m_latest.value(QStringLiteral("url")).toString().trimmed();
    if (source.isEmpty())
        source = attachmentUrl(m_pendingMessage, fileName);
    if (source.isEmpty()) {
        fail(QStringLiteral("The release message has no attachment named '%1', and the manifest "
                            "does not name a url to download it from.")
                 .arg(fileName));
        return;
    }
    // A publisher may paste "pcloud.link/..." without the scheme; the manifest
    // still has to point at https.
    if (!source.startsWith(QLatin1String("http"), Qt::CaseInsensitive))
        source.prepend(QLatin1String("https://"));
    if (!isHttpsUrl(source)) {
        fail(QStringLiteral("The download link is not a valid https address, so it was refused: %1")
                 .arg(source));
        return;
    }

    m_downloadUrl = source;
    const QString target = stagingDirectory() + QLatin1Char('/') + QFileInfo(fileName).fileName();
    if (!m_stagedFile.isEmpty() && m_stagedFile != target && QFileInfo::exists(m_stagedFile))
        QFile::remove(m_stagedFile);
    m_stagedFile.clear();
    emit stateChanged();

    if (m_sink) {
        m_sink->close();
        delete m_sink;
        m_sink = nullptr;
    }
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
             m_hops == 0
                 ? QStringLiteral("Downloading version %1...").arg(latestVersion())
                 : QStringLiteral("Downloading version %1 from %2...")
                       .arg(latestVersion(), QUrl(source).host()));

    QNetworkRequest request{QUrl(source)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // File hosts take a browser-looking agent more seriously than a bare one.
    request.setRawHeader("User-Agent",
                         QByteArrayLiteral("BSI-Customizer/") + QByteArray(BSI_VERSION)
                             + QByteArrayLiteral(" (Windows; self-updater)"));
    request.setRawHeader("Accept", QByteArrayLiteral("*/*"));
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

    // A "share" link (pCloud and similar) answers with a web page, not the file.
    // The page carries the real download URL, so follow it once or twice.
    QString pageError;
    if (looksLikeHtmlFile(target) && m_hops < 2) {
        QFile page(target);
        QString resolved;
        if (page.open(QIODevice::ReadOnly)) {
            const QByteArray html = page.read(4ll * 1024ll * 1024ll);
            page.close();
            QString detail;
            resolved = resolveDownloadLink(html, QUrl(m_downloadUrl), &detail);
            pageError = detail;
        }
        if (!resolved.isEmpty()) {
            QFile::remove(target);
            ++m_hops;
            setProgress(0);
            beginDownload(resolved);
            return;
        }
        QFile::remove(target);
        fail(QStringLiteral("The link opened a web page instead of the file (%1). %2")
                 .arg(QUrl(m_downloadUrl).host(),
                      pageError.isEmpty()
                          ? QStringLiteral("Use the file's direct download link, or a link that "
                                           "downloads it without a page in between.")
                          : pageError));
        return;
    }

    const qint64 expectedSize = qint64(m_latest.value(QStringLiteral("size")).toDouble(0));
    QString expectedSha = m_latest.value(QStringLiteral("sha256")).toString();
    // A release message that names only the version and the link has no hash to
    // compare against. What this PC learned the first time is remembered, so
    // the same version can never silently turn into different bytes.
    bool firstUse = false;
    if (expectedSha.isEmpty()) {
        const QString known = pinnedHash(latestVersion());
        if (!known.isEmpty())
            expectedSha = known;
        else
            firstUse = true;
    }
    QString verifyError;
    if (!verifyStagedFile(target, expectedSize, expectedSha, &verifyError)) {
        QFile::remove(target);
        fail(expectedSha.isEmpty()
                 ? QStringLiteral("The downloaded update is not a usable program, so it was "
                                  "deleted: %1").arg(verifyError)
                 : QStringLiteral("The downloaded update did not verify, so it was deleted: %1")
                       .arg(verifyError),
             verifyError);
        return;
    }
    if (firstUse) {
        const QString actual = fileSha256(target);
        if (!actual.isEmpty()) {
            storePinnedHash(latestVersion(), actual);
            m_latest.insert(QStringLiteral("sha256"), actual);
            m_latest.insert(QStringLiteral("size"), double(QFileInfo(target).size()));
            m_pinnedRelease = true;
            emit releaseChanged();
        }
    }

    m_stagedFile = target;
    setProgress(1);
    setState(QStringLiteral("ready"),
             firstUse
                 ? QStringLiteral("Version %1 is downloaded. It carried no SHA-256, so its "
                                  "fingerprint was recorded for next time.")
                       .arg(latestVersion())
                 : QStringLiteral("Version %1 is downloaded and verified.").arg(latestVersion()),
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
    if (!busy()) {
        m_hops = 0;
        m_downloadUrl.clear();
        beginDownload();
    }
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

QString UpdateChecker::channelReport() const
{
    if (m_lastMessages.isEmpty())
        return QStringLiteral("No check has run yet (or it never reached the channel).");
    return explainChannel(m_lastMessages, m_config.manifestName, m_lastAllowText);
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
    QString url = m_downloadUrl;
    if (url.isEmpty())
        url = m_latest.value(QStringLiteral("url")).toString().trimmed();
    if (url.isEmpty())
        url = attachmentUrl(m_pendingMessage, m_latest.value(QStringLiteral("file")).toString());
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

UpdateChecker::ReleasePick UpdateChecker::findRelease(const QJsonArray &messages,
                                                      const QString &manifestName,
                                                      bool allowTextReleases)
{
    ReleasePick pick;

    // 1. A published manifest always wins: it is the strict form, it can be
    //    signed, and publish.mjs keeps exactly one of those messages around.
    const ManifestRef manifest = findManifestMessage(messages, manifestName);
    if (manifest.found) {
        pick.found = true;
        pick.fromManifest = true;
        pick.manifestUrl = manifest.manifestUrl;
        pick.message = manifest.message;
        return pick;
    }

    if (!allowTextReleases) {
        pick.error = QStringLiteral("this channel is signature-protected, so it needs a "
                                    "manifest.json attachment instead of a text message");
        return pick;
    }

    // 2. Otherwise the best ordinary message: highest version wins, and among
    //    equal versions the newest message (a re-posted link beats the old one).
    QString bestVersion;
    qulonglong bestId = 0;
    QString problem;
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        QJsonObject release;
        QString error;
        if (!parseMessageRelease(message, &release, &error)) {
            if (!error.isEmpty() && problem.isEmpty())
                problem = error;
            continue;
        }
        const QString version = release.value(QStringLiteral("version")).toString();
        bool idOk = false;
        const qulonglong id = message.value(QStringLiteral("id")).toString().toULongLong(&idOk);
        const int order = bestVersion.isEmpty() ? 1 : compareVersions(version, bestVersion);
        if (order > 0 || (order == 0 && (!pick.found || id > bestId))) {
            bestVersion = version;
            bestId = idOk ? id : 0;
            pick.found = true;
            pick.fromManifest = false;
            pick.manifest = release;
            pick.message = message;
        }
    }
    if (!pick.found && !problem.isEmpty())
        pick.error = problem;
    return pick;
}

QString UpdateChecker::messageText(const QJsonObject &message)
{
    QStringList parts;
    const auto add = [&parts](const QString &value) {
        const QString text = value.trimmed();
        if (!text.isEmpty() && !parts.contains(text))
            parts << text;
    };

    add(message.value(QStringLiteral("content")).toString());
    // A link preview, a webhook post, or a message written through another
    // tool can carry the whole announcement in an embed instead of the text.
    for (const QJsonValue &value : message.value(QStringLiteral("embeds")).toArray()) {
        const QJsonObject embed = value.toObject();
        add(embed.value(QStringLiteral("title")).toString());
        add(embed.value(QStringLiteral("url")).toString());
        add(embed.value(QStringLiteral("description")).toString());
        for (const QJsonValue &fieldValue : embed.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject field = fieldValue.toObject();
            add(field.value(QStringLiteral("name")).toString());
            add(field.value(QStringLiteral("value")).toString());
        }
        add(embed.value(QStringLiteral("author")).toObject().value(QStringLiteral("name")).toString());
        add(embed.value(QStringLiteral("footer")).toObject().value(QStringLiteral("text")).toString());
    }
    return parts.join(QLatin1Char('\n'));
}

QString UpdateChecker::explainChannel(const QJsonArray &messages, const QString &manifestName,
                                      bool allowTextReleases)
{
    // Discord answers with the messages even when the app may not read them:
    // whatever content, embeds and attachments it withheld simply arrive empty.
    // Counting the empty ones is what separates "nothing was posted" from
    // "posted, but the bot is blind to it" - two problems with different fixes.
    int blank = 0;
    int readable = 0;
    int attachments = 0;
    QStringList lines;
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        const QString text = messageText(message);
        const QJsonArray files = message.value(QStringLiteral("attachments")).toArray();
        const bool hidden = text.isEmpty() && files.isEmpty();
        if (hidden)
            ++blank;
        else
            ++readable;
        attachments += files.size();

        QStringList names;
        for (const QJsonValue &fileValue : files)
            names << fileValue.toObject().value(QStringLiteral("filename")).toString();
        const QString id = message.value(QStringLiteral("id")).toString();
        const QString author =
            message.value(QStringLiteral("author")).toObject().value(QStringLiteral("username"))
                .toString();
        QString verdict;
        if (hidden) {
            verdict = QStringLiteral("text withheld by Discord");
        } else if (!allowTextReleases) {
            verdict = QStringLiteral("readable, but this channel only accepts manifest.json");
        } else {
            QJsonObject release;
            QString error;
            if (parseMessageRelease(message, &release, &error))
                verdict = QStringLiteral("release %1").arg(release.value(QStringLiteral("version")).toString());
            else if (!error.isEmpty())
                verdict = error;
            else
                verdict = QStringLiteral("no version + link");
        }
        // Qt 6.8's QString::arg() takes at most four arguments (the variadic
        // overload is 6.9+), so the attachment list is a second call.
        QString line = QStringLiteral("  #%1 %2%3 - %4%5")
                           .arg(id.isEmpty() ? QStringLiteral("?") : id,
                                author.isEmpty() ? QString() : QStringLiteral("<%1> ").arg(author),
                                hidden ? QStringLiteral("0 chars")
                                       : QStringLiteral("%1 chars").arg(text.size()),
                                verdict);
        line = line.arg(names.isEmpty()
                            ? QString()
                            : QStringLiteral(" [%1]").arg(names.join(QStringLiteral(", "))));
        lines << line;
        if (lines.size() >= 30)
            break;
    }

    QStringList out;
    out << QStringLiteral("Bot sees %1 message(s) in the channel: %2 readable, %3 withheld, "
                          "%4 attachment(s).")
               .arg(messages.size())
               .arg(readable)
               .arg(blank)
               .arg(attachments);
    if (!messages.isEmpty() && readable == 0) {
        out << QStringLiteral(
                   "Discord returns no text, no embeds and no attachments for any of them. That "
                   "is the Message Content intent: an app without it is blind to every message it "
                   "did not post itself. Fix it in the Developer Portal (your app -> Bot -> "
                   "Privileged Gateway Intents -> Message Content Intent) and check again.");
        out << QStringLiteral(
                   "No portal access? Two things work without the intent: mention the bot in the "
                   "release message (@bot v2.0.1 ...), or publish with updater-bot/publish.mjs so "
                   "the message is written by the bot itself.");
    }
    if (!messages.isEmpty() && readable > 0 && blank > 0)
        out << QStringLiteral("Some messages were withheld and some were not, which means the "
                              "intent is off and only the bot's own posts are readable.");
    if (messages.isEmpty())
        out << QStringLiteral("The channel is empty for this bot: check that the channel ID is "
                              "the releases channel and not a category, and that the bot can read "
                              "its history.");
    if (!lines.isEmpty())
        out << lines;
    return out.join(QLatin1Char('\n'));
}

bool UpdateChecker::parseMessageRelease(const QJsonObject &message, QJsonObject *manifest,
                                       QString *error)
{
    const auto reject = [error](const QString &text) {
        if (error)
            *error = text;
        return false;
    };
    if (error)
        error->clear();
    if (manifest)
        *manifest = QJsonObject();
    if (!manifest)
        return false;

    // Markdown emphasis and backticks are formatting, not content: the same
    // message has to parse whether it was typed **v2.0.1** or `v2.0.1`.
    QString text = messageText(message);
    if (text.trimmed().isEmpty())
        return reject({});
    text.remove(QLatin1Char('`'));
    text.remove(QStringLiteral("**"));
    text.remove(QStringLiteral("__"));
    text.remove(QStringLiteral("~~"));

    // ---- the download link ------------------------------------------------
    static const QRegularExpression urlPattern(
        QStringLiteral("https?://[^\\s<>()\\[\\]\"']+"),
        QRegularExpression::CaseInsensitiveOption);
    struct Candidate
    {
        QString url;
        int score = 0;
    };
    Candidate bestUrl;
    static const QStringList fileHosts{
        QStringLiteral("pcloud"), QStringLiteral("dropbox"), QStringLiteral("google"), // drive.google
        QStringLiteral("drive.google"), QStringLiteral("1drv.ms"), QStringLiteral("onedrive"),
        QStringLiteral("mega.nz"), QStringLiteral("mediafire"), QStringLiteral("catbox.moe"),
        QStringLiteral("litter.catbox"), QStringLiteral("transfer.sh"), QStringLiteral("file.io"),
        QStringLiteral("githubusercontent"), QStringLiteral("github.com"), QStringLiteral("amazonaws"),
        QStringLiteral("r2.dev"), QStringLiteral("backblazeb2"), QStringLiteral("sourceforge"),
        QStringLiteral("pixeldrain"), QStringLiteral("gofile.io"), QStringLiteral("krakenfiles"),
        QStringLiteral("storage.googleapis"), QStringLiteral("files."), QStringLiteral("dl.")
    };
    static const QStringList notDownloads{QStringLiteral(".png"), QStringLiteral(".jpg"),
                                         QStringLiteral(".jpeg"), QStringLiteral(".gif"),
                                         QStringLiteral(".webp"), QStringLiteral(".mp4"),
                                         QStringLiteral("tenor.com"), QStringLiteral("giphy.com"),
                                         QStringLiteral("discord.gg")};
    for (const auto &match : urlPattern.globalMatch(text)) {
        const QString url = match.captured(0);
        const QString lower = url.toLower();
        const QString path = lower.section(QLatin1Char('?'), 0, 0);

        bool skip = false;
        for (const QString &bad : notDownloads) {
            if (lower.contains(bad)) {
                skip = true;
                break;
            }
        }
        if (skip)
            continue;

        int score = 0;
        if (path.endsWith(QLatin1String(".exe")))
            score += 100;
        for (const QString &host : fileHosts) {
            if (lower.contains(host)) {
                score += 60;
                break;
            }
        }
        // A line that is nothing but the link reads as "this is the download".
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            if (line.trimmed() == url) {
                score += 25;
                break;
            }
        }
        if (lower.contains(QLatin1String("download")))
            score += 15;
        if (score > bestUrl.score)
            bestUrl = { url, score };
    }
    if (bestUrl.url.isEmpty()) {
        // A link pasted without its scheme ("u.pcloud.link/publink/show?code=X",
        // which is what a browser's address bar shows without https://) is
        // still the download link; the same tolerance the manifest url gets.
        static const QRegularExpression bareHost(
            QStringLiteral("\\b([a-z0-9][a-z0-9-]*(?:\\.[a-z0-9-]+)+/[^\\s<>()\\[\\]\"']*)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression notAHost(
            QStringLiteral("^(?:[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)"),
            QRegularExpression::CaseInsensitiveOption);
        for (const auto &match : bareHost.globalMatch(text)) {
            const QString candidate = QStringLiteral("https://") + match.captured(1);
            if (notAHost.match(match.captured(1)).hasMatch())
                continue;
            const QString lower = candidate.toLower();
            const QString path = lower.section(QLatin1Char('?'), 0, 0);
            bool skip = false;
            for (const QString &bad : notDownloads) {
                if (lower.contains(bad)) {
                    skip = true;
                    break;
                }
            }
            if (skip)
                continue;
            int score = 0;
            if (path.endsWith(QLatin1String(".exe")))
                score += 100;
            for (const QString &host : fileHosts) {
                if (lower.contains(host)) {
                    score += 60;
                    break;
                }
            }
            if (score > bestUrl.score)
                bestUrl = { candidate, score };
        }
        // Never invent a release out of a sentence: a bare candidate only
        // counts when it is a file host or an .exe.
        if (!bestUrl.url.isEmpty() && bestUrl.score <= 0)
            bestUrl = Candidate();
    }
    if (bestUrl.url.isEmpty())
        return reject(QStringLiteral("the message has no download link"));
    if (!bestUrl.url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        return reject(QStringLiteral("the download link must start with https://"));
    if (!isHttpsUrl(bestUrl.url))
        return reject(QStringLiteral("the download link is not a usable https address"));

    // ---- the version -----------------------------------------------------
    QString version;
    static const QRegularExpression explicitV(
        QStringLiteral("\\bv(\\d+\\.\\d+(?:\\.\\d+){0,2})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression labelled(
        QStringLiteral("(?:version|ver\\.?|bsi(?:\\s*customizer)?)\\s*[:=\\-]?\\s*v?"
                       "(\\d+\\.\\d+(?:\\.\\d+){0,2})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bareVersion(
        QStringLiteral("^\\s*v?(\\d+\\.\\d+(?:\\.\\d+){0,2})\\s*$"));
    const auto firstMatch = [&text](const QRegularExpression &pattern) {
        const auto match = pattern.match(text);
        return match.hasMatch() ? match.captured(1) : QString();
    };
    version = firstMatch(explicitV);
    if (version.isEmpty())
        version = firstMatch(labelled);
    if (version.isEmpty()) {
        for (const QString &line : text.split(QLatin1Char('\n'))) {
            const auto match = bareVersion.match(line);
            if (match.hasMatch()) {
                version = match.captured(1);
                break;
            }
        }
    }
    if (version.isEmpty())
        return reject(QStringLiteral("the message does not name a version (write v2.0.1)"));

    // ---- optional pinning: sha256 / size ---------------------------------
    QString sha256;
    static const QRegularExpression shaPattern(
        QStringLiteral("(?:sha-?256|sha|hash)\\s*[:=]\\s*([0-9a-fA-F]{64})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression shaBare(QStringLiteral("\\b([0-9a-fA-F]{64})\\b"));
    const auto shaMatch = shaPattern.match(text);
    if (shaMatch.hasMatch())
        sha256 = shaMatch.captured(1).toLower();
    else {
        const auto bare = shaBare.match(text);
        if (bare.hasMatch())
            sha256 = bare.captured(1).toLower();
    }
    qint64 size = 0;
    static const QRegularExpression sizePattern(
        QStringLiteral("(?:size|bytes)\\s*[:=]\\s*([0-9]{3,12})\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const auto sizeMatch = sizePattern.match(text);
    if (sizeMatch.hasMatch())
        size = sizeMatch.captured(1).toLongLong();

    // ---- notes: what is left once the bookkeeping is removed -------------
    QString notes = text;
    notes.remove(bestUrl.url);
    // ...and the same link as it was pasted, without the scheme.
    if (bestUrl.url.startsWith(QLatin1String("https://")))
        notes.remove(bestUrl.url.mid(8));
    // Any other link (a video, a chat message) is not release notes.
    notes.remove(urlPattern);
    notes.remove(shaPattern);
    if (!sha256.isEmpty())
        notes.remove(sha256, Qt::CaseInsensitive); // the bare-hash form
    if (size > 0)
        notes.remove(sizePattern);
    const auto versionMatch = explicitV.match(notes);
    if (versionMatch.hasMatch())
        notes.remove(versionMatch.captured(0));
    static const QRegularExpression productLine(
        QStringLiteral("^bsi(\\s*customizer)?$"), QRegularExpression::CaseInsensitiveOption);
    QStringList kept;
    for (const QString &line : notes.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() && kept.isEmpty())
            continue; // no leading blank lines
        if (productLine.match(trimmed).hasMatch())
            continue; // what is left of "BSI Customizer v2.0.1"
        static const QRegularExpression labelLine(
            QStringLiteral("^(download|link|url|get|file|mirror)\\s*:?\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        if (labelLine.match(trimmed).hasMatch())
            continue; // the word before the link we already removed
        // publish.mjs appends a mention of the bot so Discord hands the text
        // over without the Message Content intent; it is plumbing, not notes.
        static const QRegularExpression mentionLine(
            QStringLiteral("^\\(?\\s*(posted|published)\\s+by\\s+<@[!&]?[0-9]+>.*$|"
                           "^<@[!&]?[0-9]+>\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        if (mentionLine.match(trimmed).hasMatch())
            continue;
        kept << trimmed;
    }
    while (!kept.isEmpty() && kept.last().isEmpty())
        kept.removeLast();
    QString cleaned;
    for (const QString &line : kept) {
        if (line.isEmpty() && !cleaned.isEmpty() && cleaned.endsWith(QLatin1Char('\n')))
            continue; // collapse runs of blank lines
        cleaned += line + QLatin1Char('\n');
    }
    notes = cleaned.trimmed().left(4000);

    // ---- the file name the download is saved under -----------------------
    QString file = QUrl(bestUrl.url).fileName();
    if (!file.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        file = QStringLiteral("BSI-Customizer-%1-win64.exe").arg(version);

    QJsonObject release{
        { QStringLiteral("schema"), 1 },
        { QStringLiteral("version"), version },
        { QStringLiteral("file"), file },
        { QStringLiteral("url"), bestUrl.url },
        { QStringLiteral("messageId"), message.value(QStringLiteral("id")).toString() },
        { QStringLiteral("source"), QStringLiteral("discord message") },
    };
    if (!sha256.isEmpty())
        release.insert(QStringLiteral("sha256"), sha256);
    if (size > 0)
        release.insert(QStringLiteral("size"), double(size));
    if (!notes.isEmpty())
        release.insert(QStringLiteral("notes"), notes);
    *manifest = release;
    return true;
}

QString UpdateChecker::trust() const
{
    if (!m_available)
        return {};
    if (!m_config.hmacKey.isEmpty() && !m_pinnedRelease)
        return QStringLiteral("unverified");
    if (m_pinnedRelease)
        return m_config.hmacKey.isEmpty() ? QStringLiteral("pinned") : QStringLiteral("signed");
    return QStringLiteral("first-use");
}

// What this PC learned about a version the first time it downloaded it. A
// message that carries no SHA-256 cannot be checked before the download, so the
// result is remembered: if the same version ever downloads different bytes,
// that is a changed upload, not an update.
QString UpdateChecker::pinnedHash(const QString &version) const
{
    if (version.isEmpty())
        return {};
    return QSettings().value(QStringLiteral("updates/pinned/%1").arg(version)).toString();
}

void UpdateChecker::storePinnedHash(const QString &version, const QString &hash) const
{
    if (version.isEmpty() || hash.isEmpty())
        return;
    QSettings().setValue(QStringLiteral("updates/pinned/%1").arg(version), hash);
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

bool UpdateChecker::looksLikeHtml(const QByteArray &bytes)
{
    QByteArray probe = bytes.left(1024);
    if (probe.startsWith("\xEF\xBB\xBF"))
        probe = probe.mid(3);
    probe = probe.trimmed().toLower();
    if (probe.startsWith("mz") || probe.startsWith("pk"))
        return false;
    return probe.startsWith("<!doctype html") || probe.startsWith("<html")
           || probe.startsWith("<head") || probe.startsWith("<?xml")
           || (probe.startsWith("<") && probe.contains("</"));
}

QString UpdateChecker::resolveDownloadLink(const QByteArray &page, const QUrl &baseUrl,
                                           QString *detail)
{
    const auto none = [detail, &baseUrl](const QString &why) {
        if (detail) {
            const QString host = baseUrl.host().toLower();
            if (host.contains(QLatin1String("pcloud")))
                *detail = QStringLiteral("On pCloud, a normal share link only opens a page. Either "
                                         "put the EXE in the Public folder and use Share > Direct "
                                         "link, or make sure the page offers a download button "
                                         "(%1)").arg(why);
            else
                *detail = why;
        }
        return QString();
    };
    if (detail)
        detail->clear();
    if (page.isEmpty())
        return none(QStringLiteral("the page was empty"));

    // 2 MB is far more than any landing page needs and keeps a wrong URL cheap.
    const QString text = QString::fromUtf8(page.left(2 * 1024 * 1024));

    // pCloud (and many other hosts) embed the real URL in JSON as
    // "downloadlink":"https:\/\/p-def2.pcloud.com\/...". The key names differ.
    static const QStringList keys{
        QStringLiteral("downloadlink"), QStringLiteral("downloadLink"),
        QStringLiteral("downloadurl"), QStringLiteral("downloadUrl"),
        QStringLiteral("directlink"), QStringLiteral("directLink"),
        QStringLiteral("filelink"), QStringLiteral("fileLink"),
        QStringLiteral("fileurl"), QStringLiteral("fileUrl")
    };
    for (const QString &key : keys) {
        const QRegularExpression pattern(
            QStringLiteral("\"%1\"\\s*:\\s*\"([^\"]{8,4096})\"").arg(QRegularExpression::escape(key)),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = pattern.match(text);
        if (!match.hasMatch())
            continue;
        const QString candidate = unescapePageValue(match.captured(1));
        if (isHttpsUrl(candidate))
            return candidate;
    }

    // An ordinary <a href="...">download...</a> button.
    static const QRegularExpression href(
        QStringLiteral("href\\s*=\\s*[\"']([^\"']+\\.(?:exe|zip|7z|msi|bin)(?:\\?[^\"']*)?)[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    for (const auto &match : href.globalMatch(text)) {
        const QString candidate = unescapePageValue(match.captured(1));
        const QUrl url(candidate);
        const QString absolute = url.isRelative() ? baseUrl.resolved(url).toString()
                                                  : candidate;
        if (isHttpsUrl(absolute))
            return absolute;
    }

    // Last resort: a bare link to the file somewhere in the page source.
    static const QRegularExpression bare(
        QStringLiteral("https://[^\"'\\s<>()]+\\.(?:exe|zip)(?:\\?[^\"'\\s<>()]*)?"));
    const auto bareMatch = bare.match(text);
    if (bareMatch.hasMatch() && isHttpsUrl(bareMatch.captured(0)))
        return bareMatch.captured(0);

    return none(QStringLiteral("no https download link was found on the page"));
}

QString UpdateChecker::downloadHost() const
{
    return QUrl(m_downloadUrl).host();
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
