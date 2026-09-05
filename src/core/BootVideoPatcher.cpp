#include "BootVideoPatcher.h"
#include "ByteUtils.h"
#include "BsDetector.h"
#include "PatchFile.h"
#include "QtResources.h"
#include "ThemeEngine.h"
#include "ZlibUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
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
struct Stream { qint64 entry; QString text; QtResources::Link link; };
QList<Stream> streams(const QByteArray &data)
{
    QList<Stream> result;
    for (qint64 at : ZlibUtils::findMagicOffsets(data)) {
        if (at < 8) continue;
        const quint32 stored = be32(data, at - 8), size = be32(data, at - 4);
        if (stored < 6 || stored > 5000004 || !size || size > 5000000 || !inRange(data, at - 4, stored)) continue;
        const auto plain = qUncompress(data.mid(at - 4, stored));
        if (plain.size() != size) continue;
        const QString qml = QString::fromUtf8(plain);
        if (!qml.contains(QLatin1String("fSetBootPromotion")) && !qml.contains(QLatin1String("BootVideo"))
            && !qml.contains(QLatin1String("QtMultimedia")) && !qml.contains(QLatin1String("MediaPlayer"))
            && !qml.contains(QLatin1String("Video{")) && !qml.contains(QLatin1String("Video {"))) continue;
        QtResources::Link link;
        const QStringList names = qml.contains(QLatin1String("fSetBootPromotion"))
            ? QStringList { QStringLiteral("PlrBootProgressBar.qml"), QStringLiteral("boot.qml"), QStringLiteral("Boot.qml"), QStringLiteral("BootScreen.qml") }
            : QStringList { QStringLiteral("BootVideo.qml"), QStringLiteral("bootvideo.qml") };
        for (const auto &name : names) { link = QtResources::findLink(data, at - 8, name); if (link.found && !link.ambiguous) break; }
        if (!link.found) link = QtResources::findLink(data, at - 8, QString()); // explicit registered aliases
        if (link.found && !link.ambiguous) result << Stream { at - 8, qml, link };
    }
    return result;
}
}

Result patch(const QString &exePath)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before patching boot video.") };
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return { false, QStringLiteral("Cannot read HD-Player.exe.") };
    const QByteArray original = file.readAll(); file.close();
    const auto candidates = streams(original);
    QList<Stream> controllers, players;
    for (const auto &stream : candidates) {
        if (stream.text.contains(QLatin1String("fSetBootPromotion")) && stream.text.contains(QLatin1String("iBootImage"))) controllers << stream;
        if (stream.link.sourceName.compare(QLatin1String("BootVideo.qml"), Qt::CaseInsensitive) == 0) players << stream;
    }
    if (controllers.size() != 1 || players.size() > 1)
        return { false, QStringLiteral("A unique linked boot QML resource is required. No EXE was written; use --theme-probe to inspect this build.") };
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
                if (!QtResources::replaceCompressed(data, player.entry, player.link, player.text.toUtf8(), &error)) return { false, error };
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
        // A Loader loads QML, not an MP4. Only add a Video component when an
        // existing resource already proves that QtMultimedia is available.
        const QRegularExpression mediaImport(QStringLiteral("(?:^|\\n)\\s*import\\s+QtMultimedia(?:\\s+([0-9]+(?:\\.[0-9]+)?))?(?:\\s+as\\s+[A-Za-z_][A-Za-z0-9_]*)?"));
        auto media = mediaImport.match(boot);
        if (!media.hasMatch()) for (const auto &candidate : candidates) { media = mediaImport.match(candidate.text); if (media.hasMatch()) break; }
        if (!media.hasMatch()) {
            // In real MSI/BlueStacks builds the boot screen is PlrBootProgressBar,
            // while QtMultimedia is imported by another file in the same pool
            // (e.g. the notification window). Do not require BootVideo.qml to exist.
            for (const auto &resource : QtResources::filesInPool(original, controller.link)) {
                if (!resource.name.endsWith(QLatin1String(".qml"))) continue;
                const auto source = QtResources::readFile(original, resource);
                if (source.isEmpty()) continue;
                media = mediaImport.match(QString::fromUtf8(source));
                if (media.hasMatch()) break;
            }
        }
        if (!media.hasMatch()) return { false, QStringLiteral("The boot player type is not supported in this build. A real Video/MediaPlayer resource or an existing QtMultimedia import is required; no file was written.") };
        const QString import = QStringLiteral("import QtMultimedia%1 as BsiMedia\n").arg(media.captured(1).isEmpty() ? QString() : QLatin1Char(' ') + media.captured(1));
        const QString id = loaders.isEmpty() || loaders.first().id.isEmpty() ? QStringLiteral("pv") : loaders.first().id;
        const QString loader = QStringLiteral("Loader{id:%1;z:0;anchors.fill:parent;sourceComponent:Component{BsiMedia.Video{objectName:\"bsiBootVideo\";anchors.fill:parent;source:%2;loops:BsiMedia.MediaPlayer.Infinite;fillMode:BsiMedia.VideoOutput.PreserveAspectCrop;Component.onCompleted:play()}}}")
                                   .arg(id, url);
        if (!loaders.isEmpty()) boot.replace(loaders.first().begin, loaders.first().end - loaders.first().begin, loader);
        else {
            const auto all = objects(boot);
            if (all.isEmpty()) return { false, QStringLiteral("Cannot locate the boot QML root object.") };
            boot.insert(all.first().body, loader);
        }
        if (!boot.contains(import.trimmed())) boot.prepend(import);
    }
    visible(boot, QStringLiteral("iBootImage"), false);
    visible(boot, QStringLiteral("iBootLoadingText"), true);
    assignments(boot);
    QString error;
    if (!QtResources::replaceCompressed(data, controller.entry, controller.link, boot.toUtf8(), &error)) return { false, error };
    if (!ThemeEngine::backup(exePath)) return { false, QStringLiteral("Could not preserve the original .bak; no EXE was written.") };
    if (!PatchFile::writeVerified(exePath, original, data, &error)) return { false, error };
    return { true, QStringLiteral("Boot video resource written, caches refreshed and bytes verified. Video: %1. Restart BlueStacks.%2")
                       .arg(QDir::toNativeSeparators(movie), QFileInfo::exists(movie) ? QString() : QStringLiteral(" Copy Promo.mp4 into Assets before starting.")) };
}
} // namespace BootVideoPatcher
