#include "SettingsManager.h"
#include "FpsPatcher.h"
#include "BsDetector.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QProcessEnvironment>
#include <QUrl>

SettingsManager::SettingsManager(QObject *parent, bool initialize)
    : QObject(parent)
{
    if (!initialize) return;
    QSettings settings;
    m_exePath = settings.value(QStringLiteral("paths/exe")).toString();
    m_dllPath = settings.value(QStringLiteral("paths/dll")).toString();
    m_confPath = settings.value(QStringLiteral("paths/conf")).toString();
    m_fpsAutoDetect = settings.value(QStringLiteral("fps/autoDetect"), true).toBool();
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
