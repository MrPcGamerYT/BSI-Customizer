#include "BootVideoPatcher.h"
#include "ByteUtils.h"
#include "BsDetector.h"
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
        edits << qMakePair(m.capturedStart(2), qMakePair(m.capturedLength(2), m.captured(1).startsWith(QLatin1String("iBootLoadingText")) ? QStringLiteral("true") : QStringLiteral("false")));
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


QString externalPlayer()
{
    // Adapted from the supplied script. Keep module imports inside dynamically
    // created components, so an unavailable QtMultimedia module cannot prevent
    // the boot controller itself from loading. No module is copied into the
    // emulator, and no Loader is ever given an MP4 as its QML source.
    return QStringLiteral(R"QML(import QtQuick 2.15
Item {
    id: root
    anchors.fill: parent
    clip: true
    Rectangle { anchors.fill: parent; color: "#000000" }
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
        catch(e) { console.warn("Boot video needs QtMultimedia or QtWebEngine and a supported MP4 codec: " + e) }
    }
}
)QML");
}

bool writeAsset(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

// Preserve the Android GL container and only gate the known boot Loader. The
// original controller continues to own loading text/progress and Android ads.
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

Result patch(const QString &exePath, int holdMs)
{
    if (holdMs < 0 || holdMs > 15000)
        return { false, QStringLiteral("Boot hold must be between 0 and 15000 ms.") };
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before patching boot video.") };
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return { false, QStringLiteral("Cannot read HD-Player.exe.") };
    const QByteArray original = file.readAll(); file.close();
    if (PeUtils::sections(original).isEmpty())
        return {false, QStringLiteral("This file is not a player executable; nothing was written.")};
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
    if (controllers.size() > 1) {
        // Several OEM/portrait variants may coexist. Use the type instantiated
        // by the host's boot Component when it unambiguously identifies one.
        QSet<qint64> referenced;
        for (const auto &host : candidates) {
            if (!bootHost(host.text)) continue;
            for (const auto &component : objects(host.text)) {
                if (component.id != QLatin1String("iBootProgressComponent")) continue;
                const auto body = host.text.mid(component.body, component.end - component.body - 1);
                const auto code = mask(body);
                for (const auto &child : objects(body)) {
                    if (depthAt(code, 0, child.begin) != 0) continue;
                    for (const auto &controller : controllers)
                        if (controller.link.sourceName == child.type + QStringLiteral(".qml")) referenced.insert(controller.entry);
                }
            }
        }
        if (referenced.size() == 1) {
            const auto chosen = *referenced.cbegin();
            for (int i = controllers.size() - 1; i >= 0; --i) if (controllers[i].entry != chosen) controllers.removeAt(i);
        }
    }
    if (controllers.size() != 1 || players.size() > 1)
        return { false, QStringLiteral("Found %1 linked boot controller(s) and %2 stock player(s), but no unique supported boot layout. No EXE was written; an unrecognized version cannot be patched by guessing.").arg(controllers.size()).arg(players.size()) };
    const auto controller = controllers.first();
    QString boot = controller.text;
    const QString movie = QDir(QFileInfo(exePath).absolutePath()).filePath(QStringLiteral("Assets/Promo.mp4"));
    const QString url = QLatin1Char('"') + QUrl::fromLocalFile(movie).toString(QUrl::FullyEncoded) + QLatin1Char('"');
    QByteArray data = original;
    bool reusedPlayer = false;
    if (players.size() == 1) {
        auto player = players.first();
        for (const auto &object : objects(player.text)) {
            if (object.type == QLatin1String("Video") || object.type == QLatin1String("MediaPlayer")) {
                property(player.text, object, QStringLiteral("source"), url, true);
                QString error;
                if (!QtResources::replaceResource(data, player.entry, player.link, player.text.toUtf8(), &error)) return { false, error };
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
    } else {
        const QString helperPath = QDir(QFileInfo(exePath).absolutePath()).filePath(QStringLiteral("Assets/BootVideo.qml"));
        const QString helperUrl = QLatin1Char('"') + QUrl::fromLocalFile(helperPath).toString(QUrl::FullyEncoded) + QLatin1Char('"');
        const QString id = loaders.isEmpty() || loaders.first().id.isEmpty() ? QStringLiteral("pv") : loaders.first().id;
        const QString loader = QStringLiteral("Loader{id:%1;objectName:\"bsiBootVideoLoader\";z:-1;anchors.fill:parent;source:%2}").arg(id, helperUrl);
        if (!loaders.isEmpty()) boot.replace(loaders.first().begin, loaders.first().end-loaders.first().begin, loader);
        else {
            const auto all = objects(boot);
            if (all.isEmpty()) return { false, QStringLiteral("Cannot locate the boot QML root object.") };
            boot.insert(all.first().body, loader);
        }
    }
    visible(boot, QStringLiteral("iBootImage"), false);
    visible(boot, QStringLiteral("iBootLoadingText"), true);
    assignments(boot);
    for (const auto &name : {QStringLiteral("pBootImageButtonVisible"), QStringLiteral("pBootGridVisible")}) {
        const auto roots = objects(boot);
        if (!roots.isEmpty()) property(boot, roots.first(), name, QStringLiteral("false"), false);
    }
    QString error;
    if (!QtResources::replaceResource(data, controller.entry, controller.link, boot.toUtf8(), &error)) return { false, error };
    bool holdApplied = false;
    QList<Stream> hosts;
    for (const auto &candidate : candidates) if (bootHost(candidate.text)) hosts << candidate;
    if (hosts.size() > 1) return { false, QStringLiteral("Multiple boot hosts were found; no executable was written.") };
    if (!hosts.isEmpty()) {
        QString host = hosts.first().text;
        if (!updateHost(host, holdMs, &holdApplied, &error)) return { false, error };
        if (host != hosts.first().text && !QtResources::replaceResource(data, hosts.first().entry, hosts.first().link, host.toUtf8(), &error))
            return { false, error };
    }
    if (!ThemeEngine::backup(exePath)) return { false, QStringLiteral("Could not preserve the original .bak; no EXE was written.") };
    QString helperPath;
    QByteArray previousHelper;
    bool hadHelper = false;
    if (!reusedPlayer) {
        const QDir install(QFileInfo(exePath).absolutePath());
        const QString assets = install.filePath(QStringLiteral("Assets"));
        const QFileInfo assetInfo(assets);
        helperPath = QDir(assets).filePath(QStringLiteral("BootVideo.qml"));
        const QFileInfo helperInfo(helperPath);
        if (assetInfo.isSymLink() || assetInfo.isJunction() || helperInfo.isSymLink() || helperInfo.isJunction())
            return { false, QStringLiteral("Linked Assets/player files are not overwritten.") };
        hadHelper = helperInfo.exists();
        if (hadHelper) {
            QFile saved(helperPath);
            if (!saved.open(QIODevice::ReadOnly)) return { false, QStringLiteral("Cannot preserve the existing BootVideo.qml.") };
            previousHelper = saved.readAll();
        }
        if (!QDir().mkpath(assets) || !writeAsset(helperPath, externalPlayer().toUtf8()))
            return { false, QStringLiteral("Cannot atomically write Assets/BootVideo.qml; no executable was written.") };
    }
    if (!PatchFile::writeVerified(exePath, original, data, &error)) {
        if (!helperPath.isEmpty()) {
            const bool restored = hadHelper ? writeAsset(helperPath, previousHelper) : QFile::remove(helperPath);
            if (!restored) error += QStringLiteral(" The external player could not be rolled back.");
        }
        return { false, error };
    }
    QtResources::clearPlayerQmlCache(exePath);
    return { true, QStringLiteral("Boot video written, caches refreshed and bytes verified. Video: %1. %2 Restart BlueStacks.%3")
        .arg(QDir::toNativeSeparators(movie),
             holdApplied ? QStringLiteral("Extra boot hold: %1 ms.").arg(holdMs)
                         : QStringLiteral("No supported completion gate was found; no extra hold was applied."),
             QFileInfo::exists(movie) ? QString() : QStringLiteral(" Copy Promo.mp4 into Assets before starting.")) };

}
} // namespace BootVideoPatcher
