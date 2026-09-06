#pragma once

#include <QObject>

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

public:
    explicit SettingsManager(QObject *parent = nullptr);

    QString exePath() const { return m_exePath; }
    void setExePath(const QString &path);

    QString dllPath() const { return m_dllPath; }
    void setDllPath(const QString &path);

    QString confPath() const { return m_confPath; }
    void setConfPath(const QString &path);

    bool fpsAutoDetect() const { return m_fpsAutoDetect; }
    void setFpsAutoDetect(bool enabled);
    QString exeFolder() const;

    static QString version();

    Q_INVOKABLE void openExeFolder();

signals:
    void exePathChanged();
    void dllPathChanged();
    void confPathChanged();
    void fpsAutoDetectChanged();

private:
    QString m_exePath;
    QString m_dllPath;
    QString m_confPath;
    bool m_fpsAutoDetect = true;
    void refreshFpsPath();
};
