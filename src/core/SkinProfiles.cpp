#include "SkinProfiles.h"
#include "AddonPacks.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

namespace SkinProfiles {
namespace {
QVariantMap fail(const QString &msg) { return {{"ok", false}, {"msg", msg}}; }
bool whole(const QVariant &v, int lo, int hi) {
    const auto t = v.metaType().id();
    return t != QMetaType::Bool && t != QMetaType::QString && v.canConvert<double>()
        && v.toDouble() == v.toInt() && v.toInt() >= lo && v.toInt() <= hi;
}
bool color(const QVariant &v) {
    static const QRegularExpression re(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
    return v.metaType().id() == QMetaType::QString && re.match(v.toString()).hasMatch();
}
const QMap<QString, QString> &glyphs() {
    static const QMap<QString, QString> paths {
        {"home", "<path d='m3 10 9-7 9 7v11h-6v-7H9v7H3z'/>"},
        {"close", "<path d='m6 6 12 12M6 18 18 6'/>"},
        {"minimize", "<path d='M4 16h16'/>"},
        {"maximize", "<rect x='4' y='4' width='16' height='16' rx='2'/>"},
        {"menu", "<path d='M4 6h16M4 12h16M4 18h16'/>"},
        {"chat", "<path d='M3 3h18v14H9l-6 4z'/><path d='M7 8h10M7 12h6'/>"},
        {"help", "<circle cx='12' cy='12' r='9'/><path d='M9 8a3 3 0 0 1 6 1c0 2-3 2-3 5m0 3h.01'/>"},
        {"gallery", "<rect x='3' y='4' width='18' height='16' rx='2'/><circle cx='8' cy='9' r='1'/><path d='m3 17 6-5 4 3 4-6 4 6'/>"},
        {"record", "<circle cx='12' cy='12' r='8'/><circle cx='12' cy='12' r='3'/>"},
        {"stop", "<rect x='5' y='5' width='14' height='14' rx='2'/>"},
        {"rotate", "<path d='M20 8a9 9 0 1 0 1 7M20 3v5h-5'/>"},
        {"shake", "<rect x='8' y='3' width='8' height='18' rx='2'/><path d='m4 6-2 4 2 4-2 4m18-12 2 4-2 4 2 4'/>"},
        {"location", "<path d='M19 10c0 5-7 12-7 12S5 15 5 10a7 7 0 1 1 14 0z'/><circle cx='12' cy='10' r='2'/>"},
        {"apk", "<path d='M4 8h16v13H4zM4 8l8-5 8 5M12 3v18M8 11l4 3 4-3'/>"},
        {"instances", "<rect x='2' y='8' width='14' height='13' rx='2'/><path d='M7 8V3h14v13h-5'/>"},
        {"macro", "<circle cx='12' cy='12' r='9'/><path d='M12 6v6l4 2'/>"},
        {"memory", "<rect x='6' y='6' width='12' height='12' rx='2'/><path d='M9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m12 0h4M2 15h4m12 0h4'/>"},
        {"pin", "<path d='m8 3 8 0-1 7 4 3H5l4-3zM12 13v9'/>"},
        {"account", "<circle cx='12' cy='8' r='4'/><path d='M4 21a8 8 0 0 1 16 0'/>"},
        {"volume-muted", "<path d='M3 9h4l5-5v16l-5-5H3zM16 9l6 6m-6 0 6-6'/>"},
        {"sidepanel", "<rect x='3' y='3' width='18' height='18' rx='2'/><path d='M15 3v18'/>"},
        {"play", "<path d='m7 4 14 8-14 8z'/>"},
        {"settings", "<circle cx='12' cy='12' r='4'/><path d='M12 2v3m0 14v3M2 12h3m14 0h3M5 5l2 2m10 10 2 2M5 19l2-2M17 7l2-2'/>"},
        {"volume", "<path d='M3 9h4l5-5v16l-5-5H3zM16 8a6 6 0 0 1 0 8m3-11a10 10 0 0 1 0 14'/>"},
        {"controls", "<rect x='2' y='6' width='20' height='14' rx='4'/><path d='M6 10v6m-3-3h6M16 11h1m1 4h1'/>"},
        {"capture", "<path d='M3 7h4l2-3h6l2 3h4v14H3z'/><circle cx='12' cy='13' r='4'/>"},
        {"fullscreen", "<path d='M3 9V3h6m6 0h6v6M3 15v6h6m6 0h6v-6'/>"},
        {"back", "<path d='m12 4-8 8 8 8M4 12h17'/>"},
        {"apps", "<rect x='3' y='3' width='7' height='7' rx='1'/><rect x='14' y='3' width='7' height='7' rx='1'/><rect x='3' y='14' width='7' height='7' rx='1'/><rect x='14' y='14' width='7' height='7' rx='1'/>"}
    };
    return paths;
}
}
QByteArray icon(const QString &glyph, const QString &style, const QString &tint) {
    if (!glyphs().contains(glyph) || !color(tint) || !QStringList{"line", "rounded", "bold"}.contains(style)) return {};
    const QString weight = style == QLatin1String("bold") ? QStringLiteral("2.8") : QStringLiteral("1.7");
    const QString cap = style == QLatin1String("line") ? QStringLiteral("square") : QStringLiteral("round");
    return QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='%1' stroke-width='%2' stroke-linecap='%3' stroke-linejoin='%3'>%4</svg>")
        .arg(tint, weight, cap, glyphs().value(glyph)).toUtf8();
}
namespace {
bool safeSvg(const QByteArray &data) {
    if (data.isEmpty() || data.size()>32768) return false;
    QXmlStreamReader xml(data);int count=0;
    const QSet<QString> tags{"svg","g","path","rect","circle","ellipse","line","polyline","polygon","defs","linearGradient","radialGradient","stop","clipPath"};
    const QSet<QString> attributes{"xmlns","width","height","viewBox","fill","fill-rule","fill-opacity","stroke","stroke-width","stroke-linecap","stroke-linejoin","stroke-opacity","opacity","d","x","y","x1","x2","y1","y2","cx","cy","r","rx","ry","points","transform","id","clip-path","offset","stop-color","stop-opacity","gradientUnits","gradientTransform","spreadMethod","fx","fy"};
    while(!xml.atEnd()) {
        xml.readNext();
        if(xml.isDTD() || xml.isEntityReference() || xml.isProcessingInstruction()) return false;
        if(!xml.isStartElement()) continue;
        const QString tag=xml.name().toString();
        if(++count>2048 || !tags.contains(tag) || (count==1 && tag!=QLatin1String("svg"))) return false;
        if(!xml.namespaceUri().isEmpty() && xml.namespaceUri()!=QLatin1String("http://www.w3.org/2000/svg")) return false;
        for(const auto &a:xml.attributes()) {
            const QString key=a.name().toString(),value=a.value().toString();
            if(!attributes.contains(key) || !a.namespaceUri().isEmpty() || value.size()>16384 || value.contains("://") || value.contains("data:",Qt::CaseInsensitive)
                || (value.contains("url",Qt::CaseInsensitive) && !QRegularExpression(QStringLiteral("^url\\(#[A-Za-z0-9_-]+\\)$")).match(value).hasMatch())) return false;
        }
    }
    return !xml.hasError() && count>0;
}
}
QVariantMap readIcon(const QString &file) {
    QFile f(file);if(!f.open(QIODevice::ReadOnly)||f.size()>32768) return fail(QStringLiteral("Choose a static SVG icon up to 32 KB."));
    const auto data=f.readAll();if(!safeSvg(data))return fail(QStringLiteral("SVG refused: use static vector shapes without scripts, external links, text, animation or embedded images."));
    return {{"ok",true},{"svg",QString::fromLatin1(data.toBase64())},{"msg","Custom static SVG loaded into the draft only. Map an exact player resource before staging."}};
}
QByteArray mappedIcon(const QVariantMap &mapping,const QString &style,const QString &tint) {
    if(mapping.contains("glyph") && !mapping.contains("svg")) return icon(mapping.value("glyph").toString(),style,tint);
    if(!mapping.contains("svg") || mapping.contains("glyph") || mapping.value("svg").metaType().id()!=QMetaType::QString) return {};
    const auto text=mapping.value("svg").toString();if(text.size()>44000)return {};
    const auto decoded=QByteArray::fromBase64Encoding(text.toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
    return decoded && safeSvg(decoded.decoded) ? decoded.decoded : QByteArray();
}
QVariantMap validate(const QVariantMap &p) {
    const QSet<QString> keys{"schema","version","name","base","accent","surface","radius","height","padding","motion","duration","iconStyle","palette","tokens","icons","playerUi"};
    for (auto it=p.cbegin();it!=p.cend();++it) if (!keys.contains(it.key())) return fail(QStringLiteral("Unknown skin field: %1").arg(it.key()));
    if (p.value("schema").toString()!=QLatin1String("bsi.skin") || !whole(p.value("version"),1,1)) return fail(QStringLiteral("Unsupported skin schema/version."));
    const QString name=p.value("name").toString();
    if (p.value("name").metaType().id()!=QMetaType::QString || name.trimmed().isEmpty() || name.size()>80 || name.contains(QChar(0))) return fail(QStringLiteral("Choose a pack name (1–80 characters)."));
    if (!AddonPacks::find(p.value("base").toString())) return fail(QStringLiteral("Unknown base collection."));
    if (!color(p.value("accent")) || !color(p.value("surface"))) return fail(QStringLiteral("Colors must be #RRGGBB."));
    if (!whole(p.value("radius"),0,32) || !whole(p.value("height"),16,96) || !whole(p.value("padding"),0,64)
        || !whole(p.value("duration"),0,1200)) return fail(QStringLiteral("Invalid geometry or preview duration."));
    if (!QStringList{"none","fade","rise","slide","scale"}.contains(p.value("motion").toString())
        || !QStringList{"line","rounded","bold"}.contains(p.value("iconStyle").toString())) return fail(QStringLiteral("Unknown motion or icon style."));
    if (p.contains("playerUi") && p.value("playerUi").metaType().id()!=QMetaType::Bool) return fail(QStringLiteral("playerUi must be a boolean."));
    // Accept typed color/token data only, never QML expressions or offsets.
    if (p.contains("palette") && p.value("palette").metaType().id()!=QMetaType::QVariantMap) return fail(QStringLiteral("Palette must be an object."));
    if (p.contains("tokens") && p.value("tokens").metaType().id()!=QMetaType::QVariantMap) return fail(QStringLiteral("Tokens must be an object."));
    const auto palette=p.value("palette").toMap();
    static const QRegularExpression property(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]{0,127}$"));
    if (palette.size()>1024) return fail(QStringLiteral("Too many palette entries."));
    for (auto it=palette.cbegin();it!=palette.cend();++it) if (!property.match(it.key()).hasMatch() || !color(it.value())) return fail(QStringLiteral("Invalid palette override."));
    const auto tokens=p.value("tokens").toMap();
    if (tokens.size()>512) return fail(QStringLiteral("Too many design tokens."));
    static const QRegularExpression token(QStringLiteral("^[^#\\s]{1,512}#root\\.[A-Za-z0-9_.]+$"));
    for (auto it=tokens.cbegin();it!=tokens.cend();++it) if (!token.match(it.key()).hasMatch() || it.key().contains("..") || !whole(it.value(),0,1000000)) return fail(QStringLiteral("Invalid design token."));
    if (p.contains("icons") && p.value("icons").metaType().id()!=QMetaType::QVariantList) return fail(QStringLiteral("Icons must be an array."));
    const auto icons=p.value("icons").toList();QSet<QString> seen;
    if (icons.size()>24) return fail(QStringLiteral("A pack supports at most 24 explicitly mapped SVG replacements."));
    for (const auto &v:icons) {
        if (v.metaType().id()!=QMetaType::QVariantMap) return fail(QStringLiteral("Invalid icon mapping."));
        const auto i=v.toMap();const QString path=i.value("path").toString();
        if (i.size()!=2 || !i.contains("path") || (i.contains("glyph") == i.contains("svg")) || path.isEmpty() || path.size()>1024 || path.contains("..") || !path.endsWith(".svg",Qt::CaseInsensitive) || seen.contains(path)
            || mappedIcon(i,p.value("iconStyle").toString(),p.value("accent").toString()).isEmpty()) return fail(QStringLiteral("Invalid or duplicate SVG mapping."));
        seen.insert(path);
    }
    if (QJsonDocument(QJsonObject::fromVariantMap(p)).toJson().size()>256*1024) return fail(QStringLiteral("Skin profile exceeds 256 KB."));
    return {{"ok",true},{"profile",p},{"msg","Skin profile validated. Import does not apply any player changes."}};
}
QVariantMap read(const QString &file) {
    QFile f(file);if(!f.open(QIODevice::ReadOnly)||f.size()>256*1024) return fail(QStringLiteral("Cannot read skin profile (maximum 256 KB)."));
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(f.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject()) return fail(QStringLiteral("Invalid skin JSON."));
    return validate(doc.object().toVariantMap());
}
QVariantMap save(const QString &file,const QVariantMap &p) {
    const auto r=validate(p);if(!r.value("ok").toBool())return r;
    if(QFileInfo(file).suffix().compare("json",Qt::CaseInsensitive)!=0) return fail(QStringLiteral("Save skins as .json, never over an executable."));
    QSaveFile f(file);f.setDirectWriteFallback(false);const auto bytes=QJsonDocument(QJsonObject::fromVariantMap(p)).toJson();
    if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit())return fail(QStringLiteral("Could not atomically save skin profile."));
    return {{"ok",true},{"msg","Editable skin saved. No emulator files changed."}};
}
}
