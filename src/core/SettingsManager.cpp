#include "SettingsManager.h"
#include "FpsPatcher.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

namespace {
// Same defaults as the Python tool's DEFAULT_PATHS["MSI5"].
const char *kDefaultExe = R"(C:\Program Files\BlueStacks_msi5\HD-Player.exe)";
const char *kDefaultConf = R"(C:\ProgramData\BlueStacks_msi5\bluestacks.conf)";
} // namespace

SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    QSettings s;
    m_exePath = s.value(QStringLiteral("paths/exe"), QLatin1String(kDefaultExe)).toString();
    m_dllPath = s.value(QStringLiteral("paths/dll")).toString();
    // Migrate the renderer path saved by older versions. Do not leave it
    // selected just because it exists; it does not export the slider setters.
    QString oldName = m_dllPath;
    oldName.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (m_dllPath.isEmpty()
        || oldName.section(QLatin1Char('/'), -1).compare(QLatin1String("libOpenglRender.dll"),
                                                       Qt::CaseInsensitive) == 0) {
        m_dllPath = FpsPatcher::findDll(QFileInfo(m_exePath).absolutePath());
        s.setValue(QStringLiteral("paths/dll"), m_dllPath);
    }
    m_confPath = s.value(QStringLiteral("paths/conf"), QLatin1String(kDefaultConf)).toString();
}

void SettingsManager::setExePath(const QString &path)
{
    const QString clean = path.trimmed().isEmpty() ? QString() : QDir::cleanPath(path.trimmed());
    if (clean == m_exePath)
        return;
    m_exePath = clean;
    QSettings().setValue(QStringLiteral("paths/exe"), m_exePath);
    emit exePathChanged();
    setDllPath(FpsPatcher::findDll(QFileInfo(m_exePath).absolutePath()));
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
