#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include "SessionStore.h"

/**
 * AuthManager — the Discord-only sign-in gate, ported from Premium-Optimizer's
 * (FalcoForge) AuthClient + LicenseStore login system.
 *
 * Same architecture, trimmed to one way in:
 *  - the login card is UX only; THIS class is the authority (Backend refuses
 *    its actions until authorized, exactly like Core::needsPaidAccess);
 *  - Discord OAuth2 public-client PKCE: code_verifier stays on this PC, the
 *    authorize URL carries the S256 challenge;
 *  - a loopback listener on 127.0.0.1:<redirectPort> catches the browser
 *    redirect (path /auth/discord/callback) and a branded result page is shown;
 *    when the port cannot be opened the card falls back to paste-the-link;
 *  - access = the Discord user id is on the dev allowlist (discord.allowUsers);
 *    this tool is private dev software, so there is no plan/tier layer;
 *  - a successful sign-in writes an HMAC-signed, machine-bound session which
 *    only earns a one-click "Welcome back" continue on the next launch - a
 *    saved session is never an automatic login.
 *
 * Configuration (see docs/AUTH.md and auth.example.json):
 *   auth.json next to the EXE (or $BSIC_AUTH_FILE), with env overrides
 *   BSIC_DISCORD_CLIENT_ID / BSIC_DISCORD_CLIENT_SECRET /
 *   BSIC_DISCORD_ALLOW_USERS / BSIC_DISCORD_REDIRECT_PORT / BSIC_DISCORD_SCOPE.
 *   The whole auth.json can also be supplied as one JSON blob - $ZLP_AUTH_JSON
 *   (alias $BSIC_AUTH_JSON) at runtime, and the ZLP_AUTH_JSON repository secret
 *   baked into the EXE at build time so releases need no sidecar file.
 *
 * The redirect URI must be registered in the Discord developer portal exactly:
 *   http://127.0.0.1:43713/auth/discord/callback
 * (127.0.0.1 on purpose - not localhost, which may resolve to ::1.)
 */
class AuthManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool authorized READ isAuthorized NOTIFY authorizedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString note READ note NOTIFY noteChanged)
    Q_PROPERTY(QString noteKind READ noteKind NOTIFY noteChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool progressIndeterminate READ progressIndeterminate NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap user READ user NOTIFY userChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    Q_PROPERTY(QString setupHint READ setupHint NOTIFY configChanged)
    Q_PROPERTY(QString sourceFile READ sourceFile NOTIFY configChanged)
    Q_PROPERTY(bool hasSavedSession READ hasSavedSession NOTIFY sessionChanged)
    Q_PROPERTY(QVariantMap savedUser READ savedUser NOTIFY sessionChanged)
    Q_PROPERTY(QString savedMeta READ savedMeta NOTIFY sessionChanged)
    Q_PROPERTY(QString redirectUri READ redirectUri NOTIFY configChanged)

public:
    struct Config {
        bool configured = false;
        QString clientId;
        QString clientSecret; // optional - public-client PKCE needs no secret
        QString scope = QStringLiteral("identify");
        QString redirectUri;
        int redirectPort = 43713;
        QStringList allowUsers;
        QString inviteUrl;
        QString sourceFile;
        QString setupHint;
    };

    struct DiscordProfile {
        bool ok = false;
        QVariantMap identity;
        QString message;
    };

    explicit AuthManager(QObject *parent = nullptr);

    /** Load auth.json + env overrides. Exposed like FalcoForge's loadAuthConfig. */
    static Config loadConfig();

    /**
     * The auth.json blob baked into this build from the ZLP_AUTH_JSON secret
     * (empty when the build carried none). Lowest-priority config source: a
     * file next to the EXE or the ZLP_AUTH_JSON env blob still overrides it.
     */
    static QByteArray embeddedAuthJson();

    bool isAuthorized() const { return m_authorized; }
    bool busy() const { return m_busy; }
    QString note() const { return m_note; }
    QString noteKind() const { return m_noteKind; }
    qreal progress() const { return m_progress; }
    bool progressIndeterminate() const { return m_progressIndeterminate; }
    QVariantMap user() const { return m_user; }
    bool configured() const { return m_config.configured; }
    QString setupHint() const { return m_config.setupHint; }
    QString sourceFile() const { return m_config.sourceFile; }
    QString redirectUri() const { return m_config.redirectUri; }

    bool hasSavedSession() const;
    QVariantMap savedUser() const;
    QString savedMeta() const;

    // ---- gate actions (the ONLY things the locked UI may do) ----
    /** Start the Discord PKCE flow. Returns the DiscordBegin map:
        {ok, Url, Listener, Redirect, State, Port, Manual, Reason, LocalIssue,
        message}. */
    Q_INVOKABLE QVariantMap startDiscord();
    /** Paste-the-link fallback: finish with a pasted redirect URL or code. */
    Q_INVOKABLE QVariantMap completeManual(const QString &urlOrCode);
    Q_INVOKABLE void cancelDiscord();
    /** One-click continue over the saved session (re-checks the allowlist). */
    Q_INVOKABLE bool continueSaved();
    /** "Use a different account": drop the saved session, stay on the card. */
    Q_INVOKABLE void forgetSaved();
    Q_INVOKABLE void signOut();

    // PKCE + code parsing primitives (same construction as FalcoForge's
    // AuthClient; public so the selftest can pin the S256 math).
    static QString pkceVerifier();
    static QString pkceChallenge(const QString &verifier);
    static QString randomHex(int bytes);
    QString authorizeUrl(const QString &state, const QString &challenge) const;
    static void readCode(const QString &input, QString &code, QString &state);

signals:
    void authorizedChanged();
    void busyChanged();
    void noteChanged();
    void progressChanged();
    void userChanged();
    void configChanged();
    void sessionChanged();
    void signInFinished(bool ok, const QString &message);

private:
    struct Handoff {
        bool active = false;
        QString state;
        QString verifier;
        QString redirect;
        qint64 deadlineMs = 0;
    };

    struct HttpAnswer {
        int status = 0;
        QByteArray body;
        QString error;
    };

    bool startLoopback(QString *error);
    void stopLoopback();
    QString handleCallback(const QString &path, const QString &query);

    HttpAnswer httpGet(const QString &url, const QString &bearer = QString());
    HttpAnswer httpPostForm(const QString &url, const QByteArray &form);

    /** Full exchange: code -> token -> /users/@me -> allowlist check. */
    DiscordProfile completeDiscord(const QString &urlOrCode);
    void finishHandoff(const DiscordProfile &profile);

    void setNote(const QString &text, const QString &kind);
    void setProgress(qreal value, bool indeterminate);
    void setBusy(bool busy);
    void setAuthorized(bool on);

    QString callbackPage(bool success, const QString &title, const QString &detail) const;
    static QString mapDiscordTokenError(int status, const QByteArray &body,
                                        const QString &redirectUri, const QString &scope);
    static QString avatarUrlFor(const QVariantMap &user);
    bool allowlisted(const QString &id) const;

    Config m_config;
    SessionStore m_store;
    QNetworkAccessManager m_nam;
    QTcpServer *m_loopback = nullptr;
    Handoff m_handoff;

    bool m_authorized = false;
    bool m_busy = false;
    QString m_note;
    QString m_noteKind;
    qreal m_progress = 0.0;
    bool m_progressIndeterminate = false;
    QVariantMap m_user;
};
