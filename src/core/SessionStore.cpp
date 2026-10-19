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

void SessionStore::clear()
{
    m_identity.clear();
    m_signedInAt.clear();
    QFile::remove(m_sessionFile);
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
