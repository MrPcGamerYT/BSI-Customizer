// Standalone Qt Core/Gui/Network test; never executes an emulator or accesses Discord.
#include "core/CustomizationWorkspace.h"
#include "core/ThemeEngine.h"
#include "core/EmbeddedResources.h"
#include "core/QtResources.h"
#include "core/ByteUtils.h"
#include <QCoreApplication>
#include <QColor>
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLockFile>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cstdio>

using namespace ByteUtils;
namespace WorkspaceTests {
namespace W = CustomizationWorkspace;
namespace {
int failures = 0;
void check(bool yes, const char *msg)
{
    std::printf("%s: %s\n", yes ? "PASS" : "FAIL", msg);
    if (!yes) {
        ++failures;
        if (qEnvironmentVariableIsSet("GITHUB_ACTIONS"))
            std::printf("::error file=tests/workspace_core_test.cpp::%s\n", msg);
    }
    std::fflush(stdout); // Keep the last assertion visible if a native test crashes.
}
bool write(const QString &p, const QByteArray &b) { QFile f(p); return f.open(QIODevice::WriteOnly) && f.write(b) == b.size(); }
QByteArray read(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; }
quint32 hash(const QString &s) { quint32 h = 0; for (const auto c : s) { h = (h << 4) + c.unicode(); h ^= (h & 0xf0000000) >> 23; h &= 0xfffffff; } return h; }
QByteArray fixture()
{
    QByteArray b(0x20000, '\0'); b.replace(0,2,"MZ"); putLe32(b,0x3c,0x80); b.replace(0x80,4,QByteArray("PE\0\0",4));
    putLe16(b,0x84,0x8664); putLe16(b,0x86,1); putLe16(b,0x94,0xf0); putLe16(b,0x98,0x20b);
    putLe32(b,0x98+32,0x1000); putLe32(b,0x98+36,0x200); putLe32(b,0x98+56,0x20000); putLe32(b,0x98+60,0x1000); putLe32(b,0x98+108,16);
    b.replace(0x188,8,QByteArray(".rdata\0\0",8)); putLe32(b,0x190,0x1f000); putLe32(b,0x194,0x1000); putLe32(b,0x198,0x1f000); putLe32(b,0x19c,0x1000); putLe32(b,0x1ac,0x40000040);
    b.replace(0x1100,4,"qres"); putBe32(b,0x1104,2); putBe32(b,0x1108,0x7000-0x1100); putBe32(b,0x110c,0x2000-0x1100); putBe32(b,0x1110,0x6000-0x1100);
    const QByteArray theme("import QtQuick\nQtObject { property QtObject colors: QtObject { property color primary100: '#112233'; property color accentNormal: '#168bff' } }\n");
    QList<QPair<QString,QByteArray>> files{{QStringLiteral("UiThemeObject.qml"),theme}, {QStringLiteral("panel.qml"),QByteArray("import QtQuick\nItem { width: 12 }\n")}};
    std::sort(files.begin(),files.end(),[](const auto &a,const auto &c){ return hash(a.first)<hash(c.first); });
    putBe16(b,0x7004,2); putBe32(b,0x7006,files.size()); putBe32(b,0x700a,1);
    int name=0x6000,payload=0x2000;
    for (int i=0;i<files.size();++i) {
        const auto &file=files[i]; const auto compressed=qCompress(file.second,9);
        putBe32(b,payload,compressed.size()); b.replace(payload+4,compressed.size(),compressed);
        putBe16(b,name,file.first.size()); putBe32(b,name+2,hash(file.first));
        for(int j=0;j<file.first.size();++j) putBe16(b,name+6+j*2,file.first[j].unicode());
        const int node=0x7000+(i+1)*22; putBe32(b,node,name-0x6000);putBe16(b,node+4,1);putBe16(b,node+8,1);putBe32(b,node+10,payload-0x2000);putBe64(b,node+14,1600000000000ULL);
        name+=6+file.first.size()*2; payload+=4+compressed.size();
    }
    return b;
}
}
int run()
{
    failures = 0;
    QTemporaryDir temp; check(temp.isValid(),"isolated test directory");
    const auto original=fixture(); const QString exe=temp.filePath(QStringLiteral("HD-Player.exe")); write(exe,original);
    auto inspection=W::inspect(exe,{{"primary100","#abcdef"}});
    if (!inspection.value("themeSupported").toBool()) std::printf("inspection: %s\n",qPrintable(inspection.value("msg").toString()));
    check(inspection.value("themeSupported").toBool(),"verified theme layout and capacity inspection");
    QVariantMap profile{{"schema","bsi.customization.profile"},{"version",1},{"name","test"},{"theme",QVariantMap{{"primary100","#abcdef"}}},{"boot",QVariantMap{{"holdMs",3000}}}};
    check(W::validateProfile(profile).value("ok").toBool(),"portable profile validation");
    auto bad=profile;bad["token"]="secret";check(!W::validateProfile(bad).value("ok").toBool(),"credentials and unknown profile fields rejected");
    bad=profile;bad["theme"]=QVariantMap{{"primary100","red\"; malicious()"}};check(!W::validateProfile(bad).value("ok").toBool(),"QML expression injection rejected as a color");
    bad=profile;bad["boot"]=QVariantMap{{"holdMs",16000}};check(!W::validateProfile(bad).value("ok").toBool(),"out-of-range boot profile rejected");
    const QString p=temp.filePath(QStringLiteral("profile.json"));check(W::saveProfile(p,profile).value("ok").toBool()&&W::readProfile(p).value("ok").toBool(),"profile JSON round trip");
    check(!W::saveProfile(exe,profile).value("ok").toBool()&&read(exe)==original,"profile export cannot overwrite an EXE");
    bad=profile;bad["version"]=1.1;check(!W::validateProfile(bad).value("ok").toBool(),"fractional schema versions refused");
    bad=profile;bad["boot"]=QVariantMap{{"holdMs",3.5}};check(!W::validateProfile(bad).value("ok").toBool(),"fractional boot durations refused");
    bad=profile;bad["about"]=QVariantMap{{"links",QVariantList{QVariantMap{{"id","bad"},{"url","file:///C:/Windows/test.exe"}}}}};
    check(!W::validateProfile(bad).value("ok").toBool(),"unsafe About links refused");
    const QString sha=W::digest(original);
    QVariantList operations{QVariantMap{{"kind","theme"},{"edits",QVariantMap{{"primary100","#abcdef"}}}}};
    QByteArray next;const auto dry=W::simulate(exe,operations,sha,&next);
    if (!dry.value("ok").toBool())std::printf("dry run: %s\n",qPrintable(dry.value("msg").toString()));
    check(dry.value("ok").toBool()&&!next.isEmpty(),"full theme dry run verifies a prepared image");
    check(read(exe)==original&&!QFile::exists(exe+QStringLiteral(".bak")),"dry run never writes target or backup");
    const auto loaded = ThemeEngine::detectTheme(original,QStringLiteral("qml"));
    QByteArray namedImage;
    const auto named = ThemeEngine::patchTheme(exe, loaded, ThemeEngine::parseQmlEntries(loaded.qml), {{"primary100","red"}}, {}, &namedImage);
    check(named.ok && ThemeEngine::detectTheme(namedImage,QStringLiteral("qml")).qml.contains("\"#ff0000\"") && read(exe)==original,"named colors are emitted as quoted canonical QML hex");
    auto duplicate=operations;duplicate<<operations.first();check(!W::simulate(exe,duplicate,sha).value("ok").toBool(),"duplicate theme conflict rejected");
    const QString resource=temp.filePath(QStringLiteral("panel.qml"));write(resource,"import QtQuick\nItem { width: 45; height: 90 }\n");
    const auto staged=W::stageResource(QStringLiteral("panel.qml"),resource);check(staged.value("ok").toBool(),"logical resource replacement staged without raw offsets");
    auto combined=operations;combined<<staged.value("operation");const auto combinedDry=W::simulate(exe,combined,sha);
    check(combinedDry.value("ok").toBool()&&read(exe)==original,"combined palette and resource dry run is isolated");
    QThread::msleep(5);
    check(W::simulate(exe,combined,sha).value("outputSha256")==combinedDry.value("outputSha256"),"repeated dry runs have deterministic output despite wall-clock changes");
    const auto root=temp.filePath(QStringLiteral("history"));
    check(!W::apply(exe,operations,sha,QString(64,QLatin1Char('0')),root).value("ok").toBool()&&read(exe)==original,"unreviewed output fingerprint rejected");
    {
        QLockFile lock(exe+QStringLiteral(".bsi-workspace.lock"));
        check(lock.tryLock(0) && !W::apply(exe,combined,sha,combinedDry.value("outputSha256").toString(),root).value("ok").toBool() && read(exe)==original,"concurrent Workspace writer refused before any EXE change");
    }
    const auto applied=W::apply(exe,combined,sha,combinedDry.value("outputSha256").toString(),root);
    if (!applied.value("ok").toBool())std::printf("apply: %s\n",qPrintable(applied.value("msg").toString()));
    check(applied.value("ok").toBool()&&W::digest(read(exe))==combinedDry.value("outputSha256").toString(),"single-EXE batch commit verified");
    check(W::history(root).size()==1,"recovery journal lists the committed batch");
    check(!W::simulate(exe,operations,sha).value("ok").toBool(),"emulator updates invalidate stale fingerprints");
    const QString id=applied.value("id").toString();
    check(!W::restore(exe,QStringLiteral("../escape"),root).value("ok").toBool(),"recovery path traversal rejected");
    check(W::restore(exe,id,root).value("ok").toBool()&&read(exe)==original,"hash-verified recovery restores exact original bytes");
    check(!W::restore(exe,id,root).value("ok").toBool(),"repeat restore refuses a mismatched post-patch hash");
    const auto records=EmbeddedResources::list(original);
    if (records.size() >= 2) {
        const auto first=*std::min_element(records.cbegin(),records.cend(),[](const auto &a,const auto &b){return a.entryOff<b.entryOff;});
        QByteArray bigger=QtResources::readFile(original,{first.leaf,first.entryOff,first.flags})+"\n// ";
        quint32 noise=12345;
        for (int i=0;i<5000;++i) { noise=noise*1664525u+1013904223u; bigger.append(char('A'+((noise>>24)%26))); }
        bigger+='\n';
        QByteArray moved;
        const auto relocated=EmbeddedResources::replaceBytes(exe,first.path,bigger,&moved);
        const auto relocatedRows=EmbeddedResources::list(moved);
        bool foundMoved=false,othersUnchanged=relocated.ok;
        for (const auto &row : relocatedRows) {
            if (row.path==first.path) foundMoved=row.entryOff!=first.entryOff && QtResources::readFile(moved,{row.leaf,row.entryOff,row.flags})==bigger;
            else for (const auto &before : records) if (before.path==row.path)
                othersUnchanged &= QtResources::readFile(moved,{row.leaf,row.entryOff,row.flags})==QtResources::readFile(original,{before.leaf,before.entryOff,before.flags});
        }
        check(relocated.ok&&moved.size()==original.size()&&foundMoved&&othersUnchanged&&read(exe)==original,"mapped-gap relocation preserves other resources and never grows or writes the EXE in preview");
    } else check(false,"fixture exposes resource records for relocation checks");
    QByteArray growth;const auto grown=EmbeddedResources::replaceBytes(exe,QStringLiteral("panel.qml"),QByteArray("import QtQuick\nItem {}\n")+QByteArray(4096,' '),&growth);
    check(grown.ok&&growth.size()==original.size(),"larger decoded resource can fit compressed storage without EXE growth");
    QByteArray probe;const auto theme=ThemeEngine::detectTheme(original,QStringLiteral("qml"));
    const auto invalid=ThemeEngine::patchTheme(exe,theme,ThemeEngine::parseQmlEntries(theme.qml),{{"primary100","not-a-color"}},{},&probe);
    check(!invalid.ok&&read(exe)==original,"invalid theme colors refused before writes");
    const QString report=temp.filePath(QStringLiteral("probe.txt"));check(W::exportDiagnostics(exe,report).value("ok").toBool()&&read(report).contains("Probe complete."),"GUI diagnostic export includes complete mapping report");
    check(!W::exportDiagnostics(exe,exe).value("ok").toBool(),"report export cannot overwrite selected EXE");
    const QString source=temp.filePath(QStringLiteral("source.png")),output=temp.filePath(QStringLiteral("prepared.png"));
    QImage image(20,10,QImage::Format_ARGB32);image.fill(QColor(20,40,60,120));image.save(source);
    const auto prepared=W::prepareImage(source,output,8,8,true,95);
    check(prepared.value("ok").toBool()&&QImage(output).size()==QSize(8,8),"image preparation crops safely and preserves source");
    check(!W::prepareImage(source,temp.filePath(QStringLiteral("lossy.jpg")),8,8,false,90).value("ok").toBool(),"transparent image cannot silently lose alpha");
    check(!W::prepareImage(source,source,8,8,false,95).value("ok").toBool(),"image preparation refuses source overwrite");
    check(!W::inspectVideo(source).value("ok").toBool(),"non-MP4 is not reported as a compatible video");
    std::printf("%s (%d failures)\n",failures?"WORKSPACE TESTS FAILED":"ALL WORKSPACE TESTS PASSED",failures);
    return failures?1:0;
}

} // namespace WorkspaceTests
#ifndef BSI_WORKSPACE_INTEGRATED
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BSI-Workspace-Test"));
    return WorkspaceTests::run();
}
#endif
