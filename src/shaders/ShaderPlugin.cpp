#include "ShaderBridge.h"
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QtQml/qqml.h>
#include <cstring>

// A normal Qt QML plugin. It neither replaces version.dll nor forwards any
// Windows system-library exports. Qt invokes registration outside DllMain.
#if defined(_M_X64) || defined(__x86_64__)
#define BSI_SHADER_ARCH "x64"
#else
#define BSI_SHADER_ARCH "unsupported"
#endif
#if defined(_MSC_VER) && _MSC_VER >= 1900
#define BSI_SHADER_ABI "msvc"
#else
#define BSI_SHADER_ABI "other"
#endif
#if defined(QT_NO_DEBUG) && !defined(QT_DEBUG)
#define BSI_SHADER_BUILD "release"
#else
#define BSI_SHADER_BUILD "debug"
#endif
#ifdef QT_NAMESPACE
#define BSI_SHADER_NAMESPACE "custom"
#else
#define BSI_SHADER_NAMESPACE "default"
#endif
extern "C" Q_DECL_EXPORT const char BSIShadersDescriptor[] =
    "BSI_SHADERS_V2;QT=" QT_VERSION_STR ";ARCH=" BSI_SHADER_ARCH
    ";ABI=" BSI_SHADER_ABI ";BUILD=" BSI_SHADER_BUILD ";NS=" BSI_SHADER_NAMESPACE ";END;";
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
