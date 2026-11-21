// Public native-Display method contract + preferences/recovery fixture.
// These authored config doubles are NOT an emulator, restart or FPS test.
#include "ShaderPreferences.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
using namespace BsiShaders;
namespace {
void require(bool value,const QString &message){if(!value){std::cerr<<"FAIL: "<<message.toStdString()<<'\n';std::exit(1);}}
QByteArray bytes(const QString &path){QFile f(path);require(f.open(QIODevice::ReadOnly),QStringLiteral("Read test-owned preferences"));return f.readAll();}
void write(const QString &path,const QByteArray &data){QFile f(path);require(f.open(QIODevice::WriteOnly|QIODevice::Truncate)&&f.write(data)==data.size(),QStringLiteral("Write test-owned preference fixture"));}
QJsonObject json(const QString &path){const auto doc=QJsonDocument::fromJson(bytes(path));require(doc.isObject(),QStringLiteral("Read actual JSON preferences"));return doc.object();}
bool success(const QVariantMap &result){return result.value(QStringLiteral("ok")).toBool();}
void applied(ShaderPreferences &p,const QVariantMap &values,bool keep=false){const auto r=p.apply(values,keep);require(success(r),r.value(QStringLiteral("msg")).toString());}
QVariantMap mode(ShaderPreferences &p,int value){auto v=p.values();v.insert(QStringLiteral("potatoMode"),value);return v;}
QByteArray encoded(const QJsonObject &object){return QJsonDocument(object).toJson(QJsonDocument::Compact);}
class Backend final:public QObject {
    Q_OBJECT
public:
    DisplayResolution configured{1920,1080,false},draft=configured;
    int dpi=320,fps=90,other=42,otherDraft=other,commits=0,discards=0,writes=0;
    QString failOnce,expectedJournal;
    bool rejectCommit=false,commitThenRefuse=false,rejectRestart=false;
    int restarts=0;
    Q_INVOKABLE bool restartPlayer(){++restarts;return !rejectRestart;}
    std::function<void()> onCommit;
    void external(DisplayResolution value){configured=draft=value;}
    void discard(){draft=configured;otherDraft=other;++discards;}
    Q_INVOKABLE qint32 confGetInstanceI32(const QString &key)const{
        require(key==QLatin1String("fb_width")||key==QLatin1String("fb_height"),QStringLiteral("Do not read unrelated native integer settings"));
        return key==QLatin1String("fb_width")?draft.width:draft.height;
    }
    Q_INVOKABLE bool confGetInstanceBool(const QString &key)const{
        require(key==QLatin1String("custom_resolution_selected"),QStringLiteral("Do not read unrelated native flags"));return draft.custom;
    }
    Q_INVOKABLE bool confIsModified()const{return draft!=configured||otherDraft!=other;}
    Q_INVOKABLE bool confSetInstanceI32(const QString &key,qint32 value){
        ++writes;checkJournal();
        require(key==QLatin1String("fb_width")||key==QLatin1String("fb_height"),QStringLiteral("Only the native framebuffer size may be written"));
        if(failOnce==key){failOnce.clear();return false;}
        if(key==QLatin1String("fb_width"))draft.width=value;else draft.height=value;return true;
    }
    Q_INVOKABLE bool confSetInstanceBool(const QString &key,bool value){
        ++writes;checkJournal();require(key==QLatin1String("custom_resolution_selected"),QStringLiteral("Only the native custom-resolution flag may be written"));
        draft.custom=value;return true;
    }
    Q_INVOKABLE bool confCommit(){
        checkJournal();if(rejectCommit)return false;
        ++commits;configured=draft;other=otherDraft;if(onCommit)onCommit();return !commitThenRefuse;
    }
    void checkJournal()const{
        if(!expectedJournal.isEmpty())require(json(expectedJournal).contains(QStringLiteral("pendingDisplay")),QStringLiteral("Recovery must exist BEFORE any native write/commit"));
    }
};
struct Fixture {
    QTemporaryDir directory;
    QString path=directory.filePath(QStringLiteral("profile.json"));
    Backend backend;
    ShaderPreferences preferences;
    Fixture(){require(directory.isValid(),QStringLiteral("Create isolated test directory"));preferences.load(path);require(preferences.bindDisplay(&backend),preferences.error());require(preferences.bindPlayer(&backend),QStringLiteral("Bind supported native player restart contract"));backend.expectedJournal=path;}
    void untouched()const{require(backend.dpi==320&&backend.fps==90&&backend.other==42&&backend.discards==0,QStringLiteral("DPI/FPS/unrelated fields/global Discard must remain untouched"));}
};
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    const DisplayResolution original{1920,1080,false};
    require(planPotatoResolution(PotatoMode::Light,original).target==DisplayResolution{1280,720,true},QStringLiteral("Light actually plans 1280x720 from 1080p"));
    require(planPotatoResolution(PotatoMode::Strong,original).target==DisplayResolution{960,540,true},QStringLiteral("Strong actually plans 960x540 from 1080p"));
    for(const auto resolution:{original,DisplayResolution{1080,1920,false},{5120,2160,false},{1601,901,true},{1440,1440,true}}){
        for(auto quality:{PotatoMode::Light,PotatoMode::Strong}){
            const auto plan=planPotatoResolution(quality,resolution);
            require(plan.reduced&&plan.target.valid()&&plan.target.width%2==0&&plan.target.height%2==0,QStringLiteral("Legal even, aspect-preserving native sizes"));
            require(std::int64_t(plan.target.width)*plan.target.height<std::int64_t(resolution.width)*resolution.height,QStringLiteral("Every accepted Potato profile reduces ACTUAL native pixel count"));
            require(std::abs(double(plan.target.width)/plan.target.height-double(resolution.width)/resolution.height)<.002,QStringLiteral("No stretched-aspect Potato plan"));
        }
    }
    for(const auto resolution:{DisplayResolution{960,540,true},{1280,539,false},{8192,8192,false},{5120,2160,true}})
        require(!planPotatoResolution(PotatoMode::Strong,resolution).reduced,QStringLiteral("No pretend/invalid reduction or unsupported custom size"));
    require(!planPotatoResolution(PotatoMode(3),original).supported,QStringLiteral("Unknown mode cannot activate"));
    for(const auto current:{DisplayResolution{1280,720,false},{720,1280,true},{960,540,false},{540,960,true},{721,720,false}}){
        const auto profile=(std::min)(current.width,current.height)==720?PotatoMode::Light:PotatoMode::Strong;
        const auto plan=planPotatoResolution(profile,current);
        require(plan.supported&&!plan.reduced&&plan.target==current,QStringLiteral("An exact short-edge selection is an honest no-op, preserving both dimensions and custom/predefined flag"));
    }
    for(const auto current:{DisplayResolution{960,540,false},{1066,600,true},{540,960,false}})
        require(!planPotatoResolution(PotatoMode::Light,current).supported,QStringLiteral("A reduction profile never upscales a smaller original framebuffer"));
    {
        Fixture f;
        require(f.preferences.displayStatus().value(QStringLiteral("restartAvailable")).toBool()
                &&!f.preferences.displayStatus().value(QStringLiteral("canRestart")).toBool(),
                QStringLiteral("Pre-save restart capability is separate from permission to restart a finalized exact target"));
    }
    {
        Fixture f;auto values=mode(f.preferences,2);
        const auto reviewed=f.preferences.previewPotato(2).value(QStringLiteral("reviewKey")).toString();
        require(reviewed.size()==64&&!QFile::exists(f.path)&&f.backend.writes==0,QStringLiteral("Read-only preview issues an exact-size concurrency stamp before any dialog/write"));
        const auto message=f.preferences.previewPotato(2).value(QStringLiteral("msg")).toString();
        require(message.count(QChar(0x00d7))==2&&message.count(QChar(0x2192))==1&&!message.contains(QChar(0x00c3))&&!message.contains(QChar(0x00e2)),
                QStringLiteral("Native preview dimensions/arrows are Unicode on every compiler code page, not UTF-8 bytes decoded as an ANSI literal"));
        f.backend.external({1600,900,false}); // same target, different unreviewed source size
        require(!success(f.preferences.apply(values,false,reviewed))&&f.backend.writes==0&&f.backend.commits==0&&!QFile::exists(f.path),
                QStringLiteral("Restart confirmation cannot silently replan an externally changed source, even when its eventual target is the same"));
        const auto fresh=f.preferences.previewPotato(2).value(QStringLiteral("reviewKey")).toString();
        require(fresh!=reviewed,QStringLiteral("A new source/restore snapshot needs a fresh review"));
        require(success(f.preferences.apply(values,false,fresh))&&f.backend.configured==DisplayResolution{960,540,true}&&f.backend.commits==1,
                QStringLiteral("The exact reviewed target commits through the existing durable journal and read-back"));
        const auto restorePreview=f.preferences.previewPotato(0);
        const auto restoreMessage=restorePreview.value(QStringLiteral("msg")).toString();
        require(success(restorePreview)&&restoreMessage.count(QChar(0x00d7))==2&&restoreMessage.count(QChar(0x2192))==1
                &&!restoreMessage.contains(QChar(0x00c3))&&!restoreMessage.contains(QChar(0x00e2)),
                QStringLiteral("Original-size restore uses the same code-page-independent Unicode symbols as reduction and current-size previews"));
        const auto saved=bytes(f.path);const int writes=f.backend.writes;
        require(!success(f.preferences.apply(values,false,fresh))&&bytes(f.path)==saved&&f.backend.writes==writes&&f.backend.commits==1,
                QStringLiteral("A confirmed/replayed review cannot write or commit a second time"));
        f.untouched();
    }
    {
        Fixture f;const auto reviewed=f.preferences.previewPotato(2).value(QStringLiteral("reviewKey")).toString();
        f.backend.external({1920,1080,true});
        require(!success(f.preferences.apply(mode(f.preferences,2),false,reviewed))&&f.backend.writes==0&&!QFile::exists(f.path),
                QStringLiteral("The reviewed custom/predefined flag is checked as well as dimensions"));
    }
    {
        Fixture f;const auto reviewed=f.preferences.previewPotato(2).value(QStringLiteral("reviewKey")).toString();
        auto visual=f.preferences.values();visual[QStringLiteral("enabled")]=true;visual[QStringLiteral("contrast")]=29;applied(f.preferences,visual);
        const auto saved=bytes(f.path);
        require(!success(f.preferences.apply(mode(f.preferences,2),false,reviewed))&&f.preferences.parameters().values[1]==29
                &&bytes(f.path)==saved&&f.backend.writes==0,QStringLiteral("A modal review cannot overwrite a newer shader Save even if native size stayed unchanged"));
    }
    {
        Fixture f;f.backend.external({960,540,false});
        const auto reviewed=f.preferences.previewPotato(2).value(QStringLiteral("reviewKey")).toString();
        f.backend.external(original);
        require(!success(f.preferences.apply(mode(f.preferences,2),false,reviewed))&&f.backend.writes==0&&!QFile::exists(f.path),
                QStringLiteral("A reviewed no-op must not become a native write/restart because current size changed before Save"));
    }
    for(const auto current:{DisplayResolution{1280,720,false},{720,1280,true},{960,540,false},{540,960,true},{721,720,false}}){
        Fixture f;f.backend.external(current);
        const int profile=(std::min)(current.width,current.height)==720?1:2;
        const auto preview=f.preferences.previewPotato(profile);
        require(success(preview)&&!preview.value(QStringLiteral("displayChanged")).toBool()
                &&preview.value(QStringLiteral("width")).toInt()==current.width
                &&preview.value(QStringLiteral("height")).toInt()==current.height
                &&preview.value(QStringLiteral("custom")).toBool()==current.custom&&f.backend.writes==0&&!QFile::exists(f.path),
                QStringLiteral("Current-size preview is read-only and reports no Display change or restart"));
        require(preview.value(QStringLiteral("msg")).toString().contains(QChar(0x00d7)),
                QStringLiteral("The user's already-current resolution message paints a real multiplication sign, including on Windows ANSI builds"));
        auto values=mode(f.preferences,profile);values[QStringLiteral("enabled")]=true;values[QStringLiteral("contrast")]=27;
        values[QStringLiteral("potatoGraphic")]=true;values[QStringLiteral("textureOverridesReviewed")]=true;
        const auto response=f.preferences.apply(values,false,preview.value(QStringLiteral("reviewKey")).toString());
        require(success(response)&&response.value(QStringLiteral("resolutionUnchanged")).toBool()
                &&!response.value(QStringLiteral("displayChanged")).toBool()&&f.preferences.parameters().enabled&&f.preferences.texture().enabled
                &&f.backend.configured==current&&f.backend.writes==0&&f.backend.commits==0&&f.backend.restarts==0
                &&!f.preferences.displayStatus().value(QStringLiteral("restartRequired")).toBool(),
                QStringLiteral("An already-configured size never blocks independent visual/texture Save, writes native fields, commits, restarts or pretends to reduce pixels"));
        ShaderPreferences reopened;reopened.load(f.path);require(reopened.bindDisplay(&f.backend)&&reopened.error().isEmpty()
                &&reopened.values()==values,QStringLiteral("Honest current-size selection and its exact original survive reopen"));
        applied(reopened,mode(reopened,0));require(f.backend.configured==current&&f.backend.writes==0&&f.backend.commits==0,
                QStringLiteral("Restoring a no-op selection clears only metadata, preserving an odd/predefined/custom original exactly"));
        f.untouched();
    }
    {
        Fixture f;const DisplayResolution original720{1280,720,false};f.backend.external(original720);
        applied(f.preferences,mode(f.preferences,2));
        const auto preview=f.preferences.previewPotato(1);
        require(success(preview)&&preview.value(QStringLiteral("displayChanged")).toBool()
                &&preview.value(QStringLiteral("width")).toInt()==1280&&!preview.value(QStringLiteral("custom")).toBool(),
                QStringLiteral("Changing an existing 540 reduction to Light can explicitly restore the captured 720 predefined original, never upscale beyond it"));
        require(success(f.preferences.apply(mode(f.preferences,1),false,preview.value(QStringLiteral("reviewKey")).toString()))
                &&f.backend.configured==original720&&f.backend.commits==2&&f.preferences.displayStatus().value(QStringLiteral("restartRequired")).toBool(),
                QStringLiteral("A profile equal to the ORIGINAL is not a no-op if CURRENT is the reduced size; it still writes exact fields and requires restart"));
        ShaderPreferences reopened;reopened.load(f.path);require(reopened.bindDisplay(&f.backend)&&reopened.error().isEmpty(),
                QStringLiteral("An exact-original target remains valid recovery metadata on reopen"));
        const int writes=f.backend.writes;applied(reopened,mode(reopened,0));
        require(f.backend.configured==original720&&f.backend.writes==writes&&f.backend.commits==2,QStringLiteral("Native after explicit original-size restoration only clears metadata"));
    }
    {
        Fixture f;f.backend.external({960,540,false});
        require(!success(f.preferences.previewPotato(1)),QStringLiteral("Unavailable upscaling profile is refused by the shared preview"));
        const auto values=mode(f.preferences,1);
        require(!success(f.preferences.apply(values))&&f.backend.writes==0&&f.backend.commits==0&&!QFile::exists(f.path),
                QStringLiteral("Bypassing the filtered UI cannot force upscaling or a fake reduction"));
    }
    {
        QTemporaryDir dir;ShaderPreferences p;p.load(dir.filePath(QStringLiteral("p.json")));
        require(p.values().size()==14&&p.parameters().neutral(),QStringLiteral("Neutral v3 defaults with independent native-resolution/texture modes"));
        auto values=p.values();values[QStringLiteral("enabled")]=true;values[QStringLiteral("saturation")]=31;applied(p,values);
        const auto visual=p.parameters();const auto before=bytes(dir.filePath(QStringLiteral("p.json")));
        require(!success(p.apply(mode(p,2)))&&p.parameters()==visual,QStringLiteral("Absent native backend blocks Potato, not independent visual effects"));
        applied(p,p.values());require(bytes(dir.filePath(QStringLiteral("p.json")))==before,QStringLiteral("Visual Save is byte-idempotent"));
        for(const QVariant &invalid:{QVariant(true),QVariant(QStringLiteral("2")),QVariant(1.5),QVariant(-1),QVariant(3),QVariant(std::numeric_limits<double>::quiet_NaN())}){
            values=p.values();values[QStringLiteral("potatoMode")]=invalid;
            require(!success(p.apply(values))&&p.parameters()==visual,QStringLiteral("Strict finite integer mode schema"));
        }
    }
    {
        Fixture f;
        auto legacy=f.preferences.values();legacy.remove(QStringLiteral("potatoMode"));legacy.remove(QStringLiteral("potatoGraphic"));legacy.remove(QStringLiteral("textureOverridesReviewed"));legacy[QStringLiteral("enabled")]=true;legacy[QStringLiteral("contrast")]=21;
        const auto old=encoded({{QStringLiteral("schema"),QStringLiteral("bsi.shaders.v1")},{QStringLiteral("values"),QJsonObject::fromVariantMap(legacy)}});
        write(f.path,old);f.preferences.load(f.path);require(f.preferences.bindDisplay(&f.backend),f.preferences.error());
        require(f.preferences.values().value(QStringLiteral("contrast")).toInt()==21&&bytes(f.path)==old,QStringLiteral("Released v1 visual values load WITHOUT writing native state or migrating on open"));
        applied(f.preferences,f.preferences.values());require(json(f.path).value(QStringLiteral("schema")).toString()==QLatin1String("bsi.shaders.v3")&&f.backend.writes==0,QStringLiteral("v1 migrates only on Save, with Native mode and no Display writes"));
    }
    {
        Fixture f;
        const auto preview=f.preferences.previewPotato(2);require(success(preview)&&preview.value(QStringLiteral("width")).toInt()==960&&f.backend.writes==0&&!QFile::exists(f.path),QStringLiteral("Preview is read-only, no hidden config/file writes"));
        applied(f.preferences,mode(f.preferences,2));
        require(f.backend.configured==DisplayResolution{960,540,true}&&f.backend.commits==1&&f.preferences.parameters().neutral(),QStringLiteral("Potato writes the real three-field native Display config even with postprocessing OFF"));
        const auto saved=json(f.path);require(!saved.contains(QStringLiteral("pendingDisplay"))&&f.preferences.displayStatus().value(QStringLiteral("restartRequired")).toBool(),QStringLiteral("Successful two-stage save finalizes recovery and requires a restart"));
        applied(f.preferences,f.preferences.values());require(f.backend.commits==1&&json(f.path)==saved,QStringLiteral("Already-saved mode does not commit native settings twice"));
        ShaderPreferences reopened;reopened.load(f.path);require(reopened.bindDisplay(&f.backend)&&reopened.values()==f.preferences.values(),QStringLiteral("Mode and exact restore snapshot survive reopen"));
        applied(reopened,mode(reopened,1));require(f.backend.configured==DisplayResolution{1280,720,true},QStringLiteral("Changing strength replans from ORIGINAL, not the already reduced size"));
        applied(reopened,mode(reopened,0));require(f.backend.configured==original&&!f.backend.confIsModified(),QStringLiteral("Native mode restores exact original dimensions AND custom flag"));f.untouched();
    }
    {
        Fixture f;applied(f.preferences,mode(f.preferences,2));
        auto legacy=json(f.path);legacy.insert(QStringLiteral("schema"),QStringLiteral("bsi.shaders.v2"));
        auto values=legacy.value(QStringLiteral("values")).toObject();values.remove(QStringLiteral("potatoGraphic"));values.remove(QStringLiteral("textureOverridesReviewed"));
        legacy.insert(QStringLiteral("values"),values);const auto previous=encoded(legacy);write(f.path,previous);
        ShaderPreferences p;p.load(f.path);require(p.bindDisplay(&f.backend),p.error());
        require(bytes(f.path)==previous&&!p.texture().enabled&&p.values().value(QStringLiteral("potatoMode")).toInt()==2,
                QStringLiteral("Released v2 native resolution recovery survives read-only migration; texture reduction defaults OFF"));
        applied(p,mode(p,0));require(f.backend.configured==original,QStringLiteral("v2 original dimensions/custom flag still restore exactly after v3 migration"));
    }
    {
        Fixture f;auto values=f.preferences.values();values[QStringLiteral("potatoGraphic")]=true;
        require(!success(f.preferences.apply(values))&&!f.preferences.texture().enabled&&f.backend.writes==0,
                QStringLiteral("No unchecked texture Potato switch or hidden native-resolution substitution"));
        values[QStringLiteral("textureOverridesReviewed")]=true;applied(f.preferences,values);
        require(f.preferences.texture().enabled&&f.preferences.parameters().neutral()&&f.backend.writes==0&&f.backend.configured==original,
                QStringLiteral("Texture Potato saves independently of visual effects and changes NO native Display field"));
        ShaderPreferences p;p.load(f.path);require(p.texture().enabled&&p.texture().externalOverridesReviewed
                &&p.texture().drawPolicy==TextureDrawPolicy::OwnedMipCompatibility,
                QStringLiteral("Saved/reopened Potato On actually selects depth-independent owned-mip compatibility, not the legacy depth gate"));
        require(p.values().size()==14,QStringLiteral("Compatibility adds no persisted mode/flag, native Display edit or extra UI control"));
        values[QStringLiteral("potatoGraphic")]=false;applied(f.preferences,values);
        require(!f.preferences.texture().enabled&&f.preferences.texture().drawPolicy==TextureDrawPolicy::DepthQualifiedScene
                &&f.backend.writes==0,QStringLiteral("Turning texture Potato off disarms compatibility without a native Display write"));
        for(const QVariant &invalid:{QVariant(1),QVariant(QStringLiteral("true")),QVariant()}){
            values[QStringLiteral("potatoGraphic")]=invalid;
            require(!success(f.preferences.apply(values))&&!f.preferences.texture().enabled,QStringLiteral("Texture toggle has a strict boolean schema"));
        }
    }
    {
        Fixture f;require(!success(f.preferences.requestRestart())&&f.backend.restarts==0,QStringLiteral("No restart before a finalized Display save"));
        applied(f.preferences,mode(f.preferences,2));
        require(f.preferences.displayStatus().value(QStringLiteral("canRestart")).toBool(),QStringLiteral("Successful resolution Save offers supported native restart"));
        const int writes=f.backend.writes,commits=f.backend.commits;
        f.backend.otherDraft=77;
        require(!success(f.preferences.requestRestart())&&f.backend.restarts==0,QStringLiteral("Restart never commits/discards somebody else's native draft"));
        f.backend.otherDraft=f.backend.other;f.backend.rejectRestart=true;
        require(!success(f.preferences.requestRestart())&&f.backend.restarts==1&&f.preferences.displayStatus().value(QStringLiteral("restartRequired")).toBool(),
                QStringLiteral("Refused native restart retains successful Save and retry/manual-restart state"));
        f.backend.rejectRestart=false;
        const auto accepted=f.preferences.requestRestart();
        require(success(accepted)&&accepted.value(QStringLiteral("accepted")).toBool()&&f.backend.restarts==2
                &&!success(f.preferences.requestRestart())&&f.backend.restarts==2&&f.backend.writes==writes&&f.backend.commits==commits,
                QStringLiteral("Explicit restart invokes native backend once, never fakes completion, commits again or resizes a live guest"));f.untouched();
    }
    {
        Fixture f;applied(f.preferences,mode(f.preferences,2));f.backend.external({1440,900,true});
        require(!f.preferences.displayStatus().value(QStringLiteral("canRestart")).toBool()
                &&!success(f.preferences.requestRestart())&&f.backend.restarts==0,
                QStringLiteral("A later Display edit disables the restart offer and cannot restart under a stale saved target"));
    }
    {
        Fixture f;QObject incomplete;require(!f.preferences.bindPlayer(&incomplete),QStringLiteral("Missing native restart contract is optional, not guessed"));
        applied(f.preferences,mode(f.preferences,2));
        require(f.preferences.displayStatus().value(QStringLiteral("restartRequired")).toBool()
                &&!f.preferences.displayStatus().value(QStringLiteral("canRestart")).toBool()&&!success(f.preferences.requestRestart()),
                QStringLiteral("Unsupported native restart preserves a successful save and honest manual-restart requirement"));
    }
    for(const auto originalSize:{DisplayResolution{1600,1000,true},{5120,2160,false}}){
        Fixture f;f.backend.external(originalSize);applied(f.preferences,mode(f.preferences,2));applied(f.preferences,mode(f.preferences,0));
        require(f.backend.configured==originalSize,QStringLiteral("Exact custom and large-predefined restoration, not a hardcoded 1080p reset"));f.untouched();
    }
    {
        Fixture f;f.backend.otherDraft=99;
        require(!success(f.preferences.apply(mode(f.preferences,2)))&&f.backend.commits==0&&f.backend.writes==0&&!QFile::exists(f.path),QStringLiteral("Never commit someone else's unsaved native draft"));
        auto visual=f.preferences.values();visual[QStringLiteral("enabled")]=true;visual[QStringLiteral("bloom")]=10;applied(f.preferences,visual);
        require(f.backend.otherDraft==99&&f.backend.commits==0,QStringLiteral("Visual-only saves neither commit nor discard unrelated native drafts"));
    }
    {
        Fixture f;applied(f.preferences,mode(f.preferences,2));const int writes=f.backend.writes,commits=f.backend.commits;
        const DisplayResolution external{1280,800,true};f.backend.external(external);
        require(!success(f.preferences.apply(mode(f.preferences,0)))&&f.backend.configured==external&&f.backend.writes==writes,QStringLiteral("A later native Display edit is NEVER overwritten by stale restore metadata"));
        require(f.preferences.displayStatus().value(QStringLiteral("conflict")).toBool(),QStringLiteral("Expose the conflict explicitly"));
        applied(f.preferences,mode(f.preferences,0),true);
        require(f.backend.configured==external&&f.backend.commits==commits&&f.backend.writes==writes,QStringLiteral("Explicit Keep current display forgets only old metadata, with no Display write"));
        applied(f.preferences,mode(f.preferences,2));applied(f.preferences,mode(f.preferences,0));require(f.backend.configured==external,QStringLiteral("The next reduction snapshots the new original, not stale 1080p"));f.untouched();
    }
    {
        Fixture f;f.backend.failOnce=QStringLiteral("fb_height");
        require(!success(f.preferences.apply(mode(f.preferences,2)))&&f.backend.configured==original&&f.backend.draft==original&&f.backend.commits==0,QStringLiteral("Partial native staging failure restores only our fields, without any commit"));
        require(!json(f.path).contains(QStringLiteral("pendingDisplay"))&&f.preferences.values().value(QStringLiteral("potatoMode")).toInt()==0,QStringLiteral("Proven clean staging rollback clears recovery; no false applied mode"));f.untouched();
    }
    for(bool didCommit:{false,true}){
        Fixture f;f.backend.rejectCommit=!didCommit;f.backend.commitThenRefuse=didCommit;
        require(!success(f.preferences.apply(mode(f.preferences,2)))&&json(f.path).contains(QStringLiteral("pendingDisplay")),QStringLiteral("Refused/uncertain native commit retains BOTH sides on disk"));
        ShaderPreferences recovered;recovered.load(f.path);
        if(!didCommit){
            require(!recovered.bindDisplay(&f.backend)&&recovered.displayStatus().value(QStringLiteral("pending")).toBool(),QStringLiteral("Uncommitted native drafts cannot be mistaken for committed recovery"));
            f.backend.discard(); // Explicit native/user action, NEVER called by Shaders.
        }
        const int commits=f.backend.commits;
        require(recovered.bindDisplay(&f.backend),recovered.error());
        require(recovered.values().value(QStringLiteral("potatoMode")).toInt()==(didCommit?2:0)&&f.backend.commits==commits&&!json(f.path).contains(QStringLiteral("pendingDisplay")),QStringLiteral("Restart recovery reconciles exact before/target read-back WITHOUT re-committing"));
    }
    {
        Fixture f;const auto moved=f.path+QStringLiteral(".held");
        f.backend.onCommit=[&]{require(QFile::rename(f.path,moved)&&QDir().mkdir(f.path),QStringLiteral("Inject a real finalization I/O failure AFTER native commit"));};
        const auto result=f.preferences.apply(mode(f.preferences,2));
        require(!success(result)&&f.backend.configured==DisplayResolution{960,540,true}&&json(moved).contains(QStringLiteral("pendingDisplay")),QStringLiteral("Failed final preference write cannot lose native restore evidence or claim success"));
        require(QDir().rmdir(f.path)&&QFile::rename(moved,f.path),QStringLiteral("Remove only the test-owned I/O blocker"));
        ShaderPreferences recovered;recovered.load(f.path);require(recovered.bindDisplay(&f.backend)&&recovered.values().value(QStringLiteral("potatoMode")).toInt()==2,QStringLiteral("Next process completes the interrupted finalization from exact native read-back"));
    }
    {
        Fixture f;f.backend.rejectCommit=true;require(!success(f.preferences.apply(mode(f.preferences,2))),QStringLiteral("Stage pending fixture"));
        f.backend.external({1440,900,false});ShaderPreferences recovered;recovered.load(f.path);
        require(!recovered.bindDisplay(&f.backend)&&json(f.path).contains(QStringLiteral("pendingDisplay")),QStringLiteral("Recovery refuses a third-party size different from BOTH known sides"));
        const int writes=f.backend.writes;applied(recovered,mode(recovered,0),true);
        require(f.backend.configured==DisplayResolution{1440,900,false}&&f.backend.writes==writes,QStringLiteral("Explicit conflict recovery preserves newer Display values"));
    }
    {
        Fixture f;applied(f.preferences,f.preferences.values());const auto good=json(f.path);
        QList<QByteArray> invalid{QByteArray("{"),QByteArray(8193,'x')};
        auto unknown=good;unknown.insert(QStringLiteral("schema"),QStringLiteral("bsi.shaders.v999"));invalid<<encoded(unknown);
        auto extra=good;extra.insert(QStringLiteral("unrecognized"),true);invalid<<encoded(extra);
        auto corrupt=good;auto vals=corrupt.value(QStringLiteral("values")).toObject();vals.insert(QStringLiteral("potatoMode"),2);corrupt.insert(QStringLiteral("values"),vals);invalid<<encoded(corrupt); // no restore snapshot
        auto pending=good;pending.insert(QStringLiteral("pendingDisplay"),QJsonObject{{QStringLiteral("before"),QJsonObject{}},{QStringLiteral("target"),QJsonObject{}},{QStringLiteral("next"),good}});invalid<<encoded(pending);
        for(const auto &data:invalid){
            write(f.path,data);ShaderPreferences p;p.load(f.path);p.bindDisplay(&f.backend);
            require(!p.error().isEmpty()&&p.parameters().neutral()&&!success(p.apply(p.values()))&&bytes(f.path)==data&&f.backend.writes==0,QStringLiteral("Invalid/oversized/unknown restore metadata stays neutral and is NEVER overwritten"));
        }
    }
    {
        Fixture f;applied(f.preferences,f.preferences.values());const auto good=bytes(f.path);
        write(f.path,good+QByteArray(" "));require(!success(f.preferences.apply(mode(f.preferences,2)))&&f.backend.writes==0,QStringLiteral("Externally changed preferences are refused BEFORE native staging"));
        f.preferences.load(f.path);require(f.preferences.bindDisplay(&f.backend),f.preferences.error());
        QLockFile lock(f.path+QStringLiteral(".lock"));require(lock.tryLock(0),QStringLiteral("Acquire a competing writer lock"));
        require(!success(f.preferences.apply(mode(f.preferences,2)))&&f.backend.writes==0,QStringLiteral("A competing writer cannot race native or preference changes"));
    }
#ifndef Q_OS_WIN
    {
        Fixture f;const auto linked=f.directory.filePath(QStringLiteral("linked.json"));write(f.path,QByteArray("evidence"));
        require(QFile::link(f.path,linked),QStringLiteral("Create symlink fixture"));ShaderPreferences p;p.load(linked);
        require(!success(p.apply(p.values()))&&bytes(f.path)==QByteArray("evidence"),QStringLiteral("Preference links cannot redirect writes"));
    }
#endif
    {
        NativeDisplayBinding binding;QString error;QObject incomplete;
        require(!binding.bind(&incomplete,&error),QStringLiteral("Incomplete/untyped native contract stays unavailable"));
        auto *backend=new Backend;require(binding.bind(backend,&error)&&binding.bindPlayer(backend,&error),error);delete backend;
        require(!binding.canRestart()&&!binding.restart(&error),QStringLiteral("Destroyed player backend cannot leave a dangling restart invokable"));
        DisplayResolution size;require(!binding.read(&size,&error),QStringLiteral("Destroyed native backend cannot leave a dangling invokable"));
        QThread worker;auto *other=new Backend;QPointer<QObject> tracked=other;other->moveToThread(&worker);
        // The loop can be asked to quit before servicing a queued invokeMethod.
        // Qt's documented finished->deleteLater pattern handles that ordering;
        // do not leak the wrong-thread refusal fixture or suppress LSan.
        QObject::connect(&worker,&QThread::finished,other,&QObject::deleteLater);worker.start();
        require(!binding.bind(other,&error),QStringLiteral("Never invoke native Display methods across threads"));
        worker.quit();require(worker.wait(5000)&&tracked.isNull(),QStringLiteral("The wrong-thread fixture must be destroyed on its owning thread"));
    }
    std::cout<<"PASS: current-size 720/540 honest no-op preview/combined visual+texture Save/reopen/exact flag and odd-dimension preservation/no native write or restart; no upscaling via UI-bypass API; exact reviewed-source/target/custom flag and saved-configuration stamps, stale/no-op-to-write/replay rejection before writes; independent texture-toggle v3 preferences, v1/v2 read-only migration, pre-save restart capability vs finalized-target permission, explicit restart acceptance/refusal/unsaved/changed-target/destruction gates, public typed native resolution adapter, exact restore/conflict handling, no unrelated commits/discards, locking, malformed input, native write/commit/finalization failure and interruption recovery\n";
    std::cout<<"LIMIT: authored native-config contract only; no installed player, restart, game texture LOD, FPS or physical GPU certification\n";
}
#include "shader_preferences_test.moc"
