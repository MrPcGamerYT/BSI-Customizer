// Fast, data-only source-contract test. Link with --gc-sections so the EXE
// resource planner/writer is tested by the native engine suite, not stubbed here.
#include "core/PlayerSkinCompiler.h"
#include "core/AddonPacks.h"
#include "core/EmulatorUiStudio.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cstdio>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    if(argc!=3)return 2;
    int failures=0;
    for(const auto &id:QStringList{"carbon","amethyst","arctic","matrix","ember","nord"}) {
        const auto *pack=AddonPacks::find(id);
        QVariantMap profile{{"schema","bsi.skin"},{"version",1},{"name",id},{"base",id},
            {"accent","#aabbcc"},{"surface","#101012"},{"radius",pack->radius},{"height",pack->height},
            {"padding",pack->padding},{"motion","fade"},{"duration",180},{"iconStyle","rounded"},{"playerUi",true}};
        const QStringList motions{"none","fade","rise","slide","scale","rise"};
        profile["motion"]=motions[QStringList{"carbon","amethyst","arctic","matrix","ember","nord"}.indexOf(id)];
        const QString studioRoot=QString::fromLocal8Bit(argv[2])+"-studio/"+id;
        const auto exported=EmulatorUiStudio::publish(studioRoot+"/Assets with ' quote",profile);
        if(!exported.value("ok").toBool()){std::printf("Studio export: %s\n",qPrintable(exported.value("msg").toString()));return 1;}
        const QString directory=exported.value("directory").toString();
        if(!EmulatorUiStudio::verify(directory,exported.value("hashes").toMap()) ||
           EmulatorUiStudio::publish(studioRoot+"/Assets with ' quote",profile).value("directory").toString()!=directory)++failures;
        QDir studioOutput(studioRoot+"/native");QDir().mkpath(studioOutput.path());
        QDir output(QString::fromLocal8Bit(argv[2])+"/"+id);QDir().mkpath(output.path());
        for(const auto &leaf:PlayerSkinCompiler::components()) {
            QFile f(QDir(QString::fromLocal8Bit(argv[1])).filePath(leaf));
            if(!f.open(QIODevice::ReadOnly)){std::printf("Missing source: %s\n",qPrintable(leaf));++failures;continue;}
            const auto source=f.readAll();
            const auto r=PlayerSkinCompiler::compile(leaf,source,profile);
            if(!r.value("ok").toBool()) {
                std::printf("::error::FAIL %s / %s: %s\n",qPrintable(id),qPrintable(leaf),qPrintable(r.value("msg").toString()));++failures;continue;
            }
            const auto bytes=r.value("source").toByteArray();
            QFile target(output.filePath(leaf));
            if(!target.open(QIODevice::WriteOnly)||target.write(bytes)!=bytes.size())++failures;
            const auto hooked=EmulatorUiStudio::hook(leaf,bytes,directory);
            if(!hooked.value("ok").toBool()) {
                std::printf("::error::Studio hook %s: %s\n",qPrintable(leaf),qPrintable(hooked.value("msg").toString()));++failures;
            } else {
                const auto connected=hooked.value("source").toByteArray();
                QFile targetStudio(studioOutput.filePath(leaf));
                if(!targetStudio.open(QIODevice::WriteOnly)||targetStudio.write(connected)!=connected.size())++failures;
                if(connected.count("onClicked:")!=source.count("onClicked:") || connected.count("onClosing:")!=source.count("onClosing:"))++failures;
                auto changed=profile;changed["radius"]=21;
                const auto recompiled=PlayerSkinCompiler::compile(leaf,connected,changed);
                const auto rehooked=EmulatorUiStudio::hook(leaf,recompiled.value("source").toByteArray(),directory+"/new");
                const auto updated=rehooked.value("source").toByteArray();
                if(!recompiled.value("ok").toBool()||!rehooked.value("ok").toBool()||updated.count("BSI_STUDIO_HOOK_BEGIN")!=1||updated.count("BSIStudio.Theme.")!=4)++failures;
                if(EmulatorUiStudio::hook(leaf,"Item {}",directory).value("ok").toBool())++failures;
            }
            auto next=profile;next["radius"]=23;next["duration"]=0;
            const auto twice=PlayerSkinCompiler::compile(leaf,bytes,next);
            if(!twice.value("ok").toBool() || twice.value("source").toByteArray().count("BSI_PLAYER_SKIN_V1_BEGIN")!=1)++failures;
            if(bytes.count("onClicked:")!=source.count("onClicked:") || bytes.count("onClosing:")!=source.count("onClosing:"))++failures;
            std::printf("PASS %s / %s\n",qPrintable(id),qPrintable(leaf));
        }
    }
    const QString safety=QString::fromLocal8Bit(argv[2])+"-studio/safety";
    QVariantMap safe{{"schema","bsi.skin"},{"version",1},{"name","Safety"},{"base","carbon"},{"accent","#aabbcc"},{"surface","#101012"},{"radius",8},{"height",32},{"padding",12},{"motion","none"},{"duration",0},{"iconStyle","line"}};
    const auto saved=EmulatorUiStudio::publish(safety,safe);
    if(!saved.value("ok").toBool())return 1;
    QFile tamper(QDir(saved.value("directory").toString()).filePath("Theme.qml"));
    if(!tamper.open(QIODevice::Append))return 1;
    tamper.write("// changed");tamper.close();
    if(EmulatorUiStudio::verify(saved.value("directory").toString(),saved.value("hashes").toMap())||EmulatorUiStudio::publish(safety,safe).value("ok").toBool())++failures;
    auto invalid=safe;invalid["accent"]="'; Qt.quit(); //";
    if(!EmulatorUiStudio::files(invalid).isEmpty()||EmulatorUiStudio::publish(safety,invalid).value("ok").toBool()||EmulatorUiStudio::publish("",safe).value("ok").toBool())++failures;
    return failures?1:0;
}
