#include "ShaderPreferences.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMetaType>
#include <QSaveFile>
#include <cmath>

namespace BsiShaders {
namespace {
constexpr qint64 maximumFile=8192;
bool refuse(QString *error,const QString &message){if(error)*error=message;return false;}
bool safePath(const QString &path){
    QFileInfo f(QFileInfo(path).absoluteFilePath());
    for(;;){
        if(f.isSymLink()||f.isJunction())return false;
        const QFileInfo parent(f.absolutePath());
        if(parent.absoluteFilePath()==f.absoluteFilePath())return true;
        f=parent;
    }
}
bool readFile(const QString &path,QByteArray *bytes){
    if(!safePath(path))return false;
    if(!QFileInfo::exists(path)){bytes->clear();return true;}
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)||file.size()<0||file.size()>maximumFile)return false;
    *bytes=file.read(maximumFile+1);
    return bytes->size()==file.size()&&file.atEnd()&&file.error()==QFileDevice::NoError;
}
bool integer(const QVariant &value,int minimum,int maximum,int *out){
    const auto type=value.userType();
    if(type!=QMetaType::Int&&type!=QMetaType::LongLong&&type!=QMetaType::Double)return false;
    const double number=value.toDouble();
    if(!std::isfinite(number)||std::floor(number)!=number||number<minimum||number>maximum)return false;
    *out=int(number);return true;
}
QVariantMap configValues(const ShaderSavedConfiguration &c){
    QVariantMap result{{QStringLiteral("enabled"),c.visual.enabled},{QStringLiteral("potatoMode"),int(c.potato)},
        {QStringLiteral("potatoGraphic"),c.texture.enabled},{QStringLiteral("textureOverridesReviewed"),c.texture.externalOverridesReviewed}};
    for(std::size_t i=0;i<controls.size();++i)result.insert(QString::fromLatin1(controls[i].key),c.visual.values[i]);
    return result;
}
bool decodeValues(const QVariantMap &map,ShaderSavedConfiguration *c,QString *error,int version=3){
    const bool legacy=version==1;
    if(map.size()!=int(controls.size())+(legacy?1:version==2?2:4)||map.value(QStringLiteral("enabled")).userType()!=QMetaType::Bool)
        return refuse(error,QStringLiteral("Shader settings have an invalid schema."));
    c->visual.enabled=map.value(QStringLiteral("enabled")).toBool();
    for(std::size_t i=0;i<controls.size();++i)
        if(!integer(map.value(QString::fromLatin1(controls[i].key)),controls[i].minimum,controls[i].maximum,&c->visual.values[i]))
            return refuse(error,QStringLiteral("Shader values must be finite integer steps in their supported ranges."));
    int mode=0;
    if(!legacy&&!integer(map.value(QStringLiteral("potatoMode")),0,2,&mode))
        return refuse(error,QStringLiteral("Resolution mode must be Native, Light or Strong."));
    if(version>=3){
        if(map.value(QStringLiteral("potatoGraphic")).userType()!=QMetaType::Bool
           ||map.value(QStringLiteral("textureOverridesReviewed")).userType()!=QMetaType::Bool)
            return refuse(error,QStringLiteral("Potato graphic and its driver-override review must be booleans."));
        c->texture.enabled=map.value(QStringLiteral("potatoGraphic")).toBool();
        c->texture.externalOverridesReviewed=map.value(QStringLiteral("textureOverridesReviewed")).toBool();
        if(c->texture.enabled&&!c->texture.externalOverridesReviewed)
            return refuse(error,QStringLiteral("Review existing driver LOD/AA overrides before enabling experimental Potato graphic. No NVIDIA/AMD/Intel profile is reset or certified by this switch."));
    }
    c->potato=PotatoMode(mode);return true;
}
QJsonObject resolutionJson(const DisplayResolution &r){return {{QStringLiteral("width"),r.width},{QStringLiteral("height"),r.height},{QStringLiteral("custom"),r.custom}};}
bool decodeResolution(const QJsonValue &value,DisplayResolution *out){
    if(!value.isObject())return false;
    const auto object=value.toObject();
    DisplayResolution r;
    if(object.size()!=3||!object.value(QStringLiteral("custom")).isBool()
       ||!integer(object.value(QStringLiteral("width")).toVariant(),540,8192,&r.width)
       ||!integer(object.value(QStringLiteral("height")).toVariant(),540,8192,&r.height))return false;
    r.custom=object.value(QStringLiteral("custom")).toBool();if(!r.valid())return false;
    *out=r;return true;
}
QJsonObject configJson(const ShaderSavedConfiguration &c){
    QJsonObject display;
    if(c.potato!=PotatoMode::Native)display={{QStringLiteral("original"),resolutionJson(c.original)},{QStringLiteral("target"),resolutionJson(c.target)}};
    return {{QStringLiteral("schema"),QStringLiteral("bsi.shaders.v3")},
        {QStringLiteral("values"),QJsonObject::fromVariantMap(configValues(c))},{QStringLiteral("display"),display}};
}
QString displayReviewKey(PotatoMode mode,const DisplayResolution &before,const DisplayResolution &target,const ShaderSavedConfiguration &saved){
    // A concurrency stamp, NOT an authorization token or hardware attestation.
    // A modal may remain open while an external tool changes native Display.
    // Hash the exact reviewed sizes/flags AND saved configuration so confirmation
    // cannot silently replan another target or overwrite a newer shader save.
    const auto bytes=QJsonDocument(QJsonObject{{QStringLiteral("mode"),int(mode)},
        {QStringLiteral("before"),resolutionJson(before)},{QStringLiteral("target"),resolutionJson(target)},
        {QStringLiteral("saved"),configJson(saved)}}).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
bool decodeConfig(const QJsonObject &root,ShaderSavedConfiguration *c,ShaderDisplayPending *pending,QString *error){
    const auto schema=root.value(QStringLiteral("schema")).toString();
    const bool legacy=schema==QLatin1String("bsi.shaders.v1");
    const int version=legacy?1:schema==QLatin1String("bsi.shaders.v2")?2:3;
    const bool journal=root.contains(QStringLiteral("pendingDisplay"));
    if((!legacy&&schema!=QLatin1String("bsi.shaders.v2")&&schema!=QLatin1String("bsi.shaders.v3"))||root.size()!=(legacy?2:journal?4:3)
       ||(legacy&&journal)||(!pending&&journal)||!root.value(QStringLiteral("values")).isObject())
        return refuse(error,QStringLiteral("Saved shader preferences have an unknown or invalid format; no native display setting was changed."));
    if(!decodeValues(root.value(QStringLiteral("values")).toObject().toVariantMap(),c,error,version))return false;
    if(!legacy){
        if(!root.value(QStringLiteral("display")).isObject())return refuse(error,QStringLiteral("Invalid native Display recovery metadata."));
        const auto display=root.value(QStringLiteral("display")).toObject();
        if(c->potato==PotatoMode::Native){
            if(!display.isEmpty())return refuse(error,QStringLiteral("Native resolution mode must not carry an active override."));
        }else{
            if(display.size()!=2||!decodeResolution(display.value(QStringLiteral("original")),&c->original)
               ||!decodeResolution(display.value(QStringLiteral("target")),&c->target))
                return refuse(error,QStringLiteral("The saved original/target display sizes are invalid; automatic restoration is refused."));
            const auto plan=planPotatoResolution(c->potato,c->original);
            if(!plan.supported||plan.target!=c->target)return refuse(error,QStringLiteral("The saved render-resolution target does not match its bounded plan."));
        }
    }
    if(journal){
        const auto value=root.value(QStringLiteral("pendingDisplay"));
        if(!value.isObject())return refuse(error,QStringLiteral("Invalid pending Display transaction."));
        const auto transaction=value.toObject();
        if(transaction.size()!=3||!decodeResolution(transaction.value(QStringLiteral("before")),&pending->before)
           ||!decodeResolution(transaction.value(QStringLiteral("target")),&pending->target)
           ||!transaction.value(QStringLiteral("next")).isObject()
           ||(transaction.value(QStringLiteral("next")).toObject().value(QStringLiteral("schema")).toString()!=QLatin1String("bsi.shaders.v2")
              &&transaction.value(QStringLiteral("next")).toObject().value(QStringLiteral("schema")).toString()!=QLatin1String("bsi.shaders.v3"))
           ||!decodeConfig(transaction.value(QStringLiteral("next")).toObject(),&pending->next,nullptr,error))
            return refuse(error,QStringLiteral("Invalid pending Display recovery evidence; no native value was overwritten."));
        const bool restoring=pending->next.potato==PotatoMode::Native;
        if((restoring&&(c->potato==PotatoMode::Native||pending->before!=c->target||pending->target!=c->original))
           ||(!restoring&&(pending->next.target!=pending->target
               ||pending->next.original!=(c->potato==PotatoMode::Native?pending->before:c->original)
               ||(c->potato!=PotatoMode::Native&&pending->before!=c->target))))
            return refuse(error,QStringLiteral("The pending native Display transition is inconsistent; no recovery was guessed."));
        pending->active=true;
    }
    return true;
}
QString dimensions(const DisplayResolution &r){return QStringLiteral("%1 × %2").arg(r.width).arg(r.height);}
}
void ShaderPreferences::load(const QString &path){
    file=path;saved={};pending={};restartTarget={};nativeDisplay={};lastError.clear();displayDetail.clear();restartRequired=false;
    persistedBytes.clear();persistedExists=QFileInfo::exists(file);readable=readFile(file,&persistedBytes);
    if(!readable){lastError=QStringLiteral("Saved shader settings are unreadable, linked or too large; neutral bypass is active.");return;}
    if(!persistedExists)return;
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(persistedBytes,&parse);
    ShaderSavedConfiguration loaded;ShaderDisplayPending transaction;
    if(parse.error!=QJsonParseError::NoError||!document.isObject()||!decodeConfig(document.object(),&loaded,&transaction,&lastError)){
        readable=false; // Do not erase a corrupt/unknown native restore snapshot on Save.
        if(lastError.isEmpty())lastError=QStringLiteral("Saved shader settings are invalid; neutral bypass is active. Native resolution was not changed.");
        return;
    }
    saved=loaded;pending=transaction;
    if(pending.active)displayDetail=QStringLiteral("A native Display save was interrupted. Its before/target evidence is retained; waiting for the native backend to reconcile without guessing.");
}
QVariantMap ShaderPreferences::values()const{return configValues(saved);}
bool ShaderPreferences::persist(const ShaderSavedConfiguration &configuration,const ShaderDisplayPending &transaction,QString *error){
    QByteArray actual;
    if(!readable||QFileInfo::exists(file)!=persistedExists||!readFile(file,&actual)||actual!=persistedBytes)
        return refuse(error,QStringLiteral("Shader preferences changed externally or became unsafe/unreadable. No preference file was overwritten; reopen/restart to reload."));
    auto object=configJson(configuration);
    if(transaction.active)object.insert(QStringLiteral("pendingDisplay"),QJsonObject{
        {QStringLiteral("before"),resolutionJson(transaction.before)},{QStringLiteral("target"),resolutionJson(transaction.target)},
        {QStringLiteral("next"),configJson(transaction.next)}});
    const auto bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);
    if(bytes.isEmpty()||bytes.size()>maximumFile||!safePath(file))return refuse(error,QStringLiteral("Shader settings exceed their safe storage bounds."));
    QSaveFile output(file);output.setDirectWriteFallback(false);
    if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())
        return refuse(error,QStringLiteral("Could not atomically save shader preferences. Native Display recovery evidence, if staged, was retained."));
    persistedBytes=bytes;persistedExists=true;return true;
}
bool ShaderPreferences::reconcile(QString *error){
    if(!pending.active)return true;
    DisplayResolution current;bool modified=false;
    if(!nativeDisplay.read(&current,error)||!nativeDisplay.modified(&modified,error))return false;
    if(modified)return refuse(error,QStringLiteral("An interrupted resolution save and native unsaved settings are present. Use the player's native Discard/Save first; no unrelated draft will be committed or discarded by Shaders."));
    if(current!=pending.before&&current!=pending.target)
        return refuse(error,QStringLiteral("Native Display differs from both sides of an interrupted resolution save. Choose Keep current display, then Save, or restore the known size in Display. No resolution is guessed."));
    const auto resolved=current==pending.target?pending.next:saved;
    if(!persist(resolved,{},error))return false;
    saved=resolved;pending={};displayDetail.clear();return true;
}
bool ShaderPreferences::bindDisplay(QObject *backend){
    displayDetail.clear();
    if(!nativeDisplay.bind(backend,&displayDetail))return false;
    if(!pending.active)return true;
    if(!safePath(file+QStringLiteral(".lock")))return refuse(&displayDetail,QStringLiteral("Linked shader writer locks are refused."));
    QLockFile lock(file+QStringLiteral(".lock"));
    if(!lock.tryLock(0))return refuse(&displayDetail,QStringLiteral("Another shader-preference writer is active; Display recovery was not attempted."));
    const bool okay=reconcile(&displayDetail);
    if(!okay)lastError=displayDetail;
    else lastError.clear();
    return okay;
}
bool ShaderPreferences::bindPlayer(QObject *backend){
    QString error;
    return nativeDisplay.bindPlayer(backend,&error);
}
QVariantMap ShaderPreferences::requestRestart(){
    const auto fail=[](const QString &message){return QVariantMap{{"ok",false},{"msg",message}};};
    if(!readable||pending.active||!restartRequired)
        return fail(QStringLiteral("No finalized resolution change from this session is awaiting restart. Nothing was restarted."));
    DisplayResolution actual;QString error;bool modified=true;
    if(!nativeDisplay.read(&actual,&error)||!nativeDisplay.modified(&modified,&error))return fail(error);
    if(modified)return fail(QStringLiteral("Save or Discard the player's other unsaved settings before restarting. Shaders did not commit or discard them."));
    if(actual!=restartTarget)return fail(QStringLiteral("Display changed after this resolution save. Review it in Display before restarting; no stale target was assumed."));
    if(!nativeDisplay.restart(&error))return fail(error);
    // Accepted invocation != proof of completed restart/live guest resolution.
    restartRequired=false;
    return {{"ok",true},{"accepted",true},{"msg",QStringLiteral("The player's native restart request was accepted. Running apps may close; verify the game size after it reopens.")}};
}
QVariantMap ShaderPreferences::displayStatus()const{
    DisplayResolution current;QString readError;bool modified=false;
    const bool available=readable&&nativeDisplay.read(&current,&readError)&&nativeDisplay.modified(&modified,&readError);
    const bool conflict=available&&saved.potato!=PotatoMode::Native&&current!=saved.target;
    QString detail=readable?displayDetail:lastError;
    if(detail.isEmpty()){
        if(!available)detail=QStringLiteral("Native Display backend unavailable. Render resolution changes are disabled here; visual effects still work independently.");
        else if(pending.active)detail=QStringLiteral("Pending native Display save needs reconciliation. No live rendering result or successful restore is claimed.");
        else if(conflict)detail=QStringLiteral("Display changed outside this profile. Keep the current display explicitly before replacing/forgetting its old restore snapshot; newer settings will not be overwritten.");
        else if(restartRequired)detail=QStringLiteral("Native render size saved. Fully restart the player to use it; the current running game's size has not been measured.");
        else if(saved.potato!=PotatoMode::Native)detail=saved.original==saved.target
            ?QStringLiteral("The selected profile matches the captured original Display size; no reduction from that original is configured. The running game's size has not been measured.")
            :QStringLiteral("A native Display size is configured. Actual guest/game resolution must be checked after restart; this is not texture-detail reduction.");
        else detail=QStringLiteral("Optional native Display size, separate from texture sampling. A changed size needs Save and a player restart; an already-configured size needs neither Display write nor restart.");
    }
    return {{"available",available},{"width",current.width},{"height",current.height},{"custom",current.custom},
        {"nativeUnsaved",modified},{"mode",int(saved.potato)},{"pending",pending.active},{"conflict",conflict},
        {"restartRequired",restartRequired},{"restartAvailable",available&&nativeDisplay.canRestart()},
        {"canRestart",available&&restartRequired&&current==restartTarget&&nativeDisplay.canRestart()&&!pending.active&&!modified},{"originalWidth",saved.original.width},{"originalHeight",saved.original.height},
        {"targetWidth",saved.target.width},{"targetHeight",saved.target.height},{"detail",detail}};
}
QVariantMap ShaderPreferences::previewPotato(int mode)const{
    const auto fail=[](const QString &message){return QVariantMap{{"ok",false},{"msg",message}};};
    DisplayResolution current;QString error;
    if(mode<0||mode>2)return fail(QStringLiteral("Unknown resolution mode."));
    if(!readable)return fail(lastError);
    if(!nativeDisplay.read(&current,&error))return fail(displayDetail.isEmpty()?error:displayDetail);
    if(pending.active)return fail(QStringLiteral("Resolve the interrupted native Display save before selecting another profile."));
    if(saved.potato!=PotatoMode::Native&&current!=saved.target)return fail(QStringLiteral("Display was changed elsewhere. Keep current display, then Save, before choosing another reduction."));
    const auto original=saved.potato==PotatoMode::Native?current:saved.original;
    const auto plan=planPotatoResolution(PotatoMode(mode),original);
    if(!plan.supported)return fail(QString::fromLatin1(plan.diagnostic));
    const auto target=plan.target;
    const bool changes=current!=target;
    QString message;
    if(!changes)message=QStringLiteral("Display is already %1. This selection needs no Display write or restart; other shader changes can still be saved.").arg(dimensions(target));
    else if(!mode)message=QStringLiteral("Restore original Display: %1 → %2. Save, then restart. Later external Display edits are preserved.").arg(dimensions(current),dimensions(target));
    else {
        const auto difference=int(std::int64_t(target.width)*target.height*100/(std::int64_t(current.width)*current.height))-100;
        message=QStringLiteral("%1 → %2 (about %3% %4 configured pixels). Save, then restart. DPI/FPS caps are unchanged; game resolution and FPS are not measured.")
            .arg(dimensions(current),dimensions(target)).arg(std::abs(difference))
            .arg(difference>0?QStringLiteral("more"):QStringLiteral("fewer"));
    }
    return {{"ok",true},{"width",target.width},{"height",target.height},{"custom",target.custom},
        {"displayChanged",changes},{"reviewKey",displayReviewKey(PotatoMode(mode),current,target,saved)},{"msg",message}};
}
QVariantMap ShaderPreferences::apply(const QVariantMap &input,bool keepCurrentDisplay,const QString &reviewKey){
    const auto fail=[&](const QString &message){lastError=message;return QVariantMap{{"ok",false},{"msg",message}};};
    ShaderSavedConfiguration requested;QString error;
    if(file.isEmpty())return fail(QStringLiteral("No native player instance is attached."));
    if(!decodeValues(input,&requested,&error))return fail(error);
    if(keepCurrentDisplay&&requested.potato!=PotatoMode::Native)return fail(QStringLiteral("Keep current display requires Native mode."));
    if(!safePath(file)||!QDir().mkpath(QFileInfo(file).absolutePath())||!safePath(file)||!safePath(file+QStringLiteral(".lock")))
        return fail(QStringLiteral("Cannot create a safe shader settings/lock directory."));
    QLockFile lock(file+QStringLiteral(".lock"));
    if(!lock.tryLock(0))return fail(QStringLiteral("Another shader-settings writer is active; nothing was changed."));
    if(!reviewKey.isEmpty()&&pending.active)
        return fail(QStringLiteral("A Display recovery transaction appeared after preview. Resolve it and review the size again; nothing was saved or restarted."));
    if(pending.active&&!keepCurrentDisplay&&!reconcile(&error))return fail(error);
    if(!reviewKey.isEmpty()&&(keepCurrentDisplay||requested.potato==saved.potato))
        return fail(QStringLiteral("The reviewed Display selection is stale. Review the current size and Save again; nothing was changed."));
    ShaderSavedConfiguration next=saved;next.visual=requested.visual;next.texture=requested.texture;next.potato=requested.potato;
    DisplayResolution before,target;bool nativeChange=false;
    if(keepCurrentDisplay||next.potato!=saved.potato){
        bool modified=false;
        if(!nativeDisplay.read(&before,&error)||!nativeDisplay.modified(&modified,&error))return fail(error);
        if(modified)return fail(QStringLiteral("The player has native unsaved settings. Save or Discard those first; Shaders will not commit or discard unrelated drafts."));
        if(keepCurrentDisplay){next.original={};next.target={};}
        else {
            if(saved.potato!=PotatoMode::Native&&before!=saved.target)
                return fail(QStringLiteral("Native Display changed outside this profile. Choose Keep current display and Save; the newer resolution will not be overwritten."));
            if(next.potato==PotatoMode::Native){target=saved.original;next.original={};next.target={};}
            else {
                const auto original=saved.potato==PotatoMode::Native?before:saved.original;
                const auto plan=planPotatoResolution(next.potato,original);
                if(!plan.supported)return fail(QString::fromLatin1(plan.diagnostic));
                target=plan.target;next.original=original;next.target=target;
            }
            nativeChange=before!=target;
        }
    }
    if(!reviewKey.isEmpty()&&reviewKey!=displayReviewKey(next.potato,before,target,saved))
        return fail(QStringLiteral("Display or saved shader settings changed after the restart preview. Review the current size and Save again; nothing was saved or restarted."));
    if(!nativeChange){
        const bool unchangedSelection=!keepCurrentDisplay&&next.potato!=saved.potato;
        if(!persist(next,{},&error))return fail(error);
        saved=next;pending={};lastError.clear();displayDetail.clear();
        return {{"ok",true},{"displayChanged",false},{"resolutionUnchanged",unchangedSelection},
            {"msg",keepCurrentDisplay?QStringLiteral("Current native display kept; the old resolution restore snapshot was cleared. No display setting was written.")
                :unchangedSelection?QStringLiteral("Display is already %1. Shader settings saved; this selection made no Display write and needs no additional restart.").arg(dimensions(before))
                :QStringLiteral("Shader settings saved.")}};
    }
    ShaderDisplayPending transaction;transaction.active=true;transaction.before=before;transaction.target=target;transaction.next=next;
    // The old preferences plus BOTH native sizes are durable application-level
    // recovery evidence before the first backend write. Never confDiscard()
    // the global draft, and never commit a pre-existing unrelated native edit.
    if(!persist(saved,transaction,&error))return fail(error);
    pending=transaction;
    if(!nativeDisplay.stage(target,&error)){
        const auto failure=error;QString rollbackError;DisplayResolution actual;bool modified=true;
        nativeDisplay.stage(before,&rollbackError); // restore ONLY our three fields, not a global Discard
        if(nativeDisplay.read(&actual,&rollbackError)&&actual==before&&nativeDisplay.modified(&modified,&rollbackError)&&!modified
           &&persist(saved,{},&rollbackError))pending={};
        return fail(failure+(pending.active?QStringLiteral(" Pending recovery retained."):QStringLiteral(" Original display retained; nothing was committed.")));
    }
    bool modified=true;DisplayResolution actual;
    if(!nativeDisplay.commit(&error)||!nativeDisplay.read(&actual,&error)||actual!=target
       ||!nativeDisplay.modified(&modified,&error)||modified){
        if(error.isEmpty())error=QStringLiteral("The native Display commit did not produce a clean, exact configured size.");
        return fail(error+QStringLiteral(" Pending recovery retained. No visual settings were activated; do not treat this as a successful resolution save."));
    }
    restartRequired=true;restartTarget=target;
    if(!persist(next,{},&error))return fail(QStringLiteral("Native render size was submitted successfully; restart is required, but preference finalization failed. Recovery is retained. ")+error);
    saved=next;pending={};lastError.clear();displayDetail.clear();
    return {{"ok",true},{"displayChanged",true},{"restartRequired",true},
        {"msg",QStringLiteral("Native Display size saved: %1. Restart the player to apply it. DPI, render API and FPS caps were not changed; actual game resolution and FPS are not measured.").arg(dimensions(target))}};
}
} // namespace BsiShaders
