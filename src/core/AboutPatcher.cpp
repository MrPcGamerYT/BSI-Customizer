#include "AboutPatcher.h"

#include "BsDetector.h"
#include "ByteUtils.h"
#include "PatchFile.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace AboutPatcher {
namespace {

// The stock rows we hide while the custom content is shown. Every edit keeps
// the original text in a tagged comment, so undo is byte-exact even though the
// patch rewrites a live page instead of blanking it.
const QString kHiddenWrap = QStringLiteral(
    "false /* BSI CUSTOMIZER: about hidden */ && (");
const QString kHiddenLine = QStringLiteral(
    "visible: false // BSI CUSTOMIZER: about hidden");
// BlueStacks hides the whole "Important links" and "About and other info"
// blocks on MSI builds (`visible: uiBackend.oem !== "msi5"`). The custom page
// must show them there too, so their binding is switched to `true` while the
// patch is applied and restored from the tag afterwards.
const QString kShownTag = QStringLiteral(" /* BSI CUSTOMIZER: about shown:");
const QString kTextTag = QStringLiteral(" /* BSI CUSTOMIZER: about text:");
const QString kHandlerTag = QStringLiteral(" /* BSI CUSTOMIZER: about handler:");
const QString kHandlerMarker = QStringLiteral("// BSI CUSTOMIZER: about handler");
const QString kConfigBegin = QStringLiteral("// BSI CUSTOMIZER: about-config ");

struct Edit
{
    int begin = 0;
    int length = 0;
    QString replacement;
};

void addEdit(QList<Edit> &edits, int begin, int length, const QString &replacement)
{
    edits << Edit { begin, length, replacement };
}

QString lineEnding(const QString &qml)
{
    return qml.contains(QLatin1String("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
}

struct Doc
{
    QtResources::File file;
    QtResources::Link link;
    QString qml;
    bool bom = false;
};

bool findDoc(const QByteArray &image, const QString &name, Doc *out, QString *error)
{
    auto files = QtResources::registeredFiles(image);
    const bool registered = std::any_of(files.cbegin(), files.cend(),
                                        [&](const QtResources::File &file) { return file.name == name; });
    if (!registered) {
        const auto theme = ThemeEngine::findThemeZlib(image);
        if (theme.hasQrcNode)
            for (const auto &file : QtResources::filesInPool(image, theme.resource)) {
                bool present = false;
                for (const auto &old : files)
                    if (old.entryOff == file.entryOff && old.name == file.name) present = true;
                if (!present) files << file;
            }
    }
    for (const auto &file : files) {
        if (file.name != name) continue;
        const auto bytes = QtResources::readFile(image, file);
        if (bytes.isEmpty()) {
            if (error) *error = QStringLiteral("%1 could not be read; no file was written.").arg(name);
            return false;
        }
        const bool bom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
        const QByteArray body = bom ? bytes.mid(3) : bytes;
        QString qml = QString::fromUtf8(body);
        if (qml.toUtf8() != body) {
            if (error) *error = QStringLiteral("%1 is not UTF-8; no file was written.").arg(name);
            return false;
        }
        const auto link = QtResources::findLink(image, file.entryOff, file.name);
        if (!link.found || link.ambiguous) {
            if (error) *error = QStringLiteral("The %1 resource mapping is ambiguous; no file was written.")
                                     .arg(name);
            return false;
        }
        *out = Doc { file, link, qml, bom };
        return true;
    }
    if (error) *error = QStringLiteral("%1 is not embedded in this player.").arg(name);
    return false;
}

bool applyEdits(QString &qml, QList<Edit> edits)
{
    std::sort(edits.begin(), edits.end(),
              [](const Edit &a, const Edit &b) { return a.begin > b.begin; });
    int boundary = qml.size();
    for (const auto &edit : edits) {
        if (edit.begin < 0 || edit.begin + edit.length > boundary) return false;
        qml.replace(edit.begin, edit.length, edit.replacement);
        boundary = edit.begin;
    }
    return true;
}

bool directChild(const QString &masked, int parentOpen, int childBegin)
{
    int depth = 0;
    for (int i = parentOpen + 1; i < childBegin; ++i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}')) --depth;
    }
    return depth == 0;
}

bool directBinding(const QString &masked, int open, int end, const QString &name,
                   QmlText::Binding *out)
{
    int from = open;
    while (from < end) {
        QmlText::Binding binding;
        if (!QmlText::binding(masked, from, end, name, &binding)) return false;
        if (directChild(masked, open, binding.nameBegin)) {
            *out = binding;
            return true;
        }
        from = binding.nameBegin + name.size();
    }
    return false;
}

int matchingBrace(const QString &masked, int open)
{
    int depth = 0;
    for (int i = open; i < masked.size(); ++i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}') && --depth == 0) return i + 1;
    }
    return -1;
}

// The innermost object whose braces enclose `pos`.
bool enclosingObject(const QString &masked, int pos, QmlText::Object *object)
{
    // Nearest brace still open at `pos` (walking backwards). A forward scan
    // that remembers the last '{' returns an already-closed sibling, which
    // breaks parent lookup for objects declared after a nested child.
    int depth = 0;
    int open = -1;
    for (int i = qMin(pos, int(masked.size())) - 1; i >= 0; --i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('}')) ++depth;
        else if (c == QLatin1Char('{')) {
            if (depth == 0) { open = i; break; }
            --depth;
        }
    }
    if (open < 0) return false;
    if (matchingBrace(masked, open) <= pos) return false;
    int nameEnd = open;
    while (nameEnd > 0 && masked.at(nameEnd - 1).isSpace()) --nameEnd;
    int nameBegin = nameEnd;
    while (nameBegin > 0 && (masked.at(nameBegin - 1).isLetterOrNumber()
                             || masked.at(nameBegin - 1) == QLatin1Char('_')
                             || masked.at(nameBegin - 1) == QLatin1Char('.')))
        --nameBegin;
    if (nameBegin == nameEnd) return false;
    const int close = matchingBrace(masked, open);
    if (close <= open) return false;
    *object = QmlText::Object { nameBegin, open, close };
    return true;
}

bool findObjectById(const QString &qml, const QString &masked, const QString &id,
                    QmlText::Object *out)
{
    const QRegularExpression rx(QStringLiteral("\\bid\\s*:\\s*")
                                + QRegularExpression::escape(id) + QStringLiteral("\\b"));
    auto iter = rx.globalMatch(masked);
    while (iter.hasNext()) {
        const auto match = iter.next();
        QmlText::Object object;
        if (!enclosingObject(masked, int(match.capturedStart()), &object)) continue;
        QmlText::Binding binding;
        if (!directBinding(masked, object.open, object.end, QStringLiteral("id"), &binding))
            continue;
        if (qml.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin) != id) continue;
        *out = object;
        return true;
    }
    return false;
}

// The object that directly contains `object`.
bool parentObject(const QString &masked, const QmlText::Object &object, QmlText::Object *parent)
{
    return enclosingObject(masked, object.nameBegin, parent);
}

// Walk up from `object` until its parent is the About content column. Returns
// the direct child of that column which holds the object (the section block).
bool sectionOf(const QString &masked, const QmlText::Object &content,
               const QmlText::Object &object, QmlText::Object *section)
{
    QmlText::Object current = object;
    for (int guard = 0; guard < 64; ++guard) {
        QmlText::Object parent;
        if (!parentObject(masked, current, &parent)) return false;
        if (parent.open == content.open && parent.end == content.end) {
            *section = current;
            return true;
        }
        if (parent.open <= content.open || parent.end > content.end) return false;
        current = parent;
    }
    return false;
}

// The About page always has a content column; a build that renamed its id is
// still handled by taking the first column layout inside the page's scroll
// view.
bool findContent(const QString &qml, const QString &masked, QmlText::Object *content)
{
    if (findObjectById(qml, masked, QStringLiteral("iContentLayout"), content)) return true;
    for (const auto &view : QmlText::objects(masked, QStringLiteral("UiScrollView"))) {
        for (const auto &column : QmlText::objects(masked, QStringLiteral("UiColumnLayout"))) {
            if (column.nameBegin <= view.open || column.end > view.end) continue;
            if (!directChild(masked, view.open, column.nameBegin)) continue;
            *content = column;
            return true;
        }
    }
    return false;
}

// The "Check for updates" / "Download Update" button. It is located through
// its own text/onClicked body, not through a wrapper, because both known page
// generations place it in a different column.
bool findUpdateButton(const QString &qml, const QString &masked, const QmlText::Object &content,
                      QmlText::Object *out)
{
    for (const auto &button : QmlText::objects(masked, QStringLiteral("UiButton"))) {
        if (button.open <= content.open || button.end > content.end) continue;
        const QString body = qml.mid(button.open, button.end - button.open);
        if (body.contains(QLatin1String("checkForUpdate"))
            || body.contains(QLatin1String("Check for updates"))
            || body.contains(QLatin1String("Download Update"))
            || body.contains(QLatin1String("pUpdateAvailable"))) {
            *out = button;
            return true;
        }
    }
    return false;
}

// Stock rows that only exist to offer an update or a BlueStacks help page:
// the "available · More info" line and the incompatible-update warning. Both
// carry a stock link that must not survive next to the custom one.
QList<QmlText::Object> findUpdateInfoRows(const QString &qml, const QString &masked,
                                          const QmlText::Object &content,
                                          const QList<QmlText::Object> &keep)
{
    QList<QmlText::Object> out;
    for (const auto &row : QmlText::objects(masked, QStringLiteral("UiTextUrl"))) {
        if (row.open <= content.open || row.end > content.end) continue;
        const QString body = qml.mid(row.open, row.end - row.open);
        if (!body.contains(QLatin1String("href"))) continue;
        if (!body.contains(QLatin1String("pMoreInfoURL"))
            && !body.contains(QLatin1String("incompatible_update_version")))
            continue;
        bool skipped = false;
        for (const auto &object : keep)
            if (object.open == row.open && object.end == row.end) skipped = true;
        if (!skipped) out << row;
    }
    return out;
}

// The "联系我们 / QQ" row only exists in the Chinese build, but it carries a
// second stock link there and must go as well. Both the outer row and the
// inner button row are returned: hiding only the inner one would leave the
// "联系我们:" label behind.
QList<QmlText::Object> findQqRows(const QString &qml, const QString &masked,
                                  const QmlText::Object &links)
{
    QList<QmlText::Object> out;
    for (const auto &type : { QStringLiteral("UiRowLayout"), QStringLiteral("RowLayout"),
                              QStringLiteral("Row"), QStringLiteral("UiRow"),
                              QStringLiteral("Item") }) {
        for (const auto &object : QmlText::objects(masked, type)) {
            if (object.open <= links.open || object.end > links.end) continue;
            const QString body = qml.mid(object.open, object.end - object.open);
            if (!body.contains(QLatin1String("openQQSupportUrl"))
                && !body.contains(QLatin1String("jq.qq.com"))
                && !body.contains(QString::fromUtf8("联系我们")))
                continue;
            bool seen = false;
            for (const auto &old : out)
                if (old.open == object.open && old.end == object.end) seen = true;
            if (!seen) out << object;
        }
    }
    return out;
}

QString memberIndent(const QString &qml, const QmlText::Object &object)
{
    const int lineStart = qml.lastIndexOf(QLatin1Char('\n'), object.nameBegin - 1) + 1;
    QString indent;
    int i = lineStart;
    while (i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
        indent += qml.at(i++);
    return indent;
}

// Where a `visible: false` member can be inserted into `object`, and the exact
// text written there. Multi-line objects get a whole line at the first
// member's indentation (no extra blank line); an object written on one line
// gets the member inline with a `;` separator and a block comment, so the
// remaining members are neither split nor swallowed by a line comment.
bool hiddenMemberSite(const QString &qml, const QmlText::Object &object, int *at,
                      QString *text)
{
    const QString nl = lineEnding(qml);
    int probe = object.open + 1;
    while (probe < qml.size() && (qml.at(probe) == QLatin1Char(' ') || qml.at(probe) == QLatin1Char('\t')))
        ++probe;
    const bool firstMemberOnNewLine = probe < object.end
        && (qml.at(probe) == QLatin1Char('\n') || qml.at(probe) == QLatin1Char('\r'));
    if (firstMemberOnNewLine) {
        const int newline = qml.indexOf(QLatin1Char('\n'), probe);
        if (newline < 0 || newline >= object.end) return false;
        const int lineStart = newline + 1;
        QString indent;
        int i = lineStart;
        while (i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
            indent += qml.at(i++);
        *at = lineStart;
        *text = indent + kHiddenLine + nl;
        return true;
    }
    // The object shares its opening line with its members (`UiButton { id: …`),
    // so a newline insertion would split that line. Add the member inline with
    // a separator and a block comment instead; the inline form is removed by
    // the same byte-exact site match in showObject().
    *at = object.open + 1;
    *text = QStringLiteral(" visible: false /* BSI CUSTOMIZER: about hidden */;");
    return true;
}

QString tagged(const QString &value, const QString &original, const QString &marker)
{
    return value + marker + QString::fromLatin1(original.toUtf8().toBase64())
        + QStringLiteral(" */");
}

// The trailing comment that follows a patched value on the same line; the
// binding scanner trims comments, so it must be read from the source text.
bool taggedAfter(const QString &qml, int valueBegin, const QString &marker,
                 QString *original, int *tagEnd)
{
    const int at = qml.indexOf(marker, valueBegin);
    if (at < 0) return false;
    if (qml.mid(valueBegin, at - valueBegin).contains(QLatin1Char('\n'))) return false;
    const int close = qml.indexOf(QLatin1String("*/"), at + marker.size());
    if (close < 0) return false;
    const QByteArray encoded = qml.mid(at + marker.size(), close - at - marker.size())
                                   .trimmed().toLatin1();
    const QByteArray bytes = QByteArray::fromBase64(encoded);
    if (bytes.isEmpty() || bytes.toBase64() != encoded) return false;
    if (QString::fromUtf8(bytes).toUtf8() != bytes) return false;
    *original = QString::fromUtf8(bytes);
    *tagEnd = close + 2;
    return true;
}

// Hide one object: wrap an existing `visible` binding, or add a plain false
// member when the stock object has none.
bool hideObject(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Binding visible;
    if (directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible)) {
        const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
        if (value.startsWith(kHiddenWrap)) return false;
        addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                kHiddenWrap + value + QStringLiteral(")"));
    } else {
        int at = 0;
        QString text;
        if (!hiddenMemberSite(qml, object, &at, &text)) return false;
        addEdit(edits, at, 0, text);
    }
    return true;
}

bool showObject(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    int at = 0;
    QString inserted;
    if (!hiddenMemberSite(qml, object, &at, &inserted)) return false;
    if (qml.mid(at, inserted.size()) == inserted) {
        addEdit(edits, at, inserted.size(), QString());
        return true;
    }
    QmlText::Binding visible;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible))
        return false;
    const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
    if (value.startsWith(kHiddenWrap) && value.endsWith(QLatin1Char(')'))) {
        addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                value.mid(kHiddenWrap.size(), value.size() - kHiddenWrap.size() - 1));
        return true;
    }
    return false;
}

// Force a section visible even on a build that hides it (`oem !== "msi5"`).
bool forceShown(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Binding visible;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible))
        return false; // no binding: the section is already visible
    const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
    if (value == QLatin1String("true")) return false;
    addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
            tagged(QStringLiteral("true"), value, kShownTag));
    return true;
}

bool restoreShown(const QString &qml, const QmlText::Object &object, QList<Edit> &edits)
{
    const QString masked = QmlText::mask(qml);
    QmlText::Binding visible;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible))
        return false;
    const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
    if (value != QLatin1String("true")) return false;
    QString original;
    int tagEnd = -1;
    if (!taggedAfter(qml, visible.valueBegin, kShownTag, &original, &tagEnd)) return false;
    addEdit(edits, visible.valueBegin, tagEnd - visible.valueBegin, original);
    return true;
}

QString escapeQml(const QString &value)
{
    QString out;
    out.reserve(value.size() + 16);
    for (const QChar c : value) {
        switch (c.unicode()) {
        case '\\': out += QLatin1String("\\\\"); break;
        case '"': out += QLatin1String("\\\""); break;
        case '\n': out += QLatin1String("\\n"); break;
        case '\r': break;
        case '\t': out += QLatin1String("\\t"); break;
        default: out += c;
        }
    }
    return out;
}

QString escapeHtml(const QString &value)
{
    QString out;
    out.reserve(value.size() + 16);
    for (const QChar c : value) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        case '\'': out += QLatin1String("&#39;"); break;
        case '\n': out += QLatin1String("<br/>"); break;
        case '\r': break;
        case '\t': out += QLatin1String("&nbsp;&nbsp;&nbsp;&nbsp;"); break;
        default: out += c;
        }
    }
    return out;
}

QString configLine(const Config &config)
{
    QJsonObject object;
    object.insert(QStringLiteral("text"), config.text);
    object.insert(QStringLiteral("show"), config.showSections);
    QJsonArray links;
    for (const auto &link : config.links) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), link.id);
        entry.insert(QStringLiteral("name"), link.name);
        entry.insert(QStringLiteral("url"), link.url);
        entry.insert(QStringLiteral("hidden"), link.hidden);
        links.append(entry);
    }
    object.insert(QStringLiteral("links"), links);
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return kConfigBegin + QString::fromLatin1(json.toBase64());
}

bool readConfig(const QString &qml, Config *config, bool *hasLinksField)
{
    const int at = qml.indexOf(kConfigBegin);
    if (at < 0) return false;
    int end = qml.indexOf(QLatin1Char('\n'), at);
    if (end < 0) end = qml.size();
    QString encoded = qml.mid(at + kConfigBegin.size(), end - at - kConfigBegin.size());
    encoded.remove(QLatin1Char('\r'));
    encoded = encoded.trimmed();
    const QByteArray latin = encoded.toLatin1();
    const QByteArray bytes = QByteArray::fromBase64(latin);
    if (bytes.isEmpty() || bytes.toBase64() != latin) return false;
    QJsonParseError error {};
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object();
    if (hasLinksField) *hasLinksField = object.contains(QStringLiteral("links"));
    Config out;
    out.text = object.value(QStringLiteral("text")).toString();
    out.showSections = object.value(QStringLiteral("show")).toBool(true);
    for (const auto &value : object.value(QStringLiteral("links")).toArray()) {
        const auto entry = value.toObject();
        Link link;
        link.id = entry.value(QStringLiteral("id")).toString();
        link.name = entry.value(QStringLiteral("name")).toString();
        link.url = entry.value(QStringLiteral("url")).toString();
        link.hidden = entry.value(QStringLiteral("hidden")).toBool();
        if (!link.id.isEmpty()) out.links << link;
    }
    *config = out;
    return true;
}

// The address a row opens. A bare e-mail address keeps the stock "Support
// email" row working after it is edited, and a bare domain becomes a real URL
// instead of a dead Qt.openUrlExternally call.
QString targetUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (trimmed.contains(QLatin1Char(':'))) return trimmed;
    if (trimmed.contains(QLatin1Char('@'))) return QStringLiteral("mailto:") + trimmed;
    if (trimmed.contains(QLatin1Char('.')) && !trimmed.contains(QLatin1Char(' ')))
        return QStringLiteral("https://") + trimmed;
    return trimmed;
}

bool sameLink(const Link &a, const Link &b)
{
    return a.id == b.id && a.name == b.name && a.url == b.url && a.hidden == b.hidden;
}

bool sameConfig(const Config &a, const Config &b)
{
    if (a.text != b.text || a.showSections != b.showSections || a.links.size() != b.links.size())
        return false;
    for (int i = 0; i < a.links.size(); ++i)
        if (!sameLink(a.links.at(i), b.links.at(i))) return false;
    return true;
}

Config normalized(Config config)
{
    config.text = config.text.trimmed();
    for (auto &link : config.links) {
        link.name = link.name.trimmed();
        link.url = link.url.trimmed();
        // Store the form the page ends up with, so reading it back matches
        // what was applied.
        link.url = targetUrl(link.url);
        // A row with nothing to point at and nothing to say is a hidden row.
        if (link.url.isEmpty() && link.name.isEmpty()) link.hidden = true;
        if (link.name.isEmpty()) link.name = link.url;
    }
    return config;
}

// ------------------------------------------------- what the page shows right now
// The About editors are prefilled with the values the page has at this moment,
// so the stock text/link can be edited instead of retyped. Reading them needs
// no QML engine: a text expression is flattened to the strings it can render
// (`qsTranslate("c", "x")` -> `x`, literals decoded), and the markup the stock
// page uses is turned back into plain text.

// The index just past the ')' that closes the '(' at `open`, quote-aware.
int closingParen(const QString &text, int open)
{
    int depth = 0;
    QChar quote;
    for (int i = open; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (!quote.isNull()) {
            if (c == QLatin1Char('\\')) { ++i; continue; }
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) { quote = c; continue; }
        if (c == QLatin1Char('(')) ++depth;
        else if (c == QLatin1Char(')') && --depth == 0) return i + 1;
    }
    return -1;
}

QString decodeQmlString(const QString &raw)
{
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c != QLatin1Char('\\')) {
            out += c;
            continue;
        }
        if (++i >= raw.size()) break;
        const QChar escaped = raw.at(i);
        switch (escaped.unicode()) {
        case 'n': out += QLatin1Char('\n'); break;
        case 'r': out += QLatin1Char('\r'); break;
        case 't': out += QLatin1Char('\t'); break;
        default: out += escaped; break;
        }
    }
    return out;
}

// Every string a QML text expression can contribute, in source order. Calls
// such as `qsTranslate("QObject", "Terms of use")` contribute their text
// argument only; other identifiers and operators are skipped.
QStringList renderedChunks(const QString &expression)
{
    QStringList out;
    const int n = expression.size();
    int i = 0;
    while (i < n) {
        const QChar c = expression.at(i);
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            const QChar quote = c;
            QString raw;
            int j = i + 1;
            while (j < n && expression.at(j) != quote) {
                if (expression.at(j) == QLatin1Char('\\') && j + 1 < n) raw += expression.at(j++);
                raw += expression.at(j++);
            }
            out << decodeQmlString(raw);
            i = j + 1;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (expression.at(j).isLetterOrNumber()
                             || expression.at(j) == QLatin1Char('_')))
                ++j;
            const QString name = expression.mid(i, j - i);
            int open = j;
            while (open < n && expression.at(open).isSpace()) ++open;
            const bool translated = name == QLatin1String("qsTranslate")
                || name == QLatin1String("qsTr") || name == QLatin1String("qsTrId");
            if (translated && open < n && expression.at(open) == QLatin1Char('(')) {
                const int close = closingParen(expression, open);
                if (close > 0) {
                    // `close` is one past the ')', so the arguments are the
                    // characters strictly between it and the '('.
                    const QString arguments = expression.mid(open + 1, close - open - 2);
                    // Quote-aware split, so a comma inside the text is kept.
                    QStringList parts;
                    QString part;
                    QChar active;
                    for (int k = 0; k < arguments.size(); ++k) {
                        const QChar d = arguments.at(k);
                        if (!active.isNull()) {
                            part += d;
                            if (d == QLatin1Char('\\') && k + 1 < arguments.size())
                                part += arguments.at(++k);
                            else if (d == active) active = QChar();
                            continue;
                        }
                        if (d == QLatin1Char('"') || d == QLatin1Char('\'')) active = d;
                        if (d == QLatin1Char(',')) { parts << part; part.clear(); continue; }
                        part += d;
                    }
                    parts << part;
                    // The words to show: `qsTranslate("ctx", "text")` keeps its
                    // text in the second argument, `qsTr("text")` and
                    // `qsTrId("id")` in the first. A trailing argument (a plural
                    // count, a disambiguation string) is not part of the text.
                    QStringList literals;
                    for (const auto &candidate : parts) {
                        const QString value = candidate.trimmed();
                        if (value.size() >= 2
                            && (value.front() == QLatin1Char('"')
                                || value.front() == QLatin1Char('\''))
                            && value.back() == value.front())
                            literals << value.mid(1, value.size() - 2);
                    }
                    const int messageIndex = name == QLatin1String("qsTranslate") ? 1 : 0;
                    if (literals.size() > messageIndex)
                        out << decodeQmlString(literals.at(messageIndex));
                    i = close;
                    continue;
                }
            }
            i = j;
            continue;
        }
        ++i;
    }
    return out;
}

QString renderedText(const QString &expression)
{
    return renderedChunks(expression).join(QString());
}

QString htmlToPlain(const QString &html)
{
    QString out;
    out.reserve(html.size());
    bool inTag = false;
    for (const QChar c : html) {
        if (inTag) {
            if (c == QLatin1Char('>')) inTag = false;
            continue;
        }
        if (c == QLatin1Char('<')) {
            inTag = true;
            continue;
        }
        out += c;
    }
    out.replace(QLatin1String("&nbsp;"), QStringLiteral(" "));
    out.replace(QLatin1String("&lt;"), QStringLiteral("<"));
    out.replace(QLatin1String("&gt;"), QStringLiteral(">"));
    out.replace(QLatin1String("&quot;"), QStringLiteral("\""));
    out.replace(QLatin1String("&#39;"), QStringLiteral("'"));
    out.replace(QLatin1String("&#039;"), QStringLiteral("'"));
    out.replace(QLatin1String("&amp;"), QStringLiteral("&"));
    return out;
}

// The text of the last `<a …>label</a>` a row can render. The stock rows read
// `cond ? " - <a…>cn</a>" : " - <a…>com</a>"`, and the second branch is the
// one this build shows outside China.
QString lastAnchorLabel(const QString &rendered)
{
    static const QRegularExpression rx(QStringLiteral(">([^<>]*)</a\\s*>"),
                                       QRegularExpression::CaseInsensitiveOption);
    QString label;
    auto iter = rx.globalMatch(rendered);
    while (iter.hasNext()) label = iter.next().captured(1);
    return htmlToPlain(label).trimmed();
}

// Where a stock link row sends the user: the URL of its own handler first
// (`Qt.openUrlExternally("…")`), otherwise the anchor's href when it is a real
// address instead of the stock `javascript:void(0)` placeholder.
QString shownLinkUrl(const QString &handler, const QString &rendered)
{
    static const QRegularExpression call(
        QStringLiteral("Qt\\s*\\.\\s*openUrlExternally\\s*\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    QString url;
    auto calls = call.globalMatch(handler);
    while (calls.hasNext()) url = decodeQmlString(calls.next().captured(1));
    if (!url.isEmpty()) return url;
    static const QRegularExpression href(QStringLiteral("href\\s*=\\s*\"([^\"]*)\""),
                                         QRegularExpression::CaseInsensitiveOption);
    auto links = href.globalMatch(rendered);
    while (links.hasNext()) {
        const QString candidate = decodeQmlString(links.next().captured(1)).trimmed();
        if (candidate.isEmpty() || candidate.startsWith(QLatin1String("javascript:"))
            || candidate == QLatin1String("#"))
            continue;
        url = candidate;
    }
    return url;
}

// One stock link row, located and read from the document.
struct Row
{
    QString id;
    QmlText::Object object;
    QmlText::Binding text;
    QmlText::Binding handler;
    bool hasHandler = false;
};

// The About columns and the rows inside them.
struct Anchors
{
    QmlText::Object content;
    QmlText::Object aboutBlock;
    QmlText::Object linksBlock;
    QmlText::Object aboutText;
    QList<QmlText::Object> updateInfoRows;
    QList<QmlText::Object> qqRows;
    QList<Row> links;
    QmlText::Object updateButton;
    QmlText::Binding aboutTextBinding;
    bool sectionsHidden = false;
    bool ok = false;
};

bool sameObject(const QmlText::Object &a, const QmlText::Object &b)
{
    return a.open == b.open && a.end == b.end;
}

// True when the build's own binding can keep the section switched off, i.e.
// the MSI shape `visible: uiBackend.oem !== "msi5"`. A plain `true` (or no
// binding) is already shown and needs no unhide.
bool hidesItself(const QString &masked, const QmlText::Object &object)
{
    QmlText::Binding visible;
    if (!directBinding(masked, object.open, object.end, QStringLiteral("visible"), &visible))
        return false;
    return masked.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin).trimmed()
        != QLatin1String("true");
}

// The stock link rows, in the order the page lays them out. A build may have
// fewer of them; the ones it has are the ones the editor shows.
const QStringList &stockLinkIds()
{
    static const QStringList ids {
        QStringLiteral("iSupportEmailText"), QStringLiteral("iSupportText"),
        QStringLiteral("iWebsiteText")
    };
    return ids;
}

Anchors findAnchors(const QString &qml, const QString &masked, QString *why)
{
    Anchors anchors;
    if (!findContent(qml, masked, &anchors.content)) {
        if (why) *why = QStringLiteral("This player's About page has no replaceable content column.");
        return anchors;
    }
    if (!findObjectById(qml, masked, QStringLiteral("iTermsOfUseText"), &anchors.aboutText)) {
        if (why) *why = QStringLiteral("This player's About page has no replaceable About text line.");
        return anchors;
    }
    if (!directBinding(masked, anchors.aboutText.open, anchors.aboutText.end,
                       QStringLiteral("text"), &anchors.aboutTextBinding)) {
        if (why) *why = QStringLiteral("The About text line is not a text field; nothing would be written.");
        return anchors;
    }
    if (!sectionOf(masked, anchors.content, anchors.aboutText, &anchors.aboutBlock)) {
        if (why) *why = QStringLiteral("The About text is not inside the About content column.");
        return anchors;
    }
    for (const auto &id : stockLinkIds()) {
        Row row;
        row.id = id;
        if (!findObjectById(qml, masked, id, &row.object)) continue;
        if (!directBinding(masked, row.object.open, row.object.end,
                           QStringLiteral("text"), &row.text))
            continue;
        row.hasHandler = directBinding(masked, row.object.open, row.object.end,
                                       QStringLiteral("onLinkActivated"), &row.handler);
        anchors.links << row;
    }
    if (anchors.links.isEmpty()) {
        if (why) *why = QStringLiteral("The stock About links were not found in this player.");
        return anchors;
    }
    if (!sectionOf(masked, anchors.content, anchors.links.first().object,
                   &anchors.linksBlock)) {
        if (why) *why = QStringLiteral("The stock About links are not inside the About content column.");
        return anchors;
    }
    findUpdateButton(qml, masked, anchors.content, &anchors.updateButton);
    anchors.qqRows = findQqRows(qml, masked, anchors.linksBlock);
    QList<QmlText::Object> keep { anchors.aboutText };
    for (const auto &row : anchors.links) keep << row.object;
    anchors.updateInfoRows = findUpdateInfoRows(qml, masked, anchors.content, keep);
    anchors.sectionsHidden = hidesItself(masked, anchors.aboutBlock);
    if (!sameObject(anchors.aboutBlock, anchors.linksBlock))
        anchors.sectionsHidden = anchors.sectionsHidden || hidesItself(masked, anchors.linksBlock);
    anchors.ok = true;
    return anchors;
}

// The name a row prints before its link ("Support email - <a…>"). That is the
// first string the text expression contributes; the branches after it only
// repeat the label with a different link.
QString rowName(const QString &expression)
{
    QString prefix = renderedChunks(expression).value(0);
    const int anchor = prefix.indexOf(QLatin1String("<a"), 0, Qt::CaseInsensitive);
    if (anchor >= 0) prefix = prefix.left(anchor);
    prefix = htmlToPlain(prefix).trimmed();
    while (prefix.endsWith(QLatin1Char('-')) || prefix.endsWith(QLatin1Char(':'))
           || prefix.endsWith(QLatin1Char(' ')))
        prefix.chop(1);
    return prefix.trimmed();
}

bool rowIsHidden(const QString &qml, const QmlText::Object &object)
{
    const QString body = qml.mid(object.open, object.end - object.open);
    return body.contains(kHiddenLine) || body.contains(kHiddenWrap);
}

// What the page says right now: the About text without its markup, and every
// stock link row with the name it prints and the URL it opens.
bool readShown(const QString &qml, const QString &masked, const Anchors &anchors, Config *config)
{
    Config out;
    QmlText::Binding text;
    if (anchors.aboutText.open >= 0
        && directBinding(masked, anchors.aboutText.open, anchors.aboutText.end,
                         QStringLiteral("text"), &text)) {
        const QString value = qml.mid(text.valueBegin, text.valueEnd - text.valueBegin);
        out.text = htmlToPlain(renderedText(value)).trimmed();
    }
    for (const auto &row : anchors.links) {
        Link link;
        link.id = row.id;
        const QString value = qml.mid(row.text.valueBegin, row.text.valueEnd - row.text.valueBegin);
        const QString rendered = renderedText(value);
        link.name = rowName(value);
        const QString handler = row.hasHandler
            ? qml.mid(row.handler.valueBegin, row.handler.valueEnd - row.handler.valueBegin)
            : QString();
        link.url = shownLinkUrl(handler, rendered);
        if (link.url.isEmpty()) {
            // Rows that open a mail client (`uiBackend.openEmail`) or a help
            // page carry no external URL: their printed address is the target.
            const QString label = lastAnchorLabel(rendered).trimmed();
            if (label.contains(QLatin1Char('@'))
                || (label.contains(QLatin1Char('.')) && !label.contains(QLatin1Char(' '))))
                link.url = label;
        }
        link.hidden = rowIsHidden(qml, row.object);
        if (link.name.isEmpty()) link.name = link.url;
        out.links << link;
    }
    // Start from what the page does now: on the MSI-style pages both sections
    // are bound to `oem !== "msi5"` and drawn hidden, which is exactly the
    // state the "Unhide links + About" button is there to change.
    out.showSections = !anchors.sectionsHidden;
    if (out.text.isEmpty() && out.links.isEmpty()) return false;
    *config = out;
    return true;
}

// The text a link row should print: the name, then the address the way the
// stock page prints it (no scheme, no "mailto:").
QString displayFor(const QString &url)
{
    QString shown = url.trimmed();
    if (shown.startsWith(QLatin1String("mailto:"), Qt::CaseInsensitive))
        shown = shown.mid(7);
    if (shown.startsWith(QLatin1String("http://"), Qt::CaseInsensitive))
        shown = shown.mid(7);
    else if (shown.startsWith(QLatin1String("https://"), Qt::CaseInsensitive))
        shown = shown.mid(8);
    if (shown.endsWith(QLatin1Char('/'))) shown.chop(1);
    return shown;
}

QString linkLiteral(const Link &link)
{
    QString html;
    if (!link.name.isEmpty()) html += escapeHtml(link.name) + QStringLiteral(" - ");
    html += QStringLiteral("<a href=\"") + escapeHtml(targetUrl(link.url))
        + QStringLiteral("\">") + escapeHtml(displayFor(link.url)) + QStringLiteral("</a>");
    return QLatin1Char('"') + escapeQml(html) + QLatin1Char('"');
}

const Link *findLink(const Config &config, const QString &id)
{
    for (const auto &link : config.links)
        if (link.id == id) return &link;
    return nullptr;
}

// Rewrite a row's text and handler, or add the handler when the build has none.
void writeLink(const QString &qml, const Row &row, const Link &link, QList<Edit> &edits)
{
    const QString nl = lineEnding(qml);
    const QString original = qml.mid(row.text.valueBegin, row.text.valueEnd - row.text.valueBegin);
    addEdit(edits, row.text.valueBegin, row.text.valueEnd - row.text.valueBegin,
            tagged(linkLiteral(link), original, kTextTag));
    const QString call = QStringLiteral("Qt.openUrlExternally(\"")
        + escapeQml(targetUrl(link.url)) + QStringLiteral("\")");
    if (row.hasHandler) {
        const QString handlerOriginal =
            qml.mid(row.handler.valueBegin, row.handler.valueEnd - row.handler.valueBegin);
        addEdit(edits, row.handler.valueBegin,
                row.handler.valueEnd - row.handler.valueBegin,
                tagged(call, handlerOriginal, kHandlerTag));
        return;
    }
    // No handler in this build: add one above the text member, tagged so
    // restoring removes exactly these lines.
    const int lineStart = qml.lastIndexOf(QLatin1Char('\n'), row.text.nameBegin - 1) + 1;
    QString indent;
    int i = lineStart;
    while (i < qml.size() && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
        indent += qml.at(i++);
    addEdit(edits, lineStart, 0,
            indent + kHandlerMarker + nl + indent
                + QStringLiteral("onLinkActivated: ") + call + nl);
}

bool planEnable(const QString &qml, const Config &config, QList<Edit> &edits, QString *error)
{
    const QString masked = QmlText::mask(qml);
    const Anchors anchors = findAnchors(qml, masked, error);
    if (!anchors.ok) return false;

    const bool hasText = !config.text.isEmpty();
    bool visibleLinks = false;
    for (const auto &link : config.links)
        if (!link.hidden && !link.url.isEmpty()) visibleLinks = true;
    if (!hasText && !visibleLinks && !config.showSections) {
        if (error) *error = QStringLiteral("Nothing to apply: the text is empty, every link is "
                                           "hidden and the sections stay as they are.");
        return false;
    }

    const bool sharedBlock = sameObject(anchors.aboutBlock, anchors.linksBlock);

    // ---- the two sections the MSI build hides ---------------------------
    if (config.showSections) {
        forceShown(qml, anchors.aboutBlock, edits);
        if (!sharedBlock) forceShown(qml, anchors.linksBlock, edits);
    } else {
        // Keep the build's own behaviour: undo a force this tool applied
        // earlier, and leave the stock binding alone.
        restoreShown(qml, anchors.aboutBlock, edits);
        if (!sharedBlock) restoreShown(qml, anchors.linksBlock, edits);
    }

    // ---- the About text --------------------------------------------------
    if (hasText) {
        const QString literal = QLatin1Char('"') + escapeQml(escapeHtml(config.text))
            + QLatin1Char('"');
        const QString original = qml.mid(anchors.aboutTextBinding.valueBegin,
                                         anchors.aboutTextBinding.valueEnd
                                             - anchors.aboutTextBinding.valueBegin);
        addEdit(edits, anchors.aboutTextBinding.valueBegin,
                anchors.aboutTextBinding.valueEnd - anchors.aboutTextBinding.valueBegin,
                tagged(literal, original, kTextTag));
    } else {
        hideObject(qml, anchors.aboutText, edits);
    }

    // ---- the stock links: rename, re-point or hide each one --------------
    for (const auto &row : anchors.links) {
        const Link *link = findLink(config, row.id);
        const bool hidden = !link || link->hidden || link->url.isEmpty();
        if (hidden) {
            hideObject(qml, row.object, edits);
            continue;
        }
        showObject(qml, row.object, edits); // undo our own earlier hiding
        writeLink(qml, row, *link, edits);
    }
    for (const auto &row : anchors.qqRows) hideObject(qml, row, edits);

    // ---- the update button and its "More info" links: never offered again -
    if (anchors.updateButton.open >= 0) hideObject(qml, anchors.updateButton, edits);
    for (const auto &row : anchors.updateInfoRows) hideObject(qml, row, edits);

    addEdit(edits, qml.size(), 0, lineEnding(qml) + configLine(config));
    return true;
}

// Remove the lines this tool inserted for rows that had no handler.
void restoreAddedHandlers(const QString &qml, QList<Edit> &edits)
{
    for (int at = qml.indexOf(kHandlerMarker); at >= 0;
         at = qml.indexOf(kHandlerMarker, at + kHandlerMarker.size())) {
        const int start = qml.lastIndexOf(QLatin1Char('\n'), at - 1) + 1;
        const int firstEnd = qml.indexOf(QLatin1Char('\n'), at);
        int stop = firstEnd < 0 ? qml.size() : qml.indexOf(QLatin1Char('\n'), firstEnd + 1);
        if (stop < 0) stop = qml.size();
        else ++stop;
        addEdit(edits, start, stop - start, QString());
    }
}

bool planDisable(const QString &qml, QList<Edit> &edits, QString *error)
{
    const QString masked = QmlText::mask(qml);
    const Anchors anchors = findAnchors(qml, masked, error);
    if (!anchors.ok) return false;

    restoreShown(qml, anchors.aboutBlock, edits);
    if (!sameObject(anchors.linksBlock, anchors.aboutBlock))
        restoreShown(qml, anchors.linksBlock, edits);
    // Rows/sections hidden when the text or every link was switched off.
    showObject(qml, anchors.aboutText, edits);
    showObject(qml, anchors.aboutBlock, edits);
    if (!sameObject(anchors.linksBlock, anchors.aboutBlock))
        showObject(qml, anchors.linksBlock, edits);

    for (const auto &row : anchors.links) showObject(qml, row.object, edits);
    for (const auto &row : anchors.qqRows) showObject(qml, row, edits);
    if (anchors.updateButton.open >= 0) showObject(qml, anchors.updateButton, edits);
    for (const auto &row : anchors.updateInfoRows) showObject(qml, row, edits);

    // The About text.
    QmlText::Binding text;
    if (directBinding(masked, anchors.aboutText.open, anchors.aboutText.end,
                      QStringLiteral("text"), &text)) {
        QString original;
        int tagEnd = -1;
        if (taggedAfter(qml, text.valueBegin, kTextTag, &original, &tagEnd))
            addEdit(edits, text.valueBegin, tagEnd - text.valueBegin, original);
    }
    // Every link row's text and handler.
    for (const auto &row : anchors.links) {
        QmlText::Binding link;
        if (directBinding(masked, row.object.open, row.object.end,
                          QStringLiteral("text"), &link)) {
            QString original;
            int tagEnd = -1;
            if (taggedAfter(qml, link.valueBegin, kTextTag, &original, &tagEnd))
                addEdit(edits, link.valueBegin, tagEnd - link.valueBegin, original);
        }
        QmlText::Binding handler;
        if (directBinding(masked, row.object.open, row.object.end,
                          QStringLiteral("onLinkActivated"), &handler)) {
            QString original;
            int tagEnd = -1;
            if (taggedAfter(qml, handler.valueBegin, kHandlerTag, &original, &tagEnd))
                addEdit(edits, handler.valueBegin, tagEnd - handler.valueBegin, original);
        }
    }
    restoreAddedHandlers(qml, edits);

    const int at = qml.indexOf(kConfigBegin);
    if (at >= 0) {
        int begin = at;
        if (begin > 0 && qml.at(begin - 1) == QLatin1Char('\n')) {
            --begin;
            if (begin > 0 && qml.at(begin - 1) == QLatin1Char('\r')) --begin;
        }
        int end = qml.indexOf(QLatin1Char('\n'), at);
        if (end < 0) end = qml.size();
        addEdit(edits, begin, end - begin, QString());
    }
    return true;
}

Result refuse(const QString &message)
{
    return Result { false, message };
}

} // namespace

State analyze(const QByteArray &image)
{
    State state;
    if (image.isEmpty()) {
        state.detail = QStringLiteral("Select the HD-Player.exe path first.");
        return state;
    }
    Doc doc;
    QString error;
    if (!findDoc(image, QStringLiteral("PlrAboutSetting.qml"), &doc, &error)) {
        state.detail = error;
        return state;
    }
    const QString masked = QmlText::mask(doc.qml);
    const Anchors anchors = findAnchors(doc.qml, masked, &error);
    if (!anchors.ok) {
        state.detail = error;
        return state;
    }
    state.supported = true;
    state.sectionsHidden = anchors.sectionsHidden;
    bool hasLinksField = false;
    if (readConfig(doc.qml, &state.config, &hasLinksField)) {
        state.applied = true;
        // A tag written by an older version holds one custom link only; the
        // rows are then read back from the page itself. A current tag always
        // lists its rows, so an empty list really means "no row was named".
        if (!hasLinksField) {
            Config shown;
            if (readShown(doc.qml, masked, anchors, &shown)) state.config.links = shown.links;
        }
        state.detail = QStringLiteral("Applied: the About text and the link rows below are the "
                                      "patched ones, and the update button is disabled.");
    } else {
        // Not patched: hand the fields what the page says now, so the text and
        // each link can be edited in place instead of retyped.
        Config shown;
        if (readShown(doc.qml, masked, anchors, &shown)) state.config = shown;
        state.detail = QStringLiteral("Stock About page. The fields show the text and links the "
                                      "page has now; edit them, untick a row to hide it and "
                                      "apply. The update button is disabled by the patch.");
    }
    return state;
}

Result apply(const QString &exePath, bool enabled, const Config &rawConfig)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return refuse(QStringLiteral("Close all BlueStacks instances before changing the About page."));
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return refuse(QStringLiteral("Cannot read the selected EXE."));
    const QByteArray original = file.readAll();
    file.close();

    const Config config = normalized(rawConfig);
    bool anyLink = false;
    for (const auto &link : config.links)
        if (!link.hidden && !link.url.isEmpty()) anyLink = true;
    if (enabled && config.text.isEmpty() && !anyLink && !config.showSections)
        return refuse(QStringLiteral("Enter the About text, keep at least one link or switch the "
                                     "sections on first."));

    QByteArray changed = original;
    Doc doc;
    QString error;
    if (!findDoc(changed, QStringLiteral("PlrAboutSetting.qml"), &doc, &error))
        return refuse(error);

    const State before = analyze(original);
    if (!before.supported) return refuse(before.detail);

    if (enabled) {
        if (before.applied && sameConfig(before.config, config))
            return { true, QStringLiteral("This About page content is already applied.") };
        QList<Edit> edits;
        if (before.applied) {
            // Re-enable with different content: build the stock document
            // first, then plan the new content against it.
            if (!planDisable(doc.qml, edits, &error) || !applyEdits(doc.qml, edits))
                return refuse(QStringLiteral("Cannot safely update the previous About page edit; "
                                             "no file was written."));
            edits.clear();
        }
        if (!planEnable(doc.qml, config, edits, &error))
            return refuse(error.isEmpty() ? QStringLiteral("The About page is not a supported shape; "
                                                           "no file was written.") : error);
        if (!applyEdits(doc.qml, edits))
            return refuse(QStringLiteral("About page edits overlap in this player; no file was written."));
        const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link,
                                          header + doc.qml.toUtf8(), &error))
            return refuse(error);
    } else {
        if (!before.applied)
            return { true, QStringLiteral("The stock About page is already in place.") };
        QList<Edit> edits;
        if (!planDisable(doc.qml, edits, &error) || !applyEdits(doc.qml, edits))
            return refuse(error.isEmpty() ? QStringLiteral("Cannot safely restore the stock About page; "
                                                           "no file was written.") : error);
        const QByteArray header = doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray();
        if (!QtResources::replaceResource(changed, doc.file.entryOff, doc.link,
                                          header + doc.qml.toUtf8(), &error))
            return refuse(error);
    }

    const State after = analyze(changed);
    if (!after.supported)
        return refuse(QStringLiteral("The written About page did not pass read-back verification; "
                                     "no file was written."));
    if (enabled && (!after.applied || !sameConfig(after.config, config)))
        return refuse(QStringLiteral("The written About page did not read back with the new content; "
                                     "no file was written."));
    if (!enabled && after.applied)
        return refuse(QStringLiteral("The written About page is still patched; no file was written."));

    if (!ThemeEngine::backup(exePath))
        return refuse(QStringLiteral("Could not preserve the original backup; no EXE was written."));
    if (!PatchFile::writeVerified(exePath, original, changed, &error)) return refuse(error);
    const int caches = QtResources::clearPlayerQmlCache(exePath);
    QString msg = enabled
        ? QStringLiteral("About page updated: the text and every link row are written as edited "
                         "and the update button is disabled. Restart BlueStacks.")
        : QStringLiteral("Stock About page restored (text, every link row, section visibility and "
                         "the update button). Restart BlueStacks.");
    if (caches > 0)
        msg += QStringLiteral(" Cleared the player QML disk cache so BlueStacks reloads the source.");
    return { true, msg };
}


// ---------------------------------------------------------------- diagnostics

namespace {

QString oneLine(const QString &text, int limit = 160)
{
    QString out = text;
    out.replace(QLatin1Char('\r'), QLatin1Char(' '));
    out.replace(QLatin1Char('\n'), QLatin1Char(' '));
    out = out.simplified();
    if (out.size() > limit)
        out = out.left(limit) + QStringLiteral(" …");
    return out;
}

QString span(const QmlText::Object &object)
{
    return object.open < 0 ? QStringLiteral("not found")
                           : QStringLiteral("%1..%2").arg(object.open).arg(object.end);
}

} // namespace

QString probeReport(const QString &exePath)
{
    QStringList lines;
    const auto add = [&lines](const QString &text) { lines << text; };

    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) {
        add(QStringLiteral("*** CANNOT READ %1 ***").arg(exePath));
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }
    const QByteArray image = file.readAll();
    file.close();
    add(QStringLiteral("file: %1 (%2 bytes)").arg(exePath).arg(image.size()));

    const auto files = QtResources::registeredFiles(image);
    add(QStringLiteral("registered resource files: %1").arg(files.size()));
    int matches = 0;
    for (const auto &entry : files) {
        if (entry.name == QLatin1String("PlrAboutSetting.qml")) {
            ++matches;
            const auto bytes = QtResources::readFile(image, entry);
            add(QStringLiteral("  PlrAboutSetting.qml: entryOff=%1 flags=%2 stored=%3 decoded=%4")
                    .arg(entry.entryOff)
                    .arg(entry.flags)
                    .arg(QtResources::readFile(image, entry).isEmpty()
                             ? QStringLiteral("unreadable")
                             : QString::number(ByteUtils::be32(image, entry.entryOff)))
                    .arg(bytes.size()));
        }
    }
    add(QStringLiteral("PlrAboutSetting.qml entries: %1").arg(matches));

    const ThemeInfo theme = ThemeEngine::findThemeZlib(image);
    add(QStringLiteral("theme pool: hasQrcNode=%1 ambiguous=%2")
            .arg(theme.hasQrcNode ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(theme.ambiguous ? QStringLiteral("yes") : QStringLiteral("no")));
    if (theme.hasQrcNode) {
        int named = 0;
        for (const auto &entry : QtResources::filesInPool(image, theme.resource))
            if (entry.name == QLatin1String("PlrAboutSetting.qml")) ++named;
        add(QStringLiteral("  PlrAboutSetting.qml in that pool: %1").arg(named));
    }

    QString error;
    Doc doc;
    if (!findDoc(image, QStringLiteral("PlrAboutSetting.qml"), &doc, &error)) {
        add(QStringLiteral("*** DOC NOT FOUND: %1 ***").arg(error));
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }
    add(QStringLiteral("document: %1 chars, bom=%2, crlf=%3")
            .arg(doc.qml.size())
            .arg(doc.bom ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(doc.qml.contains(QLatin1String("\r\n")) ? QStringLiteral("yes")
                                                         : QStringLiteral("no")));

    const QString masked = QmlText::mask(doc.qml);
    const Anchors anchors = findAnchors(doc.qml, masked, &error);
    if (!anchors.ok) {
        add(QStringLiteral("*** ANCHORS FAILED: %1 ***").arg(error));
        // Say which half of the shape is missing, so the next build can be
        // compared instead of re-guessed.
        QmlText::Object content;
        add(QStringLiteral("  iContentLayout: %1")
                .arg(findObjectById(doc.qml, masked, QStringLiteral("iContentLayout"), &content)
                         ? span(content)
                         : QStringLiteral("missing")));
        for (const auto &type : { QStringLiteral("UiScrollView"), QStringLiteral("ScrollView"),
                                  QStringLiteral("Flickable") }) {
            add(QStringLiteral("  %1 objects: %2").arg(type)
                    .arg(QmlText::objects(masked, type).size()));
        }
        for (const auto &id : stockLinkIds()) {
            QmlText::Object object;
            add(QStringLiteral("  %1: %2").arg(id,
                    findObjectById(doc.qml, masked, id, &object) ? span(object)
                                                                 : QStringLiteral("missing")));
        }
        QmlText::Object terms;
        add(QStringLiteral("  iTermsOfUseText: %1")
                .arg(findObjectById(doc.qml, masked, QStringLiteral("iTermsOfUseText"), &terms)
                         ? span(terms)
                         : QStringLiteral("missing")));
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    add(QStringLiteral("content column: %1").arg(span(anchors.content)));
    add(QStringLiteral("about text object: %1  (binding %2..%3)")
            .arg(span(anchors.aboutText))
            .arg(anchors.aboutTextBinding.valueBegin)
            .arg(anchors.aboutTextBinding.valueEnd));
    add(QStringLiteral("  raw: %1")
            .arg(oneLine(doc.qml.mid(anchors.aboutTextBinding.valueBegin,
                                     anchors.aboutTextBinding.valueEnd
                                         - anchors.aboutTextBinding.valueBegin))));
    add(QStringLiteral("  chunks: %1")
            .arg(oneLine(renderedChunks(doc.qml.mid(anchors.aboutTextBinding.valueBegin,
                                                    anchors.aboutTextBinding.valueEnd
                                                        - anchors.aboutTextBinding.valueBegin))
                             .join(QStringLiteral(" | ")))));
    add(QStringLiteral("about block: %1   links block: %2   hidden=%3/%4")
            .arg(span(anchors.aboutBlock), span(anchors.linksBlock))
            .arg(hidesItself(masked, anchors.aboutBlock) ? QStringLiteral("yes")
                                                         : QStringLiteral("no"))
            .arg(hidesItself(masked, anchors.linksBlock) ? QStringLiteral("yes")
                                                         : QStringLiteral("no")));
    add(QStringLiteral("update button: %1   update rows: %2   qq rows: %3")
            .arg(anchors.updateButton.open >= 0 ? span(anchors.updateButton)
                                                : QStringLiteral("not found"))
            .arg(anchors.updateInfoRows.size())
            .arg(anchors.qqRows.size()));
    for (const auto &row : anchors.links) {
        const QString text = doc.qml.mid(row.text.valueBegin,
                                         row.text.valueEnd - row.text.valueBegin);
        add(QStringLiteral("row %1: %2 handler=%3")
                .arg(row.id, span(row.object),
                     row.hasHandler ? QStringLiteral("yes") : QStringLiteral("no")));
        add(QStringLiteral("   text: %1").arg(oneLine(text)));
        add(QStringLiteral("   chunks: %1")
                .arg(oneLine(renderedChunks(text).join(QStringLiteral(" | ")))));
        if (row.hasHandler) {
            add(QStringLiteral("   handler: %1")
                    .arg(oneLine(doc.qml.mid(row.handler.valueBegin,
                                             row.handler.valueEnd - row.handler.valueBegin))));
        }
    }

    Config config;
    if (readShown(doc.qml, masked, anchors, &config)) {
        add(QStringLiteral("readShown text: \"%1\"").arg(oneLine(config.text, 240)));
        for (const auto &link : config.links) {
            add(QStringLiteral("readShown link %1: name=\"%2\" url=\"%3\" hidden=%4")
                    .arg(link.id, oneLine(link.name, 60), oneLine(link.url, 80))
                    .arg(link.hidden ? QStringLiteral("yes") : QStringLiteral("no")));
        }
    } else {
        add(QStringLiteral("*** readShown FAILED (empty text and links) ***"));
    }

    const State state = analyze(image);
    add(QStringLiteral("analyze: supported=%1 applied=%2 sectionsHidden=%3")
            .arg(state.supported ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(state.applied ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(state.sectionsHidden ? QStringLiteral("yes") : QStringLiteral("no")));
    add(QStringLiteral("  detail: %1").arg(oneLine(state.detail, 300)));
    add(QStringLiteral("  config.text: \"%1\"").arg(oneLine(state.config.text, 240)));
    for (const auto &link : state.config.links) {
        add(QStringLiteral("  config link %1: name=\"%2\" url=\"%3\" hidden=%4")
                .arg(link.id, oneLine(link.name, 60), oneLine(link.url, 80))
                .arg(link.hidden ? QStringLiteral("yes") : QStringLiteral("no")));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace AboutPatcher
