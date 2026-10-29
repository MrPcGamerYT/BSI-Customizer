#include "AuthManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPair>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrlQuery>
#include <QVariantMap>

namespace {

const char kAuthorizeUrl[] = "https://discord.com/oauth2/authorize";
const char kTokenUrl[] = "https://discord.com/api/oauth2/token";
const char kApiUrl[] = "https://discord.com/api";
const char kRedirectPath[] = "/auth/discord/callback";
const char kGuildMemberScope[] = "guilds.members.read";
const int kDefaultRedirectPort = 43713;
const int kDiscordHandoffMs = 180000;   // same 3-minute window as FalcoForge
const int kLoopbackRetries = 4;
const int kLoopbackRetryDelayMs = 150;

QString envOr(const QProcessEnvironment &env, const char *name, const QString &fallback)
{
    const QString value = env.value(QString::fromLatin1(name)).trimmed();
    return value.isEmpty() ? fallback : value;
}

QString lower(const QString &s) { return s.toLower(); }

bool containsText(const QString &haystack, const QString &needle)
{
    return haystack.contains(needle, Qt::CaseInsensitive);
}

QString firstNonEmpty(const QStringList &values)
{
    for (const QString &v : values) {
        if (!v.trimmed().isEmpty())
            return v.trimmed();
    }
    return {};
}

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString buildForm(const QList<QPair<QString, QString>> &fields)
{
    QUrlQuery query;
    for (const auto &f : fields)
        query.addQueryItem(f.first, f.second);
    return query.toString(QUrl::FullyEncoded);
}

// GitHub secrets are often pasted with a surrounding ```json fence (or a BOM).
// Strip that packaging so the blob itself parses as JSON.
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

// Snowflake-ish config values are sometimes written unquoted in JSON. Those
// arrive as doubles; accept them so a numeric clientId/guildId still works.
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

// Chat apps auto-link "guilds.members.read" (and bare invite URLs) into
// "[label](target)" markdown when an auth.json is copied through them. Strip
// that shape so the value that reaches Discord is the real scope/URL.
QString stripMarkdownLink(const QString &input, bool preferLabel)
{
    static const QRegularExpression markdown(
        QStringLiteral("\\[([^\\]]*)\\]\\(([^)]*)\\)"));
    if (!markdown.match(input).hasMatch())
        return input.trimmed();
    QString cleaned = input;
    // For scopes keep the label ("guilds.members.read"); for URLs keep the
    // target ("https://discord.gg/..."). Everything around the link survives,
    // so "identify [guilds.members.read](...)" keeps the identify scope.
    cleaned.replace(markdown, preferLabel ? QStringLiteral("\\1") : QStringLiteral("\\2"));
    return cleaned.trimmed();
}

// Discord wants scopes space separated. Accept commas/semicolons/extra spaces,
// dedupe, always keep identify, and make sure the role gate carries the member
// scope. A malformed scope here is the usual cause of Discord refusing login.
QString normalizeScope(const QString &raw, bool needsGuildMemberRead)
{
    QString cleaned = stripMarkdownLink(raw, true);
    cleaned.replace(QLatin1Char(','), QLatin1Char(' '));
    cleaned.replace(QLatin1Char(';'), QLatin1Char(' '));
    QStringList scopes;
    for (const QString &piece : cleaned.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts)) {
        const QString scope = piece.trimmed().toLower();
        if (!scope.isEmpty() && !scopes.contains(scope))
            scopes << scope;
    }
    if (!scopes.contains(QStringLiteral("identify")))
        scopes.prepend(QStringLiteral("identify"));
    if (needsGuildMemberRead && !scopes.contains(QLatin1String(kGuildMemberScope)))
        scopes << QLatin1String(kGuildMemberScope);
    return scopes.join(QLatin1Char(' '));
}

} // namespace

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

AuthManager::Config AuthManager::loadConfig()
{
    Config config;
    config.redirectPort = kDefaultRedirectPort;

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // Config sources for the `discord` object, first one that parses wins:
    //   1. the whole auth.json as a JSON blob - $ZLP_AUTH_JSON (the name the
    //      CI secret carries; $BSIC_AUTH_JSON accepted as an alias);
    //   2. an auth.json file: $BSIC_AUTH_FILE, then next to the EXE, then app
    //      data;
    //   3. the blob baked into this build from the ZLP_AUTH_JSON secret.
    auto parseDiscord = [](const QByteArray &bytes, QJsonObject *out) {
        const QByteArray json = cleanJsonBlob(bytes);
        if (json.isEmpty())
            return false;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            return false;
        // A file without a "discord" object is not a sign-in config - a
        // sidecar that only carries the updater must not hide the build-time
        // blob (or the next candidate) that does configure the gate.
        if (!doc.object().value(QStringLiteral("discord")).isObject())
            return false;
        *out = doc.object().value(QStringLiteral("discord")).toObject();
        return true;
    };

    QJsonObject discord;
    bool embeddedPolicyAuthoritative = false;
#ifdef BSI_ENFORCE_EMBEDDED_AUTH
    if (!parseDiscord(embeddedAuthJson(), &discord)) {
        config.setupHint = QStringLiteral("This release has no valid embedded Discord access policy. "
                                          "It cannot be unlocked with a local auth.json file.");
        return config;
    }
    config.sourceFile = QStringLiteral("embedded, locked release policy");
    embeddedPolicyAuthoritative = true;
#else
    const QByteArray zlpBlob = env.value(QStringLiteral("ZLP_AUTH_JSON")).toUtf8();
    const QByteArray bsicBlob = env.value(QStringLiteral("BSIC_AUTH_JSON")).toUtf8();
    if (parseDiscord(zlpBlob, &discord)) {
        config.sourceFile = QStringLiteral("environment (ZLP_AUTH_JSON)");
    } else if (parseDiscord(bsicBlob, &discord)) {
        config.sourceFile = QStringLiteral("environment (BSIC_AUTH_JSON)");
    } else {
        QStringList candidates;
        const QString envFile = env.value(QStringLiteral("BSIC_AUTH_FILE")).trimmed();
        if (!envFile.isEmpty())
            candidates << envFile;
        candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/auth.json");
        candidates << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + QStringLiteral("/auth.json");

        for (const QString &path : candidates) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            if (parseDiscord(f.readAll(), &discord)) {
                config.sourceFile = path;
                f.close();
                break;
            }
            f.close();
        }
        if (config.sourceFile.isEmpty() && parseDiscord(embeddedAuthJson(), &discord))
            config.sourceFile = QStringLiteral("embedded auth.json (build-time ZLP_AUTH_JSON)");
    }
#endif
    // In official release builds environment variables cannot replace a baked
    // guild, role, allowlist, client ID, or secret.
    const QProcessEnvironment settingsEnv = embeddedPolicyAuthoritative
                                                ? QProcessEnvironment()
                                                : env;

    config.clientId = envOr(settingsEnv, "BSIC_DISCORD_CLIENT_ID",
                            jsonText(discord.value("clientId")))
                          .trimmed();
    config.clientSecret = envOr(settingsEnv, "BSIC_DISCORD_CLIENT_SECRET",
                                jsonText(discord.value("clientSecret")))
                              .trimmed();
    config.guildId = envOr(settingsEnv, "BSIC_DISCORD_GUILD_ID",
                           jsonText(discord.value("guildId")))
                         .trimmed();
    config.requiredTier = envOr(settingsEnv, "BSIC_DISCORD_REQUIRED_TIER",
                                jsonText(discord.value("requiredTier")))
                              .trimmed();

    // Role gate: discord.roles maps a role snowflake to either a tier string or
    // {"tier": "Dev", "label": "Dev"} - the shape Premium-Optimizer's auth.json
    // uses. An array of {"id", "tier"} objects is accepted too.
    const QJsonValue rolesValue = discord.value(QStringLiteral("roles"));
    if (rolesValue.isObject()) {
        const QJsonObject roleMap = rolesValue.toObject();
        for (auto it = roleMap.constBegin(); it != roleMap.constEnd(); ++it) {
            const QString roleId = it.key().trimmed();
            if (roleId.isEmpty())
                continue;
            QString tier;
            if (it.value().isObject()) {
                const QJsonObject definition = it.value().toObject();
                tier = firstNonEmpty({
                    jsonText(definition.value(QStringLiteral("tier"))),
                    jsonText(definition.value(QStringLiteral("label"))),
                    jsonText(definition.value(QStringLiteral("name"))),
                });
            } else {
                tier = jsonText(it.value());
            }
            config.roles.insert(roleId, tier.trimmed());
        }
    } else if (rolesValue.isArray()) {
        for (const QJsonValue &entry : rolesValue.toArray()) {
            if (entry.isString()) {
                const QString roleId = entry.toString().trimmed();
                if (!roleId.isEmpty())
                    config.roles.insert(roleId, QString());
                continue;
            }
            if (!entry.isObject())
                continue;
            const QJsonObject definition = entry.toObject();
            const QString roleId = firstNonEmpty({
                jsonText(definition.value(QStringLiteral("id"))),
                jsonText(definition.value(QStringLiteral("roleId"))),
                jsonText(definition.value(QStringLiteral("role"))),
            });
            if (roleId.isEmpty())
                continue;
            QString tier = firstNonEmpty({
                jsonText(definition.value(QStringLiteral("tier"))),
                jsonText(definition.value(QStringLiteral("label"))),
                jsonText(definition.value(QStringLiteral("name"))),
            });
            config.roles.insert(roleId, tier.trimmed());
        }
    }

    // Keep the scopes Discord needs. guilds.members.read is added for a role
    // gate even when the blob forgets it; identify is always present.
    config.scope = normalizeScope(envOr(settingsEnv, "BSIC_DISCORD_SCOPE",
                                        jsonText(discord.value("scope"))),
                                  config.roleGateConfigured());
    config.inviteUrl = stripMarkdownLink(
        envOr(settingsEnv, "BSIC_DISCORD_INVITE", jsonText(discord.value("inviteUrl"))), false);

    const QString portEnv = settingsEnv.value(QStringLiteral("BSIC_DISCORD_REDIRECT_PORT")).trimmed();
    bool portOk = false;
    const int portFromEnv = portEnv.toInt(&portOk);
    const int portFromFile = discord.value("redirectPort").toInt(kDefaultRedirectPort);
    config.redirectPort = (portOk && portFromEnv > 0 && portFromEnv < 65536)
                              ? portFromEnv
                              : (portFromFile > 0 && portFromFile < 65536 ? portFromFile
                                                                         : kDefaultRedirectPort);
    config.redirectUri = QStringLiteral("http://127.0.0.1:%1%2")
                             .arg(config.redirectPort)
                             .arg(QString::fromLatin1(kRedirectPath));

    // Allowlist: BSIC_DISCORD_ALLOW_USERS ("id1,id2") or discord.allowUsers.
    QStringList allow;
    const QString allowEnv = settingsEnv.value(QStringLiteral("BSIC_DISCORD_ALLOW_USERS")).trimmed();
    if (!allowEnv.isEmpty()) {
        for (const QString &piece : allowEnv.split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                  Qt::SkipEmptyParts))
            allow << piece.trimmed();
    } else {
        const QJsonValue list = discord.value("allowUsers");
        if (list.isArray()) {
            for (const QJsonValue &v : list.toArray()) {
                const QString id = v.toString().trimmed();
                if (!id.isEmpty())
                    allow << id;
            }
        } else if (list.isString()) {
            for (const QString &piece : list.toString().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                                             Qt::SkipEmptyParts))
                allow << piece.trimmed();
        }
    }
    config.allowUsers = allow;

#ifdef BSI_ENFORCE_EMBEDDED_AUTH
    if (!config.roleGateConfigured()) {
        config.setupHint = QStringLiteral("This release requires an embedded Discord server-role "
                                          "policy (guildId and roles); an allowlist-only policy is "
                                          "not sufficient.");
        return config;
    }
#endif

    config.configured = !config.clientId.isEmpty();
    if (!config.configured) {
#ifdef BSI_ENFORCE_EMBEDDED_AUTH
        config.setupHint = QStringLiteral("The embedded release policy is missing discord.clientId. "
                                          "Rebuild with the correct ZLP_AUTH_JSON secret.");
#else
        config.setupHint = QStringLiteral(
            "Discord sign-in is not configured yet. Set BSIC_DISCORD_CLIENT_ID, or copy "
            "auth.example.json to auth.json next to the EXE and fill in discord.clientId - "
            "or bake the whole auth.json into the build with the ZLP_AUTH_JSON secret. "
            "See docs/AUTH.md.");
#endif
    } else if (config.allowUsers.isEmpty() && !config.roleGateConfigured()) {
        if (!config.roles.isEmpty() && config.guildId.isEmpty()) {
            config.setupHint = QStringLiteral(
                "Discord roles are configured but discord.guildId is missing, so the role gate "
                "cannot be checked. Add the server ID (Developer Mode -> Copy Server ID) - see "
                "docs/AUTH.md.");
        } else {
            config.setupHint = QStringLiteral(
                "No dev access gate is configured: add your Discord user ID to "
                "discord.allowUsers, or set discord.guildId + discord.roles + "
                "discord.requiredTier for a server-role gate - see docs/AUTH.md.");
        }
    } else {
        QString gate = QStringLiteral("allowlist");
        if (config.roleGateConfigured()) {
            gate = config.requiredTier.isEmpty()
                       ? QStringLiteral("server roles")
                       : QStringLiteral("server role tier %1").arg(config.requiredTier);
        }
        config.setupHint = QStringLiteral("Sign-in config: %1 (%2)")
                               .arg(config.sourceFile.isEmpty() ? QStringLiteral("environment")
                                                                : config.sourceFile,
                                    gate);
    }
    return config;
}

QByteArray AuthManager::embeddedAuthJson()
{
    QFile f(QStringLiteral(":/bsic_auth/auth.json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

// ---------------------------------------------------------------------------
// Construction / state
// ---------------------------------------------------------------------------

AuthManager::AuthManager(QObject *parent)
    : AuthManager(QString(), parent)
{
}

AuthManager::AuthManager(const QString &sessionDataDir, QObject *parent)
    : QObject(parent)
    , m_config(loadConfig())
    , m_store(sessionDataDir)
{
    if (m_store.hasSaved())
        emit sessionChanged();

    if (!m_config.configured) {
        setNote(m_config.setupHint, QStringLiteral("bad"));
    } else if (m_store.hasSaved()) {
        // The local identity is only a hint for the Welcome back card. It never
        // opens the gate: refresh Discord and fetch current roles every launch.
        setBusy(true);
        setProgress(0, true);
        setNote(QStringLiteral("Checking your current Discord account and server access…"),
                QString());
        QTimer::singleShot(0, this, [this] { recheckSavedSession(false); });
    } else {
        setNote(QStringLiteral("Sign in with Discord to verify your current access."),
                QString());
    }
}

bool AuthManager::hasSavedSession() const
{
    return m_store.hasSaved();
}

QVariantMap AuthManager::savedUser() const
{
    return m_store.identity();
}

QString AuthManager::savedMeta() const
{
    if (!m_store.hasSaved())
        return {};
    const QString at = m_store.signedInAt();
    if (at.isEmpty())
        return QStringLiteral("Access verified on this PC.");
    const QDateTime when = QDateTime::fromString(at, Qt::ISODate);
    return QStringLiteral("Last verified %1").arg(
        when.isValid() ? when.toLocalTime().toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("on this PC"));
}

void AuthManager::setNote(const QString &text, const QString &kind)
{
    if (m_note == text && m_noteKind == kind)
        return;
    m_note = text;
    m_noteKind = kind;
    emit noteChanged();
}

void AuthManager::setProgress(qreal value, bool indeterminate)
{
    // The +1 offset keeps qFuzzyCompare honest at exactly 0.
    if (qFuzzyCompare(m_progress + 1, value + 1) && m_progressIndeterminate == indeterminate)
        return;
    m_progress = value;
    m_progressIndeterminate = indeterminate;
    emit progressChanged();
}

void AuthManager::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void AuthManager::setAuthorized(bool on)
{
    if (m_authorized == on)
        return;
    m_authorized = on;
    emit authorizedChanged();
}

bool AuthManager::allowlisted(const QString &id) const
{
    if (id.isEmpty())
        return false;
    return m_config.allowUsers.contains(id);
}

bool AuthManager::tierForRoles(const Config &config, const QStringList &memberRoles,
                               QString *tier, QString *roleId)
{
    const QString required = config.requiredTier.trimmed();
    for (auto it = config.roles.constBegin(); it != config.roles.constEnd(); ++it) {
        if (!memberRoles.contains(it.key()))
            continue;
        const QString label = it.value().trimmed();
        // A plain role ID (no tier info) is an explicit grant; otherwise the
        // role's tier must match discord.requiredTier (case-insensitive).
        if (!required.isEmpty() && !label.isEmpty()
            && label.compare(required, Qt::CaseInsensitive) != 0)
            continue;
        if (tier) {
            *tier = label.isEmpty()
                        ? (required.isEmpty() ? QStringLiteral("member") : required)
                        : label;
        }
        if (roleId)
            *roleId = it.key();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// PKCE + authorize URL (same construction as FalcoForge's AuthClient)
// ---------------------------------------------------------------------------

QString AuthManager::pkceVerifier()
{
    QByteArray raw(32, Qt::Uninitialized);
    for (int i = 0; i < raw.size(); ++i)
        raw[i] = char(QRandomGenerator::global()->generate() & 0xff);
    return QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding
                                            | QByteArray::OmitTrailingEquals));
}

QString AuthManager::pkceChallenge(const QString &verifier)
{
    const QByteArray hash = QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

QString AuthManager::randomHex(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    for (int i = 0; i < raw.size(); ++i)
        raw[i] = char(QRandomGenerator::global()->generate() & 0xff);
    return QString::fromLatin1(raw.toHex());
}

QString AuthManager::authorizeUrl(const QString &state, const QString &challenge) const
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), m_config.clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), m_config.redirectUri);
    query.addQueryItem(QStringLiteral("scope"), m_config.scope);
    query.addQueryItem(QStringLiteral("state"), state);
    query.addQueryItem(QStringLiteral("prompt"), QStringLiteral("consent"));
    query.addQueryItem(QStringLiteral("code_challenge"), challenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    return QString::fromLatin1(kAuthorizeUrl) + QLatin1Char('?')
           + query.toString(QUrl::FullyEncoded);
}

void AuthManager::readCode(const QString &input, QString &code, QString &state)
{
    code.clear();
    state.clear();
    const QString clean = input.trimmed();
    if (clean.isEmpty())
        return;
    if (!clean.contains(QLatin1String("://")) && !clean.contains(QLatin1Char('?'))
        && !clean.contains(QLatin1String("code="))) {
        code = clean;
        return;
    }
    QString query = clean;
    const int mark = clean.indexOf(QLatin1Char('?'));
    if (mark >= 0)
        query = clean.mid(mark + 1);
    if (query.startsWith(QLatin1Char('#')))
        query.remove(0, 1);
    for (const QString &pair : query.split(QLatin1Char('&'))) {
        const int equals = pair.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = QUrl::fromPercentEncoding(pair.left(equals).toUtf8());
        const QString value = QUrl::fromPercentEncoding(pair.mid(equals + 1).toUtf8());
        if (key == QLatin1String("code"))
            code = value;
        else if (key == QLatin1String("state"))
            state = value;
    }
}

// ---------------------------------------------------------------------------
// HTTP (synchronous on the GUI thread, with a hard timeout)
// ---------------------------------------------------------------------------

AuthManager::HttpAnswer AuthManager::httpGet(const QString &url, const QString &bearer)
{
    HttpAnswer out;
    QNetworkRequest request { QUrl(url) };
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BSI-Customizer/2.0 (dev gate)"));
    request.setRawHeader("Accept", "application/json");
    if (!bearer.isEmpty())
        request.setRawHeader("Authorization", ("Bearer " + bearer).toUtf8());

    QNetworkReply *reply = m_nam.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, [&]() { reply->abort(); });
    connect(reply, &QNetworkReply::finished, &loop, [&]() { loop.quit(); });
    timeout.start(20000);
    loop.exec();
    timeout.stop();

    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.body = reply->readAll();
    if (out.status == 0)
        out.error = reply->errorString();
    reply->deleteLater();
    return out;
}

AuthManager::HttpAnswer AuthManager::httpPostForm(const QString &url, const QByteArray &form)
{
    HttpAnswer out;
    QNetworkRequest request { QUrl(url) };
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("BSI-Customizer/2.0 (dev gate)"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_nam.post(request, form);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, [&]() { reply->abort(); });
    connect(reply, &QNetworkReply::finished, &loop, [&]() { loop.quit(); });
    timeout.start(20000);
    loop.exec();
    timeout.stop();

    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    out.body = reply->readAll();
    if (out.status == 0)
        out.error = reply->errorString();
    reply->deleteLater();
    return out;
}

// ---------------------------------------------------------------------------
// Token-error mapping (port of FalcoForge's mapDiscordTokenError)
// ---------------------------------------------------------------------------

QString AuthManager::mapDiscordTokenError(int status, const QByteArray &body,
                                          const QString &redirectUri, const QString &scope)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    const QJsonObject tokenBody = doc.object();
    QString detail = tokenBody.value(QStringLiteral("error_description")).toString();
    if (detail.isEmpty())
        detail = tokenBody.value(QStringLiteral("error")).toString();
    const QString desc = lower(detail + QLatin1Char(' ') + QString::fromUtf8(body));

    if (status == 0)
        return QStringLiteral("The sign-in service could not be reached. Discord login needs an "
                              "internet connection.");
    if (containsText(desc, QStringLiteral("invalid form body"))
        || containsText(desc, QStringLiteral("50035")))
        return QStringLiteral("Discord rejected the token request as a malformed form body "
                              "(50035). Press the Discord button once more for a fresh attempt; "
                              "if it keeps happening, compare the redirect below with the one in "
                              "your Discord Developer Portal and check the client id.\n\nRedirect: ")
               + redirectUri;
    if (containsText(desc, QStringLiteral("redirect_uri"))
        || containsText(desc, QStringLiteral("redirect uri"))
        || containsText(desc, QStringLiteral("redirect-uri")))
        return QStringLiteral("The redirect URI does not match the one registered in the Discord "
                              "Developer Portal. Add exactly this redirect: ")
               + redirectUri
               + QStringLiteral("  (127.0.0.1 on purpose - not localhost), then press the Discord "
                                "button once more.");
    if (containsText(desc, QStringLiteral("invalid_client")))
        return QStringLiteral(
            "Discord rejected the application credentials (invalid_client). Check that the "
            "client id is the numeric Application ID from the Discord Developer Portal. If "
            "discord.clientSecret is empty, Discord only accepts a secret-less PKCE exchange "
            "when the application has the Public Client flag enabled (Developer Portal -> "
            "OAuth2); otherwise put the current Client Secret into the ZLP_AUTH_JSON secret as "
            "discord.clientSecret and re-run the build.");
    if (containsText(desc, QStringLiteral("code_verifier"))
        || containsText(desc, QStringLiteral("code verifier"))
        || containsText(desc, QStringLiteral("pkce"))
        || containsText(desc, QStringLiteral("challenge")))
        return QStringLiteral("The one-time sign-in code and its PKCE proof no longer match. "
                              "Press the Discord button once more to start a fresh attempt.");
    if (containsText(desc, QStringLiteral("invalid_grant"))
        || containsText(desc, QStringLiteral("expired")))
        return QStringLiteral("That one-time sign-in code has already been used or has expired. "
                              "Press the Discord button once more and finish in the browser "
                              "right away.");
    if (containsText(desc, QStringLiteral("scope")))
        return QStringLiteral(
                   "Discord refused the requested scopes (%1). Scopes must be space separated "
                   "and the application has to be allowed to request them - see docs/AUTH.md.")
            .arg(scope);
    if (!detail.isEmpty())
        return QStringLiteral("Discord refused the sign-in: ") + detail;
    return QStringLiteral("Discord refused the sign-in (HTTP %1). Press the Discord button once "
                          "more to start a fresh attempt.")
        .arg(status);
}

QString AuthManager::avatarUrlFor(const QVariantMap &user)
{
    const QString id = user.value(QStringLiteral("Id")).toString();
    const QString hash = user.value(QStringLiteral("Avatar")).toString();
    if (!hash.isEmpty()) {
        const QString suffix = hash.startsWith(QLatin1String("a_")) ? QStringLiteral("gif")
                                                                    : QStringLiteral("png");
        return QStringLiteral("https://cdn.discordapp.com/avatars/%1/%2.%3?size=128")
            .arg(id, hash, suffix);
    }
    // No avatar: (id >> 22) % 6 picks the same default picture Discord shows.
    bool ok = false;
    const qulonglong snowflake = id.toULongLong(&ok);
    const int index = ok ? int((snowflake >> 22) % 6) : 0;
    return QStringLiteral("https://cdn.discordapp.com/embed/avatars/%1.png").arg(index);
}

// ---------------------------------------------------------------------------
// Branded callback page (port of FalcoForge's callbackPage)
// ---------------------------------------------------------------------------

QString AuthManager::callbackPage(bool success, const QString &title, const QString &detail) const
{
    const QString accent = success ? QStringLiteral("#3ddc97") : QStringLiteral("#ff6b6b");
    const QString mark = success ? QStringLiteral("&#10003;") : QStringLiteral("&#33;");
    QString page;
    page += QStringLiteral(
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>BSI Customizer - Discord sign-in</title><style>"
        "body{margin:0;min-height:100vh;display:grid;place-items:center;background:#02050a;"
        "font:15px/1.6 'Segoe UI',system-ui,sans-serif;color:#e8f0ff}"
        ".card{max-width:520px;margin:24px;padding:32px 30px;border-radius:20px;text-align:center;"
        "background:linear-gradient(160deg,rgba(20,32,58,.96),rgba(8,14,26,.96));"
        "border:1px solid rgba(120,170,255,.18);box-shadow:0 30px 80px rgba(0,0,0,.55)}"
        ".mark{width:64px;height:64px;margin:0 auto 18px;border-radius:50%;display:grid;place-items:center;"
        "font-size:30px;color:#02050a;background:");
    page += accent;
    page += QStringLiteral(";box-shadow:0 0 0 10px ");
    page += accent;
    page += QStringLiteral(
        "22}"
        "h1{margin:0 0 8px;font-size:21px;letter-spacing:.2px}p{margin:0 0 18px;color:#a9bde0}"
        "small{color:#7f92b5}</style></head><body><div class=\"card\">"
        "<div class=\"mark\">");
    page += mark;
    page += QStringLiteral("</div><h1>");
    page += title.toHtmlEscaped();
    page += QStringLiteral("</h1><p>");
    page += detail.toHtmlEscaped();
    page += QStringLiteral(
        "</p>"
        "<small>You can close this tab and go back to BSI Customizer.</small>"
        "</div><script>setTimeout(function(){try{window.close()}catch(e){}},1200);</script>"
        "</body></html>");
    return page;
}

// ---------------------------------------------------------------------------
// Loopback listener
// ---------------------------------------------------------------------------

bool AuthManager::startLoopback(QString *error)
{
    stopLoopback();

    for (int attempt = 0; attempt < kLoopbackRetries; ++attempt) {
        auto *server = new QTcpServer(this);
        if (server->listen(QHostAddress::LocalHost, quint16(m_config.redirectPort))) {
            m_loopback = server;
            connect(server, &QTcpServer::newConnection, this, [this, server]() {
                while (server->hasPendingConnections()) {
                    QTcpSocket *sock = server->nextPendingConnection();
                    // Sockets are children of the AuthManager, not the server,
                    // so closing the listener (which finishes the hand-off from
                    // inside its own callback) never kills the response.
                    sock->setParent(this);
                    connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
                    connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
                        if (sock->property("bsiResponded").toBool())
                            return;
                        sock->setProperty("bsiResponded", true);
                        const QByteArray request = sock->readAll();
                        const int lineEnd = request.indexOf("\r\n");
                        const QByteArray line = request.left(lineEnd > 0 ? lineEnd : request.size());
                        const QList<QByteArray> parts = line.split(' ');
                        QString path;
                        QString query;
                        if (parts.size() >= 2) {
                            const QUrl url = QUrl::fromEncoded(parts.at(1));
                            path = url.path();
                            query = url.query(QUrl::FullyEncoded);
                        }
                        const QString page = handleCallback(path, query);
                        const QByteArray body = page.toUtf8();
                        QByteArray response;
                        response += "HTTP/1.1 200 OK\r\n";
                        response += "Content-Type: text/html; charset=utf-8\r\n";
                        response += "Cache-Control: no-store\r\n";
                        response += "Connection: close\r\n";
                        response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
                        response += body;
                        sock->write(response);
                        sock->disconnectFromHost();
                    });
                }
            });
            return true;
        }
        // The usual reason the registered port is busy is this app's own
        // previous run still holding it, so a short retry almost always wins it
        // back and the redirect URI stays the one registered in the portal.
        const QString why = server->errorString();
        server->deleteLater();
        if (error)
            *error = why;
        QThread::msleep(kLoopbackRetryDelayMs);
    }
    return false;
}

void AuthManager::stopLoopback()
{
    QTcpServer *server = m_loopback;
    m_loopback = nullptr;
    if (!server)
        return;
    server->close();
    server->deleteLater();
}

QString AuthManager::handleCallback(const QString &path, const QString &query)
{
    QUrlQuery params(query);

    if (path == QLatin1String("/auth/discord/ping"))
        return QStringLiteral("<!doctype html><title>bsi</title>bsi-customizer");

    if (path != QLatin1String(kRedirectPath)) {
        return callbackPage(false, QStringLiteral("Not the sign-in page"),
                            QStringLiteral("This helper only handles the Discord sign-in "
                                           "redirect."));
    }

    if (params.hasQueryItem(QStringLiteral("error"))) {
        const QString raw = params.queryItemValue(QStringLiteral("error"));
        const QString description = params.hasQueryItem(QStringLiteral("error_description"))
                                        ? params.queryItemValue(QStringLiteral("error_description"))
                                        : (raw == QLatin1String("access_denied")
                                               ? QStringLiteral("You cancelled the Discord sign-in.")
                                               : raw);
        DiscordProfile profile;
        profile.ok = false;
        profile.message = description.isEmpty() ? QStringLiteral("Discord refused the sign-in.")
                                                : description;
        const QString page = callbackPage(false, QStringLiteral("Discord sign-in stopped"),
                                          profile.message);
        finishHandoff(profile);
        return page;
    }

    const QString fullUrl = m_config.redirectUri + QLatin1Char('?') + query;
    const DiscordProfile profile = completeDiscord(fullUrl);
    QString page;
    if (profile.ok) {
        const QString name = profile.identity.value(QStringLiteral("DisplayName")).toString();
        page = callbackPage(true, QStringLiteral("Signed in") + (name.isEmpty() ? QString() : QStringLiteral(" as ") + name),
                            QStringLiteral("BSI Customizer has your access and is opening now."));
    } else {
        page = callbackPage(false, QStringLiteral("That sign-in did not finish"), profile.message);
    }
    finishHandoff(profile);
    return page;
}

// ---------------------------------------------------------------------------
// The flow
// ---------------------------------------------------------------------------

QVariantMap AuthManager::startDiscord()
{
    QVariantMap begin;
    if (m_busy) {
        begin.insert(QStringLiteral("ok"), false);
        begin.insert(QStringLiteral("message"),
                     QStringLiteral("A sign-in is already running - finish it in the browser."));
        return begin;
    }

    if (!m_config.configured) {
        begin.insert(QStringLiteral("ok"), false);
        begin.insert(QStringLiteral("Listener"), QStringLiteral("none"));
        QString message;
        if (m_config.sourceFile.isEmpty())
            message = QStringLiteral(
                "Discord sign-in is not configured yet. Set BSIC_DISCORD_CLIENT_ID, or copy "
                "auth.example.json to auth.json next to the EXE and fill in discord.clientId, "
                "or bake the whole auth.json into the build with the ZLP_AUTH_JSON secret. "
                "See docs/AUTH.md.");
        else
            message = QStringLiteral("Discord sign-in is not configured yet (source: %1). Add "
                                     "your Discord client ID to it, or set BSIC_DISCORD_CLIENT_ID.")
                          .arg(m_config.sourceFile);
        begin.insert(QStringLiteral("message"), message);
        setNote(message, QStringLiteral("bad"));
        setProgress(0, false);
        emit signInFinished(false, message);
        return begin;
    }

    setBusy(true);
    setProgress(0, true);

    const QString verifier = pkceVerifier();
    const QString state = randomHex(16);

    QString error;
    const bool listening = startLoopback(&error);

    m_handoff.active = true;
    m_handoff.state = state;
    m_handoff.verifier = verifier;
    m_handoff.redirect = m_config.redirectUri;
    m_handoff.deadlineMs = nowMs() + kDiscordHandoffMs;

    const QString url = authorizeUrl(state, pkceChallenge(verifier));

    begin.insert(QStringLiteral("ok"), true);
    begin.insert(QStringLiteral("Url"), url);
    begin.insert(QStringLiteral("State"), state);
    begin.insert(QStringLiteral("Redirect"), m_config.redirectUri);
    begin.insert(QStringLiteral("Port"), m_config.redirectPort);
    begin.insert(QStringLiteral("Listener"), listening ? QStringLiteral("loopback")
                                                       : QStringLiteral("manual"));
    begin.insert(QStringLiteral("Manual"), !listening);
    if (!listening) {
        const QString reason = QStringLiteral(
                                   "The sign-in helper port (127.0.0.1:%1) could not be opened on "
                                   "this PC%2. A second copy of BSI Customizer, Windows Defender "
                                   "Firewall, a proxy or your antivirus is the usual cause.")
                                   .arg(m_config.redirectPort)
                                   .arg(error.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(error));
        begin.insert(QStringLiteral("localIssue"), QStringLiteral("EADDRINUSE"));
        begin.insert(QStringLiteral("Reason"), reason);
        begin.insert(QStringLiteral("message"),
                     reason + QStringLiteral(" Finish in the browser and paste the address it "
                                             "lands on."));
        setNote(begin.value(QStringLiteral("message")).toString(), QStringLiteral("bad"));
    } else {
        begin.insert(QStringLiteral("message"),
                     QStringLiteral("Finish the sign-in in the browser window that just opened - "
                                    "the app picks it up by itself."));
        setNote(QStringLiteral("Authorize in Discord in the browser window that just opened. This "
                               "card closes automatically when Discord returns to the app."),
                QString());
    }

    QDesktopServices::openUrl(QUrl(url));
    return begin;
}

QVariantMap AuthManager::completeManual(const QString &urlOrCode)
{
    QVariantMap out;
    const QString link = urlOrCode.trimmed();
    if (link.isEmpty()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("message"),
                   QStringLiteral("Paste the address the browser landed on after approving "
                                  "Discord."));
        setNote(out.value(QStringLiteral("message")).toString(), QStringLiteral("bad"));
        return out;
    }

    setBusy(true);
    setProgress(0, true);
    setNote(QStringLiteral("Finishing the Discord sign-in..."), QString());

    const DiscordProfile profile = completeDiscord(link);
    out.insert(QStringLiteral("ok"), profile.ok);
    out.insert(QStringLiteral("message"),
               profile.ok ? QStringLiteral("Discord linked.") : profile.message);
    finishHandoff(profile);
    return out;
}

AuthManager::GuildRoleAnswer AuthManager::checkGuildRole(const QString &accessToken)
{
    GuildRoleAnswer out;
    if (!m_config.roleGateConfigured())
        return out;

    const QString url = QStringLiteral("%1/users/@me/guilds/%2/member")
                            .arg(QString::fromLatin1(kApiUrl), m_config.guildId);
    const HttpAnswer answer = httpGet(url, accessToken);

    if (answer.status == 0) {
        out.message = answer.error.isEmpty()
                          ? QStringLiteral("Discord could not be reached to check your server "
                                           "roles. Check the internet connection and sign in "
                                           "again.")
                          : QStringLiteral("Discord could not be reached to check your server "
                                           "roles: %1")
                                .arg(answer.error);
        return out;
    }

    QJsonParseError parseError;
    const QJsonObject body = QJsonDocument::fromJson(answer.body, &parseError).object();
    const int apiCode = body.value(QStringLiteral("code")).toInt();

    // 404 = 10004 unknown guild / 10007 unknown member: the sign-in worked,
    // the account simply is not in that server. That is a gate answer, not a
    // technical failure.
    if (answer.status == 404) {
        out.ok = true;
        out.notMember = true;
        return out;
    }
    if (answer.status == 403) {
        out.missingScope = true;
        out.message = QStringLiteral(
                          "Discord refused the server role check (HTTP 403%1). The access token "
                          "must carry the guilds.members.read scope - the requested scopes were "
                          "\"%2\". Check the application in the Discord Developer Portal, then "
                          "sign in again.")
                          .arg(apiCode > 0 ? QStringLiteral(", code %1").arg(apiCode) : QString(),
                               m_config.scope);
        return out;
    }
    if (answer.status == 401) {
        out.message = QStringLiteral("Discord did not accept the access token (HTTP 401). Press "
                                     "Continue with Discord to sign in again.");
        return out;
    }
    if (answer.status != 200) {
        out.message = QStringLiteral("Discord could not check your server roles (HTTP %1%2).")
                          .arg(answer.status)
                          .arg(apiCode > 0 ? QStringLiteral(", code %1").arg(apiCode) : QString());
        return out;
    }

    const QJsonValue rolesValue = body.value(QStringLiteral("roles"));
    if (!rolesValue.isArray()) {
        out.message = QStringLiteral("Discord returned an unexpected server member object, so "
                                     "the role gate cannot be checked. Press Continue with Discord "
                                     "to try again.");
        return out;
    }

    QStringList memberRoles;
    for (const QJsonValue &value : rolesValue.toArray()) {
        if (value.isString())
            memberRoles << value.toString();
    }

    out.ok = true;
    out.granted = tierForRoles(m_config, memberRoles, &out.tier, &out.roleId);
    return out;
}

AuthManager::DiscordProfile AuthManager::completeDiscord(const QString &urlOrCode)
{
    DiscordProfile profile;

    if (!m_config.configured) {
        profile.message = QStringLiteral("Discord sign-in is not set up in this build yet.");
        return profile;
    }

    QString code;
    QString state;
    readCode(urlOrCode, code, state);
    if (code.isEmpty()) {
        profile.message = QStringLiteral("That link has no sign-in code in it.");
        return profile;
    }
    const bool isRedirectUrl = urlOrCode.contains(QLatin1String("://"))
                               || urlOrCode.contains(QLatin1Char('?'))
                               || urlOrCode.contains(QLatin1String("code="))
                               || urlOrCode.contains(QLatin1String("state="));
    if ((isRedirectUrl && (state.isEmpty() || m_handoff.state.isEmpty()))
        || (!state.isEmpty() && state != m_handoff.state)) {
        profile.message = QStringLiteral(
            "The sign-in state did not match this one-time Discord attempt. Start a fresh sign-in.");
        return profile;
    }

    // ---- code -> token (public-client PKCE) ----
    QList<QPair<QString, QString>> fields {
        { QStringLiteral("client_id"), m_config.clientId },
        { QStringLiteral("grant_type"), QStringLiteral("authorization_code") },
        { QStringLiteral("code"), code },
        { QStringLiteral("redirect_uri"), m_config.redirectUri },
        { QStringLiteral("code_verifier"), m_handoff.verifier },
    };
    // A public-client exchange must not carry an empty secret.
    if (!m_config.clientSecret.isEmpty())
        fields.append({ QStringLiteral("client_secret"), m_config.clientSecret });

    const HttpAnswer token = httpPostForm(QString::fromLatin1(kTokenUrl), buildForm(fields).toUtf8());
    QJsonParseError parseError;
    const QJsonDocument tokenDoc = QJsonDocument::fromJson(token.body, &parseError);
    const QJsonObject tokenBody = tokenDoc.object();
    const QString accessToken = tokenBody.value(QStringLiteral("access_token")).toString();
    const QString refreshToken = tokenBody.value(QStringLiteral("refresh_token")).toString();
    if (token.status != 200 || accessToken.isEmpty()) {
        profile.message = mapDiscordTokenError(token.status, token.body, m_config.redirectUri,
                                               m_config.scope);
        return profile;
    }

    // ---- identity ----
    const HttpAnswer me = httpGet(QString::fromLatin1(kApiUrl) + QStringLiteral("/users/@me"),
                                  accessToken);
    const QJsonDocument meDoc = QJsonDocument::fromJson(me.body, &parseError);
    const QJsonObject user = meDoc.object();
    if (me.status != 200 || !user.contains(QStringLiteral("id"))) {
        profile.message = me.status == 0
                              ? (me.error.isEmpty()
                                     ? QStringLiteral("Discord did not return the account.")
                                     : me.error)
                              : QStringLiteral("Discord did not return the account (HTTP %1).")
                                    .arg(me.status);
        return profile;
    }

    const QString id = user.value(QStringLiteral("id")).toString();
    const QString username = user.value(QStringLiteral("username")).toString();
    const QString globalName = firstNonEmpty({
        user.value(QStringLiteral("global_name")).toString(),
        user.value(QStringLiteral("globalName")).toString(),
    });

    QVariantMap identity;
    identity.insert(QStringLiteral("Id"), id);
    identity.insert(QStringLiteral("Username"), username);
    identity.insert(QStringLiteral("GlobalName"), globalName);
    identity.insert(QStringLiteral("DisplayName"),
                    firstNonEmpty({ globalName, username, id }));
    identity.insert(QStringLiteral("Avatar"), user.value(QStringLiteral("avatar")).toString());
    identity.insert(QStringLiteral("AvatarUrl"), avatarUrlFor(identity));
    identity.insert(QStringLiteral("Provider"), QStringLiteral("discord"));
    identity.insert(QStringLiteral("LinkedAt"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    // A configured server role gate is authoritative and is checked for every
    // sign-in, even when this ID is also on the developer allowlist. The
    // allowlist is used only when no role gate is configured.
    const QString displayName = identity.value(QStringLiteral("DisplayName")).toString();
    bool granted = false;
    QString accessVia;
    QString accessTier;
    QString accessRole;

    if (m_config.roleGateConfigured()) {
        const GuildRoleAnswer role = checkGuildRole(accessToken);
        if (!role.ok) {
            profile.identity = identity;
            profile.message = role.message;
            return profile;
        }
        if (role.granted) {
            granted = true;
            accessVia = QStringLiteral("role");
            accessTier = role.tier;
            accessRole = role.roleId;
        } else {
            QString why;
            if (role.notMember) {
                why = QStringLiteral(
                          "that account is not a member of the required Discord server (guild "
                          "%1). Join the server%2, then press Continue with Discord again.")
                          .arg(m_config.guildId,
                               m_config.inviteUrl.isEmpty()
                                   ? QString()
                                   : QStringLiteral(": %1").arg(m_config.inviteUrl));
            } else {
                const QString requirement =
                    m_config.requiredTier.isEmpty()
                        ? QStringLiteral("one of the configured server roles")
                        : QStringLiteral("the \"%1\" role tier").arg(m_config.requiredTier);
                why = QStringLiteral(
                          "none of that account's current roles in the required Discord server "
                          "grant %1. Ask a server admin for the role.")
                          .arg(requirement);
            }
            profile.identity = identity;
            profile.message = QStringLiteral("Discord linked as %1 (ID %2), but %3")
                                  .arg(displayName, id, why);
            return profile;
        }
    } else if (allowlisted(id)) {
        granted = true;
        accessVia = QStringLiteral("allowlist");
    }

    identity.insert(QStringLiteral("AccessVia"), accessVia);
    identity.insert(QStringLiteral("AccessTier"), accessTier);
    identity.insert(QStringLiteral("AccessRole"), accessRole);

    if (!granted) {
        profile.identity = identity;
        profile.message = m_config.allowUsers.isEmpty()
                ? QStringLiteral(
                      "Discord linked as %1 (ID %2), but no access gate is configured. Add "
                      "that ID to discord.allowUsers, or configure discord.guildId + "
                      "discord.roles + discord.requiredTier - see docs/AUTH.md.")
                      .arg(displayName, id)
                : QStringLiteral(
                      "Discord linked as %1 (ID %2), but this account is not on the "
                      "allowlist.")
                      .arg(displayName, id);
        return profile;
    }

    profile.ok = true;
    profile.identity = identity;
    profile.refreshToken = refreshToken;
    return profile;
}

void AuthManager::finishHandoff(const DiscordProfile &profile)
{
    const bool wasActive = m_handoff.active;
    m_handoff = Handoff();
    // The hand-off is over either way - whether the code came back through the
    // loopback listener or was pasted by hand, the helper port goes down.
    stopLoopback();

    if (profile.ok) {
        m_user = profile.identity;
        emit userChanged();

        QString saveError;
        const bool identitySaved = m_store.saveIdentity(profile.identity, &saveError);
        QString credentialError;
        const bool credentialSaved = identitySaved && !profile.refreshToken.isEmpty()
                                     && m_store.saveRefreshToken(profile.refreshToken,
                                                                 &credentialError);
        QString successMessage = QStringLiteral("Discord linked. Current Discord access was verified.");
        if (!identitySaved) {
            successMessage = QStringLiteral("Signed in for this run, but the saved identity could not "
                                           "be written (%1).")
                                 .arg(saveError);
        } else {
            emit sessionChanged();
            if (!credentialSaved)
                successMessage = QStringLiteral("Signed in for this run. The Discord refresh token "
                                                "could not be saved securely, so sign in again next time.");
        }

        setBusy(false);
        setProgress(100, false);
        setNote(successMessage, identitySaved && credentialSaved
                                   ? QStringLiteral("good") : QString());
        setAuthorized(true);
        emit signInFinished(true, QStringLiteral("Discord linked."));
        return;
    }

    setBusy(false);
    setProgress(0, false);
    if (!profile.message.isEmpty())
        setNote(profile.message, QStringLiteral("bad"));
    else if (wasActive)
        setNote(QStringLiteral("The Discord sign-in did not finish. Press the Discord button to "
                               "try again."),
                QStringLiteral("bad"));
    emit signInFinished(false, profile.message);
}

void AuthManager::cancelDiscord()
{
    m_handoff = Handoff();
    stopLoopback();
    setBusy(false);
    setProgress(0, false);
    setNote(QStringLiteral("Discord sign-in cancelled."), QString());
}

bool AuthManager::recheckSavedSession(bool interactiveIfCredentialMissing)
{
    if (!m_config.configured) {
        setBusy(false);
        setProgress(0, false);
        setNote(m_config.setupHint, QStringLiteral("bad"));
        return false;
    }
    if (!m_store.hasSaved()) {
        setBusy(false);
        setProgress(0, false);
        return false;
    }

    setAuthorized(false);
    setBusy(true);
    setProgress(0, true);
    QString credentialError;
    const QString oldRefreshToken = m_store.refreshToken(&credentialError);
    if (oldRefreshToken.isEmpty()) {
        setBusy(false);
        setProgress(0, false);
        setNote(QStringLiteral("Your saved sign-in cannot be checked securely. Sign in with Discord "
                               "again to verify your current server role."),
                QStringLiteral("bad"));
        if (interactiveIfCredentialMissing)
            startDiscord();
        return false;
    }

    QList<QPair<QString, QString>> fields {
        { QStringLiteral("client_id"), m_config.clientId },
        { QStringLiteral("grant_type"), QStringLiteral("refresh_token") },
        { QStringLiteral("refresh_token"), oldRefreshToken },
    };
    if (!m_config.clientSecret.isEmpty())
        fields.append({ QStringLiteral("client_secret"), m_config.clientSecret });

    const HttpAnswer token = httpPostForm(QString::fromLatin1(kTokenUrl),
                                          buildForm(fields).toUtf8());
    QJsonParseError parseError;
    const QJsonObject tokenBody = QJsonDocument::fromJson(token.body, &parseError).object();
    const QString accessToken = tokenBody.value(QStringLiteral("access_token")).toString();
    const QString newRefreshToken = tokenBody.value(QStringLiteral("refresh_token")).toString();
    if (token.status != 200 || accessToken.isEmpty()) {
        if (token.status == 400
            && tokenBody.value(QStringLiteral("error")).toString()
                   == QLatin1String("invalid_grant"))
            m_store.clearRefreshToken();
        setBusy(false);
        setProgress(0, false);
        const QString detail = token.status == 0
                                   ? (token.error.isEmpty()
                                          ? QStringLiteral("Discord could not be reached.")
                                          : token.error)
                                   : mapDiscordTokenError(token.status, token.body,
                                                         m_config.redirectUri, m_config.scope);
        setNote(QStringLiteral("Current Discord access could not be verified; the tool remains "
                               "locked. %1").arg(detail), QStringLiteral("bad"));
        return false;
    }

    // Discord may rotate the refresh token. Persist the new one immediately so
    // a later profile/role API outage does not leave only an invalid old token.
    const QString tokenToSave = newRefreshToken.isEmpty() ? oldRefreshToken : newRefreshToken;
    QString tokenSaveError;
    bool refreshSaved = m_store.saveRefreshToken(tokenToSave, &tokenSaveError);
    if (!refreshSaved)
        m_store.clearRefreshToken();

    const HttpAnswer me = httpGet(QString::fromLatin1(kApiUrl) + QStringLiteral("/users/@me"),
                                  accessToken);
    const QJsonObject user = QJsonDocument::fromJson(me.body, &parseError).object();
    const QVariantMap savedIdentity = m_store.identity();
    const QString savedId = savedIdentity.value(QStringLiteral("Id")).toString();
    const QString currentId = user.value(QStringLiteral("id")).toString();
    if (me.status != 200 || currentId.isEmpty() || currentId != savedId) {
        if (me.status == 401 || (me.status == 200 && currentId != savedId)) {
            m_store.clear();
            emit sessionChanged();
        }
        setBusy(false);
        setProgress(0, false);
        setNote(me.status == 0
                    ? QStringLiteral("Discord could not be reached to verify this account; the "
                                     "tool remains locked.")
                    : QStringLiteral("Discord could not verify the saved account. Sign in again; "
                                     "the tool remains locked."),
                QStringLiteral("bad"));
        return false;
    }

    QVariantMap identity = savedIdentity;
    const QString username = user.value(QStringLiteral("username")).toString();
    const QString globalName = firstNonEmpty({
        user.value(QStringLiteral("global_name")).toString(),
        user.value(QStringLiteral("globalName")).toString(),
    });
    identity.insert(QStringLiteral("Username"), username);
    identity.insert(QStringLiteral("GlobalName"), globalName);
    identity.insert(QStringLiteral("DisplayName"), firstNonEmpty({ globalName, username, currentId }));
    identity.insert(QStringLiteral("Avatar"), user.value(QStringLiteral("avatar")).toString());
    identity.insert(QStringLiteral("AvatarUrl"), avatarUrlFor(identity));

    QString accessVia;
    QString accessTier;
    QString accessRole;
    bool granted = false;
    if (m_config.roleGateConfigured()) {
        // Server role is authoritative even if this ID also appears in the
        // local allowlist. Never trust the role ID cached in session.json.
        const GuildRoleAnswer role = checkGuildRole(accessToken);
        if (!role.ok) {
            setBusy(false);
            setProgress(0, false);
            setNote(QStringLiteral("Could not re-check your current Discord server role; the tool "
                                   "remains locked. %1").arg(role.message),
                    QStringLiteral("bad"));
            return false;
        }
        if (!role.granted) {
            m_store.clear();
            emit sessionChanged();
            setBusy(false);
            setProgress(0, false);
            setNote(role.notMember
                        ? QStringLiteral("This Discord account is no longer in the required server. "
                                         "The tool remains locked.")
                        : QStringLiteral("This Discord account no longer has the required server "
                                         "role. The tool remains locked."),
                    QStringLiteral("bad"));
            return false;
        }
        granted = true;
        accessVia = QStringLiteral("role");
        accessTier = role.tier;
        accessRole = role.roleId;
    } else if (allowlisted(currentId)) {
        granted = true;
        accessVia = QStringLiteral("allowlist");
    }

    if (!granted) {
        m_store.clear();
        emit sessionChanged();
        setBusy(false);
        setProgress(0, false);
        setNote(QStringLiteral("This Discord account no longer passes the configured access gate. "
                               "The tool remains locked."), QStringLiteral("bad"));
        return false;
    }

    identity.insert(QStringLiteral("AccessVia"), accessVia);
    identity.insert(QStringLiteral("AccessTier"), accessTier);
    identity.insert(QStringLiteral("AccessRole"), accessRole);
    QString saveError;
    const bool identitySaved = m_store.saveIdentity(identity, &saveError);
    if (!identitySaved) {
        m_store.clear();
        refreshSaved = false;
    }

    m_user = identity;
    emit userChanged();
    emit sessionChanged();
    setBusy(false);
    setProgress(100, false);
    setNote(refreshSaved
                ? QStringLiteral("Welcome back - your current Discord server access was verified.")
                : QStringLiteral("Discord verified this run, but secure session storage failed. "
                                 "Sign in again next time."),
            refreshSaved ? QStringLiteral("good") : QString());
    setAuthorized(true);
    emit signInFinished(true, QStringLiteral("Welcome back."));
    return true;
}

bool AuthManager::continueSaved()
{
    return recheckSavedSession(true);
}

void AuthManager::forgetSaved()
{
    m_store.clear();
    emit sessionChanged();
    setNote(QStringLiteral("Sign in with Discord to unlock BSI Customizer."), QString());
    setProgress(0, false);
}

void AuthManager::signOut()
{
    m_store.clear();
    emit sessionChanged();
    m_user.clear();
    emit userChanged();
    setAuthorized(false);
    setBusy(false);
    setProgress(0, false);
    setNote(QStringLiteral("Signed out. Sign in with Discord to continue."), QString());
}
