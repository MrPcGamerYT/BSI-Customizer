#include "ShaderBridge.h"
#include "Parameters.h"
#include "Runtime.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaType>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <cmath>

namespace BsiShaders {
namespace {
struct State {
    Parameters saved;
    QString instance,file,error;
    QSet<ShaderBridge*> bridges;
    bool attached=false,connected=false;
};
State &state(){static auto *s=new State;return *s;}
QVariantMap variant(const Parameters &p){
    QVariantMap out{{QStringLiteral("enabled"),p.enabled}};
    for(std::size_t i=0;i<controls.size();++i)out.insert(QString::fromLatin1(controls[i].key),p.values[i]);
    return out;
}
bool decode(const QVariantMap &map,Parameters *p,QString *error){
    if(map.size()!=int(controls.size())+1||map.value(QStringLiteral("enabled")).userType()!=QMetaType::Bool){*error=QStringLiteral("Shader settings have an invalid schema.");return false;}
    p->enabled=map.value(QStringLiteral("enabled")).toBool();
    for(std::size_t i=0;i<controls.size();++i){
        const auto v=map.value(QString::fromLatin1(controls[i].key));
        const int type=v.userType();
        if(type!=QMetaType::Int&&type!=QMetaType::LongLong&&type!=QMetaType::Double){*error=QStringLiteral("Shader values must be finite integer steps.");return false;}
        const double value=v.toDouble();
        if(!std::isfinite(value)||std::floor(value)!=value||value<controls[i].minimum||value>controls[i].maximum){*error=QStringLiteral("A shader value is outside its supported range.");return false;}
        p->values[i]=int(value);
    }
    return true;
}
bool safePath(const QString &path){
    QFileInfo f(path);
    for(;;){
        if(f.isSymLink()||f.isJunction())return false;
        const QString parent=f.absolutePath();
        if(parent==f.absoluteFilePath()||parent.isEmpty())break;
        QFileInfo p(parent);if(p.absoluteFilePath()==f.absoluteFilePath())break;f=p;
    }
    return true;
}
QString fileFor(const QString &instance){
    const auto key=QCryptographicHash::hash((QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath()+QLatin1Char('\n')+instance).toUtf8(),QCryptographicHash::Sha256).toHex();
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(QStringLiteral("BSICustomizer/Shaders/")+QString::fromLatin1(key)+QStringLiteral(".json"));
}
void notify(){for(auto *bridge:state().bridges)emit bridge->settingsChanged();}
}
ShaderBridge::ShaderBridge(QObject *parent):QObject(parent){
    state().bridges.insert(this);
    auto *timer=new QTimer(this);timer->setInterval(1000);
    connect(timer,&QTimer::timeout,this,&ShaderBridge::metricsChanged);timer->start();
}
ShaderBridge::~ShaderBridge(){state().bridges.remove(this);}
QVariantMap ShaderBridge::saved()const{return variant(state().saved);}
QString ShaderBridge::error()const{return state().error;}
QVariantList ShaderBridge::controlList()const{
    QVariantList out;
    for(const auto &c:controls)out<<QVariantMap{{"key",QString::fromLatin1(c.key)},{"label",QString::fromLatin1(c.label)},{"minimum",c.minimum},{"maximum",c.maximum}};
    return out;
}
QVariantMap ShaderBridge::metrics()const{
    const auto m=Runtime::instance().metrics();
    return {{"hooksAttached",m.hooks},{"gameSurfaceObserved",m.observed},{"active",m.active},
            {"renderer",QString::fromLatin1(capability(m.renderer).label)},
            {"backendImplemented",capability(m.renderer).pipelineImplemented},
            {"processedFrames",QVariant::fromValue<qulonglong>(m.processed)},{"status",QString::fromStdString(m.status)}};
}
void ShaderBridge::attach(QObject *object,const QString &instance,const QString &graphicsRenderer){
    if(!QCoreApplication::instance()||QThread::currentThread()!=QCoreApplication::instance()->thread())return;
    auto *window=qobject_cast<QWindow*>(object);
    auto &s=state();
    if(!window||instance.trimmed().isEmpty()||instance.size()>128){s.error=QStringLiteral("The native player window/instance could not be identified. Shaders remain disabled.");notify();return;}
    if(s.attached&&s.instance!=instance){s.error=QStringLiteral("A different instance is already attached in this process; shaders remain unchanged.");notify();return;}
    s.instance=instance;s.file=fileFor(instance);s.error.clear();s.saved={};
    if(!safePath(s.file)){s.error=QStringLiteral("Linked shader settings paths are refused.");}
    else if(QFileInfo::exists(s.file)){
        QFile file(s.file);
        if(!file.open(QIODevice::ReadOnly)||file.size()<=0||file.size()>8192)s.error=QStringLiteral("Saved shader settings are unreadable or too large; neutral bypass is active.");
        else {
            QJsonParseError parse;
            const auto doc=QJsonDocument::fromJson(file.read(8193),&parse);
            const auto root=doc.object();
            Parameters loaded;
            if(parse.error!=QJsonParseError::NoError||root.value(QStringLiteral("schema")).toString()!=QLatin1String("bsi.shaders.v1")
                ||!decode(root.value(QStringLiteral("values")).toObject().toVariantMap(),&loaded,&s.error)){
                if(s.error.isEmpty())s.error=QStringLiteral("Saved shader settings are invalid; neutral bypass is active.");
            } else s.saved=loaded;
        }
    }
    auto requested=RendererFamily::Automatic;
    if(graphicsRenderer==QLatin1String("gl"))requested=RendererFamily::OpenGL;
    else if(graphicsRenderer==QLatin1String("dx"))requested=RendererFamily::DirectX;
    else if(graphicsRenderer==QLatin1String("vlcn"))requested=RendererFamily::Vulkan;
    else {
        s.error=QStringLiteral("The player's graphics renderer setting is unknown. No shader processing was enabled.");
        s.attached=false;Runtime::instance().set(Parameters{});notify();return;
    }
    s.attached=true;Runtime::instance().selectRenderer(requested);Runtime::instance().set(s.saved);
    Runtime::instance().start(std::uintptr_t(window->winId()));
    if(!s.connected){s.connected=true;QObject::connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,[] {Runtime::instance().stop();});}
    notify();
}
QVariantMap ShaderBridge::apply(const QVariantMap &values){
    auto &s=state();QString error;Parameters p;
    if(!s.attached||s.file.isEmpty())error=QStringLiteral("No native player instance is attached.");
    else if(!decode(values,&p,&error)){}
    else if(!safePath(s.file)||!QDir().mkpath(QFileInfo(s.file).absolutePath())||!safePath(s.file))error=QStringLiteral("Cannot create a safe shader settings directory.");
    else {
        const QByteArray bytes=QJsonDocument(QJsonObject{{QStringLiteral("schema"),QStringLiteral("bsi.shaders.v1")},
            {QStringLiteral("values"),QJsonObject::fromVariantMap(variant(p))}}).toJson(QJsonDocument::Compact);
        QSaveFile file(s.file);file.setDirectWriteFallback(false);
        if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())error=QStringLiteral("Could not atomically save shader settings; the running effects were not changed.");
        else {s.saved=p;s.error.clear();Runtime::instance().set(p);notify();return {{"ok",true},{"msg",QStringLiteral("Shader settings saved. Neutral settings bypass processing; other settings apply to supported OpenGL game frames.")}};}
    }
    s.error=error;notify();return {{"ok",false},{"msg",error}};
}
} // namespace BsiShaders
