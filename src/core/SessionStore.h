#pragma once

#include <QByteArray>
#include <QString>
#include <QVariantMap>

/**
 * SessionStore — the local "remember me" side of the Discord sign-in gate.
 *
 * Ported 1:1 from Premium-Optimizer's LicenseStore session layer:
 *  - the session is an HMAC-signed envelope (base64url payload + signature),
 *    signed with a per-PC secret that is created on first run and never leaves
 *    the machine;
 *  - the envelope carries this PC's machine id, so editing or copying the file
 *    to another computer invalidates it;
 *  - a saved session is NEVER an automatic login (the same rule FalcoForge
 *    follows): it only earns the one-click "Welcome back - Continue as …"
 *    strip, and the continue re-checks the allowlist before the gate opens.
 *
 * The store has no network knowledge and no Qt event-loop needs: AuthManager
 * owns the flow, this class owns the bytes on disk.
 */
class SessionStore
{
public:
    static constexpr int kSessionVersion = 1;

    /** dataDir empty -> QStandardPaths::AppDataLocation (the app's own dir). */
    explicit SessionStore(const QString &dataDir = QString());

    QString sessionFile() const { return m_sessionFile; }
    QString dataDir() const { return m_dataDir; }

    /** Machine identity used to bind the session to this PC. */
    static QString machineId();

    /** True when a readable, correctly signed session for THIS PC exists. */
    bool hasSaved() const { return !m_identity.isEmpty(); }

    /** The stored Discord identity ({Id, Username, DisplayName, AvatarUrl, ...}). */
    QVariantMap identity() const { return m_identity; }
    /** ISO timestamp of the sign-in that wrote the session. */
    QString signedInAt() const { return m_signedInAt; }

    /** Write the identity into the signed session envelope. */
    bool saveIdentity(const QVariantMap &identity, QString *error = nullptr);
    /** Remove the session file and forget the in-memory copy. */
    void clear();
    /** Re-read the session file (returns true when a valid session is loaded). */
    bool reload();

    // ---- primitives, exposed for the selftest ----
    /** base64url(payload) "." base64url(HMAC-SHA256(secret|purpose, payload)). */
    QString issueToken(const QByteArray &payload, const QString &purpose) const;
    /** Verify a token issued by issueToken; returns the payload or empty. */
    QByteArray verifyToken(const QString &token, const QString &purpose) const;

private:
    QByteArray secret() const;
    QString signBlob(const QByteArray &canonicalJson) const;

    QString m_dataDir;
    QString m_sessionFile;
    QString m_secretFile;
    mutable QByteArray m_secret;
    QVariantMap m_identity;
    QString m_signedInAt;
};
