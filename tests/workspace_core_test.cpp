// Standalone Qt Core/Gui/Network test; never executes an emulator or accesses Discord.
#include "core/CustomizationWorkspace.h"
#include "core/AddonPacks.h"
#include "core/SkinProfiles.h"
#include "core/PlayerSkinCompiler.h"
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
// Minimal structurally valid Qt 5 QV4 compilation unit. matchingUnitOffsets()
// only needs the header fields plus a string table; sourceFileIndex is left
// out of range, exactly like the real MSI/BlueStacks Qt 5 units.
QByteArray qv4Unit(const QStringList &strings)
{
    const int table = 248;
    int cursor = table + strings.size() * 4;
    QList<int> offsets;
    QByteArray blob;
    for (const auto &s : strings) {
        offsets << cursor;
        // Qt 5 QV4 string record: 24-byte QStringData header (length at +4),
        // then UTF-16LE characters. unitString() reads exactly this layout.
        QByteArray record(24, '\0');
        putLe32(record, 4, quint32(s.size()));
        for (const QChar c : s) { record.append(char(c.unicode() & 0xFF)); record.append(char(c.unicode() >> 8)); }
        blob += record;
        cursor += record.size();
    }
    QByteArray unit(cursor, '\0');
    unit.replace(0, 8, QByteArray("qv4cdata", 8));
    putLe32(unit, 8, 0x4A);              // non-zero format version => active
    putLe32(unit, 12, (5u << 16) | 15u); // Qt 5.15
    putLe32(unit, 24, quint32(unit.size()));
    putLe32(unit, 112, quint32(strings.size()));
    putLe32(unit, 116, quint32(table));
    putLe32(unit, 236, quint32(strings.size())); // sourceFileIndex out of range
    for (int i = 0; i < offsets.size(); ++i) putLe32(unit, table + i * 4, quint32(offsets[i]));
    unit.replace(table + strings.size() * 4, blob.size(), blob);
    return unit;
}
QByteArray fixture()
{
    QByteArray b(0x20000, '\0'); b.replace(0,2,"MZ"); putLe32(b,0x3c,0x80); b.replace(0x80,4,QByteArray("PE\0\0",4));
    putLe16(b,0x84,0x8664); putLe16(b,0x86,1); putLe16(b,0x94,0xf0); putLe16(b,0x98,0x20b);
    putLe32(b,0x98+32,0x1000); putLe32(b,0x98+36,0x200); putLe32(b,0x98+56,0x20000); putLe32(b,0x98+60,0x1000); putLe32(b,0x98+108,16);
    b.replace(0x188,8,QByteArray(".rdata\0\0",8)); putLe32(b,0x190,0x1f000); putLe32(b,0x194,0x1000); putLe32(b,0x198,0x1f000); putLe32(b,0x19c,0x1000); putLe32(b,0x1ac,0x40000040);
    b.replace(0x1100,4,"qres"); putBe32(b,0x1104,2); putBe32(b,0x1108,0x7000-0x1100); putBe32(b,0x110c,0x2000-0x1100); putBe32(b,0x1110,0x6000-0x1100);
    const QByteArray theme("import QtQuick\nQtObject { property QtObject colors: QtObject { property color primary100: '#112233'; property color accentNormal: '#168bff' } property QtObject fonts: QtObject { property font bodyMedium: Qt.font({ \"pixelSize\": 13 }) } property QtObject buttons: QtObject { property QtObject sizes: QtObject { property QtObject large: QtObject { property real radius: 0 } } } }\n");
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
    {
        const QStringList ids{"pMaxDropDownHeight","pVisibilityRole","pContentRightPadding"};
        const auto target=qv4Unit(ids);
        const auto imported=qv4Unit({"UiComboBox","pMaxDropDownHeight"});
        const auto substring=qv4Unit({"xpMaxDropDownHeight","xpVisibilityRole","xpContentRightPadding"});
        auto bytes=target+imported+substring;
        check(QtResources::sourceUnits(bytes,{"UiComboBox.qml"},ids)==1,
              "exact source matching requires all full identifiers, not an importing component or substring");
        check(QtResources::sourceUnits(bytes,{"UiComboBox.qml"},ids,true)==1
                  && le32(bytes,8)==0 && le32(bytes,target.size()+8)!=0,
              "precise cache invalidation leaves importing/unrelated QML units active");
        auto named=qv4Unit({":/qml/OtherWindow.qml","pMaxDropDownHeight","pVisibilityRole","pContentRightPadding"});
        putLe32(named,236,0);
        check(QtResources::sourceUnits(named,{"UiComboBox.qml"},ids)==0,
              "a known different source filename never falls back to property identifiers");
        auto duplicate=target+target;
        check(QtResources::sourceUnits(duplicate,{"UiComboBox.qml"},ids)==2,
              "duplicate compiled source identities remain detectable and cannot masquerade as one");
    }
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
    QVariantMap skin{{"schema","bsi.skin"},{"version",1},{"name","My editable skin"},{"base","carbon"},
        {"accent","#aabbcc"},{"surface","#101012"},{"radius",7},{"height",35},{"padding",14},
        {"motion","rise"},{"duration",240},{"iconStyle","rounded"},{"palette",QVariantMap{}},{"tokens",QVariantMap{}},{"icons",QVariantList{}}};
    check(SkinProfiles::validate(skin).value("ok").toBool(),"custom names and geometry are not locked to a built-in pack");
    auto invalidSkin=skin;invalidSkin["radius"]=true;
    check(!SkinProfiles::validate(invalidSkin).value("ok").toBool(),"boolean geometry refused");
    invalidSkin=skin;invalidSkin["script"]="run()";
    check(!SkinProfiles::validate(invalidSkin).value("ok").toBool(),"skin imports refuse scripts and unknown fields");
    invalidSkin=skin;invalidSkin["palette"]=QVariantMap{{"primary100","#fff; bad()"}};
    check(!SkinProfiles::validate(invalidSkin).value("ok").toBool(),"skin palette cannot inject QML expressions");
    invalidSkin=skin;invalidSkin["icons"]=QVariantList{QVariantMap{{"path","../unsafe.svg"},{"glyph","home"}}};
    check(!SkinProfiles::validate(invalidSkin).value("ok").toBool(),"skin SVG mappings reject traversal");
    for(const auto &style:{"line","rounded","bold"})
        check(!SkinProfiles::icon("home",QLatin1String(style),"#aabbcc").isEmpty(),"three safe generated SVG icon styles");
    check(SkinProfiles::icon("unknown","line","#aabbcc").isEmpty(),"unknown glyph refused");
    const QString svgFile=temp.filePath(QStringLiteral("custom.svg"));
    write(svgFile,"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'><path d='M2 2h20v20H2z'/></svg>");
    const auto importedIcon=SkinProfiles::readIcon(svgFile);
    check(importedIcon.value("ok").toBool(),"user static SVG icons are importable");
    const QVariantMap customIcon{{"path","ui/home.svg"},{"svg",importedIcon.value("svg")}};
    check(!SkinProfiles::mappedIcon(customIcon,"line","#aabbcc").isEmpty(),"custom SVG bytes preserved in portable mapping");
    write(svgFile,"<svg xmlns='http://www.w3.org/2000/svg'><script>alert(1)</script></svg>");
    check(!SkinProfiles::readIcon(svgFile).value("ok").toBool(),"SVG scripts refused");
    write(svgFile,"<svg xmlns='http://www.w3.org/2000/svg'><path fill='url(https://invalid.example/icon.svg)'/></svg>");
    check(!SkinProfiles::readIcon(svgFile).value("ok").toBool(),"SVG external references refused");
    write(svgFile,"<!DOCTYPE svg [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><svg xmlns='http://www.w3.org/2000/svg'/>");
    check(!SkinProfiles::readIcon(svgFile).value("ok").toBool(),"SVG DTD/entity declarations refused");
    auto uiSkin=skin;uiSkin["playerUi"]=true;
    check(SkinProfiles::validate(uiSkin).value("ok").toBool(),"strict player GUI deployment is a typed profile option");
    auto badUiSkin=uiSkin;badUiSkin["playerUi"]="true";
    check(!SkinProfiles::validate(badUiSkin).value("ok").toBool(),"string GUI deployment flag refused");
    check(!PlayerSkinCompiler::plan(original,uiSkin,{}).value("ok").toBool(),"incomplete EXE cannot report a full GUI pack success");
    check(!PlayerSkinCompiler::compile("Unknown.qml","Item {}",uiSkin).value("ok").toBool(),"unknown player source refused");
    const QString sourceDir=qEnvironmentVariable("BSI_SKIN_REFERENCE_DIR");
    if(!sourceDir.isEmpty()) {
        for(const auto &leaf:PlayerSkinCompiler::components()) {
            const auto source=read(QDir(sourceDir).filePath(leaf));
            check(!source.isEmpty(),qPrintable("pinned player UI source present: "+leaf));
            for(const auto &id:QStringList{"carbon","amethyst","arctic","matrix","ember","nord"}) {
                auto generatedProfile=uiSkin;generatedProfile["base"]=id;
                generatedProfile["radius"]=AddonPacks::find(id)->radius;
                const auto generated=PlayerSkinCompiler::compile(leaf,source,generatedProfile);
                if(!generated.value("ok").toBool())std::printf("compiler: %s\n",qPrintable(generated.value("msg").toString()));
                check(generated.value("ok").toBool(),qPrintable("compile player component: "+leaf+" / "+id));
                const auto output=generated.value("source").toByteArray();
                if(output.isEmpty())continue;
                auto changed=generatedProfile;changed["radius"]=19;changed["duration"]=0;
                const auto second=PlayerSkinCompiler::compile(leaf,output,changed);
                check(second.value("ok").toBool() && second.value("source").toByteArray().count("BSI_PLAYER_SKIN_V1_BEGIN")==1,
                      "switching packs updates settings without stacking behaviors");
                check(output.count("onClicked:")==source.count("onClicked:") && output.count("onClosing:")==source.count("onClosing:"),
                      "player actions and close handlers survive style compilation");
            }
        }
    }
    const QString skinFile=temp.filePath(QStringLiteral("editable-skin.json"));
    check(SkinProfiles::save(skinFile,skin).value("ok").toBool() && SkinProfiles::read(skinFile).value("profile").toMap()==skin,"custom skin profile save/load round trip");
    check(!SkinProfiles::save(exe,skin).value("ok").toBool() && read(exe)==original,"skin export never overwrites the player");
    const QVariantList customOps{QVariantMap{{"kind","theme"},{"pack","carbon"},{"name","Renamed skin"},
        {"edits",QVariantMap{{"primary100","#101012"}}},
        {"design",QVariantMap{{"UiThemeObject.qml#root.buttons.sizes.large.radius",7}}}}};
    QByteArray customPrepared;
    const auto customDry=W::simulate(exe,customOps,W::digest(original),&customPrepared);
    check(customDry.value("ok").toBool() && read(exe)==original,"custom skin palette and user geometry prepare together without writes");
    bool customReadback=false;
    for(const auto &file:QtResources::registeredFiles(customPrepared)) if(file.name==QLatin1String("UiThemeObject.qml"))
        customReadback=QtResources::readFile(customPrepared,file).contains("property real radius: 7");
    check(customReadback,"user-defined geometry reads back from the real Qt resource tree");
    check(AddonPacks::find(QStringLiteral("unknown")) == nullptr, "unknown add-on refused");
    for (const auto &id : {"carbon", "amethyst", "arctic", "ember", "matrix", "nord"}) {
        const auto *pack = AddonPacks::find(QLatin1String(id));
        check(pack && ThemePresets::byId(QLatin1String(pack->preset)), "pack references a real palette");
        if (!pack) continue;
        const QVariantList packOperations{QVariantMap{{"kind","theme"},{"pack",QLatin1String(id)},
            {"edits",QVariantMap{{"primary100","#0b0b0e"},{"accentNormal","#abcdef"}}}}};
        QByteArray packed;
        const auto dry = W::simulate(exe,packOperations,W::digest(original),&packed);
        if (!dry.value("ok").toBool()) std::printf("pack dry run %s: %s\n",id,qPrintable(dry.value("msg").toString()));
        check(dry.value("ok").toBool() && read(exe)==original,"add-on palette and geometry prepare without writing");
        bool designReadBack=false;
        for (const auto &file : QtResources::registeredFiles(packed)) if (file.name==QLatin1String("UiThemeObject.qml"))
            designReadBack=QtResources::readFile(packed,file).contains("property real radius: "+QByteArray::number(pack->radius));
        check(designReadBack,"add-on radius read back through real Qt resource tree");
        auto conflicting=packOperations;
        conflicting << QVariantMap{{"kind","appearance"},{"edits",QVariantMap{{"UiThemeObject.qml#root.buttons.sizes.large.radius",8}}}};
        check(!W::simulate(exe,conflicting,W::digest(original)).value("ok").toBool(),"add-on conflicts with later same-payload appearance edit");
    }
    check(!W::simulate(exe,{QVariantMap{{"kind","theme"},{"pack","unknown"},{"edits",QVariantMap{{"primary100","#abcdef"}}}}},W::digest(original)).value("ok").toBool(),"unknown pack dry run refused");
    auto appearanceProfile=profile;appearanceProfile["appearance"]=QVariantMap{{"UiThemeObject.qml#root.buttons.sizes.large.radius",8}};
    check(W::validateProfile(appearanceProfile).value("ok").toBool(),"appearance profile section validated");
    bad=appearanceProfile;bad["appearance"]=QVariantMap{{"UiThemeObject.qml#root.buttons.sizes.large.radius",true}};
    check(!W::validateProfile(bad).value("ok").toBool(),"boolean appearance values rejected");
    bad=appearanceProfile;bad["appearance"]=QVariantMap{{"../../offset#root.buttons.sizes.large.radius",8}};
    check(!W::validateProfile(bad).value("ok").toBool(),"unsafe appearance profile keys rejected");
    const auto appearanceInspection=W::inspectAppearance(exe);
    check(appearanceInspection.value("ok").toBool() && appearanceInspection.value("tokens").toList().size()>=2,"appearance tokens inspected without writing");
    const auto appearanceStage=W::stageAppearance(exe,{{"UiThemeObject.qml#root.buttons.sizes.large.radius",8}});
    check(appearanceStage.value("ok").toBool() && appearanceStage.value("operation").toMap().value("kind").toString()=="appearance","appearance edits staged as a logical Workspace operation");
    const QVariantList appearanceOperations{appearanceStage.value("operation")};
    QByteArray appearancePrepared;
    const auto appearanceDry=W::simulate(exe,appearanceOperations,W::digest(original),&appearancePrepared);
    if (!appearanceDry.value("ok").toBool()) std::printf("appearance dry run: %s\n",qPrintable(appearanceDry.value("msg").toString()));
    check(appearanceDry.value("ok").toBool() && !appearancePrepared.isEmpty() && read(exe)==original,"appearance dry run is isolated and verified");
    bool appearanceReadBack=false;
    for (const auto &file : QtResources::registeredFiles(appearancePrepared))
        if (file.name == QLatin1String("UiThemeObject.qml"))
            appearanceReadBack = QtResources::readFile(appearancePrepared,file).contains("property real radius: 8");
    check(appearanceReadBack,"appearance rewrite reads back through the resource tree");
    auto duplicateAppearance=appearanceOperations;duplicateAppearance<<appearanceOperations.first();
    check(!W::simulate(exe,duplicateAppearance,W::digest(original)).value("ok").toBool(),"duplicate appearance operations refused");
    const QString appearanceRawFile=temp.filePath(QStringLiteral("raw-theme.qml"));write(appearanceRawFile,"import QtQuick\nQtObject {}\n");const auto rawTheme=W::stageResource(QStringLiteral("UiThemeObject.qml"),appearanceRawFile);
    QVariantList conflict{appearanceOperations.first(),rawTheme.value("operation")};
    check(!W::simulate(exe,conflict,W::digest(original)).value("ok").toBool(),"appearance and raw replacement of the same payload conflict");
    QVariantList themeConflict{QVariantMap{{"kind","theme"},{"edits",QVariantMap{{"primary100","#abcdef"}}}},appearanceOperations.first()};
    const auto themeConflictResult=W::simulate(exe,themeConflict,W::digest(original));
    check(!themeConflictResult.value("ok").toBool(),"appearance and palette edits of the same theme payload conflict");
    check(themeConflictResult.value("msg").toString().contains(QStringLiteral("conflict"),Qt::CaseInsensitive),
          "relocated theme payload is still recognized as a conflict, not accepted or failed for an unrelated reason");
    auto reversedThemeConflict=themeConflict;std::reverse(reversedThemeConflict.begin(),reversedThemeConflict.end());
    check(!W::simulate(exe,reversedThemeConflict,W::digest(original)).value("ok").toBool(),
          "theme/appearance conflict is independent of operation order");
    {
        // "UiThemeObject" is matched by the writer's filename/stem table but is
        // NOT the precise identifier (pFontFamily). The writer would disable a
        // unit this feature cannot positively attribute, so the edit is refused.
        QByteArray shadowed=original;
        const QByteArray unit=qv4Unit({QStringLiteral("UiThemeObject"),QStringLiteral("someOtherComponent")});
        shadowed.replace(0x9000,unit.size(),unit);
        check(QtResources::matchingUnits(shadowed,{QStringLiteral("UiThemeObject.qml")},{QStringLiteral("pFontFamily")})==1
              && QtResources::matchingUnits(shadowed,{},{QStringLiteral("pFontFamily")})==0,
              "synthetic QV4 unit is matched broadly but not precisely");
        const QString shadowExe=temp.filePath(QStringLiteral("shadowed.exe"));write(shadowExe,shadowed);
        const auto shadowInspect=W::inspectAppearance(shadowExe);
        check(!shadowInspect.value("ok").toBool(),"appearance inspection refuses an imprecise compiled-QML shadow");
        check(!W::stageAppearance(shadowExe,{{"UiThemeObject.qml#root.buttons.sizes.large.radius",8}}).value("ok").toBool(),
              "appearance staging refuses an imprecise compiled-QML shadow");
        const QVariantList shadowOps{QVariantMap{{"kind","appearance"},{"edits",QVariantMap{{"UiThemeObject.qml#root.buttons.sizes.large.radius",8}}}}};
        check(!W::simulate(shadowExe,shadowOps,W::digest(shadowed)).value("ok").toBool() && read(shadowExe)==shadowed,
              "appearance dry run refuses an imprecise compiled-QML shadow without writing");
        // Adding the precise identifier to the same unit makes the shadow safe.
        QByteArray precise=original;
        const QByteArray unit2=qv4Unit({QStringLiteral("UiThemeObject"),QStringLiteral("pFontFamily")});
        precise.replace(0x9000,unit2.size(),unit2);
        const QString preciseExe=temp.filePath(QStringLiteral("precise.exe"));write(preciseExe,precise);
        check(W::inspectAppearance(preciseExe).value("ok").toBool(),"precisely identified compiled shadow still offers tokens");
        QByteArray precisePrepared;
        const auto preciseDry=W::simulate(preciseExe,shadowOps,W::digest(precise),&precisePrepared);
        check(preciseDry.value("ok").toBool() && QtResources::activeUnits(precisePrepared,{},{QStringLiteral("pFontFamily")})==0,
              "precise compiled shadow is invalidated by the verified appearance write");
    }
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
