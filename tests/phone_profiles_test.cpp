#include "PhoneProfiles.h"
#include "PhoneProfilesInstall.h"
#include "QtResources.h"
#include "PeUtils.h"
#include "phone_profiles_fixture.h"
#include "shaders/settings_image_fixture.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QUrl>
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool value, const QString &why)
{
    if (!value) {std::fprintf(stderr,"FAIL: %s\n",qPrintable(why));std::exit(1);}
}
QByteArray read(const QString &path)
{
    QFile f(path);require(f.open(QIODevice::ReadOnly),QStringLiteral("read ")+path);return f.readAll();
}
void write(const QString &path, const QByteArray &data)
{
    QFile f(path);require(f.open(QIODevice::WriteOnly)&&f.write(data)==data.size(),QStringLiteral("write ")+path);
}
QMap<QString,QByteArray> resources(const QByteArray &image)
{
    QMap<QString,QByteArray> out;
    for(const auto &f:QtResources::registeredFiles(image))out.insert(f.path,QtResources::readFile(image,f));
    return out;
}
QByteArray fixture(const QString &source)
{
    auto image=ShaderSettingsImageFixture::fixture();
    for(const auto &f:QtResources::registeredFiles(image))if(f.name==PhoneProfiles::resourceName()){
        const auto link=QtResources::findLink(image,f.entryOff,f.name);
        QString error;
        const auto destination=QtResources::findFreeSpace(image,link,qCompress(source.toUtf8(),9).size()+4,&error);
        require(destination>=0,QStringLiteral("stage registered Phone fixture: ")+error);
        require(QtResources::replaceResource(image,f.entryOff,link,source.toUtf8(),&error,nullptr,false,destination),error);
        return image;
    }
    require(false,QStringLiteral("fixture Phone resource missing"));return {};
}
void sourceTests()
{
    const QString original=PhoneProfilesFixture::source();
    QSet<QString> names,models;
    require(PhoneProfiles::catalog().size()==35,QStringLiteral("35 bounded Android model entries, retaining the original eight"));
    for(const auto &p:PhoneProfiles::catalog()){
        require(!p.name.isEmpty()&&!p.manufacturer.isEmpty()&&!p.brand.isEmpty()&&!p.model.isEmpty(),QStringLiteral("complete configured manufacturer/brand/model triplet"));
        require(!names.contains(p.name)&&!models.contains(p.model),QStringLiteral("unique names and exact model identities"));names.insert(p.name);models.insert(p.model);
    }
    require(PhoneProfiles::catalog().first().model==QLatin1String("ASUSAI2501A"),QStringLiteral("ROG 9 must use the catalogued model, not a guessed device codename"));
    const QStringList originalModels{QStringLiteral("ASUSAI2501A"), QStringLiteral("ASUS_AI2401_A"),
        QStringLiteral("NX789J"), QStringLiteral("NX769J"), QStringLiteral("CPH2649"), QStringLiteral("CPH2573"),
        QStringLiteral("SM-S938B"), QStringLiteral("SM-S928B")};
    for(int i=0;i<originalModels.size();++i)
        require(PhoneProfiles::catalog().at(i).model==originalModels.at(i),QStringLiteral("preserve all original identities and their dropdown order"));
    const QMap<QString,QStringList> added{
        {QStringLiteral("Infinix GT 50 Pro"), {QStringLiteral("INFINIX"), QStringLiteral("Infinix"), QStringLiteral("Infinix X6891")}},
        {QStringLiteral("Infinix GT 30 Pro"), {QStringLiteral("INFINIX"), QStringLiteral("Infinix"), QStringLiteral("Infinix X6873")}},
        {QStringLiteral("iQOO Neo 10"), {QStringLiteral("vivo"), QStringLiteral("iQOO"), QStringLiteral("I2405")}},
        {QStringLiteral("iQOO 15"), {QStringLiteral("vivo"), QStringLiteral("iQOO"), QStringLiteral("I2501")}},
        {QStringLiteral("iQOO 15R"), {QStringLiteral("vivo"), QStringLiteral("iQOO"), QStringLiteral("I2508")}},
        {QStringLiteral("realme P3 Ultra 5G"), {QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5030")}},
        {QStringLiteral("realme P3 5G"), {QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5070")}},
        {QStringLiteral("realme GT 7"), {QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5061")}},
        {QStringLiteral("realme GT 6T"), {QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX3853")}},
        {QStringLiteral("realme P4s 5G"), {QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5125")}},
        {QStringLiteral("realme GT 8 Pro"), {QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5210")}},
        {QStringLiteral("Redmi K60"), {QStringLiteral("Xiaomi"), QStringLiteral("Redmi"), QStringLiteral("23013RK75C")}},
        {QStringLiteral("OnePlus 7 Pro"), {QStringLiteral("OnePlus"), QStringLiteral("OnePlus"), QStringLiteral("GM1911")}},
        {QStringLiteral("OnePlus 15R"), {QStringLiteral("OnePlus"), QStringLiteral("OnePlus"), QStringLiteral("CPH2767")}},
        {QStringLiteral("OnePlus Nord 5"), {QStringLiteral("OnePlus"), QStringLiteral("OnePlus"), QStringLiteral("CPH2707")}},
        {QStringLiteral("Xiaomi 14 Ultra"), {QStringLiteral("Xiaomi"), QStringLiteral("Xiaomi"), QStringLiteral("24030PN60G")}},
        {QStringLiteral("POCO F5 Pro"), {QStringLiteral("Xiaomi"), QStringLiteral("POCO"), QStringLiteral("23013PC75G")}},
        {QStringLiteral("Samsung Galaxy S22"), {QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S901B")}},
        {QStringLiteral("Samsung Galaxy S22+"), {QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S906B")}},
        {QStringLiteral("Samsung Galaxy S22 Ultra"), {QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S908B")}},
        {QStringLiteral("Samsung Galaxy S23"), {QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S911B")}},
        {QStringLiteral("Samsung Galaxy S23+"), {QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S916B")}},
        {QStringLiteral("Samsung Galaxy S23 Ultra"), {QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S918B")}},
        {QStringLiteral("Samsung Galaxy S24"), {QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S921B")}},
        {QStringLiteral("Samsung Galaxy S24+"), {QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S926B")}},
        {QStringLiteral("vivo X300 Ultra"), {QStringLiteral("vivo"), QStringLiteral("vivo"), QStringLiteral("V2547A")}},
        {QStringLiteral("OPPO Find X9 Ultra"), {QStringLiteral("OPPO"), QStringLiteral("OPPO"), QStringLiteral("CPH2841")}}
    };
    for(auto it=added.cbegin();it!=added.cend();++it){
        bool found=false;
        for(const auto &profile:PhoneProfiles::catalog())if(profile.name==it.key())
            found=QStringList{profile.manufacturer,profile.brand,profile.model}==it.value();
        require(found,QStringLiteral("requested Android identity and case-sensitive manufacturer/brand/model: ")+it.key());
    }
    for(const auto &base:{original,QString(original).replace(QLatin1String("\n"),QLatin1String("\r\n")),QStringLiteral("// unrelated user comment\n")+original}){
        const auto add=PhoneProfiles::prepareSource(base,true);require(add.ok,add.error);
        const auto repeat=PhoneProfiles::prepareSource(add.output,true);require(repeat.ok&&repeat.output==add.output,QStringLiteral("idempotent source add: ")+repeat.error);
        const auto remove=PhoneProfiles::prepareSource(add.output,false);require(remove.ok&&remove.output==base,QStringLiteral("byte-exact native source restore: ")+remove.error);
        const auto absent=PhoneProfiles::prepareSource(base,false);require(absent.ok&&absent.output==base,QStringLiteral("absent removal no-op"));
        auto damaged=add.output;damaged.replace(QStringLiteral("property var bsiPhoneState:null"),QStringLiteral("property var bsiPhoneState:undefined"));
        require(!PhoneProfiles::prepareSource(damaged,false).ok&&!PhoneProfiles::prepareSource(damaged,true).ok,QStringLiteral("edited helpers cannot be overwritten or called restored"));
        damaged=add.output;damaged.replace(QStringLiteral("bsiPhoneApi().activate()/*BSI-PHONE-END:activate*/"),QStringLiteral("bsiPhoneApi().activate(3)/*BSI-PHONE-END:activate*/"));
        require(!PhoneProfiles::prepareSource(damaged,false).ok,QStringLiteral("edited owned callback refused"));
        const auto later=PhoneProfiles::prepareSource(add.output+QStringLiteral("\n// later unrelated edit"),false);
        require(later.ok&&later.output==base+QStringLiteral("\n// later unrelated edit"),QStringLiteral("remove retains later unrelated edits"));
    }
    auto invalid=original;invalid.replace(QStringLiteral("fMonitorChange"),QStringLiteral("fUnknownMonitor"));
    require(!PhoneProfiles::prepareSource(invalid,true).ok,QStringLiteral("unknown native save contract refused"));
    invalid=original;invalid.replace(QStringLiteral("id:iDeviceModelTxt"),QStringLiteral("id:anotherModel"));
    require(!PhoneProfiles::prepareSource(invalid,true).ok,QStringLiteral("missing native input control refused"));
    require(!PhoneProfiles::prepareSource(original+QStringLiteral("\n// BSI-PHONE partial"),true).ok,QStringLiteral("partial recovery refuses safely"));
    for (const auto &url : {QStringLiteral("https://example.invalid/phone.js"), QStringLiteral("file://server/phone.js"),
                            QStringLiteral("file:relative.js"), QStringLiteral("file:////server/phone.js"),
                            QStringLiteral("file:///%5c%5cserver/phone.js"), QStringLiteral("file:///phone%00.js"),
                            QStringLiteral("file:///phone.js?query"),
                            QStringLiteral("file:///phone.js#fragment"), QStringLiteral("file:///phone\".js"),
                            QStringLiteral("file:///phone\n.js")})
        require(!PhoneProfiles::prepareSource(original,true,url).ok,QStringLiteral("unsafe/remote/relative helper URL refused: ")+url);
}
void imageCycle(const QByteArray &image, const QString &url = QStringLiteral("file:///BSI-PhoneProfiles.js"))
{
    const auto before=resources(image);
    QByteArray baseline;
    for(const auto &f:QtResources::registeredFiles(image))if(f.name==PhoneProfiles::resourceName())baseline=QtResources::readFile(image,f);
    const auto installed=PhoneProfiles::prepare(image,true,url);
    require(installed.ok&&installed.changed&&installed.installed&&installed.output.size()==image.size(),QStringLiteral("bounded native image add: ")+installed.error);
    require(PhoneProfiles::inspect(installed.output).applied,QStringLiteral("installed Phone source is active"));
    require(!PhoneProfiles::prepare(installed.output,false,url).ok,QStringLiteral("no removal without external original-source snapshot"));
    require(!PhoneProfiles::prepare(installed.output,false,url,baseline+"// damage").ok,QStringLiteral("wrong original-source snapshot refused"));
    require(!PhoneProfiles::prepare(installed.output,true,url+QStringLiteral("-changed"),baseline).ok,QStringLiteral("installed local helper path cannot be changed silently"));
    const auto repeated=PhoneProfiles::prepare(installed.output,true,url,baseline);
    require(repeated.ok&&!repeated.changed&&repeated.output==installed.output,QStringLiteral("image add never churns timestamps: ")+repeated.error);
    const auto after=resources(installed.output);
    require(after.size()==before.size(),QStringLiteral("no registered resource added or lost"));
    for(auto i=before.cbegin();i!=before.cend();++i)if(!i.key().endsWith(PhoneProfiles::resourceName(),Qt::CaseInsensitive))
        require(after.value(i.key())==i.value(),QStringLiteral("unrelated resource changed: ")+i.key());
    require(PeUtils::importedModules(image).modules==PeUtils::importedModules(installed.output).modules,QStringLiteral("no DLL/import/renderer changes"));
    const auto restored=PhoneProfiles::prepare(installed.output,false,url,baseline);
    require(restored.ok&&restored.changed&&!restored.installed&&restored.output.size()==image.size()&&resources(restored.output)==before,QStringLiteral("exact decoded-resource restore: ")+restored.error);
}
QByteArray hash(const QByteArray &data)
{
    return QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex();
}
QString windowsRuntimeUrl()
{
    // Two independent nontrivial SHA-256 components and a percent-encoded user
    // name, as in the actual content-addressed Windows installation path.
    return QStringLiteral("file:///C:/Users/Firstname%20Lastname/AppData/Local/BSI%20Customizer/phone-profiles/")
        +QString::fromLatin1(hash("C:/Program Files/BlueStacks_nxt/HD-Player.exe"))
        +QStringLiteral("/PhoneProfiles-")+QString::fromLatin1(hash(PhoneProfiles::runtimeJavascript()))+QStringLiteral(".js");
}
void diskCycle(const QByteArray &image,bool negatives)
{
    QTemporaryDir directory;
    require(directory.isValid(),QStringLiteral("disposable EXE directory"));
    const QString exe=directory.filePath(QStringLiteral("HD-Player.exe"));
    write(exe,image);
    const QString root=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(
        QStringLiteral("phone-profiles/")+QString::fromLatin1(hash(QFileInfo(exe).canonicalFilePath().toUtf8())));
    require(!QFileInfo::exists(root),QStringLiteral("fresh per-target recovery directory"));
    const QString snapshot=QDir(root).filePath(QStringLiteral("recovery.json"));
    const QString helper=QDir(root).filePath(QStringLiteral("PhoneProfiles-%1.js").arg(QString::fromLatin1(hash(PhoneProfiles::runtimeJavascript()))));
    const QString url=QUrl::fromLocalFile(helper).toString(QUrl::FullyEncoded);
    {
        QLockFile lock(exe+QStringLiteral(".bsi-phone-profiles.lock"));
        require(lock.tryLock(),QStringLiteral("hold disposable writer lock"));
        require(!PhoneProfilesInstall::apply(exe,true).ok&&read(exe)==image&&!QFileInfo::exists(root),QStringLiteral("held lock refuses before helper/EXE writes"));
    }
    if(negatives){
        require(QDir().mkpath(exe+QStringLiteral(".bak")),QStringLiteral("stage failing backup directory"));
        const auto failed=PhoneProfilesInstall::apply(exe,true);
        require(!failed.ok&&read(exe)==image&&QFileInfo::exists(snapshot),QStringLiteral("backup I/O refusal leaves EXE intact and recovery intact"));
        require(QDir().rmdir(exe+QStringLiteral(".bak")),QStringLiteral("remove failing backup directory"));
    }
    auto result=PhoneProfilesInstall::apply(exe,true);
    require(result.ok,QStringLiteral("real installer on disposable EXE with its actual helper URL (%1 chars): ").arg(url.size())+result.msg);
    const auto installed=read(exe),record=read(snapshot);
    require(installed.size()==image.size()&&PhoneProfiles::inspect(installed).applied,QStringLiteral("disk install exact readback/no growth"));
    require(read(exe+QStringLiteral(".bak"))==image&&read(helper)==PhoneProfiles::runtimeJavascript(),QStringLiteral("verified original backup and exact local helper"));
    const auto decoded=QJsonDocument::fromJson(record).object();
    const auto original=QByteArray::fromBase64(decoded.value(QStringLiteral("original")).toString().toLatin1());
    const auto plan=PhoneProfiles::prepare(image,true,url);
    require(plan.ok&&installed==plan.output&&original==plan.beforeSource,QStringLiteral("disk planner and exact native snapshot agree"));
    const auto time=QFileInfo(exe).lastModified(),snapshotTime=QFileInfo(snapshot).lastModified();
    result=PhoneProfilesInstall::apply(exe,true);
    require(result.ok&&read(exe)==installed&&QFileInfo(exe).lastModified()==time&&read(snapshot)==record&&QFileInfo(snapshot).lastModified()==snapshotTime,
            QStringLiteral("repeated install is an EXE/snapshot timestamp-preserving no-op: ")+result.msg);
    if(negatives){
        require(QFile::remove(snapshot),QStringLiteral("stage missing snapshot"));
        require(!PhoneProfilesInstall::apply(exe,false).ok&&!PhoneProfilesInstall::apply(exe,true).ok&&read(exe)==installed&&!QFileInfo::exists(snapshot),
                QStringLiteral("missing snapshot refuses both operations without guessing source"));
        for(const auto &key:{QStringLiteral("schema"),QStringLiteral("target"),QStringLiteral("original"),QStringLiteral("originalSha256"),QStringLiteral("patchedSha256"),QStringLiteral("helperSha256")}){
            auto damaged=decoded;damaged.insert(key,QStringLiteral("not-the-reviewed-value"));
            const auto bytes=QJsonDocument(damaged).toJson(QJsonDocument::Compact);write(snapshot,bytes);
            require(!PhoneProfilesInstall::apply(exe,false).ok&&!PhoneProfilesInstall::apply(exe,true).ok&&read(exe)==installed&&read(snapshot)==bytes,
                    QStringLiteral("tampered snapshot refused without overwrite: ")+key);
        }
        write(snapshot,record);
        write(helper,QByteArray("// unknown local script, never execute this\n"));
        require(!PhoneProfilesInstall::apply(exe,true).ok&&read(exe)==installed&&read(helper)!=PhoneProfiles::runtimeJavascript(),QStringLiteral("modified checksum-named helper is not adopted or overwritten"));
    }
    result=PhoneProfilesInstall::apply(exe,false);
    require(result.ok,QStringLiteral("real installer restores even when helper is missing/edited: ")+result.msg);
    require(resources(read(exe))==resources(image)&&read(exe).size()==image.size()&&!QFileInfo::exists(snapshot),QStringLiteral("disk removal restores exact decoded original resources without EXE growth"));
    require(QFileInfo::exists(helper)&&read(exe+QStringLiteral(".bak"))==image,QStringLiteral("removal keeps existing backup/helper for patched copies"));
    const auto restored=read(exe);
    const auto restoredTime=QFileInfo(exe).lastModified();
    require(PhoneProfilesInstall::apply(exe,false).ok&&read(exe)==restored&&QFileInfo(exe).lastModified()==restoredTime,QStringLiteral("stock removal no-op"));
    if(negatives){
        require(!PhoneProfilesInstall::apply(exe,true).ok&&read(exe)==restored,QStringLiteral("unknown retained helper still blocks a new installation"));
        require(QFile::remove(helper),QStringLiteral("remove disposable damaged helper"));
        require(PhoneProfilesInstall::apply(exe,true).ok,QStringLiteral("retry safely publishes a missing helper"));
        require(QFile::remove(helper)&&PhoneProfilesInstall::apply(exe,false).ok,QStringLiteral("source recovery does not require executing or keeping a missing helper"));
#ifndef Q_OS_WIN
        const QString linked=directory.filePath(QStringLiteral("linked-player.exe"));
        require(QFile::link(exe,linked)&&!PhoneProfilesInstall::apply(linked,true).ok,QStringLiteral("linked EXE is refused"));
        const QString linkTarget=directory.filePath(QStringLiteral("linked-helper-target.js"));write(linkTarget,QByteArray("unchanged\n"));
        require(QFile::link(linkTarget,helper)&&!PhoneProfilesInstall::apply(exe,true).ok&&read(linkTarget)=="unchanged\n",QStringLiteral("linked helper is refused without touching its target"));
#endif
    }
    require(QDir(root).removeRecursively(),QStringLiteral("clean only this test's per-target data"));
}
void catalogUpdateCycle(const QByteArray &image)
{
    QTemporaryDir directory;require(directory.isValid(),QStringLiteral("disposable catalog-update EXE directory"));
    const QString exe=directory.filePath(QStringLiteral("HD-Player.exe"));write(exe,image);
    const QString canonical=QFileInfo(exe).canonicalFilePath();
    const QString root=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(
        QStringLiteral("phone-profiles/")+QString::fromLatin1(hash(canonical.toUtf8())));
    require(QDir().mkpath(root),QStringLiteral("stage only this test's recovery folder"));
    const QByteArray oldScript=QByteArray("// authored prior-catalog fixture, never executed\n")+PhoneProfiles::runtimeJavascript();
    const QString oldHelper=QDir(root).filePath(QStringLiteral("PhoneProfiles-%1.js").arg(QString::fromLatin1(hash(oldScript))));
    const QString oldUrl=QUrl::fromLocalFile(oldHelper).toString(QUrl::FullyEncoded);
    const QString snapshot=QDir(root).filePath(QStringLiteral("recovery.json"));
    const auto old=PhoneProfiles::prepare(image,true,oldUrl);require(old.ok,QStringLiteral("prepare a prior immutable helper URL: ")+old.error);
    const QJsonObject record{{QStringLiteral("schema"),QStringLiteral("bsi.phone-profiles.v1")},
        {QStringLiteral("target"),canonical},{QStringLiteral("original"),QString::fromLatin1(old.beforeSource.toBase64())},
        {QStringLiteral("originalSha256"),QString::fromLatin1(hash(old.beforeSource))},
        {QStringLiteral("patchedSha256"),QString::fromLatin1(hash(old.afterSource))},
        {QStringLiteral("helperSha256"),QString::fromLatin1(hash(oldScript))}};
    write(exe,old.output);write(oldHelper,oldScript);write(snapshot,QJsonDocument(record).toJson(QJsonDocument::Compact));
    // Fail after staging the two-generation record, before replacing the EXE.
    // Removal/retry must still accept ONLY the exact old import/native bytes.
    require(QDir().mkpath(exe+QStringLiteral(".bak")),QStringLiteral("hold an invalid backup destination during update"));
    require(!PhoneProfilesInstall::apply(exe,true).ok&&read(exe)==old.output,QStringLiteral("failed catalog update preserves the current player"));
    auto staged=QJsonDocument::fromJson(read(snapshot)).object();
    require(staged.value(QStringLiteral("previousHelperSha256")).toString().toLatin1()==hash(oldScript),QStringLiteral("failed update keeps exact previous helper evidence"));
    require(QDir().rmdir(exe+QStringLiteral(".bak")),QStringLiteral("remove disposable backup failure before testing recovery"));
    auto tampered=staged;tampered.insert(QStringLiteral("previousPatchedSha256"),QStringLiteral("invalid"));
    write(snapshot,QJsonDocument(tampered).toJson(QJsonDocument::Compact));
    require(!PhoneProfilesInstall::apply(exe,false).ok&&read(exe)==old.output,QStringLiteral("tampered previous-generation recovery cannot authorize removal"));
    write(snapshot,QJsonDocument(staged).toJson(QJsonDocument::Compact));
    const auto recovered=PhoneProfilesInstall::apply(exe,false);
    require(recovered.ok&&resources(read(exe))==resources(image),QStringLiteral("failed update remains exactly removable with the newer build: ")+recovered.msg);
    // Restore the authored old state and complete a real one-write update.
    write(exe,old.output);write(snapshot,QJsonDocument(record).toJson(QJsonDocument::Compact));
    const auto updated=PhoneProfilesInstall::apply(exe,true);
    require(updated.ok,QStringLiteral("verified old catalog migrates to the new content-addressed helper: ")+updated.msg);
    require(read(exe).size()==image.size()&&PhoneProfiles::inspect(read(exe)).applied&&read(oldHelper)==oldScript,
            QStringLiteral("catalog update neither grows the EXE nor rewrites/deletes the previous immutable helper"));
    require(read(exe+QStringLiteral(".bak"))==old.output,QStringLiteral("existing installed EXE is backed up, not a transient stock intermediate"));
    const auto after=read(exe);
    require(PhoneProfilesInstall::apply(exe,true).ok&&read(exe)==after,QStringLiteral("updated catalog reapply is an EXE no-op"));
    require(PhoneProfilesInstall::apply(exe,false).ok&&resources(read(exe))==resources(image),QStringLiteral("updated catalog restores the original native source, not the prior import"));
    require(QDir(root).removeRecursively(),QStringLiteral("clean test-only catalog-update recovery"));
}
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    QCoreApplication::setApplicationName(QStringLiteral("BSI-Phone-contract-")+QUuid::createUuid().toString(QUuid::WithoutBraces));
    QStandardPaths::setTestModeEnabled(true);
    const QString testData=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    require(!QFileInfo::exists(testData),QStringLiteral("unique test-only application-data directory"));
    sourceTests();imageCycle(fixture(PhoneProfilesFixture::source()),windowsRuntimeUrl());
    diskCycle(fixture(PhoneProfilesFixture::source()),true);
    catalogUpdateCycle(fixture(PhoneProfilesFixture::source()));
    imageCycle(fixture(QString(QChar(0xfeff))+PhoneProfilesFixture::source()));
    const auto args=app.arguments();
    if(args.size()==3&&args.at(1)==QLatin1String("--export")){
        const QDir dir(args.at(2));require(QDir().mkpath(dir.absolutePath()),QStringLiteral("fixture output directory"));
        const auto url=QUrl::fromLocalFile(dir.absoluteFilePath(QStringLiteral("PhoneRuntime.js"))).toString(QUrl::FullyEncoded);
        const auto plan=PhoneProfiles::prepareSource(PhoneProfilesFixture::source(),true,url);require(plan.ok,plan.error);
        write(dir.filePath(QStringLiteral("PhoneRuntime.js")),PhoneProfiles::runtimeJavascript());
        write(dir.filePath(QStringLiteral("Phone.qml")),PhoneProfiles::compactPreparedSource(plan.output).toUtf8());
        write(dir.filePath(QStringLiteral("StockPhone.qml")),PhoneProfilesFixture::source().toUtf8());
        write(dir.filePath(QStringLiteral("UiComboBox.qml")),QByteArray("import QtQuick.Controls 2.15\nComboBox{textRole:'name'}\n"));
        QJsonArray catalog;
        for(const auto &p:PhoneProfiles::catalog())catalog.append(QJsonObject{{QStringLiteral("name"),p.name},{QStringLiteral("manufacturer"),p.manufacturer},{QStringLiteral("brand"),p.brand},{QStringLiteral("model"),p.model}});
        write(dir.filePath(QStringLiteral("catalog.json")),QJsonDocument(catalog).toJson());
    }else if(args.size()>2&&(args.at(1)==QLatin1String("--references")||args.at(1)==QLatin1String("--disk-references"))){
        for(int i=2;i<args.size();++i){
            const auto original=read(args.at(i));
            const auto state=PhoneProfiles::inspect(original);
            require(state.supported,QStringLiteral("reference shape: ")+args.at(i)+QLatin1Char(' ')+state.detail);
            if(args.at(1)==QLatin1String("--disk-references"))diskCycle(original,false);
            else imageCycle(original,windowsRuntimeUrl());
            require(read(args.at(i))==original,QStringLiteral("reference must remain read-only"));
            std::printf("PASS: read-only reference %s\n",qPrintable(args.at(i)));
        }
    }
    require(!QFileInfo::exists(testData)||QDir(testData).removeRecursively(),QStringLiteral("clean unique test-only application data"));
    std::puts("PASS: 35 distinct Android model presets, stock-control source extension, strict contract/tamper/URL refusal, realistic helper URLs, missing/edited snapshot and helper refusal, writer locking, verified disk backup/install/removal/retry, idempotence, no EXE growth/import changes, unrelated resources preserved and exact decoded native-source removal");
    std::puts("LIMIT: source/resource contract tests, not an installed emulator, guest Android identity or per-game 90/120/144-FPS certification");
}
