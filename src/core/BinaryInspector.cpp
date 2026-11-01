#include "BinaryInspector.h"
#include "ByteUtils.h"
#include "PeUtils.h"
#include <QCryptographicHash>
#include <QSet>
#include <functional>
using namespace ByteUtils;
namespace BinaryInspector {
namespace {
QString hx(quint64 n) { return QStringLiteral("0x%1").arg(n, 0, 16).toUpper(); }
QString cstr(const QByteArray &d, qint64 at) {
    if (!inRange(d, at, 1)) return {};
    const auto end = d.indexOf('\0', at);
    if (end < at || end - at > 1024) return {};
    return QString::fromLatin1(d.mid(at, end-at));
}
QVariantMap fail(const QString &s) { return {{"ok",false},{"error",s}}; }
}
QVariantMap inspect(const QByteArray &d) {
    const int pe = PeUtils::peHeaderOffset(d);
    if (d.size() > FileLimit || pe < 0 || !inRange(d, pe, 24)) return fail("Not a supported PE file, or file exceeds 256 MiB.");
    const qint64 opt = qint64(pe)+24;
    const auto optSize = PeUtils::optionalHeaderSize(d);
    if (!inRange(d,opt,optSize) || optSize < 96) return fail("Truncated optional header.");
    const auto magic=le16(d,opt); const bool plus=magic==0x20b;
    if ((magic!=0x10b && !plus) || optSize < (plus?112:96)) return fail("Unsupported PE optional header.");
    const auto secs=PeUtils::sections(d);
    if (secs.size()!=le16(d,pe+6) || secs.isEmpty() || secs.size()>96) return fail("Invalid or oversized section table.");
    const quint32 headers=le32(d,opt+60);
    const auto raw=[&](quint32 rva, qint64 size)->qint64 {
        if (quint64(rva)+quint64(size)<=headers && inRange(d,rva,size)) return rva;
        qint64 found=-1;
        for (const auto &s:secs) {
            if (rva<s.va) continue;
            const quint64 delta=quint64(rva)-s.va;
            if (delta+quint64(size)>s.rawSize) continue;
            const qint64 at=qint64(s.rawPtr)+qint64(delta);
            if (!inRange(d,at,size) || found>=0) return -1;
            found=at;
        }
        return found;
    };
    const int directoryStart=plus?112:96;
    const quint32 directoryCount=le32(d,opt+directoryStart-4);
    const auto dir=[&](int n)->QPair<quint32,quint32> {
        if (quint32(n)>=directoryCount || directoryStart+(n+1)*8>int(optSize)) return {};
        return {le32(d,opt+directoryStart+n*8),le32(d,opt+directoryStart+n*8+4)};
    };
    QVariantList sections, exports, resources; QStringList warnings;
    qint64 imageEnd=headers;
    for (int i=0;i<secs.size();++i) {
        const auto &s=secs[i];
        const bool valid=inRange(d,s.rawPtr,s.rawSize);
        if (!valid) warnings << "Section "+s.name+" has invalid raw bounds.";
        else imageEnd=qMax(imageEnd,qint64(s.rawPtr)+s.rawSize);
        sections << QVariantMap{{"index",i},{"name",s.name},{"rva",qulonglong(s.va)},{"offset",qulonglong(s.rawPtr)},
            {"size",qulonglong(s.rawSize)},{"virtualSize",qulonglong(s.vsize)},{"flags",hx(s.chars)},
            {"permissions",QString(s.chars&0x20000000?"X":"-")+ (s.chars&0x40000000?"R":"-")+(s.chars&0x80000000?"W":"-")},{"valid",valid}};
    }
    const auto imports=PeUtils::importedModules(d);
    if (!imports.ok) warnings << "Import table: "+imports.error;
    const auto ed=dir(0); const qint64 ex=raw(ed.first,40);
    if (ed.first && ex>=0) {
        const quint32 count=le32(d,ex+24), funcs=le32(d,ex+20), base=le32(d,ex+16);
        const qint64 names=raw(le32(d,ex+32),qint64(count)*4), ords=raw(le32(d,ex+36),qint64(count)*2);
        const qint64 functions=raw(le32(d,ex+28),qint64(funcs)*4);
        if (names<0 || ords<0 || functions<0 || count>100000 || funcs>100000) warnings << "Invalid/oversized export table.";
        else {
            for (quint32 i=0;i<qMin(count,4096u);++i) {
                const auto ordinal=le16(d,ords+i*2); if (ordinal>=funcs) continue;
                const auto name=cstr(d,raw(le32(d,names+i*4),1));
                const quint32 rva=le32(d,functions+ordinal*4); const qint64 at=raw(rva,1);
                exports << QVariantMap{{"name",name},{"ordinal",qulonglong(base)+ordinal},{"rva",qulonglong(rva)},
                    {"offset",at},{"forwarder",rva>=ed.first && quint64(rva)<quint64(ed.first)+ed.second?cstr(d,at):QString()}};
            }
            if (count>4096) warnings << "Export display capped at 4096 named entries; ordinal-only exports are not listed.";
        }
    } else if (ed.first) warnings << "Unmapped export directory.";
    const auto rd=dir(2); const qint64 root=raw(rd.first,rd.second);
    if (rd.first && root>=0) {
        QSet<quint32> visited; int nodes=0;
        const auto resourceRange=[&](quint32 rel,qint64 length) { return quint64(rel)+quint64(length)<=rd.second && inRange(d,root+rel,length); };
        std::function<void(quint32,int,QString)> walk=[&](quint32 rel,int depth,QString path) {
            if (depth>4 || visited.contains(rel) || ++nodes>16384 || resources.size()>=4096 || !resourceRange(rel,16)) {
                if (warnings.size()<100) warnings << "Skipped cyclic, malformed or over-budget resource directory.";
                return;
            }
            visited.insert(rel);
            const int count=le16(d,root+rel+12)+le16(d,root+rel+14);
            if (!resourceRange(rel+16,qint64(count)*8)) return;
            for (int i=0;i<count && resources.size()<4096;++i) {
                const qint64 entry=root+rel+16+i*8;
                const quint32 id=le32(d,entry), target=le32(d,entry+4); QString name;
                if (id&0x80000000) {
                    const quint32 off=id&0x7fffffff;
                    if (!resourceRange(off,2)) continue;
                    const int len=le16(d,root+off);
                    if (len>256 || !resourceRange(off+2,len*2)) continue;
                    for (int k=0;k<len;++k) name+=QChar(le16(d,root+off+2+k*2));
                } else name=QString::number(id);
                const QString next=path.isEmpty()?name:path+"/"+name;
                if (target&0x80000000) walk(target&0x7fffffff,depth+1,next);
                else if (resourceRange(target,16)) {
                    const quint32 size=le32(d,root+target+4); const qint64 at=raw(le32(d,root+target),size);
                    if (at<0) { if(warnings.size()<100) warnings << "Skipped unmapped Win32 resource payload."; continue; }
                    resources << QVariantMap{{"index",resources.size()},{"path",next},{"offset",at},{"size",qulonglong(size)},{"codepage",le32(d,root+target+8)}};
                }
            }
        };
        walk(0,0,{});
        if (resources.size()>=4096 || nodes>16384) warnings << "Win32 resource traversal reached its display budget.";
    } else if (rd.first) warnings << "Unmapped Win32 resource directory.";
    const quint16 machine=PeUtils::machine(d);
    return {{"ok",true},{"size",d.size()},{"sha256",QString::fromLatin1(QCryptographicHash::hash(d,QCryptographicHash::Sha256).toHex())},
        {"architecture",machine==0x8664?"x64":machine==0x14c?"x86":machine==0xaa64?"ARM64":"Other"},
        {"format",plus?"PE32+":"PE32"},{"entryRva",qulonglong(le32(d,opt+16))},{"sections",sections},
        {"imports",imports.modules},{"exports",exports},{"resources",resources},{"warnings",warnings},
        {"overlayOffset",qMin(imageEnd,qint64(d.size()))},{"overlaySize",qMax(qint64(0),qint64(d.size())-imageEnd)},
        {"note","Read-only analysis. Overlay may include certificates or a packed payload; signatures are not verified. Qt resources use the separate Resource explorer."}};
}
QVariantMap strings(const QByteArray &d,const QString &query) {
    QVariantList rows; const qint64 end=qMin(qint64(d.size()),32LL*1024*1024); bool limited=false;
    for (int mode=0;mode<2 && !limited;++mode) {
        const int step=mode?2:1;
        // UTF-16 strings can begin at either byte alignment.
        for (int align=0;align<step && !limited;++align) {
            qint64 start=-1; QString text;
            const auto flush=[&]() {
                if (text.size()>=4 && text.contains(query,Qt::CaseInsensitive)) {
                    rows << QVariantMap{{"offset",start},{"encoding",mode?"UTF-16LE (ASCII range)":"ASCII"},{"text",text.left(512)}};
                    if (rows.size()>=2000) limited=true;
                }
                text.clear(); start=-1;
            };
            for (qint64 at=align;at+step<=end && !limited;at+=step) {
                const auto ch=uchar(d[at]); const bool printable=ch>=32 && ch<127 && (!mode || d[at+1]=='\0');
                if (printable) { if(start<0)start=at; if(text.size()<4096)text+=QChar(ch); }
                else flush();
            }
            if (!limited) flush();
        }
    }
    return {{"ok",true},{"rows",rows},{"scannedBytes",end},{"limited",limited || d.size()>end},{"note","Up to 2000 matching strings, minimum 4 characters; first 32 MiB only. Long strings are searched within their first 4096 characters and displayed within 512; these are leads, not verified file boundaries."}};
}
QVariantMap hex(const QByteArray &d,qint64 offset) {
    if (offset<0 || offset>=d.size()) return fail("Offset is outside this file.");
    QString text; const qint64 end=qMin(qint64(d.size()),offset+4096);
    for (qint64 at=offset;at<end;at+=16) {
        const auto bytes=d.mid(at,qMin(qint64(16),end-at)); QString ascii;
        for (char c:bytes) ascii+=(uchar(c)>=32 && uchar(c)<127)?QChar(uchar(c)):QChar('.');
        text+=QStringLiteral("%1  %2  %3\n").arg(at,8,16,QChar('0')).arg(QString::fromLatin1(bytes.toHex(' ')),-47).arg(ascii);
    }
    return {{"ok",true},{"offset",offset},{"next",end},{"text",text}};
}
}
