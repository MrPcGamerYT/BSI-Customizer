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
    if (m_aboutLinks.size() != 4) {
        m_aboutLinks = defaultAboutLinks();
        settings.setValue(QStringLiteral("about/links"), m_aboutLinks);
    }
    if (!QFileInfo(m_exePath).isFile()) {
        const auto installs = BsDetector::findBlueStacks();
        if (installs.size() == 1) {
            const auto install = installs.first().toMap();
            m_exePath = install.value(QStringLiteral("exe")).toString();
            m_confPath = install.value(QStringLiteral("conf")).toString();
            settings.setValue(QStringLiteral("paths/exe"), m_exePath);
            settings.setValue(QStringLiteral("paths/conf"), m_confPath);
        }
        // Never choose a random installation when several are present.
    }
    if (m_confPath.isEmpty() && QFileInfo(m_exePath).isFile()) {
        const QString root = QFileInfo(m_exePath).absolutePath();
        for (const auto &value : BsDetector::findBlueStacks()) {
            const auto install = value.toMap();
            if (QDir::cleanPath(install.value(QStringLiteral("root")).toString()).compare(QDir::cleanPath(root), Qt::CaseInsensitive) == 0) {
                m_confPath = install.value(QStringLiteral("conf")).toString();
                break;
            }
        }
        if (m_confPath.isEmpty()) {
            const auto base = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ProgramData"), QStringLiteral("C:/ProgramData"));
            m_confPath = QDir(base).filePath(root.contains(QLatin1String("msi"), Qt::CaseInsensitive)
                         ? QStringLiteral("BlueStacks_msi5/bluestacks.conf") : QStringLiteral("BlueStacks_nxt/bluestacks.conf"));
        }
    }
    if (m_fpsAutoDetect) refreshFpsPath();
    else if (!FpsPatcher::isSupportedPath(m_dllPath) || !QFileInfo(m_dllPath).isFile()) setDllPath({});
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

void SettingsManager::setAboutLinks(const QVariantList &links)
{
    if (links.size() != 4)
        return;

    QVariantList cleanLinks;
    cleanLinks.reserve(4);
    for (const QVariant &value : links) {
        const QVariantMap row = value.toMap();
        const QString kind = row.value(QStringLiteral("kind")).toString();
        const QString title = row.value(QStringLiteral("title")).toString().trimmed().left(64);
        const QString urlText = row.value(QStringLiteral("url")).toString().trimmed().left(512);
        if (kind != QStringLiteral("github") && kind != QStringLiteral("discord")
            && kind != QStringLiteral("youtube"))
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
