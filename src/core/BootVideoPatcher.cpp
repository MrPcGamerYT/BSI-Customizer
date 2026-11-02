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
void visible(QString &qml, const QString &id, bool value)
{
    for (const auto &object : objects(qml)) {
        if (object.id == id) { property(qml, object, QStringLiteral("visible"), value ? QStringLiteral("true") : QStringLiteral("false"), true); break; }
    }
}
void assignments(QString &qml)
{
    const auto code = mask(qml);
    const QRegularExpression re(QStringLiteral("\\b(iBootImage\\.visible|pBootImageButtonVisible|pBootGridVisible|iBootLoadingText\\.visible)\\s*=\\s*(true|false)\\b"));
    QList<QPair<int, QPair<int, QString>>> edits;
    auto matches = re.globalMatch(code);
    while (matches.hasNext()) {
        const auto m = matches.next();
        // The modern/custom player supplies its own status text. Keep every
        // stock boot overlay hidden if promotion callbacks run after patching.
        edits << qMakePair(m.capturedStart(2), qMakePair(m.capturedLength(2), QStringLiteral("false")));
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

QString externalPlayer(const QString &logoUrl)
{
    // The video remains the only background content. The emulator's own logo
    // and a quiet, indeterminate progress track are the complete overlay: no
    // product slogans, status labels, or BSI attribution are shown.
    QString qml = QString::fromUtf8(R"QML(import QtQuick 2.15
Item {
    id: root
    anchors.fill: parent
    clip: true
    Rectangle { anchors.fill: parent; color: "#02060B" }
    Rectangle { z: 8; anchors.fill: parent; color: "#06101C"; opacity: 0.26 }
    Image {
        id: emulatorLogo
        objectName: "bsiEmulatorLogo"
        z: 10
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -parent.height * 0.055
        width: Math.min(parent.width * 0.26, 280)
        height: Math.min(parent.height * 0.25, 190)
        source: "__BSI_EMULATOR_LOGO__"
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        opacity: 0.94
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { to: 0.82; duration: 1900; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.94; duration: 1900; easing.type: Easing.InOutSine }
        }
    }
    Rectangle {
        id: progressTrack
        objectName: "bsiBootProgressTrack"
        z: 11
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.max(34, parent.height * 0.10)
        width: Math.min(parent.width * 0.34, 340)
        height: 3
        radius: 2
        color: "#35495A"
        opacity: 0.68
        Rectangle {
            id: progressLight
            width: parent.width * 0.28
            height: parent.height
            radius: 2
            color: "#D5E8F6"
            NumberAnimation on x {
                from: 0
                to: progressTrack.width - progressLight.width
                duration: 2500
                loops: Animation.Infinite
                easing.type: Easing.InOutSine
            }
        }
    }
    Component.onCompleted: {
        var u = Qt.resolvedUrl("Promo.mp4").toString()
        var src = JSON.stringify(u)
        var q6 = 'import QtQuick 2.15;import QtMultimedia;Item{anchors.fill:parent;VideoOutput{id:v;anchors.fill:parent;fillMode:VideoOutput.PreserveAspectCrop}MediaPlayer{objectName:"bsiBootVideo";id:m;source:'+src+';loops:MediaPlayer.Infinite;videoOutput:v;audioOutput:AudioOutput{muted:true} Component.onCompleted:play()}}'
        try { Qt.createQmlObject(q6, root, "promo6"); return } catch(e) {}
        var q5 = 'import QtQuick 2.15;import QtMultimedia 5.15;Video{objectName:"bsiBootVideo";anchors.fill:parent;source:'+src+';fillMode:VideoOutput.PreserveAspectCrop;loops:MediaPlayer.Infinite;muted:true;autoPlay:true;Component.onCompleted:play()}'
        try { Qt.createQmlObject(q5, root, "promo5"); return } catch(e) {}
        var safe = u.replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;")
        var html = '<html><body style="margin:0;background:#000;overflow:hidden"><video src="'+safe+'" autoplay muted loop playsinline style="width:100%;height:100%;object-fit:cover"></video></body></html>'
        var du = 'data:text/html;charset=utf-8,' + encodeURIComponent(html)
        var web = 'import QtQuick 2.15;import QtWebEngine;WebEngineView{anchors.fill:parent;url:'+JSON.stringify(du)+'}'
        try { Qt.createQmlObject(web, root, "promoWeb") }
        catch(e) { console.warn("Boot video requires a supported QtMultimedia or QtWebEngine playback runtime: " + e) }
    }
}
)QML");
    qml.replace(QLatin1String("__BSI_EMULATOR_LOGO__"), logoUrl);
    return qml;
}

QString modernBootPlayer(const QString &logoUrl)
{
    // Quiet QtQuick-only splash: the real logo from this player build, smooth
    // motion, and no text, external video, codecs, or network dependencies.
    QString qml = QString::fromUtf8(R"MODERN(import QtQuick 2.15
Item {
    id: root
    objectName: "bsiModernBoot"
    anchors.fill: parent
    property real side: Math.max(1, Math.min(width, height))
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#070D15" }
            GradientStop { position: 0.54; color: "#0A1724" }
            GradientStop { position: 1.0; color: "#050A10" }
        }
    }
    Rectangle {
        width: Math.min(root.side * 0.72, 820)
        height: width
        radius: width / 2
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -root.height * 0.025
        color: "#163B58"
        opacity: 0.13
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { to: 0.19; duration: 2600; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.11; duration: 2600; easing.type: Easing.InOutSine }
        }
    }
    Rectangle {
        width: Math.min(root.side * 0.47, 480)
        height: width
        radius: width / 2
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -root.height * 0.025
        color: "transparent"
        border.width: 1
        border.color: "#4A657B"
        opacity: 0.28
        RotationAnimation on rotation {
            from: 0; to: 360; duration: 36000; loops: Animation.Infinite
            easing.type: Easing.Linear
        }
    }
    Image {
        id: emulatorLogo
        objectName: "bsiEmulatorLogo"
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -root.height * 0.035
        width: Math.min(root.width * 0.25, 270)
        height: Math.min(root.height * 0.24, 180)
        source: "__BSI_EMULATOR_LOGO__"
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        opacity: 0.96
        SequentialAnimation on scale {
            loops: Animation.Infinite
            NumberAnimation { to: 1.018; duration: 2300; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1.0; duration: 2300; easing.type: Easing.InOutSine }
        }
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { to: 0.82; duration: 2500; easing.type: Easing.InOutSine }
            NumberAnimation { to: 0.96; duration: 2500; easing.type: Easing.InOutSine }
        }
    }
    Rectangle {
        id: progressTrack
        objectName: "progressTrack"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Math.max(34, root.height * 0.10)
        width: Math.min(root.width * 0.34, 360)
        height: 3
        radius: 2
        color: "#263747"
        Rectangle {
            id: progressGlow
            width: parent.width * 0.28
            height: parent.height
            radius: 2
            color: "#D8EBF9"
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 1.8
                height: 7
                radius: 4
                color: "#B8D9EE"
                opacity: 0.16
            }
            NumberAnimation on x {
                from: 0
                to: progressTrack.width - progressGlow.width
                duration: 2650
                loops: Animation.Infinite
                easing.type: Easing.InOutSine
            }
        }
    }
}
)MODERN");
    qml.replace(QLatin1String("__BSI_EMULATOR_LOGO__"), logoUrl);
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
    const QString url = QLatin1Char('"') + QUrl::fromLocalFile(movie).toString(QUrl::FullyEncoded) + QLatin1Char('"');
    QByteArray data = original;
    bool reusedPlayer = false;
    bool stockReferenced = false;
    const QRegularExpression stockSource(QStringLiteral("\\bsource\\s*:\\s*['\"]([^'\"]*BootVideo\\.qml)['\"]"));
    for (const auto &object : objects(boot)) {
        if (object.type != QLatin1String("Loader")) continue;
        const auto match = stockSource.match(boot.mid(object.begin, object.end-object.begin));
        if (!match.hasMatch()) continue;
        const QUrl source(match.captured(1));
        if (source.scheme().isEmpty() || source.scheme() == QLatin1String("qrc")) stockReferenced = true;
    }
    // An unrelated embedded BootVideo.qml is not a stock player in use. In
    // particular, never reinterpret our external file:// loader as a reference
    // to that embedded file when repatching.
    if (mode == QLatin1String("video") && videoPath.isEmpty() && players.size() == 1 && stockReferenced) {
        auto player = players.first();
        for (const auto &object : objects(player.text)) {
            if (object.type == QLatin1String("Video") || object.type == QLatin1String("MediaPlayer")) {
                property(player.text, object, QStringLiteral("source"), url, true);
                QString error;
                if (!QtResources::replaceResource(data, player.entry, player.link, player.text.toUtf8(), &error)) return { false, error };
                QStringList playerNames = player.link.sourceNames;
                if (!playerNames.contains(player.link.sourceName)) playerNames << player.link.sourceName;
                QStringList playerIds { QStringLiteral("bsiBootVideo"), QStringLiteral("bsiModernBoot") };
                for (const auto &candidate : objects(player.text))
                    if (candidate.id.size() >= 5 && !playerIds.contains(candidate.id)) playerIds << candidate.id;
                QtResources::invalidateUnits(data, playerNames, playerIds);
                if (QtResources::activeUnits(data, playerNames, playerIds) > 0)
                    return { false, QStringLiteral("The embedded boot-video compiled QML is still active; no boot patch was committed.") };
                reusedPlayer = true; break;
            }
        }
    }
    QList<Object> loaders;
    for (const auto &object : objects(boot)) {
        if (object.type == QLatin1String("Loader") && (object.id == QLatin1String("pv")
            || boot.mid(object.begin, object.end - object.begin).contains(QLatin1String("BootVideo.qml")))) loaders << object;
    }
    if (loaders.size() > 1) return { false, QStringLiteral("Multiple boot-video loaders were found; no partial patch was written.") };
    if (reusedPlayer) {
        if (loaders.size() != 1) return { false, QStringLiteral("The stock video component has no unique boot loader. No file was written.") };
        const QString loaderId = loaders.first().id;
        // Re-find after every splice; property lengths/offsets can change.
        for (const QString &key : { QStringLiteral("active"), QStringLiteral("visible") }) {
            for (const auto &object : objects(boot)) {
                if (object.type == QLatin1String("Loader") && object.id == loaderId) { property(boot, object, key, QStringLiteral("true"), true); break; }
            }
        }
        for (const auto &object : objects(boot)) {
            if (object.type == QLatin1String("Loader") && object.id == loaderId) {
                property(boot, object, QStringLiteral("z"), QStringLiteral("10000"), true);
                property(boot, object, QStringLiteral("objectName"), QStringLiteral("\"bsiBootLoader\""), true);
                break;
            }
        }
    } else {
        const QString helperPath = QDir(QFileInfo(exePath).absolutePath()).filePath(QStringLiteral("Assets/BootVideo.qml"));
        const QString helperUrl = QLatin1Char('"') + QUrl::fromLocalFile(helperPath).toString(QUrl::FullyEncoded) + QLatin1Char('"');
        const QString id = loaders.isEmpty() || loaders.first().id.isEmpty() ? QStringLiteral("pv") : loaders.first().id;
        // The replacement is the complete splash, not an underlay. Keep it
        // above the stock progress panel, promotion image and loading text.
        const QString loader = QStringLiteral("Loader{id:%1;objectName:\"bsiBootLoader\";z:10000;anchors.fill:parent;source:%2}").arg(id, helperUrl);
        if (!loaders.isEmpty()) boot.replace(loaders.first().begin, loaders.first().end-loaders.first().begin, loader);
        else {
            const auto all = objects(boot);
            if (all.isEmpty()) return { false, QStringLiteral("Cannot locate the boot QML root object.") };
            boot.insert(all.first().body, loader);
        }
    }
    for (const auto &id : { QStringLiteral("iBootImage"), QStringLiteral("iBootLoadingText"),
                            QStringLiteral("iBootProgressContainer"), QStringLiteral("iBootAppImage"),
                            QStringLiteral("iBootAppTitle"), QStringLiteral("iBootAppDetails"),
                            QStringLiteral("iPromotionInfoRect"), QStringLiteral("iBootImageBtn") })
        visible(boot, id, false);
    for (const auto &id : { QStringLiteral("iTimer"), QStringLiteral("iBootTipstimer") }) {
        for (const auto &object : objects(boot)) {
            if (object.id == id) { property(boot, object, QStringLiteral("running"), QStringLiteral("false"), true); break; }
        }
    }
    assignments(boot);
    for (const auto &name : {QStringLiteral("pBootImageButtonVisible"), QStringLiteral("pBootGridVisible")}) {
        const auto roots = objects(boot);
        if (!roots.isEmpty()) property(boot, roots.first(), name, QStringLiteral("false"), false);
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

    QString helperPath;
    QByteArray previousHelper;
    bool hadHelper = false;
    if (!reusedPlayer) {
        helperPath = QDir(assets).filePath(QStringLiteral("BootVideo.qml"));
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
    if ((copySelectedVideo || !reusedPlayer) && !QDir().mkpath(assets))
        return { false, QStringLiteral("Cannot create the selected installation's Assets folder.") };

    bool helperWritten = false;
    bool videoWritten = false;
    auto rollbackHelper = [&] {
        if (!helperWritten) return true;
        return hadHelper ? writeAsset(helperPath, previousHelper) : (QFile::remove(helperPath) || !QFile::exists(helperPath));
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
    if (!reusedPlayer) {
        const QByteArray helper = (mode == QLatin1String("modern") ? modernBootPlayer(logoUrl) : externalPlayer(logoUrl)).toUtf8();
        if (!writeAsset(helperPath, helper)) {
            const bool restored = rollbackVideo();
            return { false, restored ? QStringLiteral("Cannot atomically write Assets/BootVideo.qml; no executable was written.")
                                     : QStringLiteral("Cannot write Assets/BootVideo.qml, and the previous Promo.mp4 could not be restored.") };
        }
        helperWritten = true;
    }
    if (!PatchFile::writeVerified(exePath, original, data, &error)) {
        const bool helperRestored = rollbackHelper();
        const bool videoRestored = rollbackVideo();
        if (!helperRestored) error += QStringLiteral(" The boot-screen helper could not be rolled back.");
        if (!videoRestored) error += QStringLiteral(" The previous Promo.mp4 could not be restored.");
        return { false, error };
    }
    QtResources::clearPlayerQmlCache(exePath);
    const QString modeMessage = mode == QLatin1String("modern")
        ? QStringLiteral("Built-in modern boot screen installed; no extra video or codec is required.")
        : QStringLiteral("Custom boot video with the modern BSI overlay installed. Video: %1.%2")
            .arg(QDir::toNativeSeparators(movie), QFileInfo::exists(movie) ? QString() : QStringLiteral(" Copy Promo.mp4 into Assets before starting."));
    const QString holdMessage = holdApplied
        ? QStringLiteral("Requested extra boot hold applied: %1 ms.").arg(holdMs)
        : holdMs > 0
            ? QStringLiteral("The boot screen was installed, but this player build has no supported completion gate; the requested %1 ms hold was not applied.").arg(holdMs)
            : QStringLiteral("No extra boot hold was requested.");
    return { true, QStringLiteral("%1 %2 Restart BlueStacks.").arg(modeMessage, holdMessage) };

}
} // namespace BootVideoPatcher
