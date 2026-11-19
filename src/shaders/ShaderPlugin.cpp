#include "ShaderBridge.h"
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QtQml/qqml.h>
#include <cstring>

// A normal Qt QML plugin. It neither replaces version.dll nor forwards any
// Windows system-library exports. Qt invokes registration outside DllMain.
extern "C" Q_DECL_EXPORT const char BSIShadersDescriptor[] =
    "BSI_SHADERS_V1;QT=" QT_VERSION_STR ";ARCH=x64;END;";
class BSIShadersPlugin final : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid FILE "plugin.json")
public:
    void registerTypes(const char *uri) override {
        Q_ASSERT(std::strcmp(uri,"BSIShaders")==0);
        qmlRegisterSingletonType<BsiShaders::ShaderBridge>(uri,1,0,"Engine",[](QQmlEngine *engine,QJSEngine*)->QObject*{
            return new BsiShaders::ShaderBridge(engine);
        });
        qmlRegisterType<BsiShaders::ShaderBootstrap>(uri,1,0,"Bootstrap");
    }
};
#include "ShaderPlugin.moc"
