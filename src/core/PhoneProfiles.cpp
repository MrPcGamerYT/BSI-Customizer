#include "PhoneProfiles.h"

#include "PeUtils.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ZlibUtils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>

namespace PhoneProfiles {
namespace {
const QString marker = QStringLiteral("BSI-PHONE");
const QString helperStart = QStringLiteral("\n// BSI-PHONE helpers v1 begin\n");
const QString helperEnd = QStringLiteral("// BSI-PHONE helpers v1 end\n");
constexpr qsizetype maxSource = 1024 * 1024;
constexpr qsizetype maxImage = 256LL * 1024 * 1024;
struct Edit { int at, size; QString before, after, key; };
struct Function { int open = -1, end = -1; };

bool refuse(QString *error, const QString &why)
{
    if (error) *error = why;
    return false;
}

bool validRuntimeUrl(const QString &value)
{
    const QUrl url(value);
    return value.size() <= 2048 && !value.contains(QLatin1Char('"'))
        && !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r'))
        && !value.contains(QChar(0)) && url.isValid() && url.isLocalFile()
        && url.host().isEmpty() && url.userInfo().isEmpty() && url.port() == -1
        && url.path().startsWith(QLatin1Char('/')) && !url.path().startsWith(QLatin1String("//"))
        && !url.path().contains(QLatin1Char('\\')) && !url.path().contains(QChar(0))
        && !url.hasQuery() && !url.hasFragment();
}

QString helpers()
{
    return helperStart + QStringLiteral(R"QML(property var bsiPhoneState:null
function bsiPhoneApi(){if(bsiPhoneState===null)bsiPhoneState=BsiPhoneRuntime.create(this,uiBackend,iSettingsWindow,[iPredefinedDeviceProfileBtn,iCustomDeviceProfileBtn,iDeviceProfileCombobox,iDeviceManufacturerTxt,iDeviceBrandTxt,iDeviceModelTxt]);return bsiPhoneState;}
)QML") + helperEnd;
}
QString javascript()
{
    QJsonArray data;
    for (const auto &p : catalog())
        data.append(QJsonObject{{QStringLiteral("name"), p.name}, {QStringLiteral("manufacturer"), p.manufacturer},
                                {QStringLiteral("brand"), p.brand}, {QStringLiteral("model"), p.model}});
    return QStringLiteral(R"JS(// Independently authored BSI phone presets. One state object per Phone page.
// This is a local Qt JS import, not a network script or an FPS guarantee.
function create(host,ui,settings,controls){
 var profiles=%1,baseline=[],mode=0;
 var predef=controls[0],custom=controls[1],combo=controls[2],fields=controls.slice(3);
 var keys=["device_custom_manufacturer","device_custom_brand","device_custom_model"];
 function values(){var a=[];for(var i=0;i<keys.length;i++)a.push(ui.confGetInstanceString(keys[i]));return a;}
 function preset(){
  if(!predef.checked||combo.currentIndex<0||combo.currentIndex>=host.pDeviceProfileModel.count)return null;
  var n=host.pDeviceProfileModel.get(combo.currentIndex).name;
  if(host.pDeviceProfileMap[n]!=="custom")return null;
  for(var i=0;i<profiles.length;i++)if(profiles[i].name===n)return profiles[i];
  return null;
 }
 function nativeMode(){return mode?mode===1:(predef.checked&&preset()===null);}
 function customMode(){return mode?mode===2:(custom.checked||preset()!==null);}
 function captureBaseline(){mode=0;baseline=values();}
 function populate(a){
  host.pDeviceProfileMap=({});
  for(var i=0;i<profiles.length;i++){
   var p=profiles[i],code="custom",matched=false;
   for(var j=0;j<a.length;)if(a[j].display_name===p.name){if(!matched){code=a[j].pcode;matched=true;}a.splice(j,1);}else j++;
   host.pDeviceProfileMap[p.name]=code;host.pDeviceProfileModel.append({name:p.name});
  }
 }
 function setFields(a){for(var i=0;i<keys.length;i++){fields[i].text=a[i];ui.confSetInstanceString(keys[i],a[i]);}}
 function identityDirty(){
  if(ui.confIsInstancePropModified("device_profile_code"))return true;
  for(var i=0;i<keys.length;i++)if(ui.confIsInstancePropModified(keys[i]))return true;
  return false;
 }
 function activate(){
  var p=preset();
  if(p)setFields([p.manufacturer,p.brand,p.model]);else if(baseline.length===3)setFields(baseline);
  ui.confSetInstanceString("device_profile_code",host.pDeviceProfileMap[host.pDeviceProfileModel.get(combo.currentIndex).name]);
 }
 function restoreChoice(){
  var code=ui.confGetInstanceString("device_profile_code"),wanted="",a=values(),found=false;
  if(code==="custom")for(var i=0;i<profiles.length;i++){
   var p=profiles[i];if(p.manufacturer===a[0]&&p.brand===a[1]&&p.model===a[2]){wanted=p.name;break;}
  }
  for(var j=0;j<host.pDeviceProfileModel.count;j++){
   var n=host.pDeviceProfileModel.get(j).name,c=host.pDeviceProfileMap[n];
   if((code==="custom"&&wanted===n&&c==="custom")||(code!=="custom"&&c===code)){
    combo.currentIndex=j;predef.checked=true;found=true;break;
   }
  }
  if(!found&&code!=="custom")combo.currentIndex=-1;
  host.fMonitorChange();
 }
 function beginSave(){
  if(mode!==0||!host.pEnableSave)return false;
  if(preset()&&identityDirty()&&ui.confGetInstanceString("device_carrier_code")===""){
   settings.fShowToast(qsTr("Select a network provider before saving this phone profile."));return false;
  }
  mode=customMode()?2:1;host.pEnableSave=false;return true;
 }
 return {nativeMode:nativeMode,customMode:customMode,captureBaseline:captureBaseline,populate:populate,activate:activate,restoreChoice:restoreChoice,beginSave:beginSave};
}
)JS").arg(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
}

bool function(const QString &masked, const QString &name, Function *out, QString *error)
{
    const QRegularExpression re(QStringLiteral("\\bfunction\\s+%1\\s*\\([^)]*\\)\\s*\\{").arg(QRegularExpression::escape(name)));
    auto matches = re.globalMatch(masked);
    if (!matches.hasNext()) return refuse(error, QStringLiteral("Phone profile function %1 is missing; this player was not modified.").arg(name));
    const auto m = matches.next();
    if (matches.hasNext()) return refuse(error, QStringLiteral("Phone profile function %1 is ambiguous.").arg(name));
    out->open = int(m.capturedEnd()) - 1;
    int depth = 0;
    for (int i = out->open; i < masked.size(); ++i) {
        if (masked.at(i) == QLatin1Char('{')) ++depth;
        else if (masked.at(i) == QLatin1Char('}') && --depth == 0) { out->end = i; return true; }
    }
    return refuse(error, QStringLiteral("Unbalanced Phone profile function %1.").arg(name));
}

bool add(const QString &source, const QString &runtimeUrl, QString *output, QString *error)
{
    if (source.contains(marker) || source.contains(QLatin1String("bsiPhone")))
        return refuse(error, QStringLiteral("Unknown or partial extra-phone edits remain; nothing was changed."));
    const QString masked = QmlText::mask(source);
    const auto roots = QmlText::objects(masked, QStringLiteral("Item"));
    if (roots.isEmpty()) return refuse(error, QStringLiteral("The native Phone page root is not recognized."));
    const auto root = roots.first();
    QmlText::Binding objectName;
    if (!QmlText::binding(masked, root.open + 1, root.end - 1, QStringLiteral("objectName"), &objectName)
        || !QRegularExpression(QStringLiteral("^[\"']deviceProfile[\"']$")).match(source.mid(objectName.valueBegin, objectName.valueEnd - objectName.valueBegin).trimmed()).hasMatch())
        return refuse(error, QStringLiteral("The native Phone profile page identity is not recognized."));
    for (const QString &id : {QStringLiteral("iPredefinedDeviceProfileBtn"), QStringLiteral("iCustomDeviceProfileBtn"),
                              QStringLiteral("iDeviceProfileCombobox"), QStringLiteral("iDeviceManufacturerTxt"),
                              QStringLiteral("iDeviceBrandTxt"), QStringLiteral("iDeviceModelTxt")}) {
        const QRegularExpression declaration(QStringLiteral("\\bid\\s*:\\s*%1\\b").arg(id));
        auto matches = declaration.globalMatch(masked);
        if (!matches.hasNext()) return refuse(error, QStringLiteral("A required native Phone control is missing: %1.").arg(id));
        const auto match = matches.next();
        if (matches.hasNext() || match.capturedStart() < root.open || match.capturedEnd() >= root.end)
            return refuse(error, QStringLiteral("A native Phone control is ambiguous: %1.").arg(id));
    }
    QList<Edit> edits;
    const auto append = [&](int at, int size, const QString &after, const QString &key) {
        edits.append({at, size, source.mid(at, size), after, key});
    };
    const auto replaceIn = [&](const Function &f, const QString &before, const QString &after, const QString &key, int expected) {
        const QString section = masked.mid(f.open + 1, f.end - f.open - 1);
        int count = 0, from = 0, at;
        while ((at = section.indexOf(before, from)) >= 0) {
            append(f.open + 1 + at, before.size(), after, key + QString::number(count++));
            from = at + before.size();
        }
        return count == expected || refuse(error, QStringLiteral("The native Phone save contract differs at %1; no rewrite was guessed.").arg(key));
    };
    Function monitor, callback, save, init, cloud, previous;
    if (!function(masked, QStringLiteral("fMonitorChange"), &monitor, error)
        || !function(masked, QStringLiteral("fDeviceProfileSettingChangedClbk"), &callback, error)
        || !function(masked, QStringLiteral("fHandleConfChange"), &save, error)
        || !function(masked, QStringLiteral("fInitUi"), &init, error)
        || !function(masked, QStringLiteral("fDeviceProfilesReceivedFromCloud"), &cloud, error)
        || !function(masked, QStringLiteral("fSetPreviousState"), &previous, error)) return false;
    for (const auto &f : {monitor, callback, save, init, cloud, previous})
        if (f.open < root.open || f.end >= root.end) return refuse(error, QStringLiteral("Phone callback is outside its native page."));
    if (!replaceIn(monitor, QStringLiteral("iPredefinedDeviceProfileBtn.checked"), QStringLiteral("bsiPhoneApi().nativeMode()"), QStringLiteral("monitor-native"), 1)
        || !replaceIn(monitor, QStringLiteral("iCustomDeviceProfileBtn.checked"), QStringLiteral("bsiPhoneApi().customMode()"), QStringLiteral("monitor-custom"), 1)
        || !replaceIn(callback, QStringLiteral("iPredefinedDeviceProfileBtn.checked"), QStringLiteral("bsiPhoneApi().nativeMode()"), QStringLiteral("callback-native"), 1)
        || !replaceIn(save, QStringLiteral("iPredefinedDeviceProfileBtn.checked"), QStringLiteral("bsiPhoneApi().nativeMode()"), QStringLiteral("save-native"), 2)
        || !replaceIn(save, QStringLiteral("iCustomDeviceProfileBtn.checked"), QStringLiteral("bsiPhoneApi().customMode()"), QStringLiteral("save-custom"), 2)) return false;
    // A local preset uses the native custom-profile API. The custom branch must
    // also notice switching from a cloud code to custom with identical fields.
    for (const auto &pair : {qMakePair(monitor, QStringLiteral("monitor-dirty")), qMakePair(save, QStringLiteral("save-dirty"))}) {
        const QString section = source.mid(pair.first.open + 1, pair.first.end - pair.first.open - 1);
        const QRegularExpression dirty(QStringLiteral("uiBackend\\.confIsInstancePropModified\\(\\s*[\"']device_custom_manufacturer[\"']\\s*\\)"));
        auto found = dirty.globalMatch(section);
        if (!found.hasNext()) return refuse(error, QStringLiteral("Native custom-profile dirty check is missing."));
        const auto m = found.next();
        if (found.hasNext()) return refuse(error, QStringLiteral("Native custom-profile dirty check is ambiguous."));
        append(pair.first.open + 1 + int(m.capturedStart()), int(m.capturedLength()),
               QStringLiteral("(uiBackend.confIsInstancePropModified(\"device_profile_code\")||") + m.captured() + QLatin1Char(')'), pair.second);
    }
    append(init.open + 1, 0, QStringLiteral("\nbsiPhoneApi().captureBaseline();\n"), QStringLiteral("init"));
    append(save.open + 1, 0, QStringLiteral("\nif(!bsiPhoneApi().beginSave())return;\n"), QStringLiteral("save-start"));
    append(previous.end, 0, QStringLiteral("\nbsiPhoneApi().captureBaseline();bsiPhoneApi().restoreChoice();\n"), QStringLiteral("previous"));
    append(cloud.end, 0, QStringLiteral("\nbsiPhoneApi().restoreChoice();\n"), QStringLiteral("restore"));
    const QString cloudMask = masked.mid(cloud.open + 1, cloud.end - cloud.open - 1);
    const QRegularExpression clear(QStringLiteral("\\bpDeviceProfileModel\\.clear\\(\\s*\\)"));
    auto clearMatches = clear.globalMatch(cloudMask);
    if (!clearMatches.hasNext()) return refuse(error, QStringLiteral("The native phone list reset is missing."));
    const auto cleared = clearMatches.next();
    if (clearMatches.hasNext()) return refuse(error, QStringLiteral("The native phone list reset is ambiguous."));
    append(cloud.open + 1 + int(cleared.capturedEnd()), 0, QStringLiteral(";bsiPhoneApi().populate(deviceProfileArr)"), QStringLiteral("populate"));
    // Touch only the original phone ComboBox's activation; no new control,
    // visual/style member, timer or native carrier change.
    QList<QmlText::Object> combos;
    for (const auto &obj : QmlText::objects(masked, QStringLiteral("UiComboBox"))) {
        QmlText::Binding id;
        if (QmlText::binding(masked, obj.open + 1, obj.end - 1, QStringLiteral("id"), &id)
            && masked.mid(id.valueBegin, id.valueEnd - id.valueBegin).trimmed() == QLatin1String("iDeviceProfileCombobox")) combos << obj;
    }
    if (combos.size() != 1) return refuse(error, QStringLiteral("The stock phone dropdown is missing or ambiguous."));
    const auto combo = combos.first();
    const QString body = source.mid(combo.open + 1, combo.end - combo.open - 1);
    const QRegularExpression activate(QStringLiteral("uiBackend\\.confSetInstanceString\\(\\s*[\"']device_profile_code[\"']\\s*,\\s*pDeviceProfileMap\\[\\s*pDeviceProfileModel\\.get\\(currentIndex\\)\\.name\\s*\\]\\s*\\)"));
    auto activations = activate.globalMatch(body);
    if (!activations.hasNext()) return refuse(error, QStringLiteral("The stock phone dropdown activation is not recognized."));
    const auto activation = activations.next();
    if (activations.hasNext()) return refuse(error, QStringLiteral("The stock phone dropdown activation is ambiguous."));
    append(combo.open + 1 + int(activation.capturedStart()), int(activation.capturedLength()), QStringLiteral("bsiPhoneApi().activate()"), QStringLiteral("activate"));
    append(root.nameBegin, 0, QStringLiteral("import \"%1\" as BsiPhoneRuntime\n").arg(runtimeUrl), QStringLiteral("runtime-import"));
    append(root.end - 1, 0, helpers(), QStringLiteral("helpers"));
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b){return a.at > b.at;});
    QString result = source;
    int boundary = source.size();
    for (const auto &e : edits) {
        if (e.at < 0 || e.at + e.size > boundary) return refuse(error, QStringLiteral("Phone profile edits overlap; no source was changed."));
        const QString replacement = e.key == QLatin1String("helpers") ? e.after
            : QStringLiteral("/*BSI-PHONE:%1:%2*/%3/*BSI-PHONE-END:%1*/")
                  .arg(e.key, QString::fromLatin1(e.before.toUtf8().toBase64()), e.after);
        result.replace(e.at, e.size, replacement);
        boundary = e.at;
    }
    *output = result;
    return true;
}

bool remove(const QString &source, QString *output, QString *error)
{
    if (source.count(helperStart) != 1 || source.count(helperEnd) != 1 || source.count(helpers()) != 1)
        return refuse(error, QStringLiteral("The extra-phone helper was edited or is incomplete; restore was refused."));
    QString result = source;
    result.remove(helpers());
    const QRegularExpression tag(QStringLiteral("/\\*BSI-PHONE:([a-z0-9-]+):([A-Za-z0-9+/=]*)\\*/"));
    QSet<QString> seen;
    QList<Edit> edits;
    auto tags = tag.globalMatch(result);
    while (tags.hasNext()) {
        const auto m = tags.next();
        const QString key = m.captured(1), endTag = QStringLiteral("/*BSI-PHONE-END:%1*/").arg(key);
        if (seen.contains(key)) return refuse(error, QStringLiteral("Duplicated extra-phone recovery tag."));
        seen.insert(key);
        const int end = result.indexOf(endTag, int(m.capturedEnd()));
        const QByteArray encoded = m.captured(2).toLatin1();
        const auto original = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (end < 0 || original.decodingStatus != QByteArray::Base64DecodingStatus::Ok || original.decoded.size() > 8192
            || original.decoded.toBase64() != encoded || QString::fromUtf8(original.decoded).toUtf8() != original.decoded)
            return refuse(error, QStringLiteral("Damaged extra-phone recovery tag."));
        edits.append({int(m.capturedStart()), end + int(endTag.size()) - int(m.capturedStart()), {}, QString::fromUtf8(original.decoded), key});
    }
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b){return a.at > b.at;});
    int boundary = result.size();
    for (const auto &e : edits) {
        if (e.at + e.size > boundary) return refuse(error, QStringLiteral("Overlapping extra-phone recovery tags."));
        result.replace(e.at, e.size, e.after);
        boundary = e.at;
    }
    QString rebuilt, why;
    const QRegularExpression imported(QStringLiteral("import\\s+\"([^\"]+)\"\\s+as\\s+BsiPhoneRuntime"));
    const auto url = imported.match(source);
    if (seen.isEmpty() || !url.hasMatch() || !add(result, url.captured(1), &rebuilt, &why) || rebuilt != source)
        return refuse(error, QStringLiteral("The extra-phone owned edits no longer match; no later edits were overwritten. ") + why);
    *output = result;
    return true;
}

bool findSource(const QByteArray &image, QtResources::File *file, QString *text, QByteArray *bytes, QString *error)
{
    if (image.isEmpty() || image.size() > maxImage || !PeUtils::isPe32Plus(image))
        return refuse(error, QStringLiteral("Select a supported, bounded HD-Player.exe first."));
    QList<QtResources::File> matches;
    QSet<qint64> offsets;
    for (const auto &f : QtResources::registeredFiles(image))
        if (f.name.compare(resourceName(), Qt::CaseInsensitive) == 0 && !offsets.contains(f.entryOff)) {matches << f; offsets.insert(f.entryOff);}
    if (matches.size() != 1) return refuse(error, QStringLiteral("The registered native Phone page is missing or ambiguous."));
    *file = matches.first();
    *bytes = QtResources::readFile(image, *file, maxSource);
    const QByteArray utf8 = bytes->startsWith(QByteArray::fromHex("efbbbf")) ? bytes->mid(3) : *bytes;
    *text = QString::fromUtf8(utf8);
    if (utf8.isEmpty() || text->toUtf8() != utf8 || text->contains(QChar(0)))
        return refuse(error, QStringLiteral("The native Phone page is not bounded UTF-8 source."));
    return true;
}
const QStringList unitIds{QStringLiteral("fDeviceProfilesReceivedFromCloud"), QStringLiteral("iDeviceProfileCombobox"), QStringLiteral("fDeviceProfileSettingChangedClbk")};
} // namespace

const QList<Profile> &catalog()
{
    // Model identifiers: certified-android-devices, pinned in PHONE-PROFILES.md.
    // No screen-refresh number is advertised as a guaranteed in-game FPS cap.
    static const QList<Profile> profiles{
        {QStringLiteral("ASUS ROG Phone 9"), QStringLiteral("asus"), QStringLiteral("asus"), QStringLiteral("ASUSAI2501A")},
        {QStringLiteral("ASUS ROG Phone 8"), QStringLiteral("asus"), QStringLiteral("asus"), QStringLiteral("ASUS_AI2401_A")},
        {QStringLiteral("REDMAGIC 10 Pro"), QStringLiteral("nubia"), QStringLiteral("nubia"), QStringLiteral("NX789J")},
        {QStringLiteral("REDMAGIC 9 Pro"), QStringLiteral("nubia"), QStringLiteral("nubia"), QStringLiteral("NX769J")},
        {QStringLiteral("OnePlus 13"), QStringLiteral("OnePlus"), QStringLiteral("OnePlus"), QStringLiteral("CPH2649")},
        {QStringLiteral("OnePlus 12"), QStringLiteral("OnePlus"), QStringLiteral("OnePlus"), QStringLiteral("CPH2573")},
        {QStringLiteral("Samsung Galaxy S25 Ultra"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S938B")},
        {QStringLiteral("Samsung Galaxy S24 Ultra"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S928B")},
        {QStringLiteral("Infinix GT 50 Pro"), QStringLiteral("INFINIX"), QStringLiteral("Infinix"), QStringLiteral("Infinix X6891")},
        {QStringLiteral("Infinix GT 30 Pro"), QStringLiteral("INFINIX"), QStringLiteral("Infinix"), QStringLiteral("Infinix X6873")},
        {QStringLiteral("iQOO Neo 10"), QStringLiteral("vivo"), QStringLiteral("iQOO"), QStringLiteral("I2405")},
        {QStringLiteral("iQOO 15"), QStringLiteral("vivo"), QStringLiteral("iQOO"), QStringLiteral("I2501")},
        {QStringLiteral("iQOO 15R"), QStringLiteral("vivo"), QStringLiteral("iQOO"), QStringLiteral("I2508")},
        {QStringLiteral("realme P3 Ultra 5G"), QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5030")},
        {QStringLiteral("realme P3 5G"), QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5070")},
        {QStringLiteral("realme GT 7"), QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5061")},
        {QStringLiteral("realme GT 6T"), QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX3853")},
        {QStringLiteral("realme P4s 5G"), QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5125")},
        {QStringLiteral("realme GT 8 Pro"), QStringLiteral("realme"), QStringLiteral("realme"), QStringLiteral("RMX5210")},
        {QStringLiteral("Redmi K60"), QStringLiteral("Xiaomi"), QStringLiteral("Redmi"), QStringLiteral("23013RK75C")},
        {QStringLiteral("OnePlus 7 Pro"), QStringLiteral("OnePlus"), QStringLiteral("OnePlus"), QStringLiteral("GM1911")},
        {QStringLiteral("OnePlus 15R"), QStringLiteral("OnePlus"), QStringLiteral("OnePlus"), QStringLiteral("CPH2767")},
        {QStringLiteral("OnePlus Nord 5"), QStringLiteral("OnePlus"), QStringLiteral("OnePlus"), QStringLiteral("CPH2707")},
        {QStringLiteral("Xiaomi 14 Ultra"), QStringLiteral("Xiaomi"), QStringLiteral("Xiaomi"), QStringLiteral("24030PN60G")},
        {QStringLiteral("POCO F5 Pro"), QStringLiteral("Xiaomi"), QStringLiteral("POCO"), QStringLiteral("23013PC75G")},
        {QStringLiteral("Samsung Galaxy S22"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S901B")},
        {QStringLiteral("Samsung Galaxy S22+"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S906B")},
        {QStringLiteral("Samsung Galaxy S22 Ultra"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S908B")},
        {QStringLiteral("Samsung Galaxy S23"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S911B")},
        {QStringLiteral("Samsung Galaxy S23+"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S916B")},
        {QStringLiteral("Samsung Galaxy S23 Ultra"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S918B")},
        {QStringLiteral("Samsung Galaxy S24"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S921B")},
        {QStringLiteral("Samsung Galaxy S24+"), QStringLiteral("samsung"), QStringLiteral("samsung"), QStringLiteral("SM-S926B")},
        {QStringLiteral("vivo X300 Ultra"), QStringLiteral("vivo"), QStringLiteral("vivo"), QStringLiteral("V2547A")},
        {QStringLiteral("OPPO Find X9 Ultra"), QStringLiteral("OPPO"), QStringLiteral("OPPO"), QStringLiteral("CPH2841")}
    };
    return profiles;
}
QString resourceName(){return QStringLiteral("PlrDeviceSetting.qml");}
SourcePlan prepareSource(const QString &source, bool enabled, const QString &runtimeUrl)
{
    SourcePlan plan;
    if (source.isEmpty() || source.size() > maxSource || source.contains(QChar(0))) {
        plan.error = QStringLiteral("The native Phone source exceeds its safe limits.");
        return plan;
    }
    const bool present = source.contains(marker);
    QString native = source;
    if (present && !remove(source, &native, &plan.error)) return plan;
    if (!present && source.contains(QLatin1String("bsiPhone"))) {
        plan.error = QStringLiteral("Partial or unknown extra-phone source remains.");return plan;
    }
    if (enabled) {
        if (!validRuntimeUrl(runtimeUrl)) {
            plan.error = QStringLiteral("The Phone helper requires a bounded absolute local file URL; no remote script was accepted.");
            return plan;
        }
        if (!add(native, runtimeUrl, &plan.output, &plan.error)) return plan;
    } else plan.output = native;
    plan.ok = true;
    plan.installed = enabled;
    return plan;
}
QByteArray runtimeJavascript(){return javascript().toUtf8();}
QString compactPreparedSource(const QString &source)
{
    QString text=source;
    text.remove(QRegularExpression(QStringLiteral("/\\*BSI-PHONE:[a-z0-9-]+:[A-Za-z0-9+/=]*\\*/")));
    text.remove(QRegularExpression(QStringLiteral("/\\*BSI-PHONE-END:[a-z0-9-]+\\*/")));
    return QmlText::compactWhitespace(text);
}
State inspect(const QByteArray &image)
{
    State state;QtResources::File f;QString source;QByteArray bytes;
    if (!findSource(image, &f, &source, &bytes, &state.detail)) return state;
    const bool installed=source.contains(helperStart)&&source.contains(helperEnd);
    if (!installed) {
        const auto test=prepareSource(source,true,QStringLiteral("file:///BSI-PhoneProfiles.js"));
        if (!test.ok) {state.detail=test.error;return state;}
    }
    state.supported=true;
    auto copy=image;
    state.applied=installed&&QtResources::sourceUnits(copy,{resourceName()},unitIds,false,true)==0;
    state.detail=state.applied
        ? QStringLiteral("Extra-phone source is installed. Keep the Customizer's local phone helper/recovery data. Select a phone and use native Save changes; game high-FPS options remain game/version dependent.")
        : QStringLiteral("Ready to add %1 phones first in the stock Phone dropdown. Apply adds choices only; it does not change the selected identity or FPS settings. Requires keeping a local helper/recovery folder in Customizer app data.").arg(catalog().size());
    return state;
}
Plan prepare(const QByteArray &image, bool enabled, const QString &runtimeUrl, const QByteArray &originalSource)
{
    Plan plan;QtResources::File f;QString source;QByteArray before;
    if (!findSource(image,&f,&source,&before,&plan.error)) return plan;
    const bool installed=source.contains(marker);
    if (!installed&&!enabled) {plan.ok=true;plan.output=image;return plan;}
    if (!validRuntimeUrl(runtimeUrl)) {
        plan.error=QStringLiteral("The Phone helper requires a bounded absolute local file URL; no remote script was accepted.");return plan;
    }
    const auto bom=QByteArray::fromHex("efbbbf");
    const QByteArray baseline=installed?originalSource:before;
    const QByteArray utf8=baseline.startsWith(bom)?baseline.mid(3):baseline;
    if (utf8.isEmpty()||utf8.size()>maxSource||QString::fromUtf8(utf8).toUtf8()!=utf8) {
        plan.error=QStringLiteral("A verified native Phone recovery source is required; no installed source was overwritten.");return plan;
    }
    const auto changed=prepareSource(QString::fromUtf8(utf8),true,runtimeUrl);
    if (!changed.ok) {plan.error=changed.error;return plan;}
    const QByteArray patched=(baseline.startsWith(bom)?bom:QByteArray{})+compactPreparedSource(changed.output).toUtf8();
    if (installed&&before!=patched) {
        plan.error=QStringLiteral("The installed Phone source, helper path or recovery snapshot differs; no later edit was overwritten.");return plan;
    }
    const QByteArray after=enabled?patched:baseline;
    plan.beforeSource=before;plan.afterSource=after;
    if (after==before) {
        auto working=image;
        if(enabled)QtResources::sourceUnits(working,{resourceName()},unitIds,true);
        if(enabled&&QtResources::sourceUnits(working,{resourceName()},unitIds,false,true)!=0){plan.error=QStringLiteral("Compiled Phone QML could not be invalidated.");return plan;}
        plan.output=working;plan.changed=working!=image;plan.installed=enabled;plan.ok=true;return plan;
    }
    const auto link=QtResources::findLink(image,f.entryOff,f.name);
    if(!link.found||link.ambiguous||!link.registered||(f.flags!=0&&f.flags!=1)){plan.error=QStringLiteral("The Phone page has no unique writable mapping.");return plan;}
    const auto capacity=QtResources::inPlaceCapacity(image,f.entryOff,f.name,&plan.error);
    if(capacity<0)return plan;
    const auto payload=f.flags==1?ZlibUtils::compressForCapacity(after,capacity):after;
    qint64 destination=-1;
    if(payload.size()>capacity){
        destination=QtResources::findFreeSpace(image,link,payload.size()+4,&plan.error);
        if(destination<0){plan.error=QStringLiteral("The extra phones do not fit this player's Phone resource or verified free space. Growing the EXE is forbidden; nothing was written.");return plan;}
    }
    auto working=image;
    if(!QtResources::replaceResource(working,f.entryOff,link,after,&plan.error,nullptr,false,destination,unitIds))return plan;
    QtResources::File verify;QString text;QByteArray actual;
    if(working.size()!=image.size()||!findSource(working,&verify,&text,&actual,&plan.error)||actual!=after||QtResources::sourceUnits(working,{resourceName()},unitIds,false,true)!=0){
        plan.error=QStringLiteral("Phone source failed exact read-back/no-growth/cache verification; nothing was written.");return plan;
    }
    plan.output=working;plan.ok=true;plan.changed=true;plan.installed=enabled;return plan;
}
} // namespace PhoneProfiles
