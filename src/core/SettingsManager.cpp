#include "SettingsManager.h"
#include "FpsPatcher.h"
#include "BsDetector.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QProcessEnvironment>
#include <QUrl>
#include <QVariantMap>

namespace {

QVariantList defaultAboutLinks()
{
    return {
        QVariantMap { { QStringLiteral("kind"), QStringLiteral("github") },
                      { QStringLiteral("title"), QStringLiteral("Open-source project") },
                      { QStringLiteral("url"), QStringLiteral("https://github.com/MrPcGamerYT/BSI-Customizer") } },
        QVariantMap { { QStringLiteral("kind"), QStringLiteral("discord") },
                      { QStringLiteral("title"), QStringLiteral("Community Discord") },
                      { QStringLiteral("url"), QStringLiteral("https://discord.gg/5qkKPRZkWa") } },
        QVariantMap { { QStringLiteral("kind"), QStringLiteral("youtube") },
                      { QStringLiteral("title"), QStringLiteral("Mr.Pc Gamer on YouTube") },
                      { QStringLiteral("url"), QStringLiteral("https://www.youtube.com/@MR.PC_GAMER_YT?sub_confirmation=1") } },
        QVariantMap { { QStringLiteral("kind"), QStringLiteral("youtube") },
                      { QStringLiteral("title"), QStringLiteral("Romulos on YouTube") },
                      { QStringLiteral("url"), QStringLiteral("https://www.youtube.com/@Romulus_FF?sub_confirmation=1") } }
    };
}

// About buttons are free-form: up to eight custom buttons, each with any
// label and HTTP(S) URL. A row may stay blank (no label and no URL) to keep a
// slot reserved; anything malformed is dropped instead of resetting the list.
QVariantList sanitizeAboutLinks(const QVariantList &links)
{
    QVariantList clean;
    for (const QVariant &value : links) {
        if (clean.size() >= 8) break;
        const QVariantMap row = value.toMap();
        const QString kind = row.value(QStringLiteral("kind")).toString();
        const QString title = row.value(QStringLiteral("title")).toString().trimmed().left(64);
        const QString urlText = row.value(QStringLiteral("url")).toString().trimmed().left(512);
        if (kind != QStringLiteral("github") && kind != QStringLiteral("discord")
            && kind != QStringLiteral("youtube") && kind != QStringLiteral("custom"))
            continue;
        if (title.isEmpty() != urlText.isEmpty())
            continue;
        if (!urlText.isEmpty()) {
            const QUrl url(urlText);
            const QString scheme = url.scheme().toLower();
            if (!url.isValid() || url.host().isEmpty()
                || (scheme != QStringLiteral("https") && scheme != QStringLiteral("http")))
                continue;
        }
        clean.append(QVariantMap {
            { QStringLiteral("kind"), kind },
            { QStringLiteral("title"), title },
            { QStringLiteral("url"), urlText }
        });
    }
    return clean;
}

} // namespace

SettingsManager::SettingsManager(QObject *parent, bool initialize)
    : QObject(parent)
{
    if (!initialize) return;
    QSettings settings;
    m_exePath = settings.value(QStringLiteral("paths/exe")).toString();
    m_dllPath = settings.value(QStringLiteral("paths/dll")).toString();
    m_confPath = settings.value(QStringLiteral("paths/conf")).toString();
    m_fpsAutoDetect = settings.value(QStringLiteral("fps/autoDetect"), true).toBool();
    m_aboutDeveloperName = settings.value(QStringLiteral("about/developerName"),
                                          QStringLiteral("Mr.Pc Gamer & Romulos")).toString().trimmed();
    if (m_aboutDeveloperName.isEmpty())
        m_aboutDeveloperName = QStringLiteral("Mr.Pc Gamer & Romulos");
    m_aboutTagline = settings.value(QStringLiteral("about/tagline"),
                                    QStringLiteral("BlueStacks tools · with contributor Nova")).toString().trimmed();
    if (m_aboutTagline.isEmpty())
        m_aboutTagline = QStringLiteral("BlueStacks tools · with contributor Nova");
    m_aboutLinks = settings.value(QStringLiteral("about/links")).toList();
    if (!settings.contains(QStringLiteral("about/links"))) {
        m_aboutLinks = defaultAboutLinks();
        settings.setValue(QStringLiteral("about/links"), m_aboutLinks);
    } else {
        const QVariantList clean = sanitizeAboutLinks(m_aboutLinks);
        if (clean != m_aboutLinks) {
            m_aboutLinks = clean;
            settings.setValue(QStringLiteral("about/links"), m_aboutLinks);
        }
    }
    m_settingsPanelProfile = settings.value(QStringLiteral("settingsPanel/profile")).toMap();
    // Startup restores preferences only. Registry/install discovery and DLL
    // parsing used to happen here, before the first window could even paint.
    // Home's explicit Detect/Use actions do that work through Backend's worker.
    if (!m_dllPath.isEmpty() && (!FpsPatcher::isSupportedPath(m_dllPath) || !QFileInfo(m_dllPath).isFile()))
        setDllPath({});

}

void SettingsManager::refreshFpsPath()
{
    if (!m_fpsAutoDetect) return;
    const auto detection = FpsPatcher::detectDll(m_exePath);
    setDllPath(detection.ok ? detection.path : QString());
}

void SettingsManager::setFpsAutoDetect(bool enabled)
{
    if (enabled != m_fpsAutoDetect) {
        m_fpsAutoDetect = enabled;
        QSettings().setValue(QStringLiteral("fps/autoDetect"), enabled);
        emit fpsAutoDetectChanged();
    }
    if (enabled) refreshFpsPath();
}

void SettingsManager::setExePath(const QString &path)
{
    const QString clean = path.trimmed().isEmpty() ? QString() : QDir::cleanPath(path.trimmed());
    if (clean == m_exePath) {
        refreshFpsPath(); // Qt can change during an in-place BlueStacks update.
        return;
    }
    m_exePath = clean;
    QSettings().setValue(QStringLiteral("paths/exe"), m_exePath);
    emit exePathChanged();
    if (m_fpsAutoDetect) refreshFpsPath();
    else setDllPath({}); // Do not retain a manual target from a different installation.
}

void SettingsManager::setDllPath(const QString &path)
{
    const QString clean = path.trimmed().isEmpty() ? QString() : QDir::cleanPath(path.trimmed());
    if (clean == m_dllPath)
        return;
    m_dllPath = clean;
    QSettings().setValue(QStringLiteral("paths/dll"), m_dllPath);
    emit dllPathChanged();
}

void SettingsManager::setAboutDeveloperName(const QString &name)
{
    QString clean = name.trimmed().left(80);
    if (clean.isEmpty())
        clean = QStringLiteral("Mr.Pc Gamer & Romulos");
    if (clean == m_aboutDeveloperName)
        return;
    m_aboutDeveloperName = clean;
    QSettings().setValue(QStringLiteral("about/developerName"), m_aboutDeveloperName);
    emit aboutProfileChanged();
}

void SettingsManager::setAboutTagline(const QString &tagline)
{
    const QString clean = tagline.trimmed().left(120);
    if (clean == m_aboutTagline)
        return;
    m_aboutTagline = clean;
    QSettings().setValue(QStringLiteral("about/tagline"), m_aboutTagline);
    emit aboutProfileChanged();
}

void SettingsManager::setSettingsPanelProfile(const QVariantMap &profile)
{
    if (profile == m_settingsPanelProfile)
        return;
    m_settingsPanelProfile = profile;
    QSettings().setValue(QStringLiteral("settingsPanel/profile"), m_settingsPanelProfile);
    emit settingsPanelProfileChanged();
}

void SettingsManager::setAboutLinks(const QVariantList &links)
{
    if (links.size() > 8)
        return;

    QVariantList cleanLinks;
    cleanLinks.reserve(links.size());
    for (const QVariant &value : links) {
        const QVariantMap row = value.toMap();
        const QString kind = row.value(QStringLiteral("kind")).toString();
        const QString title = row.value(QStringLiteral("title")).toString().trimmed().left(64);
        const QString urlText = row.value(QStringLiteral("url")).toString().trimmed().left(512);
        if (kind != QStringLiteral("github") && kind != QStringLiteral("discord")
            && kind != QStringLiteral("youtube") && kind != QStringLiteral("custom"))
            return;
        if (title.isEmpty() != urlText.isEmpty())
            return;
        if (!urlText.isEmpty()) {
            const QUrl url(urlText);
            const QString scheme = url.scheme().toLower();
            if (!url.isValid() || url.host().isEmpty()
                || (scheme != QStringLiteral("https") && scheme != QStringLiteral("http")))
                return;
        }
        cleanLinks.append(QVariantMap {
            { QStringLiteral("kind"), kind },
            { QStringLiteral("title"), title },
            { QStringLiteral("url"), urlText }
        });
    }
    if (cleanLinks == m_aboutLinks)
        return;
    m_aboutLinks = cleanLinks;
    QSettings().setValue(QStringLiteral("about/links"), m_aboutLinks);
    emit aboutProfileChanged();
}

void SettingsManager::setConfPath(const QString &path)
{
    const QString clean = path.trimmed().isEmpty() ? QString() : QDir::cleanPath(path.trimmed());
    if (clean == m_confPath)
        return;
    m_confPath = clean;
    QSettings().setValue(QStringLiteral("paths/conf"), m_confPath);
    emit confPathChanged();
}

QString SettingsManager::version()
{
    return QStringLiteral(BSI_VERSION);
}

QString SettingsManager::exeFolder() const
{
    return QDir::toNativeSeparators(QFileInfo(m_exePath).absolutePath());
}

void SettingsManager::openExeFolder()
{
    if (!m_exePath.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_exePath).absolutePath()));
}
