#include "SessionStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#  include <dpapi.h>
#endif

namespace {

const char kPurposePrefix[] = "bsi-customizer-session-v";
const char kSecretPurpose[] = "bsi-local-secret-v1";

QByteArray hmacSha256(const QByteArray &key, const QByteArray &message)
{
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

QByteArray base64Url(const QByteArray &data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray base64UrlDecode(const QByteArray &data)
{
    return QByteArray::fromBase64(data, QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

#ifdef Q_OS_WIN
const BYTE kRefreshTokenEntropy[] = "BSI-Customizer Discord refresh-token v1";

QByteArray protectForCurrentUser(const QByteArray &plain, QString *error)
{
    DATA_BLOB input { static_cast<DWORD>(plain.size()),
                      reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData())) };
    DATA_BLOB entropy { static_cast<DWORD>(sizeof(kRefreshTokenEntropy) - 1),
                        const_cast<BYTE *>(kRefreshTokenEntropy) };
    DATA_BLOB output {};
    if (!CryptProtectData(&input, L"BSI Customizer Discord refresh token", &entropy,
                          nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error)
            *error = QStringLiteral("Windows could not protect the Discord refresh token.");
        return {};
    }
    QByteArray encrypted(reinterpret_cast<const char *>(output.pbData),
                         static_cast<qsizetype>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return encrypted;
}

QByteArray unprotectForCurrentUser(const QByteArray &encrypted, QString *error)
{
    DATA_BLOB input { static_cast<DWORD>(encrypted.size()),
                      reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData())) };
    DATA_BLOB entropy { static_cast<DWORD>(sizeof(kRefreshTokenEntropy) - 1),
                        const_cast<BYTE *>(kRefreshTokenEntropy) };
    DATA_BLOB output {};
    if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error)
            *error = QStringLiteral("Windows could not unlock the saved Discord session. Sign in again.");
        return {};
    }
    QByteArray plain(reinterpret_cast<const char *>(output.pbData),
                     static_cast<qsizetype>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return plain;
}
#endif

} // namespace

SessionStore::SessionStore(const QString &dataDir)
{
    if (dataDir.isEmpty()) {
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    } else {
        m_dataDir = dataDir;
    }
    QDir().mkpath(m_dataDir);
    m_sessionFile = m_dataDir + QStringLiteral("/session.json");
    m_secretFile = m_dataDir + QStringLiteral("/secret.bin");
    m_refreshTokenFile = m_dataDir + QStringLiteral("/discord-refresh.dpapi");
    reload();
}

QString SessionStore::machineId()
{
    // Same idea as FalcoForge's machineId(): a stable, hard-to-spoof bundle of
    // this PC's traits. The session file carries it; a copy on another PC does
    // not verify and is dropped.
    const QStringList pieces {
        QSysInfo::machineHostName(),
        QString::fromLatin1(QSysInfo::machineUniqueId()),
        QSysInfo::currentCpuArchitecture(),
        QSysInfo::productType(),
        QString::number(QThread::idealThreadCount()),
    };
    return QString::fromLatin1(
        QCryptographicHash::hash(pieces.join(QLatin1Char('|')).toUtf8(), QCryptographicHash::Sha256)
            .toHex());
}

QByteArray SessionStore::secret() const
{
    if (!m_secret.isEmpty())
        return m_secret;

    QFile f(m_secretFile);
    if (f.open(QIODevice::ReadOnly)) {
        const QByteArray stored = f.readAll();
        f.close();
        if (stored.size() >= 16) {
            m_secret = stored;
            return m_secret;
        }
    }

    // First run: mint the local secret. It never leaves this PC and is used for
    // nothing except signing the session envelope.
    QByteArray fresh(32, Qt::Uninitialized);
    for (int i = 0; i < fresh.size(); ++i)
        fresh[i] = char(QRandomGenerator::global()->generate() & 0xff);
    fresh = hmacSha256(QByteArray(kSecretPurpose), fresh + machineId().toUtf8());

    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(fresh);
        f.close();
    }
    m_secret = fresh;
    return m_secret;
}

QString SessionStore::issueToken(const QByteArray &payload, const QString &purpose) const
{
    const QByteArray encoded = base64Url(payload);
    const QByteArray key = secret() + '|' + purpose.toUtf8();
    const QByteArray signature = base64Url(hmacSha256(key, encoded));
    return QString::fromLatin1(encoded) + QLatin1Char('.') + QString::fromLatin1(signature);
}

QByteArray SessionStore::verifyToken(const QString &token, const QString &purpose) const
{
    const QByteArray raw = token.toLatin1();
    const int dot = raw.indexOf('.');
    if (dot <= 0 || dot == raw.size() - 1)
        return {};
    const QByteArray encoded = raw.left(dot);
    const QByteArray signature = raw.mid(dot + 1);
    const QByteArray key = secret() + '|' + purpose.toUtf8();
    const QByteArray expected = base64Url(hmacSha256(key, encoded));
    if (expected != signature)
        return {};
    return base64UrlDecode(encoded);
}

QString SessionStore::signBlob(const QByteArray &canonicalJson) const
{
    return issueToken(canonicalJson,
                      QString::fromLatin1(kPurposePrefix) + QString::number(kSessionVersion));
}

bool SessionStore::saveIdentity(const QVariantMap &identity, QString *error)
{
    if (identity.isEmpty()) {
        if (error)
            *error = QStringLiteral("no identity to save");
        return false;
    }

    QJsonObject session;
    session.insert(QStringLiteral("version"), kSessionVersion);
    session.insert(QStringLiteral("machineId"), machineId());
    session.insert(QStringLiteral("signedInAt"),
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    session.insert(QStringLiteral("identity"), QJsonObject::fromVariantMap(identity));

    const QByteArray canonical = QJsonDocument(session).toJson(QJsonDocument::Compact);
    QJsonObject envelope = session;
    envelope.insert(QStringLiteral("_sig"), signBlob(canonical));

    QSaveFile out(m_sessionFile);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("could not open the session file for writing");
        return false;
    }
    out.write(QJsonDocument(envelope).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        if (error)
            *error = QStringLiteral("could not save the session");
        return false;
    }

    m_identity = identity;
    m_signedInAt = session.value(QStringLiteral("signedInAt")).toString();
    return true;
}

bool SessionStore::saveRefreshToken(const QString &token, QString *error)
{
    if (token.trimmed().isEmpty()) {
        if (error)
            *error = QStringLiteral("Discord did not issue a refresh token.");
        return false;
    }
#ifndef Q_OS_WIN
    Q_UNUSED(token);
    if (error)
        *error = QStringLiteral("Secure refresh-token storage is only available on Windows.");
    return false;
#else
    const QByteArray plain = token.toUtf8();
    const QByteArray encrypted = protectForCurrentUser(plain, error);
    if (encrypted.isEmpty())
        return false;
    QSaveFile out(m_refreshTokenFile);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Could not open secure Discord credential storage.");
        return false;
    }
    if (out.write(encrypted) != encrypted.size() || !out.commit()) {
        if (error)
            *error = QStringLiteral("Could not save the protected Discord refresh token.");
        return false;
    }
    QFile::setPermissions(m_refreshTokenFile,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
#endif
}

QString SessionStore::refreshToken(QString *error) const
{
#ifndef Q_OS_WIN
    if (error)
        *error = QStringLiteral("Secure refresh-token storage is only available on Windows.");
    return {};
#else
    QFile in(m_refreshTokenFile);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("There is no saved Discord refresh token.");
        return {};
    }
    const QByteArray encrypted = in.readAll();
    if (encrypted.isEmpty()) {
        if (error)
            *error = QStringLiteral("The saved Discord refresh token is empty.");
        return {};
    }
    const QByteArray plain = unprotectForCurrentUser(encrypted, error);
    if (plain.isEmpty())
        return {};
    return QString::fromUtf8(plain);
#endif
}

bool SessionStore::hasRefreshToken() const
{
    const QFileInfo info(m_refreshTokenFile);
    return info.exists() && info.isFile() && info.size() > 0;
}

void SessionStore::clearRefreshToken()
{
    QFile::remove(m_refreshTokenFile);
}

void SessionStore::clear()
{
    m_identity.clear();
    m_signedInAt.clear();
    QFile::remove(m_sessionFile);
    QFile::remove(m_refreshTokenFile);
}

bool SessionStore::reload()
{
    m_identity.clear();
    m_signedInAt.clear();

    QFile f(m_sessionFile);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = f.readAll();
    f.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        clear();
        return false;
    }
    const QJsonObject envelope = doc.object();

    // Verify the signature over the canonical session body (everything except
    // the signature itself).
    QJsonObject session = envelope;
    const QString sig = session.take(QStringLiteral("_sig")).toString();

    QByteArray canonical;
    {
        // Re-serialize exactly what was signed: the envelope minus _sig.
        canonical = QJsonDocument(session).toJson(QJsonDocument::Compact);
    }
    const QByteArray payload = verifyToken(sig,
        QString::fromLatin1(kPurposePrefix) + QString::number(kSessionVersion));
    if (payload.isEmpty() || payload != canonical) {
        clear();
        return false;
    }

    if (session.value(QStringLiteral("version")).toInt() != kSessionVersion) {
        clear();
        return false;
    }
    if (session.value(QStringLiteral("machineId")).toString() != machineId()) {
        // Copied to another PC: drop it silently.
        clear();
        return false;
    }

    m_identity = session.value(QStringLiteral("identity")).toObject().toVariantMap();
    m_signedInAt = session.value(QStringLiteral("signedInAt")).toString();
    if (m_identity.isEmpty()) {
        clear();
        return false;
    }
    return true;
}
