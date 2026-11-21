#pragma once
#include "NativeDisplayBinding.h"
#include "Parameters.h"
#include "TextureQuality.h"
#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace BsiShaders {
struct ShaderSavedConfiguration {
    Parameters visual;
    TextureConfiguration texture;
    PotatoMode potato=PotatoMode::Native;
    DisplayResolution original,target;
};
struct ShaderDisplayPending {
    bool active=false;
    DisplayResolution before,target;
    ShaderSavedConfiguration next;
};
// GUI-side preferences/transaction coordinator. Rendering receives visual and
// texture settings only; native Display is never changed on a render thread.
class ShaderPreferences {
public:
    void load(const QString &path);
    Parameters parameters()const{return saved.visual;}
    TextureConfiguration texture()const{
        auto runtime=saved.texture;
        // The default-off, explicitly enabled Potato switch now selects the
        // depth-independent owned-mip compatibility path. This is a runtime
        // strategy, not a new persisted flag or an automatic resource proof.
        if(runtime.enabled)runtime.drawPolicy=TextureDrawPolicy::OwnedMipCompatibility;
        return runtime;
    }
    QVariantMap values()const;
    QString error()const{return lastError;}
    bool bindDisplay(QObject *backend);
    bool bindPlayer(QObject *backend);
    QVariantMap requestRestart();
    QVariantMap displayStatus()const;
    QVariantMap previewPotato(int mode)const;
    QVariantMap apply(const QVariantMap &values,bool keepCurrentDisplay=false,const QString &displayReviewKey={});
private:
    bool persist(const ShaderSavedConfiguration &configuration,const ShaderDisplayPending &transaction,QString *error);
    bool reconcile(QString *error);
    QString file,lastError,displayDetail;
    QByteArray persistedBytes;
    ShaderSavedConfiguration saved;
    ShaderDisplayPending pending;
    DisplayResolution restartTarget;
    NativeDisplayBinding nativeDisplay;
    bool readable=true,persistedExists=false,restartRequired=false;
};
} // namespace BsiShaders
