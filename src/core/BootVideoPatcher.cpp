#include "BootVideoPatcher.h"
#include "ByteUtils.h"
#include "BsDetector.h"
#include "EmbeddedResources.h"
#include "PatchFile.h"
#include "PeUtils.h"
#include "QtResources.h"
#include "ThemeEngine.h"
#include "ZlibUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QSet>
#include <QSaveFile>
#include <algorithm>

using namespace ByteUtils;
namespace BootVideoPatcher {
namespace {
QString mask(const QString &text)
{
    QString result = text;
    QChar quote; bool escape = false, line = false, block = false;
    for (int i = 0; i < text.size(); ++i) {
        const auto c = text[i]; const auto next = i + 1 < text.size() ? text[i + 1] : QChar();
        if (line) { if (c == QLatin1Char('\n')) line = false; else result[i] = QLatin1Char(' '); }
        else if (block) {
            result[i] = c == QLatin1Char('\n') ? c : QLatin1Char(' ');
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) { result[++i] = QLatin1Char(' '); block = false; }
        } else if (!quote.isNull()) {
            result[i] = c == QLatin1Char('\n') ? c : QLatin1Char(' ');
            if (escape) escape = false;
            else if (c == QLatin1Char('\\')) escape = true;
            else if (c == quote) quote = QChar();
        } else if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            result[i] = result[i + 1] = QLatin1Char(' '); ++i; line = true;
        } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            result[i] = result[i + 1] = QLatin1Char(' '); ++i; block = true;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            result[i] = QLatin1Char(' '); quote = c;
        }
    }
    return result;
}
struct Object { int begin = -1, body = -1, end = -1; QString type, id; };
int depthAt(const QString &code, int from, int to)
{
    int depth = 0;
    for (int i = from; i < to; ++i) { if (code[i] == QLatin1Char('{')) ++depth; if (code[i] == QLatin1Char('}')) --depth; }
    return depth;
}
QList<Object> objects(const QString &text)
{
    const auto code = mask(text);
    const QRegularExpression types(QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_.]*)\\s*\\{"));
    const QRegularExpression ids(QStringLiteral("\\bid\\s*:\\s*([A-Za-z_][A-Za-z0-9_]*)"));
    QList<Object> result;
    auto matches = types.globalMatch(code);
    while (matches.hasNext()) {
        const auto match = matches.next();
        Object object; object.begin = match.capturedStart(); object.body = match.capturedEnd(); object.type = match.captured(1).section(QLatin1Char('.'), -1);
        int depth = 1, end = object.body;
        for (; end < code.size() && depth; ++end) { if (code[end] == QLatin1Char('{')) ++depth; if (code[end] == QLatin1Char('}')) --depth; }
        if (depth) continue;
        object.end = end;
        auto names = ids.globalMatch(code.mid(object.body, object.end - object.body - 1));
        while (names.hasNext()) {
            const auto id = names.next();
            if (depthAt(code, object.body, object.body + id.capturedStart()) == 0) { object.id = id.captured(1); break; }
        }
        result << object;
    }
    return result;
}
int expressionEnd(const QString &code, int from, int limit)
{
    int depth = 0, ternary = 0; QChar previous;
    for (int i = from; i < limit; ++i) {
        const auto c = code[i];
        if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) { if (!depth) return i; --depth; }
        else if (!depth && c == QLatin1Char('?')) ++ternary;
        else if (!depth && c == QLatin1Char(':') && ternary) --ternary;
        else if (!depth && c == QLatin1Char(';')) return i;
        else if (!depth && (c == QLatin1Char('\n') || c == QLatin1Char('\r'))) {
            int next = i + 1; while (next < limit && code[next].isSpace()) ++next;
            if (!ternary && !QStringLiteral("?:,+-*/&|=.").contains(previous)
                && (next == limit || !QStringLiteral("?:.&|").contains(code[next]))) return i;
        }
        if (!c.isSpace()) previous = c;
    }
    return limit;
}
bool property(QString &text, const Object &object, const QString &name, const QString &value, bool add)
{
    const auto code = mask(text);
    const QRegularExpression re(QStringLiteral("\\b%1\\s*:").arg(QRegularExpression::escape(name)));
    auto matches = re.globalMatch(code.mid(object.body, object.end - object.body - 1));
    while (matches.hasNext()) {
        const auto m = matches.next(); const int start = object.body + m.capturedStart();
        if (depthAt(code, object.body, start)) continue;
        const int rhs = object.body + m.capturedEnd();
        const int end = expressionEnd(code, rhs, object.end - 1);
        text.replace(rhs, end - rhs, value); return true;
    }
    if (add) { text.insert(object.body, name + QLatin1Char(':') + value + QLatin1Char(';')); return true; }
    return false;
}
void visibleUntilCustomLoaderReady(QString &qml, const QString &id, const QString &loaderId)
{
    const QString expression = QStringLiteral("!%1.item").arg(loaderId);
    for (const auto &object : objects(qml)) {
        if (object.id == id) {
            property(qml, object, QStringLiteral("visible"), expression, true);
            break;
        }
    }
}
void assignments(QString &qml, const QString &loaderId)
{
    const auto code = mask(qml);
    const QRegularExpression re(QStringLiteral("\\b(iBootImage\\.visible|pBootImageButtonVisible|pBootGridVisible|iBootLoadingText\\.visible)\\s*=\\s*(true|false)\\b"));
    const QString expression = QStringLiteral("!%1.item").arg(loaderId);
    QList<QPair<int, QPair<int, QString>>> edits;
    auto matches = re.globalMatch(code);
    while (matches.hasNext()) {
        const auto m = matches.next();
        // Preserve the stock screen until our replacement component exists;
        // after that, late promotion callbacks must not cover the custom UI.
        edits << qMakePair(m.capturedStart(2), qMakePair(m.capturedLength(2), expression));
    }
    for (auto it = edits.crbegin(); it != edits.crend(); ++it) qml.replace(it->first, it->second.first, it->second.second);
}
bool bootController(const QString &qml)
{
    const auto code = mask(qml);
    static const QRegularExpression promotion(QStringLiteral("\\bfunction\\s+fSetBootPromotion\\s*\\("));
    if (!promotion.match(code).hasMatch()) return false;
    for (const auto &object : objects(qml)) if (object.id == QLatin1String("iBootImage")) return true;
    return false;
}
bool bootStub(const QString &qml)
{
    return QRegularExpression(QStringLiteral("\\bfunction\\s+fSetBootPromotion\\s*\\(\\s*a\\s*,\\s*b\\s*,\\s*c\\s*\\)\\s*\\{\\s*\\}"))
        .match(mask(qml)).hasMatch() && !bootController(qml);
}

bool bootHost(const QString &qml)
{
    return qml.contains(QLatin1String("iBootProgressComponent")) && qml.contains(QLatin1String("iBootLoader"));
}
struct Stream { qint64 entry; QString text; QtResources::Link link; };
QList<Stream> streams(const QByteArray &data)
{
    QList<Stream> result;
    QSet<qint64> seen;
    const auto append = [&](qint64 entry, const QString &qml, const QtResources::Link &link) {
        if (!link.found || link.ambiguous || seen.contains(entry)) return;
        seen.insert(entry); result << Stream { entry, qml, link };
    };
    // Discover registered resources by structure/content, not a version-specific
    // boot filename. Every candidate is fully linked again before any write.
    for (const auto &file : QtResources::registeredFiles(data)) {
        if (!file.name.endsWith(QLatin1String(".qml"), Qt::CaseInsensitive)) continue;
        const auto source = QtResources::readFile(data, file);
        if (source.isEmpty()) continue;
        const auto qml = QString::fromUtf8(source);
        if (!bootController(qml) && !bootStub(qml) && !bootHost(qml)
            && file.name.compare(QLatin1String("BootVideo.qml"), Qt::CaseInsensitive) != 0
            && !qml.contains(QLatin1String("QtMultimedia"))) continue;
        append(file.entryOff, qml, QtResources::findLink(data, file.entryOff, file.name));
    }
    // Keep the canonical legacy fallback for builds whose registration code is
    // not recognized. No arbitrary offsets or unlinked compressed blobs qualify.
    for (qint64 at : ZlibUtils::findMagicOffsets(data)) {
        if (at < 8 || seen.contains(at - 8)) continue;
        const quint32 stored = be32(data, at - 8), size = be32(data, at - 4);
        if (stored < 6 || stored > 5000004 || !size || size > 5000000 || !inRange(data, at - 4, stored)) continue;
        const auto plain = qUncompress(data.mid(at - 4, stored));
        if (plain.size() != size) continue;
        const QString qml = QString::fromUtf8(plain);
        if (!bootController(qml) && !bootStub(qml) && !bootHost(qml) && !qml.contains(QLatin1String("BootVideo"))
            && !qml.contains(QLatin1String("QtMultimedia")) && !qml.contains(QLatin1String("MediaPlayer"))
            && !qml.contains(QLatin1String("Video{")) && !qml.contains(QLatin1String("Video {"))) continue;
        QtResources::Link link;
        const QStringList names = (bootController(qml) || bootStub(qml))
            ? QStringList { QStringLiteral("PlrBootProgressBar.qml"), QStringLiteral("boot.qml"), QStringLiteral("Boot.qml"), QStringLiteral("BootScreen.qml") }
            : bootHost(qml) ? QStringList { QStringLiteral("plr.qml"), QStringLiteral("Main.qml") }
            : QStringList { QStringLiteral("BootVideo.qml"), QStringLiteral("bootvideo.qml") };
        for (const auto &name : names) { link = QtResources::findLink(data, at - 8, name); if (link.found && !link.ambiguous) break; }
        if (!link.found) link = QtResources::findLink(data, at - 8, QString());
        append(at - 8, qml, link);
    }
    return result;
}


QString embeddedEmulatorLogo(const QByteArray &image)
{
    QList<QtResources::File> candidates;
    for (const auto &file : QtResources::registeredFiles(image)) {
        const QString leaf = file.name.toCaseFolded();
        if (!leaf.contains(QStringLiteral("bluestacks"))) continue;
        if (!leaf.endsWith(QLatin1String(".svg")) && !leaf.endsWith(QLatin1String(".png"))) continue;
        candidates << file;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        const auto rank = [](const QtResources::File &file) {
            const QString leaf = file.name.toCaseFolded();
            if (leaf == QLatin1String("bluestacks5beta.svg")) return 0;
            if (leaf == QLatin1String("bluestacks5beta.png")) return 1;
            if (leaf.endsWith(QLatin1String(".svg"))) return 2;
            return 3;
        };
        return rank(a) < rank(b);
    });
    constexpr qint64 maxLogoBytes = 1024 * 1024;
    for (const auto &file : candidates) {
        const QByteArray bytes = QtResources::readFile(image, file, maxLogoBytes);
        if (bytes.isEmpty() || bytes.size() > maxLogoBytes) continue;
        const QString leaf = file.name.toCaseFolded();
        if (leaf.endsWith(QLatin1String(".svg"))) {
            if (!bytes.toLower().contains(QByteArrayLiteral("<svg"))) continue;
            return QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(bytes.toBase64());
        }
        if (bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a")))
            return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
    }
    return {};
}

QString htmlBootPage(const QString &logoUrl, bool withVideo)
{
    QString html = QString::fromUtf8(R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<meta name="color-scheme" content="dark">
<title>Emulator startup</title>
<style>
*{box-sizing:border-box}
html,body{width:100%;height:100%;margin:0;overflow:hidden;background:#050a10}
body{color:#c7d9e6;font:16px/1.4 "Segoe UI",Arial,sans-serif}
.boot{position:fixed;inset:0;overflow:hidden;background:linear-gradient(180deg,#070d15 0%,#0a1724 54%,#050a10 100%)}
.backdrop{position:absolute;inset:0;width:100%;height:100%;object-fit:cover;background:#050a10}
.shade{position:absolute;inset:0;background:rgba(3,9,15,.42)}
.aura{position:absolute;left:50%;top:45.5%;width:min(72vmin,820px);aspect-ratio:1;transform:translate(-50%,-50%);border-radius:50%;background:#163b58;filter:blur(12px);animation:aura 5.2s ease-in-out infinite}
.ring-track{position:absolute;left:50%;top:45.5%;width:min(42vmin,330px);aspect-ratio:1;transform:translate(-50%,-50%);border:1px solid rgba(125,166,195,.52);border-radius:50%}
.ring{position:absolute;inset:-2px;border-radius:50%;background:conic-gradient(from -126deg,rgba(169,231,255,.08) 0deg,rgba(169,231,255,.42) 70deg,#d9f5ff 210deg,rgba(85,207,255,.85) 280deg,transparent 306deg 360deg);-webkit-mask:radial-gradient(farthest-side,transparent calc(100% - 5px),#000 calc(100% - 4px));mask:radial-gradient(farthest-side,transparent calc(100% - 5px),#000 calc(100% - 4px));filter:drop-shadow(0 0 10px rgba(85,207,255,.82));animation:rotate 2.45s linear infinite}
.logo{position:absolute;left:50%;top:45.5%;width:min(23vmin,190px);height:min(23vmin,190px);transform:translate(-50%,-50%);object-fit:contain;filter:drop-shadow(0 8px 30px rgba(25,75,110,.18));animation:logo 4.6s ease-in-out infinite}
.status{position:absolute;left:12px;right:12px;top:calc(45.5% + min(21vmin,165px) + 22px);text-align:center;color:#c7d9e6;font-size:clamp(12px,1.35vw,16px);letter-spacing:.8px;animation:status 3.4s ease-in-out infinite}
@keyframes rotate{to{transform:rotate(360deg)}}
@keyframes aura{0%,100%{opacity:.11}50%{opacity:.19}}
@keyframes logo{0%,100%{opacity:.82;transform:translate(-50%,-50%) scale(1)}50%{opacity:.96;transform:translate(-50%,-50%) scale(1.018)}}
@keyframes status{50%{opacity:.68}}
@media(prefers-reduced-motion:reduce){*,*:before,*:after{animation:none!important}}
</style>
</head>
<body>
<main class="boot" aria-label="Emulator startup">
__VIDEO_BACKGROUND__
<div class="aura"></div>
<div class="ring-track" aria-hidden="true"><div class="ring"></div></div>
<img class="logo" src="__BSI_EMULATOR_LOGO__" alt="">
<div class="status">Emulator is starting…</div>
</main>
</body>
</html>
)HTML");
    const QString video = withVideo
        ? QStringLiteral("<video class=\"backdrop\" autoplay muted loop playsinline><source src=\"Promo.mp4\" type=\"video/mp4\"></video><div class=\"shade\"></div>")
        : QString();
    html.replace(QLatin1String("__VIDEO_BACKGROUND__"), video);
    html.replace(QLatin1String("__BSI_EMULATOR_LOGO__"), logoUrl);
    return html;
}

QString externalPlayer(const QString &logoUrl, bool withVideo)
{
    QString qml = QString::fromUtf8(R"QML(import QtQuick
Item {
    id: root
    objectName: "bsiBootHtmlWrapper"
    anchors.fill: parent
    clip: true
    property string logoUrl: "__BSI_EMULATOR_LOGO__"
    property bool withVideo: __BSI_WITH_VIDEO__
    property bool htmlLoaded: false

    Rectangle {
        z: -1
        anchors.fill: parent
        visible: !root.htmlLoaded
        color: "#050A10"
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#070D15" }
            GradientStop { position: 0.54; color: "#0A1724" }
            GradientStop { position: 1.0; color: "#050A10" }
        }
    }
    Rectangle {
        z: 1
        visible: !root.htmlLoaded
        width: Math.min(Math.min(root.width, root.height) * 0.72, 820)
        height: width
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -root.height * 0.025
        color: "#163B58"
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { from: 0.11; to: 0.19; duration: 2600; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.11; duration: 2600; easing.type: Easing.InOutSine }
        }
    }
    Rectangle {
        id: loadingRingTrack
        z: 2
        visible: !root.htmlLoaded
        width: Math.min(Math.min(root.width, root.height) * 0.42, 330)
        height: width
        radius: width / 2
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -root.height * 0.045
        color: "transparent"
        border.width: Math.max(1, Math.min(root.width, root.height) * 0.002)
        border.color: "#38536A"
        opacity: 0.52
    }
    Canvas {
        id: loadingRing
        objectName: "bsiLoadingRing"
        z: 3
        visible: !root.htmlLoaded
        width: loadingRingTrack.width
        height: width
        anchors.centerIn: loadingRingTrack
        antialiasing: true
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            ctx.beginPath();
            ctx.lineWidth = Math.max(3, Math.min(root.width, root.height) * 0.009);
            ctx.lineCap = "round";
            ctx.strokeStyle = "#A9E7FF";
            ctx.shadowColor = "#55CFFF";
            ctx.shadowBlur = Math.max(10, Math.min(root.width, root.height) * 0.035);
            ctx.arc(width / 2, height / 2, width / 2 - ctx.lineWidth, -Math.PI * 0.70, Math.PI * 0.88, false);
            ctx.stroke();
        }
        RotationAnimation on rotation {
            from: 0; to: 360; duration: 2450; loops: Animation.Infinite
            easing.type: Easing.Linear
        }
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { from: 0.72; to: 1.0; duration: 1350; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.72; duration: 1350; easing.type: Easing.InOutSine }
        }
    }
    Image {
        id: emulatorLogo
        objectName: "bsiEmulatorLogo"
        z: 4
        visible: !root.htmlLoaded
        anchors.centerIn: loadingRingTrack
        width: Math.min(Math.min(root.width, root.height) * 0.23, 190)
        height: width
        source: root.logoUrl
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        SequentialAnimation on scale {
            loops: Animation.Infinite
            NumberAnimation { to: 1.018; duration: 2300; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 2300; easing.type: Easing.InOutSine }
        }
    }
    Text {
        id: bootStatus
        objectName: "bsiBootStatus"
        z: 5
        visible: !root.htmlLoaded
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: loadingRingTrack.bottom
        anchors.topMargin: Math.max(14, Math.min(root.width, root.height) * 0.035)
        text: "Emulator is starting…"
        color: "#C7D9E6"
        font.pixelSize: Math.min(Math.min(root.width, root.height) * 0.027, 16)
        font.letterSpacing: 0.8
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { from: 0.68; to: 0.95; duration: 1700; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.68; duration: 1700; easing.type: Easing.InOutSine }
        }
    }
    Component.onCompleted: {
        var htmlUrl = Qt.resolvedUrl("BootScreen.html").toString();
        var web = 'import QtQuick;import QtWebEngine;WebEngineView{objectName:"bsiBootHtmlView";z:0;anchors.fill:parent;backgroundColor:"#050A10";onLoadingChanged:function(request){if(request.status===WebEngineView.LoadSucceededStatus)root.htmlLoaded=true;else if(request.status===WebEngineView.LoadFailedStatus){root.htmlLoaded=false;visible=false}};onRenderProcessTerminated:function(status,exitCode){root.htmlLoaded=false;visible=false}}';
        var webView = null;
        try {
            webView = Qt.createQmlObject(web, root, "bsiHtmlBoot");
            webView.settings.playbackRequiresUserGesture = false;
            webView.settings.localContentCanAccessFileUrls = true;
            webView.url = htmlUrl;
            return;
        } catch(e) {
            if (webView) webView.visible = false;
            console.warn("WebEngine HTML loader unavailable: " + e);
        }
        if (!root.withVideo) return;
        var mediaUrl = JSON.stringify(Qt.resolvedUrl("Promo.mp4").toString());
        var q6 = 'import QtQuick;import QtMultimedia;Item{z:0;anchors.fill:parent;VideoOutput{id:v;anchors.fill:parent;fillMode:VideoOutput.PreserveAspectCrop}MediaPlayer{id:m;source:'
               + mediaUrl + ';loops:MediaPlayer.Infinite;videoOutput:v;audioOutput:AudioOutput{muted:true}Component.onCompleted:play()}}';
        try { Qt.createQmlObject(q6, root, "promo6"); return; } catch(e) {}
        var q5 = 'import QtQuick;import QtMultimedia 5.15;Video{z:0;anchors.fill:parent;source:' + mediaUrl
               + ';fillMode:VideoOutput.PreserveAspectCrop;loops:MediaPlayer.Infinite;muted:true;autoPlay:true;Component.onCompleted:play()}';
        try { Qt.createQmlObject(q5, root, "promo5"); }
        catch(e) { console.warn("This player has no HTML or supported Qt video runtime; showing the native startup animation only."); }
    }
}
)QML");
    qml.replace(QLatin1String("__BSI_EMULATOR_LOGO__"), logoUrl);
    qml.replace(QLatin1String("__BSI_WITH_VIDEO__"), withVideo ? QLatin1String("true") : QLatin1String("false"));
    return qml;
}

QString nativeFallbackComponent(const QString &logoUrl)
{
    QString qml = QString::fromUtf8(R"QML(Component {
    id: bsiNativeStartupComponent
    Item {
        objectName: "bsiNativeBootFallback"
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#070D15" }
                GradientStop { position: 0.54; color: "#0A1724" }
                GradientStop { position: 1.0; color: "#050A10" }
            }
        }
        Rectangle {
            id: bsiNativeRingTrack
            width: Math.min(Math.min(parent.width, parent.height) * 0.42, 330)
            height: width
            radius: width / 2
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -parent.height * 0.045
            color: "transparent"
            border.width: Math.max(1, Math.min(parent.width, parent.height) * 0.002)
            border.color: "#38536A"
        }
        Canvas {
            objectName: "bsiNativeBootRing"
            width: bsiNativeRingTrack.width
            height: width
            anchors.centerIn: bsiNativeRingTrack
            antialiasing: true
            onPaint: {
                var ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.beginPath();
                ctx.lineWidth = Math.max(3, Math.min(parent.width, parent.height) * 0.009);
                ctx.lineCap = "round";
                ctx.strokeStyle = "#A9E7FF";
                ctx.shadowColor = "#55CFFF";
                ctx.shadowBlur = Math.max(10, Math.min(parent.width, parent.height) * 0.035);
                ctx.arc(width / 2, height / 2, width / 2 - ctx.lineWidth, -Math.PI * 0.70, Math.PI * 0.88, false);
                ctx.stroke();
            }
            RotationAnimation on rotation {
                from: 0; to: 360; duration: 2450; loops: Animation.Infinite
                easing.type: Easing.Linear
            }
        }
        Image {
            objectName: "bsiNativeBootLogo"
            anchors.centerIn: bsiNativeRingTrack
            width: Math.min(Math.min(parent.width, parent.height) * 0.23, 190)
            height: width
            source: "__BSI_FALLBACK_LOGO__"
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
        Text {
            objectName: "bsiNativeBootStatus"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: bsiNativeRingTrack.bottom
            anchors.topMargin: Math.max(14, Math.min(parent.width, parent.height) * 0.035)
            text: "Emulator is starting…"
            color: "#C7D9E6"
            font.pixelSize: Math.min(Math.min(parent.width, parent.height) * 0.027, 16)
            font.letterSpacing: 0.8
        }
    }
})QML");
    qml.replace(QLatin1String("__BSI_FALLBACK_LOGO__"), logoUrl);
    return qml;
}

bool writeAsset(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool readBounded(QFile &file, qint64 maxBytes, QByteArray *output)
{
    output->clear();
    while (!file.atEnd()) {
        const qint64 remaining = maxBytes + 1 - output->size();
        if (remaining <= 0) return false;
        const QByteArray chunk = file.read(qMin<qint64>(64 * 1024, remaining));
        if (chunk.isEmpty()) return file.atEnd() && file.error() == QFileDevice::NoError;
        output->append(chunk);
    }
    return output->size() <= maxBytes && file.error() == QFileDevice::NoError;
}

bool samePath(const QString &a, const QString &b)
{
    const QString left = QDir::cleanPath(QFileInfo(a).absoluteFilePath());
    const QString right = QDir::cleanPath(QFileInfo(b).absoluteFilePath());
#ifdef Q_OS_WIN
    return QString::compare(left, right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

// Preserve the Android GL-container handoff and ads. The generated splash
// replaces the stock loading artwork, text and progress controls.
bool updateHost(QString &host, int holdMs, bool *applied, QString *error)
{
    *applied = false;
    const auto glObjects = objects(host);
    for (const auto &gl : glObjects) {
        if (gl.id != QLatin1String("iGlContainerComponent")) continue;
        const auto body = host.mid(gl.body, gl.end - gl.body - 1);
        const auto children = objects(body);
        bool oldVideo = false;
        for (const auto &child : children)
            if (child.type == QLatin1String("Loader") && body.mid(child.begin, child.end-child.begin).contains(QLatin1String("BootVideo.qml"))) oldVideo = true;
        if (!oldVideo) continue;
        // Recognize only the exact legacy experiment's Item/Rectangle/Loader.
        int items = 0, rectangles = 0, loaders = 0;
        for (const auto &child : children) {
            items += child.type == QLatin1String("Item");
            rectangles += child.type == QLatin1String("Rectangle");
            loaders += child.type == QLatin1String("Loader");
        }
        if (children.size() != 3 || items != 1 || rectangles != 1 || loaders != 1) {
            *error = QStringLiteral("The old GL-container video experiment is not uniquely recognizable; restore the original backup first.");
            return false;
        }
        host.replace(gl.begin, gl.end-gl.begin, QStringLiteral("Component{id:iGlContainerComponent;Rectangle{color:\"#000000\"}}"));
        break;
    }
    // Fetch a single own sourceComponent property, never a nested Loader's.
    Object loader;
    int count = 0;
    for (const auto &o : objects(host)) if (o.id == QLatin1String("iBootLoader")) { loader = o; ++count; }
    if (count != 1) return true;
    const auto code = mask(host);
    const QRegularExpression binding(QStringLiteral("\\bsourceComponent\\s*:"));
    auto matches = binding.globalMatch(code.mid(loader.body, loader.end-loader.body-1));
    QString existing;
    bool found = false;
    while (matches.hasNext()) {
        const auto m = matches.next();
        if (depthAt(code, loader.body, loader.body+m.capturedStart())) continue;
        const int start = loader.body+m.capturedEnd();
        existing = code.mid(start, expressionEnd(code, start, loader.end-1)-start);
        existing.remove(QRegularExpression(QStringLiteral("\\s+")));
        found = true; break;
    }
    if (!found) return true;
    const QString normal = QStringLiteral("pIsBootComplete||pShowAndroidAd?iGlContainerComponent:iBootProgressComponent");
    const QString held = QStringLiteral("((pIsBootComplete&&pBootVideoDone)||pShowAndroidAd)?iGlContainerComponent:iBootProgressComponent");
    if (existing != normal && existing != held) {
        *error = QStringLiteral("Unrecognized boot-completion binding; no executable was written.");
        return false;
    }
    property(host, loader, QStringLiteral("sourceComponent"), holdMs ? held : normal, false);
    // Remove the legacy forced top-level stacking override, not normal z values.
    host.replace(QRegularExpression(QStringLiteral("(id\\s*:\\s*iBootLoader\\s*;?\\s*)z\\s*:\\s*999999\\s*;?")), QStringLiteral("\\1z:0;"));
    bool timerFound = false;
    for (const auto &o : objects(host)) {
        if (o.id != QLatin1String("bvDone")) continue;
        if (timerFound) { *error = QStringLiteral("Duplicate boot-hold timers; restore the original backup."); return false; }
        timerFound = true;
    }
    for (const auto &o : objects(host)) {
        if (o.id != QLatin1String("bvDone")) continue;
        if (holdMs) property(host, o, QStringLiteral("interval"), QString::number(holdMs), false);
        else host.remove(o.begin, o.end-o.begin);
        break;
    }
    const QRegularExpression done(QStringLiteral("\\bproperty\\s+bool\\s+pBootVideoDone\\s*:\\s*false\\s*;?"));
    if (!holdMs) host.remove(done);
    else if (!timerFound) {
        if (done.match(mask(host)).hasMatch()) {
            *error = QStringLiteral("Incomplete legacy boot-hold state; restore the original backup."); return false;
        }
        const auto roots = objects(host);
        if (roots.isEmpty()) return false;
        host.insert(roots.first().body, QStringLiteral("property bool pBootVideoDone:false;Timer{id:bvDone;interval:%1;running:pIsBootComplete&&!pBootVideoDone;repeat:false;onTriggered:pBootVideoDone=true}").arg(holdMs));
    }
    *applied = true;
    return true;
}

}

QString logoPreviewDataUrl(const QString &exePath)
{
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 512ll * 1024 * 1024)
        return {};
    const QByteArray image = file.readAll();
    if (file.error() != QFileDevice::NoError || image.size() != file.size()) return {};
    return embeddedEmulatorLogo(image);
}

Result patch(const QString &exePath, int holdMs, const QString &requestedMode, const QString &videoPath)
{
    const QString mode = requestedMode.isEmpty() ? QStringLiteral("video") : requestedMode;
    if (mode != QLatin1String("video") && mode != QLatin1String("modern"))
        return { false, QStringLiteral("Choose the built-in modern boot or custom MP4 mode.") };
    if (holdMs < 0 || holdMs > 15000)
        return { false, QStringLiteral("Boot hold must be between 0 and 15000 ms.") };
    if (mode == QLatin1String("modern") && !videoPath.trimmed().isEmpty())
        return { false, QStringLiteral("The built-in boot mode does not accept a video path.") };
    QByteArray selectedVideo;
    bool copySelectedVideo = false;
    if (mode == QLatin1String("video") && !videoPath.trimmed().isEmpty()) {
        const QFileInfo sourceInfo(videoPath);
        constexpr qint64 maxVideoBytes = 128ll * 1024 * 1024;
        if (!sourceInfo.isFile() || sourceInfo.isSymLink() || sourceInfo.isJunction()
            || sourceInfo.suffix().compare(QLatin1String("mp4"), Qt::CaseInsensitive) != 0
            || sourceInfo.size() < 16 || sourceInfo.size() > maxVideoBytes)
            return { false, QStringLiteral("Choose a regular MP4 file between 16 bytes and 128 MiB; links and other formats are refused.") };
        QFile source(videoPath);
        if (!source.open(QIODevice::ReadOnly)) return { false, QStringLiteral("Cannot read the selected MP4; no files were changed.") };
        if (!readBounded(source, maxVideoBytes, &selectedVideo) || selectedVideo.size() != sourceInfo.size())
            return { false, QStringLiteral("The selected MP4 changed or exceeded the size limit while reading; no files were changed.") };
    }
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before patching boot video.") };
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return { false, QStringLiteral("Cannot read HD-Player.exe.") };
    const QByteArray original = file.readAll(); file.close();
    if (PeUtils::sections(original).isEmpty())
        return {false, QStringLiteral("This file is not a player executable; nothing was written.")};
    const QString logoUrl = embeddedEmulatorLogo(original);
    auto candidates = streams(original);
    for (auto &stream : candidates) {
        if (!bootStub(stream.text)) continue;
        QFile backup(exePath + QStringLiteral(".bak"));
        QList<Stream> originals;
        if (backup.open(QIODevice::ReadOnly)) {
            for (const auto &saved : streams(backup.readAll()))
                if (bootController(saved.text) && saved.link.sourceName == stream.link.sourceName) originals << saved;
        }
        if (originals.size() != 1)
            return { false, QStringLiteral("Old forced boot stub found. Restore the original backup once, then patch again; no executable was written.") };
        stream.text = originals.first().text;
    }
    QList<Stream> controllers, players;
    for (const auto &stream : candidates) {
        if (bootController(stream.text)) controllers << stream;
        if (stream.link.sourceName.compare(QLatin1String("BootVideo.qml"), Qt::CaseInsensitive) == 0) players << stream;
    }
    QList<Stream> hosts;
    for (const auto &candidate : candidates) if (bootHost(candidate.text)) hosts << candidate;
    if (hosts.size() != 1)
        return { false, QStringLiteral("Found %1 linked player boot host(s). A unique host is required to prove which QML boot component is actually loaded; no executable was written.").arg(hosts.size()) };

    // The resource tree can contain unused boot-looking QML. Only patch the
    // component instantiated by plr.qml's iBootProgressComponent; content
    // matching alone is not evidence that the player displays that file.
    QSet<qint64> referenced;
    for (const auto &component : objects(hosts.first().text)) {
        if (component.id != QLatin1String("iBootProgressComponent")) continue;
        const auto body = hosts.first().text.mid(component.body, component.end - component.body - 1);
        const auto code = mask(body);
        for (const auto &child : objects(body)) {
            if (depthAt(code, 0, child.begin) != 0) continue;
            for (const auto &controller : controllers)
                if (controller.link.sourceName.compare(child.type + QStringLiteral(".qml"), Qt::CaseInsensitive) == 0)
                    referenced.insert(controller.entry);
        }
    }
    if (referenced.size() == 1) {
        const auto chosen = *referenced.cbegin();
        for (int i = controllers.size() - 1; i >= 0; --i)
            if (controllers[i].entry != chosen) controllers.removeAt(i);
    }
    if (controllers.size() != 1 || !referenced.contains(controllers.first().entry) || players.size() > 1)
        return { false, QStringLiteral("Found %1 boot-looking controller(s) and %2 stock player(s), but the registered player host does not uniquely instantiate a supported controller. No EXE was written.").arg(controllers.size()).arg(players.size()) };
    const auto controller = controllers.first();
    QString boot = controller.text;
    const QString movie = QDir(QFileInfo(exePath).absolutePath()).filePath(QStringLiteral("Assets/Promo.mp4"));
    if (!selectedVideo.isEmpty())
        copySelectedVideo = !samePath(videoPath, movie);
    const QString helperPath = QDir(QFileInfo(exePath).absolutePath()).filePath(QStringLiteral("Assets/BootVideo.qml"));
    const QString helperUrl = QLatin1Char('"') + QUrl::fromLocalFile(helperPath).toString(QUrl::FullyEncoded) + QLatin1Char('"');
    QByteArray data = original;
    QList<Object> loaders;
    for (const auto &object : objects(boot)) {
        if (object.type == QLatin1String("Loader") && (object.id == QLatin1String("pv")
            || boot.mid(object.begin, object.end - object.begin).contains(QLatin1String("BootVideo.qml")))) loaders << object;
    }
    if (loaders.size() > 1) return { false, QStringLiteral("Multiple boot-video loaders were found; no partial patch was written.") };
    const QString loaderId = loaders.isEmpty() || loaders.first().id.isEmpty() ? QStringLiteral("pv") : loaders.first().id;
    QString readyHide;
    const QSet<QString> stockVisualIds { QStringLiteral("iBootImage"), QStringLiteral("iBootLoadingText"),
                                         QStringLiteral("iBootProgressContainer"), QStringLiteral("iBootAppImage"),
                                         QStringLiteral("iBootAppTitle"), QStringLiteral("iBootAppDetails"),
                                         QStringLiteral("iPromotionInfoRect"), QStringLiteral("iBootImageBtn") };
    for (const auto &object : objects(boot))
        if (stockVisualIds.contains(object.id)) readyHide += object.id + QStringLiteral(".visible=false;");
    const auto rootsBeforePatch = objects(boot);
    if (!rootsBeforePatch.isEmpty()) {
        QString probe = boot;
        for (const auto &propertyName : { QStringLiteral("pBootImageButtonVisible"), QStringLiteral("pBootGridVisible") })
            if (property(probe, rootsBeforePatch.first(), propertyName, QStringLiteral("false"), false))
                readyHide += propertyName + QStringLiteral("=false;");
    }
    // The external QML wrapper loads Assets/BootScreen.html via WebEngine.
    // Keep the old screen visible until the wrapper is ready, then switch to an
    // inline native fallback if the external QML file cannot be loaded.
    const QString loader = QStringLiteral(
        "Loader{id:%1;objectName:\"bsiBootLoader\";z:10000;anchors.fill:parent;source:%2;"
        "onLoaded:{%3}"
        "onStatusChanged:{if(status===Loader.Error){console.warn(\"External boot QML failed; using native splash\");source=\"\";sourceComponent=bsiNativeStartupComponent}}}"
    ).arg(loaderId, helperUrl, readyHide)
        + nativeFallbackComponent(logoUrl)
        + QStringLiteral(
            "Timer{id:bsiBootLoadWatchdog;interval:5000;repeat:false;running:%1.status===Loader.Loading;"
            "onTriggered:{if(%1.status===Loader.Loading&&!%1.item){console.warn(\"External boot QML timed out; using native splash\");"
            "%1.source=\"\";%1.sourceComponent=bsiNativeStartupComponent}}}"
        ).arg(loaderId);
    if (!loaders.isEmpty()) boot.replace(loaders.first().begin, loaders.first().end-loaders.first().begin, loader);
    else {
        const auto all = objects(boot);
        if (all.isEmpty()) return { false, QStringLiteral("Cannot locate the boot QML root object.") };
        boot.insert(all.first().body, loader);
    }
    for (const auto &visualId : { QStringLiteral("iBootImage"), QStringLiteral("iBootLoadingText"),
                                  QStringLiteral("iBootProgressContainer"), QStringLiteral("iBootAppImage"),
                                  QStringLiteral("iBootAppTitle"), QStringLiteral("iBootAppDetails"),
                                  QStringLiteral("iPromotionInfoRect"), QStringLiteral("iBootImageBtn") })
        visibleUntilCustomLoaderReady(boot, visualId, loaderId);
    for (const auto &id : { QStringLiteral("iTimer"), QStringLiteral("iBootTipstimer") }) {
        for (const auto &object : objects(boot)) {
            if (object.id == id) { property(boot, object, QStringLiteral("running"), QStringLiteral("false"), true); break; }
        }
    }
    assignments(boot, loaderId);
    const QString stockVisibility = QStringLiteral("!%1.item").arg(loaderId);
    for (const auto &name : {QStringLiteral("pBootImageButtonVisible"), QStringLiteral("pBootGridVisible")}) {
        const auto roots = objects(boot);
        if (!roots.isEmpty()) property(boot, roots.first(), name, stockVisibility, false);
    }
    QString error;
    if (!QtResources::replaceResource(data, controller.entry, controller.link, boot.toUtf8(), &error)) return { false, error };
    QStringList controllerNames = controller.link.sourceNames;
    if (!controllerNames.contains(controller.link.sourceName)) controllerNames << controller.link.sourceName;
    const QStringList controllerIds { QStringLiteral("iBootImage"), QStringLiteral("iBootProgressContainer"),
                                      QStringLiteral("iBootLoadingText"), QStringLiteral("iBootTipstimer"),
                                      QStringLiteral("pBootImageButtonVisible") };
    QtResources::invalidateUnits(data, controllerNames, controllerIds);
    if (QtResources::activeUnits(data, controllerNames, controllerIds) > 0)
        return { false, QStringLiteral("The boot controller's embedded compiled QML is still active; no boot patch was committed.") };
    bool holdApplied = false;
    if (!hosts.isEmpty()) {
        QString host = hosts.first().text;
        if (!updateHost(host, holdMs, &holdApplied, &error)) return { false, error };
        if (host != hosts.first().text) {
            const auto &hostStream = hosts.first();
            if (!QtResources::replaceResource(data, hostStream.entry, hostStream.link, host.toUtf8(), &error))
                return { false, error };
            QStringList hostNames = hostStream.link.sourceNames;
            if (!hostNames.contains(hostStream.link.sourceName)) hostNames << hostStream.link.sourceName;
            const QStringList hostIds { QStringLiteral("iBootProgressComponent"), QStringLiteral("iBootLoader"),
                                        QStringLiteral("iGlContainerComponent") };
            QtResources::invalidateUnits(data, hostNames, hostIds);
            if (QtResources::activeUnits(data, hostNames, hostIds) > 0)
                return { false, QStringLiteral("The boot host's embedded compiled QML is still active; no boot patch was committed.") };
        }
    }
    const QDir install(QFileInfo(exePath).absolutePath());
    const QString assets = install.filePath(QStringLiteral("Assets"));
    const QFileInfo assetInfo(assets);
    if (assetInfo.isSymLink() || assetInfo.isJunction())
        return { false, QStringLiteral("Linked Assets/player directories are not overwritten.") };

    QByteArray previousHelper;
    bool hadHelper = false;
    {
        const QFileInfo helperInfo(helperPath);
        if (helperInfo.isSymLink() || helperInfo.isJunction())
            return { false, QStringLiteral("Linked Assets/player files are not overwritten.") };
        hadHelper = helperInfo.exists();
        if (hadHelper) {
            constexpr qint64 maxHelperBytes = 5ll * 1024 * 1024;
            if (helperInfo.size() > maxHelperBytes) return { false, QStringLiteral("The existing BootVideo.qml is too large to preserve safely.") };
            QFile saved(helperPath);
            if (!saved.open(QIODevice::ReadOnly) || !readBounded(saved, maxHelperBytes, &previousHelper))
                return { false, QStringLiteral("Cannot preserve the existing BootVideo.qml.") };
        }
    }

    const QString htmlPath = QDir(assets).filePath(QStringLiteral("BootScreen.html"));
    const QString htmlBackupPath = htmlPath + QStringLiteral(".bak");
    QByteArray previousHtml;
    bool hadHtml = false;
    bool createHtmlBackup = false;
    {
        const QFileInfo htmlInfo(htmlPath);
        const QFileInfo htmlBackupInfo(htmlBackupPath);
        if (htmlBackupInfo.isSymLink() || htmlBackupInfo.isJunction())
            return { false, QStringLiteral("Linked Assets/player files are not overwritten.") };
        createHtmlBackup = htmlInfo.exists() && !htmlBackupInfo.exists();
        if (htmlInfo.isSymLink() || htmlInfo.isJunction())
            return { false, QStringLiteral("Linked Assets/player files are not overwritten.") };
        hadHtml = htmlInfo.exists();
        if (hadHtml) {
            constexpr qint64 maxHtmlBytes = 5ll * 1024 * 1024;
            if (htmlInfo.size() > maxHtmlBytes) return { false, QStringLiteral("The existing BootScreen.html is too large to preserve safely.") };
            QFile saved(htmlPath);
            if (!saved.open(QIODevice::ReadOnly) || !readBounded(saved, maxHtmlBytes, &previousHtml))
                return { false, QStringLiteral("Cannot preserve the existing BootScreen.html.") };
        }
    }

    QString videoBackupPath;
    QByteArray previousVideo;
    bool hadVideo = false;
    if (copySelectedVideo) {
        const QFileInfo movieInfo(movie);
        videoBackupPath = movie + QStringLiteral(".bak");
        const QFileInfo videoBackupInfo(videoBackupPath);
        if (movieInfo.isSymLink() || movieInfo.isJunction() || videoBackupInfo.isSymLink() || videoBackupInfo.isJunction())
            return { false, QStringLiteral("Linked Promo.mp4 files are not overwritten.") };
        if (movieInfo.exists()) {
            if (movieInfo.size() > 128ll * 1024 * 1024)
                return { false, QStringLiteral("The existing Promo.mp4 is too large to back up safely.") };
            if (videoBackupInfo.exists())
                return { false, QStringLiteral("Assets/Promo.mp4.bak already exists. Preserve or move it before replacing the current boot video.") };
            QFile saved(movie);
            if (!saved.open(QIODevice::ReadOnly) || !readBounded(saved, 128ll * 1024 * 1024, &previousVideo)
                || previousVideo.size() != movieInfo.size())
                return { false, QStringLiteral("Cannot preserve the existing Promo.mp4; no files were changed.") };
            hadVideo = true;
        }
    }
    if (!ThemeEngine::backup(exePath)) return { false, QStringLiteral("Could not preserve the original .bak; no EXE was written.") };
    if (!QDir().mkpath(assets))
        return { false, QStringLiteral("Cannot create the selected installation's Assets folder.") };

    bool helperWritten = false;
    bool htmlWritten = false;
    bool videoWritten = false;
    auto rollbackHelper = [&] {
        if (!helperWritten) return true;
        return hadHelper ? writeAsset(helperPath, previousHelper) : (QFile::remove(helperPath) || !QFile::exists(helperPath));
    };
    auto rollbackHtml = [&] {
        if (!htmlWritten) return true;
        return hadHtml ? writeAsset(htmlPath, previousHtml) : (QFile::remove(htmlPath) || !QFile::exists(htmlPath));
    };
    auto rollbackVideo = [&] {
        if (!videoWritten) return true;
        return hadVideo ? writeAsset(movie, previousVideo) : (QFile::remove(movie) || !QFile::exists(movie));
    };
    if (copySelectedVideo) {
        if (hadVideo && !writeAsset(videoBackupPath, previousVideo))
            return { false, QStringLiteral("Cannot create Promo.mp4.bak; the existing video was not replaced.") };
        if (!writeAsset(movie, selectedVideo))
            return { false, QStringLiteral("Cannot atomically install the selected MP4 as Assets/Promo.mp4.") };
        videoWritten = true;
    }
    if (createHtmlBackup && !writeAsset(htmlBackupPath, previousHtml)) {
        const bool restored = rollbackVideo();
        return { false, restored ? QStringLiteral("Cannot preserve the existing Assets/BootScreen.html as BootScreen.html.bak; no boot assets were replaced.")
                                 : QStringLiteral("Cannot preserve BootScreen.html, and the selected video could not be restored.") };
    }
    const QByteArray helper = externalPlayer(logoUrl, mode == QLatin1String("video")).toUtf8();
    if (!writeAsset(helperPath, helper)) {
        const bool restored = rollbackVideo();
        return { false, restored ? QStringLiteral("Cannot atomically write Assets/BootVideo.qml; no executable was written.")
                                 : QStringLiteral("Cannot write Assets/BootVideo.qml, and the previous Promo.mp4 could not be restored.") };
    }
    helperWritten = true;
    const QByteArray html = htmlBootPage(logoUrl, mode == QLatin1String("video")).toUtf8();
    if (!writeAsset(htmlPath, html)) {
        const bool helperRestored = rollbackHelper();
        const bool videoRestored = rollbackVideo();
        return { false, helperRestored && videoRestored
                            ? QStringLiteral("Cannot atomically write Assets/BootScreen.html; no executable was written.")
                            : QStringLiteral("Cannot write Assets/BootScreen.html, and a previous boot asset could not be restored.") };
    }
    htmlWritten = true;
    if (!PatchFile::writeVerified(exePath, original, data, &error)) {
        const bool helperRestored = rollbackHelper();
        const bool htmlRestored = rollbackHtml();
        const bool videoRestored = rollbackVideo();
        if (!helperRestored) error += QStringLiteral(" The boot-screen helper could not be rolled back.");
        if (!htmlRestored) error += QStringLiteral(" BootScreen.html could not be rolled back.");
        if (!videoRestored) error += QStringLiteral(" The previous Promo.mp4 could not be restored.");
        return { false, error };
    }
    QtResources::clearPlayerQmlCache(exePath);
    const QString modeMessage = mode == QLatin1String("modern")
        ? QStringLiteral("Built-in HTML boot screen installed in Assets/BootScreen.html.")
        : QStringLiteral("Custom HTML boot screen installed in Assets/BootScreen.html. Video: %1.%2")
            .arg(QDir::toNativeSeparators(movie), QFileInfo::exists(movie) ? QString() : QStringLiteral(" Copy Promo.mp4 into Assets before starting."));
    const QString holdMessage = holdApplied
        ? QStringLiteral("Requested extra boot hold applied: %1 ms.").arg(holdMs)
        : holdMs > 0
            ? QStringLiteral("The boot screen was installed, but this player build has no supported completion gate; the requested %1 ms hold was not applied.").arg(holdMs)
            : QStringLiteral("No extra boot hold was requested.");
    return { true, QStringLiteral("%1 %2 Restart BlueStacks.").arg(modeMessage, holdMessage) };

}
} // namespace BootVideoPatcher
