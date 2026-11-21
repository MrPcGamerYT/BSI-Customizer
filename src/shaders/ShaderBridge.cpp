#include "ShaderBridge.h"
#include "ShaderPreferences.h"
#include "Runtime.h"
#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QWindow>

namespace BsiShaders {
namespace {
// A child Bootstrap completes before its QML Window necessarily does. Do not
// force QWindow::create()/winId() in that partially constructed Quick window
// (notably Qt 5). Install API hooks early, and attach HWND ownership when Qt
// has actually created its platform surface. Also revoke it on recreation.
class SurfaceObserver final : public QObject {
public:
    explicit SurfaceObserver(QWindow *window):QObject(window),window(window){window->installEventFilter(this);}
    bool eventFilter(QObject *object,QEvent *event) override {
        if(object==window&&event->type()==QEvent::PlatformSurface){
            const auto *surface=static_cast<QPlatformSurfaceEvent*>(event);
            if(surface->surfaceEventType()==QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)Runtime::instance().setWindow(0);
            else if(window&&window->handle())Runtime::instance().setWindow(std::uintptr_t(window->winId()));
        }
        return false;
    }
private:
    QPointer<QWindow> window;
};
struct State {
    ShaderPreferences preferences;
    QString instance,error;
    QSet<ShaderBridge*> bridges;
    QPointer<SurfaceObserver> surfaceObserver;
    bool attached=false,connected=false;
};
State &state(){static auto *s=new State;return *s;}
QString fileFor(const QString &instance){
    const auto key=QCryptographicHash::hash((QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()+QLatin1Char('\n')+instance).toUtf8(),QCryptographicHash::Sha256).toHex();
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("BSICustomizer/Shaders/")+QString::fromLatin1(key)+QStringLiteral(".json"));
}
void notify(){for(auto *bridge:state().bridges){emit bridge->settingsChanged();emit bridge->metricsChanged();}}
}
ShaderBridge::ShaderBridge(QObject *parent):QObject(parent){
    state().bridges.insert(this);
    auto *timer=new QTimer(this);timer->setInterval(1000);
    connect(timer,&QTimer::timeout,this,&ShaderBridge::metricsChanged);timer->start();
}
ShaderBridge::~ShaderBridge(){state().bridges.remove(this);}
QVariantMap ShaderBridge::saved()const{return state().preferences.values();}
QString ShaderBridge::error()const{return state().error.isEmpty()?state().preferences.error():state().error;}
QVariantMap ShaderBridge::display()const{return state().preferences.displayStatus();}
QVariantMap ShaderBridge::previewPotato(int mode)const{return state().preferences.previewPotato(mode);}
bool ShaderBridge::bindNativeDisplay(QObject *backend){
    auto &s=state();
    if(!s.attached)return false;
    const bool okay=s.preferences.bindDisplay(backend);
    Runtime::instance().set(s.preferences.parameters());notify();return okay;
}
QVariantMap ShaderBridge::bridgeInfo()const{
    return {{"moduleQtVersion",QString::fromLatin1(QT_VERSION_STR)},
            {"runtimeQtVersion",QString::fromLatin1(qVersion())},{"publicQtApi",true}};
}
QVariantList ShaderBridge::controlList()const{
    QVariantList out;
    for(const auto &c:controls)out<<QVariantMap{{"key",QString::fromLatin1(c.key)},{"label",QString::fromLatin1(c.label)},{"minimum",c.minimum},{"maximum",c.maximum}};
    return out;
}
QVariantMap ShaderBridge::metrics()const{
    const auto m=Runtime::instance().metrics();
    const auto number=[](std::uint64_t value){return QVariant::fromValue<qulonglong>(value);};
    const char *requested=m.requested==RendererFamily::Vulkan?"Vulkan":m.requested==RendererFamily::DirectX?"DirectX":m.requested==RendererFamily::OpenGL?"OpenGL":"Automatic";
    return {{"hooksAttached",m.hooks},{"gameSurfaceObserved",m.observed},{"active",m.active},
            {"requestedRenderer",QString::fromLatin1(requested)},
            {"renderer",QString::fromLatin1(capability(m.renderer).label)},
            {"backendImplemented",capability(m.renderer).pipelineImplemented},{"deviceRequirementsMet",m.deviceReady},
            {"processedFrames",number(m.processed)},{"status",QString::fromStdString(m.status)},
            {"vulkanLoaders",number(m.vulkan.loaders)},{"vulkanInstances",number(m.vulkan.instances)},
            {"vulkanDevices",number(m.vulkan.devices)},{"vulkanSurfaces",number(m.vulkan.surfaces)},
            {"vulkanSwapchains",number(m.vulkan.swapchains)},{"vulkanPresents",number(m.vulkan.presents)},
            {"vulkanMissedQueues",number(m.vulkan.missedQueues)},{"vulkanRejectedWindows",number(m.vulkan.rejectedWindows)},
            {"vulkanLoader",QString::fromStdString(m.vulkan.loader)}};
}
QString ShaderBridge::diagnostics()const{
    // User-triggered clipboard output only; no upload, persistent log or private
    // process/module path. Lifecycle counters are NOT confirmed game frames.
    const auto m=metrics();
    return QStringLiteral("Requested API: %1\nObserved game API: %2\n%3\nProcessed game frames: %4\nGame surface observed: %5; processing active: %6\nVulkan capture: loaders=%7; instances=%8; devices=%9; Win32 surfaces=%10; swapchains=%11; presents=%12; missing queues=%13; excluded windows=%14\nLast hooked loader: %15\nQt bridge: %16; host: %17\n%18")
        .arg(m.value("requestedRenderer").toString(),m.value("renderer").toString(),m.value("status").toString())
        .arg(m.value("processedFrames").toString(),m.value("gameSurfaceObserved").toBool()?QStringLiteral("yes"):QStringLiteral("no"),m.value("active").toBool()?QStringLiteral("yes"):QStringLiteral("no"))
        .arg(m.value("vulkanLoaders").toString(),m.value("vulkanInstances").toString(),m.value("vulkanDevices").toString(),m.value("vulkanSurfaces").toString(),m.value("vulkanSwapchains").toString(),m.value("vulkanPresents").toString(),m.value("vulkanMissedQueues").toString(),m.value("vulkanRejectedWindows").toString(),m.value("vulkanLoader").toString())
        .arg(QString::fromLatin1(QT_VERSION_STR),QString::fromLatin1(qVersion()),error());
}
void ShaderBridge::copyDiagnostics()const{if(auto *clipboard=QGuiApplication::clipboard())clipboard->setText(diagnostics());}
void ShaderBridge::attach(QObject *object,const QString &instance,const QString &graphicsRenderer){
    if(!QCoreApplication::instance()||QThread::currentThread()!=QCoreApplication::instance()->thread())return;
    auto *window=qobject_cast<QWindow*>(object);
    auto &s=state();
    if(!window||instance.trimmed().isEmpty()||instance.size()>128){s.error=QStringLiteral("The native player window/instance could not be identified. Shaders remain disabled.");notify();return;}
    if(s.attached&&s.instance!=instance){s.error=QStringLiteral("A different instance is already attached in this process; shaders remain unchanged.");notify();return;}
    s.instance=instance;s.error.clear();s.preferences.load(fileFor(instance));
    auto requested=RendererFamily::Automatic;
    if(graphicsRenderer==QLatin1String("gl"))requested=RendererFamily::OpenGL;
    else if(graphicsRenderer==QLatin1String("dx"))requested=RendererFamily::DirectX;
    else if(graphicsRenderer==QLatin1String("vlcn"))requested=RendererFamily::Vulkan;
    else {
        s.error=QStringLiteral("The player's graphics renderer setting is unknown. No shader processing was enabled.");
        s.attached=false;Runtime::instance().set(Parameters{});notify();return;
    }
    s.attached=true;Runtime::instance().selectRenderer(requested);Runtime::instance().set(s.preferences.parameters());
    if(s.surfaceObserver)delete s.surfaceObserver;
    s.surfaceObserver=new SurfaceObserver(window);
    Runtime::instance().start(window->handle()?std::uintptr_t(window->winId()):0);
    if(!s.connected){s.connected=true;QObject::connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,[] {Runtime::instance().stop();});}
    notify();
}
QVariantMap ShaderBridge::apply(const QVariantMap &values,bool keepCurrentDisplay){
    auto &s=state();
    if(!QCoreApplication::instance()||QThread::currentThread()!=QCoreApplication::instance()->thread())
        return {{"ok",false},{"msg",QStringLiteral("Shader settings must be saved on the player GUI thread.")}};
    if(!s.attached){s.error=QStringLiteral("No native player instance is attached.");notify();return {{"ok",false},{"msg",s.error}};}
    auto response=s.preferences.apply(values,keepCurrentDisplay);
    if(response.value(QStringLiteral("ok")).toBool()){s.error.clear();Runtime::instance().set(s.preferences.parameters());}
    notify();return response;
}
} // namespace BsiShaders
