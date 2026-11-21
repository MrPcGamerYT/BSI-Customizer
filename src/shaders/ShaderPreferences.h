#pragma once
#include "NativeDisplayBinding.h"
#include "Parameters.h"
#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace BsiShaders {
struct ShaderSavedConfiguration {
    Parameters visual;
    PotatoMode potato=PotatoMode::Native;
    DisplayResolution original,target;
};
struct ShaderDisplayPending {
    bool active=false;
    DisplayResolution before,target;
    ShaderSavedConfiguration next;
};
// GUI-side preferences/transaction coordinator. Rendering still receives ONLY
// Parameters; native display configuration is never changed on a render thread.
class ShaderPreferences {
public:
    void load(const QString &path);
    Parameters parameters()const{return saved.visual;}
    QVariantMap values()const;
    QString error()const{return lastError;}
    bool bindDisplay(QObject *backend);
    QVariantMap displayStatus()const;
    QVariantMap previewPotato(int mode)const;
    QVariantMap apply(const QVariantMap &values,bool keepCurrentDisplay=false);
private:
    bool persist(const ShaderSavedConfiguration &configuration,const ShaderDisplayPending &transaction,QString *error);
    bool reconcile(QString *error);
    QString file,lastError,displayDetail;
    QByteArray persistedBytes;
    ShaderSavedConfiguration saved;
    ShaderDisplayPending pending;
    NativeDisplayBinding nativeDisplay;
    bool readable=true,persistedExists=false,restartRequired=false;
};
} // namespace BsiShaders
