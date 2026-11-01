#pragma once

#include <QObject>
#include <QVariantList>

/**
 * SettingsManager — persisted app settings (QSettings).
 * Holds the target paths (HD-Player.exe, Qt5/Qt6QuickTemplates2.dll, bluestacks.conf).
 */
class SettingsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString exePath READ exePath WRITE setExePath NOTIFY exePathChanged)
    Q_PROPERTY(QString dllPath READ dllPath WRITE setDllPath NOTIFY dllPathChanged)
    Q_PROPERTY(QString confPath READ confPath WRITE setConfPath NOTIFY confPathChanged)
    Q_PROPERTY(QString exeFolder READ exeFolder NOTIFY exePathChanged)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool fpsAutoDetect READ fpsAutoDetect WRITE setFpsAutoDetect NOTIFY fpsAutoDetectChanged)
    Q_PROPERTY(QString aboutDeveloperName READ aboutDeveloperName WRITE setAboutDeveloperName NOTIFY aboutProfileChanged)
    Q_PROPERTY(QString aboutTagline READ aboutTagline WRITE setAboutTagline NOTIFY aboutProfileChanged)
    Q_PROPERTY(QVariantList aboutLinks READ aboutLinks WRITE setAboutLinks NOTIFY aboutProfileChanged)

public:
    explicit SettingsManager(QObject *parent = nullptr, bool initialize = true);

    QString exePath() const { return m_exePath; }
    void setExePath(const QString &path);

    QString dllPath() const { return m_dllPath; }
    void setDllPath(const QString &path);

    QString confPath() const { return m_confPath; }
    void setConfPath(const QString &path);

    bool fpsAutoDetect() const { return m_fpsAutoDetect; }
    void setFpsAutoDetect(bool enabled);
    QString exeFolder() const;

    QString aboutDeveloperName() const { return m_aboutDeveloperName; }
    void setAboutDeveloperName(const QString &name);
    QString aboutTagline() const { return m_aboutTagline; }
    void setAboutTagline(const QString &tagline);
    QVariantList aboutLinks() const { return m_aboutLinks; }
    void setAboutLinks(const QVariantList &links);

    static QString version();

    Q_INVOKABLE void openExeFolder();

signals:
    void exePathChanged();
    void dllPathChanged();
    void confPathChanged();
    void fpsAutoDetectChanged();
    void aboutProfileChanged();

private:
    friend class Backend; // isolated settings snapshot for background patch jobs
    QString m_exePath;
    QString m_dllPath;
    QString m_confPath;
    bool m_fpsAutoDetect = true;
    QString m_aboutDeveloperName;
    QString m_aboutTagline;
    QVariantList m_aboutLinks;
    void refreshFpsPath();
};
