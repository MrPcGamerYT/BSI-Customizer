#include "NativeDisplayBinding.h"
#include <QCoreApplication>
#include <QMetaMethod>
#include <QMetaType>
#include <QThread>

namespace BsiShaders {
namespace {
bool guiThread(QObject *backend){
    auto *app=QCoreApplication::instance();
    return backend&&app&&backend->thread()==app->thread()&&QThread::currentThread()==app->thread();
}
QMetaMethod method(QObject *backend,const char *name,const QList<int> &arguments,int result,bool setter=false){
    if(!guiThread(backend))return {};
    const auto *meta=backend->metaObject();
    QMetaMethod found;
    for(int i=0;i<meta->methodCount();++i){
        const auto candidate=meta->method(i);
        if(candidate.name()!=name||candidate.parameterCount()!=arguments.size()
           ||candidate.access()!=QMetaMethod::Public||candidate.methodType()==QMetaMethod::Signal)continue;
        bool matches=true;
        for(int a=0;a<arguments.size();++a)matches=matches&&candidate.parameterType(a)==arguments.at(a);
        const auto type=candidate.returnType();
        if(!matches||(!setter&&type!=result)||(setter&&type!=QMetaType::Void&&type!=QMetaType::Bool))continue;
        if(found.isValid())return {}; // Never guess between ambiguous overloads.
        found=candidate;
    }
    return found;
}
bool callRead(QObject *backend,const char *name,int type,void *out,const QString *key=nullptr){
    const auto m=method(backend,name,key?QList<int>{QMetaType::QString}:QList<int>{},type);
    if(!m.isValid())return false;
    const auto args=m.parameterTypes();
    // Use the declared public type names as well as checked metatype IDs:
    // qint32/int typedef spellings must not cause a false invoke mismatch.
    return key?m.invoke(backend,Qt::DirectConnection,QGenericReturnArgument(m.typeName(),out),QGenericArgument(args.at(0).constData(),key))
              :m.invoke(backend,Qt::DirectConnection,QGenericReturnArgument(m.typeName(),out));
}
bool callWrite(QObject *backend,const char *name,const QString *key=nullptr,const void *value=nullptr,int type=QMetaType::Void){
    const auto m=method(backend,name,key?QList<int>{QMetaType::QString,type}:QList<int>{},QMetaType::Void,true);
    if(!m.isValid())return false;
    bool accepted=false;
    const auto result=m.returnType()==QMetaType::Bool?QGenericReturnArgument(m.typeName(),&accepted):QGenericReturnArgument();
    const auto args=m.parameterTypes();
    const bool invoked=key?m.invoke(backend,Qt::DirectConnection,result,QGenericArgument(args.at(0).constData(),key),QGenericArgument(args.at(1).constData(),value))
                          :m.invoke(backend,Qt::DirectConnection,result);
    return invoked&&(m.returnType()==QMetaType::Void||accepted);
}
bool refuse(QString *error,const char *message){if(error)*error=QString::fromLatin1(message);return false;}
}
bool NativeDisplayBinding::bind(QObject *object,QString *error){
    backend=nullptr;
    if(!guiThread(object))return refuse(error,"Native Display settings are not available on the player GUI thread.");
    if(!method(object,"confGetInstanceI32",{QMetaType::QString},QMetaType::Int).isValid()
       ||!method(object,"confGetInstanceBool",{QMetaType::QString},QMetaType::Bool).isValid()
       ||!method(object,"confIsModified",{},QMetaType::Bool).isValid()
       ||!method(object,"confSetInstanceI32",{QMetaType::QString,QMetaType::Int},QMetaType::Void,true).isValid()
       ||!method(object,"confSetInstanceBool",{QMetaType::QString,QMetaType::Bool},QMetaType::Void,true).isValid()
       ||!method(object,"confCommit",{},QMetaType::Void,true).isValid())
        return refuse(error,"The player's native Display read/write/commit method contract is incomplete or unsupported. Render resolution changes are unavailable; visual effects remain independent.");
    backend=object;DisplayResolution resolution;
    if(!read(&resolution,error)){backend=nullptr;return false;}
    return true;
}
bool NativeDisplayBinding::bindPlayer(QObject *object,QString *error){
    player=nullptr;
    // Exact public call made by the stock Display restart prompt. No process
    // killing, shell command, CLI guessing, global config commit or hot resize.
    if(!method(object,"restartPlayer",{},QMetaType::Void,true).isValid())
        return refuse(error,"This player has no supported public restartPlayer() method on its GUI thread. Save succeeded; restart it manually.");
    player=object;return true;
}
bool NativeDisplayBinding::canRestart()const{return method(player,"restartPlayer",{},QMetaType::Void,true).isValid();}
bool NativeDisplayBinding::restart(QString *error)const{
    return callWrite(player,"restartPlayer")?true:refuse(error,"The native player restart request was refused or its backend was destroyed. Your saved resolution remains configured; restart manually.");
}
bool NativeDisplayBinding::available()const{return guiThread(backend);}
bool NativeDisplayBinding::read(DisplayResolution *resolution,QString *error)const{
    DisplayResolution current;
    const QString width=QStringLiteral("fb_width"),height=QStringLiteral("fb_height"),custom=QStringLiteral("custom_resolution_selected");
    if(!resolution||!callRead(backend,"confGetInstanceI32",QMetaType::Int,&current.width,&width)
       ||!callRead(backend,"confGetInstanceI32",QMetaType::Int,&current.height,&height)
       ||!callRead(backend,"confGetInstanceBool",QMetaType::Bool,&current.custom,&custom))
        return refuse(error,"The native framebuffer size/custom-resolution flag could not be read; no display setting was changed.");
    if(!current.valid())return refuse(error,"The native framebuffer dimensions are outside the supported capture bounds; no display setting was changed.");
    *resolution=current;return true;
}
bool NativeDisplayBinding::modified(bool *value,QString *error)const{
    return value&&callRead(backend,"confIsModified",QMetaType::Bool,value)
        ?true:refuse(error,"The player's native unsaved-settings state could not be checked; no commit is permitted.");
}
bool NativeDisplayBinding::stage(const DisplayResolution &resolution,QString *error)const{
    if(!resolution.valid())return refuse(error,"Invalid native framebuffer dimensions; no display change was staged.");
    const QString width=QStringLiteral("fb_width"),height=QStringLiteral("fb_height"),custom=QStringLiteral("custom_resolution_selected");
    if(!callWrite(backend,"confSetInstanceI32",&width,&resolution.width,QMetaType::Int)
       ||!callWrite(backend,"confSetInstanceI32",&height,&resolution.height,QMetaType::Int)
       ||!callWrite(backend,"confSetInstanceBool",&custom,&resolution.custom,QMetaType::Bool))
        return refuse(error,"The native Display backend refused a staged value. Recovery evidence was retained; no success is claimed.");
    DisplayResolution actual;
    if(!read(&actual,error)||actual!=resolution)return refuse(error,"The native Display backend did not retain the exact staged size/flag. Recovery evidence was retained.");
    return true;
}
bool NativeDisplayBinding::commit(QString *error)const{
    return callWrite(backend,"confCommit")?true:refuse(error,"The native Display commit call failed. No restart or successful display change is claimed; recovery evidence was retained.");
}
} // namespace BsiShaders
