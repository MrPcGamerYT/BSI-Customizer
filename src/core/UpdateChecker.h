#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

/**
 * UpdateChecker — self-update delivered through a Discord bot.
 *
 * GitHub's own update flow cannot be used here: the repository is private, so
 * a released EXE has nowhere public to ask "is there a newer build?" and a
 * private-repo token cannot be shipped. Discord solves it in one step:
 *
 *   publisher (updater-bot/publish.mjs, a webhook or bot token) uploads the
 *   built EXE plus a small manifest.json to one channel of the server;
 *
 *   this class (read-only bot token, View Channel + Read Message History on
 *   that one channel) reads the newest message that carries a manifest.json
 *   attachment, compares its `version` with BSI_VERSION, and on approval
 *   streams the EXE next to it into %TEMP%, verifies size + SHA-256 + PE
 *   header, then swaps the running EXE through a hidden PowerShell helper
 *   after the app has closed and starts it again.
 *
 * Security model (see docs/UPDATER.md):
 *  - the shipped token is read-only and scoped to one channel; the publisher
 *    uses a separate webhook/edit credential that never ships;
 *  - the manifest must be an https Discord CDN attachment, schema 1;
 *  - the download is verified against `size` and `sha256` from the manifest;
 *  - when `updater.hmacKey` is configured the manifest must also carry a valid
 *    HMAC-SHA256 `signature` over "bsi-update-v1\nversion\nfile\nsize\nsha256";
 *  - the file must be a 64-bit Windows executable before anything is replaced;
 *  - nothing is written outside %TEMP% until `apply()`, which only runs on the
 *    user's explicit "Install and restart".
 *
 * Configuration (same sources as AuthManager, so one auth.json carries both):
 *   auth.json -> "updater": { "apiBase", "channelId", "botToken",
 *                             "manifest", "hmacKey", "enabled" }
 *   $ZLP_AUTH_JSON / $BSIC_AUTH_JSON blob, $BSIC_AUTH_FILE, the file next to
 *   the EXE, app data, then the blob embedded at build time.
 *   Per-field environment overrides: BSIC_UPDATE_TOKEN, BSIC_UPDATE_CHANNEL_ID,
 *   BSIC_UPDATE_API, BSIC_UPDATE_MANIFEST, BSIC_UPDATE_HMAC_KEY.
 *   BSI_NO_UPDATER=1 disables the whole feature (CI and deployment tests).
 */
class UpdateChecker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(bool configured READ isConfigured NOTIFY configChanged)
    Q_PROPERTY(QString setupHint READ setupHint NOTIFY configChanged)
    Q_PROPERTY(QString sourceFile READ sourceFile NOTIFY configChanged)
    Q_PROPERTY(QString channelId READ channelId NOTIFY configChanged)

    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString note READ note NOTIFY stateChanged)
    Q_PROPERTY(QString noteKind READ noteKind NOTIFY stateChanged)
    Q_PROPERTY(bool checking READ checking NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)

    Q_PROPERTY(bool available READ available NOTIFY releaseChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY releaseChanged)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY releaseChanged)
    Q_PROPERTY(QString releasedAt READ releasedAt NOTIFY releaseChanged)
    Q_PROPERTY(QString downloadSize READ downloadSize NOTIFY releaseChanged)
    Q_PROPERTY(bool mandatory READ mandatory NOTIFY releaseChanged)
    Q_PROPERTY(bool skipped READ skipped NOTIFY releaseChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY releaseChanged)
    /** How the announced build is trusted: "signed", "pinned" or "first-use". */
    Q_PROPERTY(QString trust READ trust NOTIFY releaseChanged)

    Q_PROPERTY(QString downloadHost READ downloadHost NOTIFY stateChanged)
    Q_PROPERTY(bool readyToInstall READ readyToInstall NOTIFY stateChanged)
    Q_PROPERTY(QString stagedFile READ stagedFile NOTIFY stateChanged)
    Q_PROPERTY(bool installedPending READ installedPending NOTIFY stateChanged)

public:
    /** One parsed "updater" config object, already merged with the environment. */
    struct Config
    {
        bool enabled = true;
        bool configured = false;
        QString apiBase = QStringLiteral("https://discord.com/api/v10");
        QString channelId;
        QString token;
        QString manifestName = QStringLiteral("manifest.json");
        QString hmacKey;
        QString sourceFile;
        QString setupHint;
        int timeoutSeconds = 25;
        qint64 maxBytes = 200ll * 1024ll * 1024ll;
    };

    /** What `findManifestMessage()` located in a channel listing. */
    struct ManifestRef
    {
        bool found = false;
        QString messageId;
        QString manifestUrl;
        QJsonObject message;
        QString error;
    };

    /**
     * One release found in the channel, in the shape the rest of the class
     * works with. Two ways of announcing a build are accepted, and this is the
     * type both of them end up as:
     *  - a `manifest.json` attachment (publish.mjs / CI) - strict, signed,
     *    hash-pinned, fetched separately;
     *  - an ordinary message that names a version and an https link, e.g.
     *
     *        BSI Customizer v2.0.1
     *        https://u.pcloud.link/publink/show?code=XXXX
     *
     *        Fixed the About editor.
     *
     *    Nothing to run and nothing to install: the app reads it and downloads
     *    from the link. A `sha256: <64 hex>` line in the same message pins it;
     *    without one the first download is pinned on this PC (see trust()).
     */
    struct ReleasePick
    {
        bool found = false;
        bool fromManifest = false; // manifest.json attachment (needs a fetch)
        QString manifestUrl;
        QJsonObject manifest;      // normalized release (text messages)
        QJsonObject message;
        QString error;
    };

    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    QString currentVersion() const;
    bool isConfigured() const { return m_config.configured && m_config.enabled; }
    QString setupHint() const { return m_config.setupHint; }
    QString sourceFile() const { return m_config.sourceFile; }
    QString channelId() const { return m_config.channelId; }

    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool enabled);

    QString state() const { return m_state; }
    QString note() const { return m_note; }
    QString noteKind() const { return m_noteKind; }
    bool checking() const { return m_state == QLatin1String("checking"); }
    bool busy() const;
    qreal progress() const { return m_progress; }

    bool available() const { return m_available; }
    QString latestVersion() const { return m_latest.value(QStringLiteral("version")).toString(); }
    QString releaseNotes() const { return m_latest.value(QStringLiteral("notes")).toString(); }
    QString releasedAt() const { return m_latest.value(QStringLiteral("released")).toString(); }
    QString downloadSize() const;
    QString fileName() const { return m_latest.value(QStringLiteral("file")).toString(); }
    bool mandatory() const { return m_latest.value(QStringLiteral("mandatory")).toBool(false); }
    bool skipped() const;

    /** Host the EXE is actually coming from (cdn.discordapp.com, p-def2.pcloud.com, ...). */
    QString downloadHost() const;
    /** The exact URL being downloaded (after any landing-page resolution). */
    QString downloadUrl() const { return m_downloadUrl; }

    QString trust() const;

    bool readyToInstall() const { return !m_stagedFile.isEmpty(); }
    QString stagedFile() const { return m_stagedFile; }
    /** True once `apply()` handed the swap to the helper: next start is the new build. */
    bool installedPending() const { return m_installed; }

    // ---- commands (QML + CLI) -------------------------------------------
    /**
     * True when the verified download is a setup program rather than another
     * copy of this EXE. A released build is shipped as an installer, so the
     * update is "run it", not "overwrite the running file": replacing this EXE
     * with a setup would leave a program that asks to install on every start.
     */
    bool stagedIsInstaller() const;
    /** Ask the releases channel for the newest manifest (no-op while busy). */
    Q_INVOKABLE void check();
    /** Stream the announced EXE into %TEMP% and verify it. */
    Q_INVOKABLE void download();
    /** Download if needed, then hand the swap to the helper and quit. */
    Q_INVOKABLE void installNow();
    /** Swap the staged EXE in and restart; emits restartRequested(). */
    Q_INVOKABLE void apply();
    /** "Later": hide this version until a newer one appears. */
    Q_INVOKABLE void skip();
    Q_INVOKABLE void unskip();
    Q_INVOKABLE void cancel();
    /** Open the download URL in the browser (manual fallback). */
    Q_INVOKABLE void openDownloadPage();
    /** Reveal the verified EXE in Explorer for a manual copy. */
    Q_INVOKABLE void showStagedFile();
    /** Path of the helper's log, so a failed swap can be explained. */
    Q_INVOKABLE QString helperLogPath() const;
    /**
     * The reason the restart helper gave when its last run failed, or an empty
     * string. The helper starts the old build again after a failure, so this is
     * what the app shows on that next start - once per log.
     */
    Q_INVOKABLE QString helperFailure() const;
    /** Open that log in the system's text editor. */
    Q_INVOKABLE void showHelperLog();
    /** What the last check received, one line per message (for --explain). */
    QString channelReport() const;

    // ---- pure helpers (shared with the self-test, no network) -----------
    /** "v2.0.1" -> "2.0.1"; keeps pre-release text, trims everything else. */
    static QString normalizedVersion(const QString &version);
    /** -1 / 0 / 1. Numeric parts compare as numbers, a pre-release sorts first. */
    static int compareVersions(const QString &a, const QString &b);
    /**
     * True when the file is a setup program, judged by its own bytes (Inno
     * Setup, NSIS, a 7-Zip SFX, a WiX bundle) rather than by its name. The
     * publisher picks the name, and a setup that is not recognized gets copied
     * over the running app, which leaves a program that only installs.
     */
    static bool looksLikeInstaller(const QString &path);
    /** True when an installer's uninstaller sits next to `exePath`. */
    static bool isInstalledCopy(const QString &exePath);
    /** Validate a schema-1 manifest; returns it (or an empty object + error). */
    static QJsonObject parseManifest(const QByteArray &json, QString *error);
    /** Newest message carrying an attachment called `manifestName`. */
    static ManifestRef findManifestMessage(const QJsonArray &messages, const QString &manifestName);
    /**
     * The best release in a message list: a manifest.json attachment when the
     * channel has one, otherwise the message that names the highest version
     * together with an https link (see ReleasePick).
     */
    static ReleasePick findRelease(const QJsonArray &messages, const QString &manifestName,
                                   bool allowTextReleases);
    /**
     * Everything a message says: its text, then the title/description/fields
     * of every embed on it. Discord hides `content` - and, with it, embeds and
     * attachments - on messages a bot did not write unless the app holds the
     * Message Content intent, but a message the bot *does* get in full can
     * carry the announcement in either place (a hand-written message, a link
     * preview, a webhook embed), so both are read the same way.
     */
    static QString messageText(const QJsonObject &message);
    /**
     * Turn one ordinary message into a release object (version/file/url/notes,
     * plus size/sha256 when the message carries them). Returns false and sets
     * `error` when the message is not an announcement.
     */
    static bool parseMessageRelease(const QJsonObject &message, QJsonObject *manifest,
                                    QString *error);
    /**
     * Why a channel produced no release, in words the user can act on: the
     * per-message summary (what the bot actually received) plus the fix for
     * the one failure Discord causes silently - an app without the Message
     * Content intent gets empty `content`, `embeds` and `attachments` on every
     * message it did not send itself.
     */
    static QString explainChannel(const QJsonArray &messages, const QString &manifestName,
                                  bool allowTextReleases, const QString &applicationId = QString());
    /**
     * The application ID of a bot token. A Discord token starts with the
     * base64url form of the bot's user ID, which is the same number as the
     * application ID - enough to link straight to the page that carries the
     * Message Content toggle. Empty when the token is not shaped like one.
     */
    static QString applicationIdFromToken(const QString &botToken);
    /** Attachment URL for `fileName` inside a message found by the above. */
    static QString attachmentUrl(const QJsonObject &message, const QString &fileName);
    /** The exact byte string the publisher signs. */
    static QByteArray signedManifestText(const QJsonObject &manifest);
    static QString manifestSignature(const QJsonObject &manifest, const QString &key);
    static bool verifyManifestSignature(const QJsonObject &manifest, const QString &key,
                                        QString *error);
    /** True when a download turned out to be a web page, not a file. */
    static bool looksLikeHtml(const QByteArray &bytes);
    /**
     * A "share" link (pCloud's u.pcloud.link/publink/show?code=..., and other
     * file hosts that serve a landing page) is not the file. The page embeds
     * the real download URL; this finds it. Empty when there is none, and then
     * `detail` says what was wrong.
     */
    static QString resolveDownloadLink(const QByteArray &page, const QUrl &baseUrl,
                                       QString *detail = nullptr);
    /** SHA-256 of a whole file, lower-case hex ("" on failure). */
    static QString fileSha256(const QString &path);
    /** Hand the staged installer to Windows and quit; emits restartRequested(). */
    void runInstaller();
    /**
     * What a Windows program's own header says about it.
     *
     * A program and the installer that carries it need not be built for the
     * same processor: Inno Setup's setup stub is **always** 32-bit x86, even
     * when it installs a 64-bit application. Judging a download against this
     * program's own architecture therefore rejected every setup this project
     * publishes - the file arrived, then was deleted with "it is not a 64-bit
     * x86 Windows program". The two questions are now asked separately: can
     * Windows run this at all, and may it replace this program.
     */
    struct PeInfo
    {
        quint16 machine = 0;   // IMAGE_FILE_MACHINE_*
        bool pe32Plus = false; // PE32+ optional header, not PE32
        quint16 subsystem = 0; // 2 = GUI, 3 = console
        bool isDll = false;
        qint64 size = 0;
        /** "x86", "x64", "ARM64", or the raw machine number. */
        QString machineName() const;
        /** A program this PC can start: x64 natively, x86 through WoW64. */
        bool runnableHere() const;
        /** Built for the same processor as the running application. */
        bool sameArchitectureAsApp() const;
    };
    /** Read and validate the PE header. False + `error` when it is not a program. */
    static bool readPeInfo(const QString &path, PeInfo *info, QString *error);
    /** Size + hash + "Windows can run the thing that was downloaded". */
    static bool verifyStagedFile(const QString &path, qint64 expectedSize,
                                 const QString &expectedSha256, QString *error);
    /** MZ / PE\0\0 / a processor this PC can run / not a DLL. */
    static bool looksLikeExecutable(const QString &path, QString *error);
    /**
     * True when the download may be copied over this program: the same
     * processor architecture, not a library, not a setup. An installed copy is
     * never replaced this way - it is updated by its setup.
     */
    static bool canReplaceThisProgram(const QString &path, QString *error);
    /**
     * Remove the Mark of the Web a download carries (the Zone.Identifier
     * stream). A file downloaded from the internet and then run without a
     * browser's say-so is exactly what SmartScreen and application-control
     * policies block, which is how an update that downloaded correctly can
     * still refuse to start.
     */
    static void clearMarkOfTheWeb(const QString &path);
    /** Turn an HTTP status into one actionable sentence. */
    static QString describeHttpError(int status, const QByteArray &body);
    static QString readableSize(qint64 bytes);

    /** Config loader, public so the CLI banner can explain a missing install. */
    static Config loadConfig();

signals:
    void configChanged();
    void autoCheckChanged();
    void stateChanged();
    void progressChanged();
    void releaseChanged();
    /** The helper is running: the application must close now. */
    void restartRequested();

private:
    void setState(const QString &state, const QString &note, const QString &kind = QString());
    void setProgress(qreal value);
    void startAutoCheck();
    void fail(const QString &note, const QString &detail = QString());
    void handleManifest(const QByteArray &bytes);
    void handleRelease(const QJsonObject &manifest, bool fromManifest);
    QString pinnedHash(const QString &version) const;
    void storePinnedHash(const QString &version, const QString &hash) const;
    void beginDownload(const QString &urlText = QString());
    void finishDownload();
    void writeHelper(const QString &sibling, const QString &target);
    QString stagingDirectory() const;
    void networkError(const QNetworkReply *reply, const QByteArray &body, const char *what);

    Config m_config;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_sink = nullptr;

    QString m_state = QStringLiteral("idle");
    QString m_note;
    QString m_noteKind;
    qreal m_progress = 0;

    bool m_available = false;
    QJsonObject m_latest;
    QJsonObject m_pendingMessage; // message the manifest came from (attachment URLs)

    QString m_stagedFile;
    QString m_downloadUrl;
    int m_hops = 0;
    bool m_installed = false;
    bool m_pinnedRelease = false;
    bool m_installAfterDownload = false;
    bool m_autoCheck = true;
    QString m_skippedVersion;
    QString m_lastError;
    QJsonArray m_lastMessages;   // exactly what the last check fetched
    bool m_lastAllowText = true;
    class QTimer *m_recheck = nullptr;  // periodic look at the channel
};
