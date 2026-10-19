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
#include <QUrlQuery>
#include <QVariantMap>

namespace {

const char kAuthorizeUrl[] = "https://discord.com/oauth2/authorize";
const char kTokenUrl[] = "https://discord.com/api/oauth2/token";
const char kApiUrl[] = "https://discord.com/api";
const char kRedirectPath[] = "/auth/discord/callback";
const int kDefaultRedirectPort = 43712;
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

} // namespace

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

AuthManager::Config AuthManager::loadConfig()
{
    Config config;
    config.redirectPort = kDefaultRedirectPort;

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    // auth.json lookup: $BSIC_AUTH_FILE, then next to the EXE, then app data.
    QStringList candidates;
    const QString envFile = env.value(QStringLiteral("BSIC_AUTH_FILE")).trimmed();
    if (!envFile.isEmpty())
        candidates << envFile;
    candidates << QCoreApplication::applicationDirPath() + QStringLiteral("/auth.json");
    candidates << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + QStringLiteral("/auth.json");

    QJsonObject discord;
    for (const QString &path : candidates) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
        f.close();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            continue;
        const QJsonObject root = doc.object();
        discord = root.value(QStringLiteral("discord")).toObject();
        config.sourceFile = path;
        break;
    }

    config.clientId = envOr(env, "BSIC_DISCORD_CLIENT_ID", discord.value("clientId").toString()).trimmed();
    config.clientSecret = envOr(env, "BSIC_DISCORD_CLIENT_SECRET",
                                discord.value("clientSecret").toString()).trimmed();
    config.scope = envOr(env, "BSIC_DISCORD_SCOPE",
                         discord.value("scope").toString(QStringLiteral("identify")))
                       .trimmed();
    if (config.scope.isEmpty())
        config.scope = QStringLiteral("identify");
    config.inviteUrl = envOr(env, "BSIC_DISCORD_INVITE", discord.value("inviteUrl").toString()).trimmed();

    const QString portEnv = env.value(QStringLiteral("BSIC_DISCORD_REDIRECT_PORT")).trimmed();
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
    const QString allowEnv = env.value(QStringLiteral("BSIC_DISCORD_ALLOW_USERS")).trimmed();
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

    config.configured = !config.clientId.isEmpty();
    if (!config.configured) {
        config.setupHint = QStringLiteral(
            "Discord sign-in is not configured yet. Set BSIC_DISCORD_CLIENT_ID, or copy "
            "auth.example.json to auth.json next to the EXE and fill in discord.clientId - "
            "see docs/AUTH.md.");
    } else if (config.allowUsers.isEmpty()) {
        config.setupHint = QStringLiteral(
            "No dev allowlist is configured (discord.allowUsers). Add your Discord user ID to "
            "auth.json or set BSIC_DISCORD_ALLOW_USERS - see docs/AUTH.md.");
    } else {
        config.setupHint = QStringLiteral("Sign-in config: %1")
                               .arg(config.sourceFile.isEmpty() ? QStringLiteral("environment")
                                                                : config.sourceFile);
    }
    return config;
}

// ---------------------------------------------------------------------------
// Construction / state
// ---------------------------------------------------------------------------

AuthManager::AuthManager(QObject *parent)
    : QObject(parent)
    , m_config(loadConfig())
{
    // A saved session is never an automatic login - it only earns the
    // one-click "Welcome back" strip (same rule as FalcoForge).
    if (m_store.hasSaved())
        emit sessionChanged();

    if (!m_config.configured) {
        setNote(m_config.setupHint, QStringLiteral("bad"));
    } else if (m_store.hasSaved()) {
        setNote(QStringLiteral("We found your previous sign-in on this PC - continue, or sign "
                               "in with Discord."),
                QString());
    } else {
        setNote(QStringLiteral("Dev access is verified on this PC before the tool opens - "
                               "Discord sign-in only."),
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
        return QStringLiteral("Discord rejected the application credentials. Check that the client "
                              "id is the numeric Application ID from the Discord Developer Portal "
                              "and that the client secret (if set) is the current one.");
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
        return QStringLiteral("Discord refused the requested scopes (%1). Make sure the "
                              "application is allowed to request them.").arg(scope);
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
                "auth.example.json to auth.json next to the EXE and fill in discord.clientId. "
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
    if (!state.isEmpty() && !m_handoff.state.isEmpty() && state != m_handoff.state) {
        profile.message = QStringLiteral(
            "The one-time sign-in code and its PKCE proof no longer match. Press the Discord "
            "button once more to start a fresh attempt.");
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

    // ---- the dev allowlist: the only plan this tool has ----
    if (!allowlisted(id)) {
        profile.identity = identity;
        profile.message =
            m_config.allowUsers.isEmpty()
                ? QStringLiteral(
                      "Discord linked as %1 (ID %2), but no dev allowlist is configured "
                      "(discord.allowUsers). Add that ID to auth.json or set "
                      "BSIC_DISCORD_ALLOW_USERS - see docs/AUTH.md.")
                      .arg(identity.value(QStringLiteral("DisplayName")).toString(), id)
                : QStringLiteral(
                      "Discord linked as %1 (ID %2), but this account is not on the dev "
                      "allowlist. Add that ID to discord.allowUsers in auth.json to grant "
                      "access.")
                      .arg(identity.value(QStringLiteral("DisplayName")).toString(), id);
        return profile;
    }

    profile.ok = true;
    profile.identity = identity;
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
        if (!m_store.saveIdentity(profile.identity, &saveError))
            setNote(QStringLiteral("Signed in, but the session could not be saved (%1).")
                        .arg(saveError),
                    QString());
        else
            emit sessionChanged();

        setBusy(false);
        setProgress(100, false);
        setNote(QStringLiteral("Discord linked."), QStringLiteral("good"));
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

bool AuthManager::continueSaved()
{
    if (!m_store.hasSaved())
        return false;

    const QVariantMap identity = m_store.identity();
    const QString id = identity.value(QStringLiteral("Id")).toString();
    if (!allowlisted(id)) {
        m_store.clear();
        emit sessionChanged();
        setNote(QStringLiteral("That account is no longer on the dev allowlist. Sign in with "
                               "Discord again."),
                QStringLiteral("bad"));
        emit signInFinished(false, QStringLiteral("Not on the dev allowlist."));
        return false;
    }

    m_user = identity;
    emit userChanged();
    setBusy(false);
    setProgress(100, false);
    setNote(QStringLiteral("Welcome back - access re-checked on this PC."), QStringLiteral("good"));
    setAuthorized(true);
    emit signInFinished(true, QStringLiteral("Welcome back."));
    return true;
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
