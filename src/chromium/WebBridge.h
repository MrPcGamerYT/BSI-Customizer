#pragma once

#include <QObject>
#include <QVariantMap>
#include <QUrl>

class AuthManager;
class Backend;
class SettingsManager;
class ThemeManager;
class UpdateChecker;

// The renderer receives data, never AuthManager, tokens, config blobs or a
// generic QObject/reflection endpoint. Additional backends must explicitly
// implement the same versioned request/result contract.
class WebBridge final : public QObject
{
    Q_OBJECT
public:
    WebBridge(Backend &backend, AuthManager &auth, SettingsManager &settings,
              ThemeManager &themes, UpdateChecker &updates, QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap snapshot() const;
    Q_INVOKABLE QVariantMap invoke(const QString &method, const QVariantMap &args);
    Q_INVOKABLE void requestFile(const QString &role);
    Q_INVOKABLE void fileChosen(const QString &role, const QUrl &url);

signals:
    void stateChanged(const QVariantMap &state);
    void logMessage(const QString &message, const QString &color);
    void openFileRequested(const QString &role);
    void fileSelected(const QString &role, const QString &path);
    void nativeToolRequested(int page);
    void videoFileRequested(const QUrl &file);
    void videoControlRequested(const QString &action, const QVariantMap &options);
    void videoStatus(const QString &message);

private:
    void publish();
    QVariantMap updateStatus() const;
    Backend &m_backend;
    AuthManager &m_auth;
    SettingsManager &m_settings;
    ThemeManager &m_themes;
    UpdateChecker &m_updates;
    quint64 m_workspaceRevision = 0;
    quint64 m_resourceRevision = 0;
    bool m_publishPending = false;
    bool m_dispatching = false;
};
