#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QQmlParserStatus>

namespace BsiShaders {
class ShaderBridge final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap saved READ saved NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList controls READ controlList CONSTANT)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY metricsChanged)
    Q_PROPERTY(QVariantMap display READ display NOTIFY metricsChanged)
    Q_PROPERTY(QString error READ error NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap bridgeInfo READ bridgeInfo CONSTANT)
public:
    explicit ShaderBridge(QObject *parent=nullptr);
    ~ShaderBridge() override;
    QVariantMap saved() const;
    QVariantList controlList() const;
    QVariantMap metrics() const;
    QString error() const;
    QVariantMap bridgeInfo() const;
    QVariantMap display() const;
    Q_INVOKABLE bool bindNativeDisplay(QObject *backend);
    Q_INVOKABLE bool bindNativePlayer(QObject *backend);
    Q_INVOKABLE QVariantMap restartPlayer();
    Q_INVOKABLE QVariantMap previewPotato(int mode) const;
    Q_INVOKABLE QVariantMap apply(const QVariantMap &values,bool keepCurrentDisplay=false);
    Q_INVOKABLE QVariantMap applyReviewed(const QVariantMap &values,const QString &displayReviewKey);
    Q_INVOKABLE QString diagnostics() const;
    Q_INVOKABLE void copyDiagnostics() const;
    static void attach(QObject *window,const QString &instance,const QString &graphicsRenderer);
signals:
    void settingsChanged();
    void metricsChanged();
private:
    QVariantMap applyConfiguration(const QVariantMap &values,bool keepCurrentDisplay,const QString &displayReviewKey);
};
// qmlRegisterType wraps creatable types in a QQmlElement subclass.
class ShaderBootstrap : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(QObject* hostWindow MEMBER hostWindow)
    Q_PROPERTY(QString instanceName MEMBER instanceName)
    Q_PROPERTY(QString graphicsRenderer MEMBER graphicsRenderer)
public:
    explicit ShaderBootstrap(QObject *parent=nullptr):QObject(parent){}
    QObject *hostWindow=nullptr;
    QString instanceName;
    QString graphicsRenderer;
    void classBegin() override {}
    void componentComplete() override {ShaderBridge::attach(hostWindow,instanceName,graphicsRenderer);}
};
} // namespace BsiShaders
