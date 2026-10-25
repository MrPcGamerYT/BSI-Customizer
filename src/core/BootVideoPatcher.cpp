#include "BootVideoPatcher.h"

#include "ByteUtils.h"
#include "BsDetector.h"
#include "PatchFile.h"
#include "PeUtils.h"
#include "QtResources.h"
#include "ThemeEngine.h"
#include "ZlibUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

using namespace ByteUtils;

namespace BootVideoPatcher {
namespace {

// ---------------------------------------------------------------------------
// QML source helpers
// ---------------------------------------------------------------------------

// The same text with every comment and string literal blanked out. Structure is
// matched against this, values are read from the original.
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
        Object object;
        object.begin = match.capturedStart();
        object.body = match.capturedEnd();
        object.type = match.captured(1).section(QLatin1Char('.'), -1);
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
        int end = expressionEnd(code, rhs, object.end - 1);
        // Only the value is replaced: the spacing the file used around it stays,
        // so the rewritten line still reads like the one it replaces.
        int from = rhs, to = end;
        while (from < to && text.at(from).isSpace()) ++from;
        while (to > from && text.at(to - 1).isSpace()) --to;
        text.replace(from, to - from, value); return true;
    }
    if (add) { text.insert(object.body, name + QStringLiteral(": ") + value + QLatin1Char(';')); return true; }
    return false;
}

void visible(QString &qml, const QString &id, bool value)
{
    for (const auto &object : objects(qml)) {
        if (object.id != id) continue;
        // A missing property is the same as a visible one, so asking for a
        // visible object never adds a line; asking for a hidden one must.
        property(qml, object, QStringLiteral("visible"),
                 value ? QStringLiteral("true") : QStringLiteral("false"), !value);
        break;
    }
}

// The boot screen's own artwork is shown by imperative assignments inside
// fSetBootPromotion/fSetDefaultAppSpecificBootBanner as well as by bindings, and
// the promotion banner paints over the video. The video needs the loading text
// and nothing else.
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

bool replaceFirst(QString &text, const QRegularExpression &re, const QString &with, int *start = nullptr)
{
    const auto match = re.match(text);
    if (!match.hasMatch()) return false;
    if (start) *start = match.capturedStart();
    text.replace(match.capturedStart(), match.capturedLength(), with);
    return true;
}

QString lineEnding(const QString &text)
{
    return text.contains(QLatin1String("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
}

// The licence banner the player puts in front of every QML file. It is 200-odd
// bytes that are never displayed, and on a full record it is the cheapest room
// there is: the alternative is refusing the patch.
QString stripHeaderComment(const QString &text)
{
    int at = 0;
    while (at < text.size() && (text[at].isSpace() || text[at].unicode() == 0xfeff)) ++at;
    if (!text.mid(at, 2).startsWith(QLatin1String("/*"))) return text;
    const int end = text.indexOf(QLatin1String("*/"), at + 2);
    if (end < 0) return text;
    int next = end + 2;
    while (next < text.size() && (text[next] == QLatin1Char('\r') || text[next] == QLatin1Char('\n'))) ++next;
    return text.left(at) + text.mid(next);
}

// ---------------------------------------------------------------------------
// The two QML files that make up the boot screen
// ---------------------------------------------------------------------------
//
// Both are found by what they say, not by the name the resource tree gives them
// and not by their compressed size. `plr.qml` is the player window that hosts
// the boot screen; `PlrBootProgressBar.qml` is the boot screen itself. Builds
// rename them, and a build whose resource registration this tool cannot follow
// still has them - so the file is read as bytes, the way the player reads it.
enum class Kind { Other, Player, Controller, ForcedStub };

// Minified builds write `id:iBootImage`, the readable ones `id: iBootImage`.
bool hasId(const QString &qml, const QString &id)
{
    return qml.contains(QStringLiteral("id: ") + id) || qml.contains(QStringLiteral("id:") + id);
}

bool isPlayerWindow(const QString &qml)
{
    return qml.contains(QLatin1String("ApplicationWindow"))
        && hasId(qml, QStringLiteral("iAppwin"))
        && hasId(qml, QStringLiteral("iBootLoader"))
        && qml.contains(QLatin1String("iBootProgressComponent"));
}

bool isBootScreen(const QString &qml)
{
    return hasId(qml, QStringLiteral("iBootImage"))
        && qml.contains(QLatin1String("bootBackgroundImage"))
        && qml.contains(QLatin1String("fSetBootPromotion"));
}

Kind classify(const QString &qml)
{
    if (isPlayerWindow(qml)) return Kind::Player;
    if (isBootScreen(qml)) return Kind::Controller;
    // A boot screen an earlier experiment replaced with an empty function: the
    // artwork is gone, so it cannot be used as a base and has to be restored.
    if (qml.contains(QLatin1String("function fSetBootPromotion(a,b,c){}"))) return Kind::ForcedStub;
    return Kind::Other;
}

struct Stream
{
    qint64 entry = -1;    // first byte of [stored size][decoded size]
    qint64 zoff = -1;     // first byte of the zlib stream
    quint32 payload = 0;  // bytes stored behind the entry (size field included)
    QString text;
    Kind kind = Kind::Other;
};

bool plausibleRecordStart(const QByteArray &data, qint64 at)
{
    if (!inRange(data, at, 12)) return false;
    const quint32 payload = be32(data, at), plain = be32(data, at + 4);
    if (payload < 16 || payload > 5000004 || plain == 0 || plain > 5000000) return false;
    const int cmf = uchar(data[int(at + 8)]), flg = uchar(data[int(at + 9)]);
    return cmf == 0x78 && ((cmf << 8) | flg) % 31 == 0;
}

// Every QML source the file stores as a compressed record, in file order. The
// record format is Qt's: a big-endian stored size, a big-endian decoded size,
// then the zlib stream - the same two numbers the resource tree's node points
// at, which is why the record can be read (and written) without the tree.
QList<Stream> streams(const QByteArray &data)
{
    QList<Stream> result;
    for (const qint64 at : ZlibUtils::findMagicOffsets(data)) {
        if (at < 8) continue;
        const qint64 entry = at - 8;
        const quint32 payload = be32(data, entry), plain = be32(data, at - 4);
        if (payload < 16 || payload > 5000004 || !plain || plain > 5000000) continue;
        if (!inRange(data, at - 4, payload)) continue;
        const QByteArray raw = qUncompress(data.mid(at - 4, payload));
        if (raw.size() != qint64(plain)) continue;
        const QString text = QString::fromUtf8(raw);
        if (text.toUtf8() != raw) continue;
        const Kind kind = classify(text);
        if (kind == Kind::Other) continue;
        bool known = false;
        for (const auto &seen : result) known = known || seen.entry == entry;
        if (known) continue;
        result << Stream { entry, at, payload, text, kind };
    }
    return result;
}

// ---------------------------------------------------------------------------
// Writing a record back
// ---------------------------------------------------------------------------

// How much room the record has: its stored payload plus the zero padding behind
// it, which is where every player leaves slack. A run shorter than 16 bytes is
// not padding - it is the high bytes of the next record's size field - and is
// never counted.
qint64 recordRoom(const QByteArray &data, const Stream &stream)
{
    const qint64 oldEnd = stream.entry + 4 + qint64(stream.payload);
    if (!inRange(data, stream.entry, qint64(4) + stream.payload)) return 0;
    const qint64 limit = qMin<qint64>(data.size(), oldEnd + 1024 * 1024);
    qint64 pad = 0;
    for (qint64 at = oldEnd; at < limit; ++at) {
        if (uchar(data[int(at)]) != 0) break;
        if (plausibleRecordStart(data, at)) break;
        ++pad;
    }
    if (pad < 16) pad = 0;
    return 4 + qint64(stream.payload) + pad;
}

// Make Qt compile the QML again instead of reusing what it compiled before the
// edit. The tree node's timestamp is what the player's disk cache is checked
// against, so it moves to "now".
bool refreshTimestampFor(QByteArray &data, const Stream &stream)
{
    // An empty name accepts any resource: the node is found by the record it
    // points at, so a renamed boot screen still resolves.
    const auto link = QtResources::findLink(data, stream.entry, QString());
    if (link.found && !link.ambiguous && QtResources::refreshTimestamp(data, link)) return true;

    // No tree this tool can walk: the resource name is stored in the clear even
    // then, and the node that owns it sits next to it. Both bases can be derived
    // from the record that was just written.
    static const QStringList names {
        QStringLiteral("plr.qml"), QStringLiteral("Main.qml"),
        QStringLiteral("PlrBootProgressBar.qml"), QStringLiteral("boot.qml"),
        QStringLiteral("Boot.qml"), QStringLiteral("BootScreen.qml")
    };
    for (const QString &name : names) {
        QByteArray needle(name.size() * 2, '\0');
        for (int i = 0; i < name.size(); ++i) putBe16(needle, i * 2, name.at(i).unicode());
        for (qsizetype hit = data.indexOf(needle); hit >= 0; hit = data.indexOf(needle, hit + 1)) {
            const qint64 record = hit - 6;
            if (record < 0 || be16(data, record) != name.size()) continue;
            // The node stores the name's offset inside the name block and the
            // record's offset inside the data block, so both blocks can be
            // reconstructed from one node that fits this record.
            const qint64 from = qMax<qint64>(0, record - 0x20000);
            const qint64 to = qMin<qint64>(data.size() - 14, record + 0x200000);
            for (qint64 node = from; node < to; ++node) {
                const quint16 flags = be16(data, node + 4);
                if (flags != 0 && flags != 1) continue;
                if (!inRange(data, node + 14, 8)) continue;
                const quint64 stamp = quint64(le32(data, node + 14))
                                      | (quint64(le32(data, node + 18)) << 32);
                if (stamp < 1577836800000ULL || stamp > 2208988800000ULL) continue;
                const quint32 nameOffset = be32(data, node), dataOffset = be32(data, node + 10);
                if (quint64(record) < nameOffset || quint64(stream.entry) < dataOffset) continue;
                const qint64 nameBase = record - nameOffset, dataBase = stream.entry - dataOffset;
                if (dataBase < 0 || dataBase >= nameBase || nameBase >= record) continue;
                putBe64(data, node + 14, quint64(QDateTime::currentMSecsSinceEpoch()));
                return true;
            }
        }
    }
    return false;
}

bool writeStream(QByteArray &data, const Stream &stream, const QString &text, const QString &label,
                 bool cacheBust, QString *error)
{
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    const qint64 room = recordRoom(data, stream);
    QByteArray body = text.toUtf8();
    QByteArray packed = qCompress(body, 9);
    if (packed.size() <= 4) return fail(QStringLiteral("%1 could not be compressed.").arg(label));
    if (packed.size() > room) {
        const QString shorter = stripHeaderComment(text);
        if (shorter != text) {
            body = shorter.toUtf8();
            packed = qCompress(body, 9);
        }
    }
    if (packed.size() > room)
        return fail(QStringLiteral("The patched %1 needs %2 bytes and this build gives its record only %3. "
                                   "Growing the player is not allowed, so nothing was written.")
                        .arg(label).arg(packed.size()).arg(room));

    putBe32(data, stream.entry, quint32(packed.size()));
    putBe32(data, stream.entry + 4, quint32(body.size()));
    const QByteArray zlib = packed.mid(4);
    data.replace(stream.entry + 8, zlib.size(), zlib); // same length: nothing moves
    // Whatever the shorter stream no longer occupies stays zero, so a second
    // patch cannot read the old text out of the gap.
    const qint64 oldEnd = stream.entry + 4 + qint64(stream.payload);
    const qint64 newEnd = stream.entry + 8 + zlib.size();
    if (newEnd < oldEnd)
        data.replace(newEnd, oldEnd - newEnd, QByteArray(int(oldEnd - newEnd), '\0'));
    if (cacheBust) refreshTimestampFor(data, stream);
    return true;
}

// ---------------------------------------------------------------------------
// The replacement boot video
// ---------------------------------------------------------------------------

// Loaded by the boot screen through a file URL, so the player's own QML keeps
// no media import and no media type: everything that can be missing is tried at
// run time, and a failure costs the video, not the boot screen.
QString bootVideoQml()
{
    static const char qml[] =
        "import QtQuick 2.15\r\n"
        "Item{id:root;anchors.fill:parent;clip:true\r\n"
        " Rectangle{anchors.fill:parent;color:\"#000000\"}\r\n"
        " Component.onCompleted:{\r\n"
        "  var u=Qt.resolvedUrl(\"Promo.mp4\").toString()\r\n"
        "  var q6='import QtQuick 2.15;import QtMultimedia;Item{anchors.fill:parent;VideoOutput{id:v;anchors.fill:parent;fillMode:VideoOutput.PreserveAspectCrop}MediaPlayer{id:m;source:\"'+u+'\";loops:MediaPlayer.Infinite;videoOutput:v;audioOutput:AudioOutput{muted:true};Component.onCompleted:play()}}'\r\n"
        "  try{Qt.createQmlObject(q6,root,\"promo6\");return}catch(e){}\r\n"
        "  var q5='import QtQuick 2.15;import QtMultimedia 5.15;Video{anchors.fill:parent;source:\"'+u+'\";fillMode:VideoOutput.PreserveAspectCrop;loops:MediaPlayer.Infinite;muted:true;autoPlay:true;Component.onCompleted:play()}'\r\n"
        "  try{Qt.createQmlObject(q5,root,\"promo5\");return}catch(e){}\r\n"
        "  var h='<html><body style=\"margin:0;background:#000;overflow:hidden\"><video src=\"'+u+'\" autoplay muted loop playsinline style=\"width:100%;height:100%;object-fit:cover\"></video></body></html>'\r\n"
        "  var du='data:text/html;charset=utf-8,'+encodeURIComponent(h)\r\n"
        "  var qw='import QtQuick 2.15;import QtWebEngine;WebEngineView{anchors.fill:parent;url:\"'+du+'\"}'\r\n"
        "  try{Qt.createQmlObject(qw,root,\"web\") }catch(e){}\r\n"
        " }\r\n"
        "}\r\n";
    return QString::fromLatin1(qml);
}

// ---------------------------------------------------------------------------
// Editing the boot screen
// ---------------------------------------------------------------------------

QList<Object> videoLoaders(const QString &qml)
{
    QList<Object> loaders;
    for (const auto &object : objects(qml)) {
        if (object.type != QLatin1String("Loader")) continue;
        const QString span = qml.mid(object.begin, object.end - object.begin);
        if (object.id == QLatin1String("pv") || span.contains(QLatin1String("BootVideo.qml")))
            loaders << object;
    }
    return loaders;
}

QString patchController(const QString &source, const QString &url, QString *error)
{
    QString qml = source;
    QString id = QStringLiteral("pv");
    // A build may already load a video from the pool: keep its id and its place
    // so nothing that references it breaks, and replace only what it points at.
    for (const auto &object : videoLoaders(qml)) {
        if (!object.id.isEmpty()) { id = object.id; break; }
    }
    const QString loader = QStringLiteral("Loader{id:%1;objectName:\"bsiBootLoader\";z:-1;"
                                          "anchors.fill:parent;source:\"%2\"}").arg(id, url);
    const auto existing = videoLoaders(qml);
    if (!existing.isEmpty()) {
        // Replace the first in place and drop the rest: a repatch must not
        // stack loaders, and descending order keeps the earlier offsets valid.
        for (int i = existing.size() - 1; i >= 0; --i) {
            const auto &object = existing.at(i);
            qml.replace(object.begin, object.end - object.begin, i == 0 ? loader : QString());
        }
    } else {
        // Right above the loading text, which is where the build draws the
        // boot screen's own content. A minified boot screen has everything on
        // one line, and then the loader goes just inside the root object so the
        // file keeps a single root.
        const auto all = objects(qml);
        if (all.isEmpty()) {
            if (error) *error = QStringLiteral("The boot screen has no QML object to attach the video to.");
            return QString();
        }
        const int rootBody = all.first().body;
        const QString nl = lineEnding(qml);
        int at = -1;
        for (const auto &object : all) if (object.id == QLatin1String("iBootLoadingText")) { at = object.begin; break; }
        const int lineStart = at > rootBody ? qml.lastIndexOf(QLatin1Char('\n'), at - 1) + 1 : -1;
        if (lineStart > rootBody) {
            QString indent;
            for (int i = lineStart; i < qml.size() && (qml[i] == QLatin1Char(' ') || qml[i] == QLatin1Char('\t')); ++i)
                indent += qml[i];
            qml.insert(lineStart, indent + loader + nl);
        } else {
            qml.insert(rootBody, nl + QStringLiteral("    ") + loader);
        }
    }
    // The stock artwork, the promotion button and the grid are painted over the
    // video; the loading text stays so the screen still says it is working.
    visible(qml, QStringLiteral("iBootImage"), false);
    visible(qml, QStringLiteral("iBootLoadingText"), true);
    assignments(qml);
    return qml;
}

// ---------------------------------------------------------------------------
// Editing the player window that hosts the boot screen
// ---------------------------------------------------------------------------

QString patchPlayer(const QString &source, int holdMs, bool *changed)
{
    QString qml = source;
    *changed = false;
    // The stock swap: the boot screen is replaced by the black GL container as
    // soon as boot reports complete, which is also the moment the video would
    // stop. The swapped form is the same statement after this tool rewired it.
    static const QRegularExpression swap(
        QStringLiteral("sourceComponent:\\s*(?:pIsBootComplete\\s*\\|\\|\\s*pShowAndroidAd\\s*\\?\\s*"
                       "iGlContainerComponent\\s*:\\s*iBootProgressComponent"
                       "|\\(\\(pIsBootComplete&&pBootVideoDone\\)\\|\\|pShowAndroidAd\\)"
                       "\\?iGlContainerComponent:iBootProgressComponent)"));
    static const QRegularExpression rewired(
        QStringLiteral("sourceComponent: \\(\\(pIsBootComplete&&pBootVideoDone\\)\\|\\|pShowAndroidAd\\)"
                       "\\?iGlContainerComponent:iBootProgressComponent"));
    static const QRegularExpression timer(QStringLiteral("Timer\\{id:bvDone;interval:\\d+;"));
    static const QRegularExpression anchor(
        QStringLiteral("property\\s+bool\\s+pIsBootComplete\\s*:\\s*false"));
    static const QRegularExpression added(
        QStringLiteral("\\s*property\\s+bool\\s+pBootVideoDone:\\s*false\\s*\\r?\\n"
                       "\\s*Timer\\{id:bvDone;[^\\n]*\\}"));
    // Older players do not swap components: boot ends by unloading the loader
    // outright, so the hold is the unload deferred to a timer.
    static const QRegularExpression oldHide(
        QStringLiteral("iBootLoader\\.sourceComponent\\s*=\\s*undefined(\\s*\\r?\\n\\s*)"
                       "pIsBootComplete\\s*=\\s*true"));
    static const QRegularExpression oldHeld(
        QStringLiteral("pIsBootComplete\\s*=\\s*true(\\s*\\r?\\n\\s*)iBootLoader\\.visible\\s*=\\s*true"
                       "\\1bvHold\\.restart\\(\\)"));
    static const QRegularExpression oldTimer(
        QStringLiteral("\\s*Timer\\{id:bvHold;interval:\\d+;onTriggered:\\{[^\\n]*\\}"));

    if (holdMs > 0) {
        if (!replaceFirst(qml, swap, QStringLiteral("sourceComponent: ((pIsBootComplete&&pBootVideoDone)"
                                                    "||pShowAndroidAd)?iGlContainerComponent:iBootProgressComponent"))) {
            // The other way a build ends boot: the loader is dropped where boot
            // finishes and the timer drops it instead.
            if (oldHeld.match(qml).hasMatch()) {
                *changed = true; // already held: only the interval may differ
            } else {
                const auto hide = oldHide.match(qml);
                if (!hide.hasMatch()) return source; // nothing to hold back
                qml.replace(hide.capturedStart(), hide.capturedEnd(),
                            QStringLiteral("pIsBootComplete = true%1iBootLoader.visible = true%1bvHold.restart()")
                                .arg(hide.captured(1)));
                const auto place = anchor.match(qml);
                if (!place.hasMatch()) return source;
                qml.insert(place.capturedEnd(),
                           QStringLiteral("%1    Timer{id:bvHold;interval:%2;onTriggered:{"
                                          "iBootLoader.sourceComponent=undefined;iBootLoader.visible=false}}")
                               .arg(lineEnding(qml)).arg(holdMs));
                *changed = true;
            }
            const auto timing = oldTimer.match(qml);
            if (timing.hasMatch()) {
                const QString fixed = lineEnding(qml)
                    + QStringLiteral("    Timer{id:bvHold;interval:%1;onTriggered:{iBootLoader.sourceComponent"
                                     "=undefined;iBootLoader.visible=false}}").arg(holdMs);
                if (timing.captured(0) != fixed) {
                    qml.replace(timing.capturedStart(), timing.capturedLength(), fixed);
                    *changed = true;
                }
            }
            return qml;
        }
        *changed = true;
        const auto timerMatch = timer.match(qml);
        if (timerMatch.hasMatch()) {
            qml.replace(timerMatch.capturedStart(), timerMatch.capturedLength(),
                        QStringLiteral("Timer{id:bvDone;interval:%1;").arg(holdMs));
        } else {
            const auto anchorMatch = anchor.match(qml);
            if (!anchorMatch.hasMatch()) {
                // Nowhere to declare the flag the swap now waits for: put the
                // host back exactly as it was instead of half-editing it.
                replaceFirst(qml, rewired, QStringLiteral("sourceComponent: pIsBootComplete || pShowAndroidAd "
                                                          "? iGlContainerComponent : iBootProgressComponent"));
                *changed = false;
                return source;
            }
            const QString nl = lineEnding(qml);
            const QString block = anchorMatch.captured(0) + nl
                + QStringLiteral("    property bool pBootVideoDone: false") + nl
                + QStringLiteral("    Timer{id:bvDone;interval:%1;running:pIsBootComplete&&!pBootVideoDone;"
                                 "onTriggered:pBootVideoDone=true}").arg(holdMs);
            qml.replace(anchorMatch.capturedStart(), anchorMatch.capturedLength(), block);
        }
    } else {
        if (replaceFirst(qml, rewired, QStringLiteral("sourceComponent: pIsBootComplete || pShowAndroidAd "
                                                      "? iGlContainerComponent : iBootProgressComponent")))
            *changed = true;
        if (replaceFirst(qml, added, QString())) *changed = true;
        const auto held = oldHeld.match(qml);
        if (held.hasMatch()) {
            qml.replace(held.capturedStart(), held.capturedEnd(),
                        QStringLiteral("iBootLoader.sourceComponent = undefined%1pIsBootComplete = true")
                            .arg(held.captured(1)));
            *changed = true;
        }
        if (replaceFirst(qml, oldTimer, QString())) *changed = true;
    }
    return qml;
}

} // namespace

// ---------------------------------------------------------------------------

Result patch(const QString &exePath, const Options &options)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before patching the boot video.") };
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return { false, QStringLiteral("Cannot read HD-Player.exe.") };
    const QByteArray original = file.readAll();
    file.close();
    if (PeUtils::sections(original).isEmpty())
        return { false, QStringLiteral("This file is not a player executable; nothing was written.") };

    const auto found = streams(original);
    QList<Stream> players, controllers;
    for (const auto &stream : found) {
        if (stream.kind == Kind::Player) players << stream;
        if (stream.kind == Kind::Controller || stream.kind == Kind::ForcedStub) controllers << stream;
    }
    // A page this tool patched before may leave its previous record behind when
    // a record had to move; the tree points at the live copy, so a linked record
    // wins over an unlinked one.
    const auto preferLinked = [&original](QList<Stream> list) {
        if (list.size() < 2) return list;
        QList<Stream> linked;
        for (const auto &stream : list) {
            const auto link = QtResources::findLink(original, stream.entry, QString());
            if (link.found && !link.ambiguous) linked << stream;
        }
        return linked.size() == 1 ? linked : list;
    };
    players = preferLinked(players);
    controllers = preferLinked(controllers);

    if (players.size() > 1 || controllers.size() > 1)
        return { false, QStringLiteral("This player carries %1 boot window(s) and %2 boot screen(s), so "
                                       "the one it runs cannot be identified. Nothing was written.")
                            .arg(players.size()).arg(controllers.size()) };
    if (controllers.isEmpty())
        return { false, QStringLiteral("No boot screen was found in this player, so its boot video "
                                       "cannot be replaced. Nothing was written.") };

    Stream controller = controllers.first();
    // A boot screen an older patch emptied cannot be built on: its artwork and
    // its promotion handler were removed. The untouched copy in the backup is
    // the one to start from.
    if (controller.kind == Kind::ForcedStub) {
        QFile backup(exePath + QStringLiteral(".bak"));
        bool healed = false;
        if (backup.open(QIODevice::ReadOnly)) {
            for (const auto &stream : streams(backup.readAll())) {
                if (stream.kind != Kind::Controller || stream.entry != controller.entry) continue;
                controller.text = stream.text;
                healed = true;
                break;
            }
        }
        if (!healed)
            return { false, QStringLiteral("The boot screen in this file was replaced by an older, forced "
                                           "patch. Restore HD-Player.exe from a backup first.") };
    }

    // The QML the boot screen loads. Written before the EXE so a folder the user
    // cannot write to is reported instead of leaving a patched EXE pointing at a
    // file that does not exist.
    const QDir folder(QFileInfo(exePath).absolutePath());
    if (!QDir().mkpath(folder.filePath(QStringLiteral("Assets"))))
        return { false, QStringLiteral("Cannot create the Assets folder next to HD-Player.exe.") };
    const QString videoQml = folder.filePath(QStringLiteral("Assets/BootVideo.qml"));
    {
        QFile out(videoQml);
        const QByteArray bytes = bootVideoQml().toUtf8();
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size())
            return { false, QStringLiteral("Cannot write %1. Run the tool as administrator, or from a "
                                           "folder you can write to.").arg(QDir::toNativeSeparators(videoQml)) };
    }
    const QString url = QUrl::fromLocalFile(videoQml).toString(QUrl::FullyEncoded);

    QByteArray data = original;
    QString error;
    bool holding = false;
    if (!players.isEmpty()) {
        bool changed = false;
        const QString player = patchPlayer(players.first().text, options.holdMs, &changed);
        if (changed && !writeStream(data, players.first(), player, QStringLiteral("boot window"),
                                    options.cacheBust, &error))
            return { false, error };
        holding = changed && options.holdMs > 0;
    }
    const QString bootSource = patchController(controller.text, url, &error);
    if (bootSource.isEmpty()) return { false, error };
    if (!writeStream(data, controller, bootSource, QStringLiteral("boot screen"),
                     options.cacheBust, &error))
        return { false, error };

    // What the player will read, checked before the file is touched: both records
    // still decode as the files they were, the boot screen points at the video,
    // and the EXE did not grow.
    const auto written = streams(data);
    bool verified = data.size() == original.size();
    bool sawController = false, sawPlayer = !holding;
    for (const auto &stream : written) {
        if (stream.kind == Kind::Controller && stream.entry == controller.entry)
            sawController = stream.text.contains(QLatin1String("bsiBootLoader"))
                            && stream.text.contains(QLatin1String("BootVideo.qml"));
        if (holding && stream.entry == players.first().entry)
            sawPlayer = stream.text.contains(QLatin1String("pBootVideoDone"))
                        || stream.text.contains(QLatin1String("bvHold"));
    }
    if (!verified || !sawController || !sawPlayer)
        return { false, QStringLiteral("The rewritten boot screen did not read back as expected; "
                                       "nothing was written.") };

    if (!ThemeEngine::backup(exePath))
        return { false, QStringLiteral("Could not preserve the original .bak; no EXE was written.") };
    if (!PatchFile::writeVerified(exePath, original, data, &error))
        return { false, error };
    if (options.cacheBust) QtResources::clearPlayerQmlCache(exePath);

    const QString movie = folder.filePath(QStringLiteral("Assets/Promo.mp4"));
    QString message = QStringLiteral("Boot video patched. The boot screen now plays %1")
                          .arg(QDir::toNativeSeparators(movie));
    if (holding)
        message += QStringLiteral(", held on screen for %1 ms after boot.").arg(options.holdMs);
    else
        message += QStringLiteral(" for as long as this build shows its boot screen.");
    message += QStringLiteral(" Restart BlueStacks completely (tray icon > Quit).");
    if (!QFileInfo::exists(movie))
        message += QStringLiteral(" Copy your video to that path first.");
    return { true, message };
}

} // namespace BootVideoPatcher
