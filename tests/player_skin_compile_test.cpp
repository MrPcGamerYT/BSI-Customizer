// Fast, data-only source-contract test. Link with --gc-sections so the EXE
// resource planner/writer is tested by the native engine suite, not stubbed here.
#include "core/PlayerSkinCompiler.h"
#include "core/AddonPacks.h"
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
        QDir output(QString::fromLocal8Bit(argv[2])+"/"+id);QDir().mkpath(output.path());
        for(const auto &leaf:PlayerSkinCompiler::components()) {
            QFile f(QDir(QString::fromLocal8Bit(argv[1])).filePath(leaf));
            if(!f.open(QIODevice::ReadOnly)){std::printf("Missing source: %s\n",qPrintable(leaf));++failures;continue;}
            const auto source=f.readAll();
            const auto r=PlayerSkinCompiler::compile(leaf,source,profile);
            if(!r.value("ok").toBool()) {
                std::printf("FAIL %s / %s: %s\n",qPrintable(id),qPrintable(leaf),qPrintable(r.value("msg").toString()));++failures;continue;
            }
            const auto bytes=r.value("source").toByteArray();
            QFile target(output.filePath(leaf));
            if(!target.open(QIODevice::WriteOnly)||target.write(bytes)!=bytes.size())++failures;
            auto next=profile;next["radius"]=23;next["duration"]=0;
            const auto twice=PlayerSkinCompiler::compile(leaf,bytes,next);
            if(!twice.value("ok").toBool() || twice.value("source").toByteArray().count("BSI_PLAYER_SKIN_V1_BEGIN")!=1)++failures;
            if(bytes.count("onClicked:")!=source.count("onClicked:") || bytes.count("onClosing:")!=source.count("onClosing:"))++failures;
            std::printf("PASS %s / %s\n",qPrintable(id),qPrintable(leaf));
        }
    }
    return failures?1:0;
}
