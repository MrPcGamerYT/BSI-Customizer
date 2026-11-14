#include "GifRpc.h"
#include "PeUtils.h"
#include "ByteUtils.h"
#include "BsDetector.h"
#include "PatchFile.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QSettings>
#include <QRegularExpression>
#include <QUrl>
#include <QSet>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace GifRpc {
namespace {
QVariantMap reply(bool ok,const QString &msg) {return {{QStringLiteral("ok"),ok},{QStringLiteral("msg"),msg}};}
QByteArray read(const QString &path) {QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
bool write(const QString &path,const QByteArray &bytes) {
    QSaveFile file(path);file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size()&&file.commit();
}
QString pathFor(const QString &exe,const QString &name) {return QDir(QFileInfo(exe).absolutePath()).filePath(name);}
QString targetError(const QString &exe) {
    const QFileInfo info(exe);
    if(!info.isFile()||info.fileName().compare(QLatin1String("HD-Player.exe"),Qt::CaseInsensitive)!=0)
        return QStringLiteral("Select the HD-Player.exe installation, not a Windows DLL or the customizer.");
    if(info.isSymLink()||info.isJunction()||QFileInfo(info.absolutePath()).isSymLink()||QFileInfo(info.absolutePath()).isJunction())
        return QStringLiteral("Linked executable/install folders are not supported.");
    const QString windows=qEnvironmentVariable("SystemRoot");
    if(!windows.isEmpty()&&(info.absoluteFilePath().startsWith(QDir(windows).absolutePath()+QLatin1Char('/'),Qt::CaseInsensitive)))
        return QStringLiteral("Windows system directories are never GIF RPC deployment targets.");
    const auto data=read(exe);
    if(PeUtils::sections(data).isEmpty()||PeUtils::machine(data)!=0x8664||!PeUtils::isPe32Plus(data))
        return QStringLiteral("This GIF RPC build requires a valid Windows x64 HD-Player.exe. Other architectures are refused.");
    for(const auto &name:{QStringLiteral("version.dll"),QStringLiteral("bsi-gif-rpc.ini"),QStringLiteral("version.dll.bsi-gif-rpc.bak")}) {
        const QFileInfo file(pathFor(exe,name));
        if(file.isSymLink()||file.isJunction())return QStringLiteral("Linked RPC files are not overwritten.");
    }
    return {};
}
const QStringList fields={QStringLiteral("State"),QStringLiteral("LargeImage"),QStringLiteral("LargeText"),QStringLiteral("SmallImage"),QStringLiteral("SmallText"),QStringLiteral("Button1Label"),QStringLiteral("Button1Url"),QStringLiteral("Button2Label"),QStringLiteral("Button2Url")};
QStringList details(const QVariantMap &s) {return s.value(QStringLiteral("Details")).toString().split(QLatin1Char('\n'),Qt::SkipEmptyParts);}
QByteArray encode(const QVariantMap &s) {
    QByteArray out="[RPC]\nSchema=1\nEnabled="+QByteArray(s.value(QStringLiteral("Enabled"),true).toBool()?"1":"0")
        +"\nAppId="+s.value(QStringLiteral("AppId")).toString().toLatin1()+"\nTimestamp="
        +QByteArray(s.value(QStringLiteral("Timestamp"),true).toBool()?"1":"0")+"\nInterval="
        +QByteArray::number(s.value(QStringLiteral("Interval"),5).toInt())+'\n';
    for(const auto &key:fields)out+=key.toLatin1()+'='+s.value(key).toString().toUtf8().toHex()+'\n';
    const auto rows=details(s);out+="DetailCount="+QByteArray::number(rows.size())+'\n';
    for(int i=0;i<rows.size();++i)out+="Detail"+QByteArray::number(i)+'='+rows[i].toUtf8().toHex()+'\n';
    return out;
}
QVariantMap settings(const QString &path) {
    QSettings file(path,QSettings::IniFormat);file.beginGroup(QStringLiteral("RPC"));QVariantMap s;
    s[QStringLiteral("AppId")]=file.value(QStringLiteral("AppId")).toString();
    s[QStringLiteral("Enabled")]=file.value(QStringLiteral("Enabled"),false).toBool();
    s[QStringLiteral("Timestamp")]=file.value(QStringLiteral("Timestamp"),true).toBool();
    s[QStringLiteral("Interval")]=file.value(QStringLiteral("Interval"),5).toInt();
    for(const auto &key:fields)s[key]=QString::fromUtf8(QByteArray::fromHex(file.value(key).toByteArray()));
    QStringList rows;for(int i=0;i<qBound(0,file.value(QStringLiteral("DetailCount"),0).toInt(),10);++i)
        rows<<QString::fromUtf8(QByteArray::fromHex(file.value(QStringLiteral("Detail%1").arg(i)).toByteArray()));
    s[QStringLiteral("Details")]=rows.join(QLatin1Char('\n'));return s;
}
}
QByteArray bundledProxy() {
#ifdef Q_OS_WIN
    HMODULE module=GetModuleHandleW(nullptr);
    HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(201),MAKEINTRESOURCEW(10));
    if(!resource)return {};
    const DWORD size=SizeofResource(module,resource);const void *ptr=LockResource(LoadResource(module,resource));
    if(ptr&&size&&size<8*1024*1024)return QByteArray(static_cast<const char *>(ptr),int(size));
#endif
    return {};
}
bool isOurProxy(const QByteArray &d,QString *error) {
    const auto fail=[&](const QString &msg){if(error)*error=msg;return false;};
    const int pe=PeUtils::peHeaderOffset(d);
    if(pe<0||!ByteUtils::inRange(d,pe,24)||PeUtils::machine(d)!=0x8664||!PeUtils::isPe32Plus(d)
        ||!(ByteUtils::le16(d,pe+22)&0x2000)||(!d.contains("BSI_GIF_RPC_PROXY_V1")&&!d.contains("BSI_GIF_RPC_PROXY_V2")))
        return fail(QStringLiteral("Not a compatible BSI RPC-only Windows x64 DLL."));
    const auto sections=PeUtils::sections(d);
    const qint64 table=PeUtils::rvaToRaw(sections,PeUtils::exportRva(d));
    if(table<0||!ByteUtils::inRange(d,table,40))return fail(QStringLiteral("Missing DLL export table."));
    const quint32 count=ByteUtils::le32(d,table+24);
    const qint64 names=PeUtils::rvaToRaw(sections,ByteUtils::le32(d,table+32));
    if(count>4096||names<0||!ByteUtils::inRange(d,names,qint64(count)*4))return fail(QStringLiteral("Invalid DLL export names."));
    QSet<QByteArray> found;
    for(quint32 i=0;i<count;++i) {
        const qint64 at=PeUtils::rvaToRaw(sections,ByteUtils::le32(d,names+i*4));
        if(at<0||!ByteUtils::inRange(d,at,1))return fail(QStringLiteral("Invalid exported name pointer."));
        const auto end=d.indexOf('\0',at);if(end<at||end-at>256)return fail(QStringLiteral("Invalid exported name."));
        found.insert(d.mid(at,end-at));
    }
    const QList<QByteArray> required={"BsiGifRpcSchema","GetFileVersionInfoSizeA","GetFileVersionInfoSizeW","GetFileVersionInfoSizeExA","GetFileVersionInfoSizeExW","GetFileVersionInfoA","GetFileVersionInfoW","GetFileVersionInfoExA","GetFileVersionInfoExW","VerQueryValueA","VerQueryValueW","VerInstallFileA","VerInstallFileW","VerFindFileA","VerFindFileW","VerLanguageNameA","VerLanguageNameW"};
    // Keep V1 recognition for safe removal/upgrading, but V2 adds the missing
    // Windows export. Never make old crash recovery depend on new exports.
    if(d.contains("BSI_GIF_RPC_PROXY_V2")&&!found.contains("GetFileVersionInfoByHandle"))
        return fail(QStringLiteral("Incomplete V2 proxy: missing GetFileVersionInfoByHandle."));
    for(const auto &name:required)if(!found.contains(name))return fail(QStringLiteral("Required DLL export missing: %1").arg(QString::fromLatin1(name)));
    return true;
}
bool validate(const QVariantMap &s,QString *error) {
    const auto fail=[&](const QString &msg){if(error)*error=msg;return false;};
    const auto id=s.value(QStringLiteral("AppId")).toString();
    bool validNumber=false;const auto numericId=id.toULongLong(&validNumber);
    if(!validNumber||numericId==0)return fail(QStringLiteral("Application ID must be a nonzero unsigned 64-bit Discord ID."));
    if(!QRegularExpression(QStringLiteral("^[1-9][0-9]{16,19}$")).match(id).hasMatch())return fail(QStringLiteral("Application ID must contain 17–20 digits and cannot start with zero."));
    const int interval=s.value(QStringLiteral("Interval"),5).toInt();if(interval<5||interval>60)return fail(QStringLiteral("Rotation interval must be 5–60 seconds."));
    const auto rows=details(s);if(rows.size()>10)return fail(QStringLiteral("Use at most ten rotating detail lines."));
    for(const auto &line:rows)if(line.size()>128||line.contains(QLatin1Char('\r'))||line.contains(QChar(0)))return fail(QStringLiteral("Each detail line is limited to 128 characters."));
    for(const auto &key:fields) {
        const auto value=s.value(key).toString();int maximum=128;
        if(key.endsWith(QLatin1String("Url"))||key.endsWith(QLatin1String("Image")))maximum=2048;
        if(key.endsWith(QLatin1String("Label")))maximum=32;
        if(value.size()>maximum||value.contains(QLatin1Char('\n'))||value.contains(QLatin1Char('\r'))||value.contains(QChar(0)))
            return fail(QStringLiteral("%1 is too long or contains a control character.").arg(key));
        if((key.endsWith(QLatin1String("Url"))||key.endsWith(QLatin1String("Image")))&&!value.isEmpty()) {
            const QUrl url(value);
            if(key.endsWith(QLatin1String("Image"))&&!value.contains(QLatin1Char(':'))&&QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]{1,256}$")).match(value).hasMatch())continue;
            if(!url.isValid()||url.scheme()!=QLatin1String("https")||url.host().isEmpty()||!url.userInfo().isEmpty())
                return fail(QStringLiteral("%1 needs an HTTPS URL (images may also use an uploaded Discord asset key).").arg(key));
        }
    }
    for(int i=1;i<=2;++i)if(s.value(QStringLiteral("Button%1Label").arg(i)).toString().isEmpty()!=s.value(QStringLiteral("Button%1Url").arg(i)).toString().isEmpty())
        return fail(QStringLiteral("Each button needs both a label and a URL, or neither."));
    return true;
}
QVariantMap inspect(const QString &exe) {
    const auto error=targetError(exe);if(!error.isEmpty())return reply(false,error);
    const auto dll=pathFor(exe,QStringLiteral("version.dll"));const auto bytes=read(dll);
    auto out=reply(true,bytes.isEmpty()?QStringLiteral("No app-local version.dll installed."):
        isOurProxy(bytes)?QStringLiteral("Validated BSI RPC-only x64 version.dll and its forwarding exports."):
            QStringLiteral("An existing non-BSI version.dll is present. It will not be replaced without explicit permission."));
    out[QStringLiteral("installed")]=isOurProxy(bytes);out[QStringLiteral("existing")]=QFileInfo::exists(dll);
    out[QStringLiteral("hasBackup")]=QFileInfo::exists(dll+QStringLiteral(".bsi-gif-rpc.bak"));
    out[QStringLiteral("settings")]=settings(pathFor(exe,QStringLiteral("bsi-gif-rpc.ini")));return out;
}
QVariantMap apply(const QString &exe,const QVariantMap &s,bool replaceExisting) {
    QString error=targetError(exe);if(error.isEmpty()&&!validate(s,&error))return reply(false,error);
    if(!error.isEmpty())return reply(false,error);
    if(BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))return reply(false,QStringLiteral("Close all emulator instances before installing or editing GIF RPC."));
    const auto payload=bundledProxy();if(!isOurProxy(payload,&error))return reply(false,QStringLiteral("Bundled RPC DLL failed validation: %1").arg(error));
    const auto dll=pathFor(exe,QStringLiteral("version.dll")),ini=pathFor(exe,QStringLiteral("bsi-gif-rpc.ini"));
    const bool hadDll=QFileInfo::exists(dll),hadIni=QFileInfo::exists(ini);const auto previous=read(dll),oldIni=read(ini);
    if(hadDll&&!isOurProxy(previous)) {
        if(!replaceExisting)return reply(false,QStringLiteral("An existing version.dll is not ours. Enable explicit replacement only if you own it and accept the backup/compatibility implications."));
        const auto backup=dll+QStringLiteral(".bsi-gif-rpc.bak");
        if(QFileInfo::exists(backup)&&read(backup)!=previous)return reply(false,QStringLiteral("An original DLL backup already exists and differs; it will not be overwritten."));
        if(!QFileInfo::exists(backup)&&(!QFile::copy(dll,backup)||read(backup)!=previous))return reply(false,QStringLiteral("Could not verify the original DLL backup; installation refused."));
    }
    if((hadDll&&previous.isEmpty())||(hadIni&&oldIni.isEmpty()))return reply(false,QStringLiteral("An existing RPC file is empty or unreadable; no files were changed."));
    // Commit settings first; if the DLL cannot be committed, roll settings back.
    if(!write(ini,encode(s)))return reply(false,QStringLiteral("Cannot write RPC settings. Run as administrator and check folder permissions."));
    bool written=hadDll?PatchFile::writeVerified(dll,previous,payload,&error):write(dll,payload);
    if(!written) {
        const bool restored=hadIni?write(ini,oldIni):QFile::remove(ini);
        return reply(false,error.isEmpty()?QStringLiteral("Cannot install the DLL.%1").arg(restored?QString():QStringLiteral(" Settings rollback also failed.")):error);
    }
    if(read(dll)!=payload||read(ini)!=encode(s))return reply(false,QStringLiteral("RPC files failed read-back verification. Do not start the emulator; restore the backup."));
    return reply(true,QStringLiteral("RPC-only version.dll and settings verified. Restart HD-Player with Discord Desktop running. Animated GIFs/external assets depend on Discord support; neither playback nor server acceptance is guaranteed."));
}
QVariantMap restore(const QString &exe) {
    const auto error=targetError(exe);if(!error.isEmpty())return reply(false,error);
    if(BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))return reply(false,QStringLiteral("Close all emulator instances before restoring GIF RPC."));
    const auto dll=pathFor(exe,QStringLiteral("version.dll"));const auto current=read(dll);
    if(!isOurProxy(current))return reply(false,QStringLiteral("Only an installed BSI RPC-only DLL may be removed or restored."));
    const auto backup=dll+QStringLiteral(".bsi-gif-rpc.bak");QString failure;
    if(QFileInfo::exists(backup)) {
        const auto original=read(backup);if(original.isEmpty()||!PatchFile::writeVerified(dll,current,original,&failure))return reply(false,QStringLiteral("Cannot restore original DLL: %1").arg(failure));
    } else if(!QFile::remove(dll))return reply(false,QStringLiteral("The RPC DLL is locked or cannot be removed."));
    return reply(true,QStringLiteral("RPC DLL removed/original DLL restored. The saved RPC settings and original backup were kept."));
}
}
