#include "AboutPatcher.h"

#include "BsDetector.h"
#include "PatchFile.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

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
    object.insert(QStringLiteral("label"), config.linkLabel);
    object.insert(QStringLiteral("url"), config.linkUrl);
    const QByteArray json = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return kConfigBegin + QString::fromLatin1(json.toBase64());
}

bool readConfig(const QString &qml, Config *config)
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
    config->text = object.value(QStringLiteral("text")).toString();
    config->linkLabel = object.value(QStringLiteral("label")).toString();
    config->linkUrl = object.value(QStringLiteral("url")).toString();
    return true;
}

bool sameConfig(const Config &a, const Config &b)
{
    return a.text == b.text && a.linkLabel == b.linkLabel && a.linkUrl == b.linkUrl;
}

Config normalized(Config config)
{
    config.text = config.text.trimmed();
    config.linkLabel = config.linkLabel.trimmed();
    config.linkUrl = config.linkUrl.trimmed();
    return config;
}

struct Anchors
{
    QmlText::Object content;
    QmlText::Object aboutBlock;
    QmlText::Object linksBlock;
    QmlText::Object aboutText;
    QmlText::Object linkText;
    QList<QmlText::Object> qqRows;
    QList<QmlText::Object> updateInfoRows;
    QmlText::Object updateButton;
    QmlText::Binding aboutTextBinding;
    QmlText::Binding linkTextBinding;
    QmlText::Binding linkHandler;
    bool hasLinkHandler = false;
    bool ok = false;
};

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
    QmlText::Object website;
    if (!findObjectById(qml, masked, QStringLiteral("iWebsiteText"), &website)
        && !findObjectById(qml, masked, QStringLiteral("iSupportText"), &website)) {
        if (why) *why = QStringLiteral("The stock About links were not found in this player.");
        return anchors;
    }
    anchors.linkText = website;
    if (!directBinding(masked, anchors.linkText.open, anchors.linkText.end,
                       QStringLiteral("text"), &anchors.linkTextBinding)) {
        if (why) *why = QStringLiteral("The stock About link is not a text field; nothing would be written.");
        return anchors;
    }
    if (!sectionOf(masked, anchors.content, anchors.linkText, &anchors.linksBlock)) {
        if (why) *why = QStringLiteral("The stock About links are not inside the About content column.");
        return anchors;
    }
    anchors.hasLinkHandler = directBinding(masked, anchors.linkText.open, anchors.linkText.end,
                                           QStringLiteral("onLinkActivated"), &anchors.linkHandler);
    findUpdateButton(qml, masked, anchors.content, &anchors.updateButton);
    anchors.qqRows = findQqRows(qml, masked, anchors.linksBlock);
    anchors.updateInfoRows = findUpdateInfoRows(qml, masked, anchors.content,
                                                QList<QmlText::Object> { anchors.linkText,
                                                                         anchors.aboutText });
    anchors.ok = true;
    return anchors;
}

bool sameObject(const QmlText::Object &a, const QmlText::Object &b)
{
    return a.open == b.open && a.end == b.end;
}

bool planEnable(const QString &qml, const Config &config, QList<Edit> &edits, QString *error)
{
    const QString masked = QmlText::mask(qml);
    const Anchors anchors = findAnchors(qml, masked, error);
    if (!anchors.ok) return false;
    const QString nl = lineEnding(qml);
    const bool hasText = !config.text.isEmpty();
    const bool hasLink = !config.linkUrl.isEmpty();
    if (!hasText && !hasLink) {
        if (error) *error = QStringLiteral("Enter the text to show on the About page first.");
        return false;
    }

    const bool sharedBlock = sameObject(anchors.aboutBlock, anchors.linksBlock);

    // ---- the About text: replace the stock copyright/Terms line ----------
    if (hasText) {
        forceShown(qml, anchors.aboutBlock, edits);
        const QString literal = QLatin1Char('"') + escapeQml(escapeHtml(config.text))
            + QLatin1Char('"');
        const QString original = qml.mid(anchors.aboutTextBinding.valueBegin,
                                         anchors.aboutTextBinding.valueEnd
                                             - anchors.aboutTextBinding.valueBegin);
        addEdit(edits, anchors.aboutTextBinding.valueBegin,
                anchors.aboutTextBinding.valueEnd - anchors.aboutTextBinding.valueBegin,
                tagged(literal, original, kTextTag));
    } else if (!sharedBlock || !hasLink) {
        // Only a link was configured: keep the page tidy by hiding the empty
        // About-text section instead of leaving a stock copyright line.
        hideObject(qml, anchors.aboutBlock, edits);
    }

    // ---- the stock links: one user link replaces all of them -------------
    if (hasLink) {
        // `forceShown` is skipped for the block that already got it above:
        // two edits on the same binding would overlap.
        if (!sharedBlock || !hasText) forceShown(qml, anchors.linksBlock, edits);
        for (const auto &id : { QStringLiteral("iSupportEmailText"), QStringLiteral("iSupportText"),
                                QStringLiteral("iWebsiteText") }) {
            QmlText::Object row;
            if (!findObjectById(qml, masked, id, &row)) continue;
            if (sameObject(row, anchors.linkText)) continue; // the row we repurpose
            hideObject(qml, row, edits);
        }
        for (const auto &row : anchors.qqRows) hideObject(qml, row, edits);

        const QString label = config.linkLabel.isEmpty() ? config.linkUrl : config.linkLabel;
        const QString html = QStringLiteral("<a href=\"") + escapeHtml(config.linkUrl)
            + QStringLiteral("\">") + escapeHtml(label) + QStringLiteral("</a>");
        const QString literal = QLatin1Char('"') + escapeQml(html) + QLatin1Char('"');
        const QString original = qml.mid(anchors.linkTextBinding.valueBegin,
                                         anchors.linkTextBinding.valueEnd
                                             - anchors.linkTextBinding.valueBegin);
        addEdit(edits, anchors.linkTextBinding.valueBegin,
                anchors.linkTextBinding.valueEnd - anchors.linkTextBinding.valueBegin,
                tagged(literal, original, kTextTag));

        const QString call = QStringLiteral("Qt.openUrlExternally(\"")
            + escapeQml(config.linkUrl) + QStringLiteral("\")");
        if (anchors.hasLinkHandler) {
            const QString handlerOriginal =
                qml.mid(anchors.linkHandler.valueBegin,
                        anchors.linkHandler.valueEnd - anchors.linkHandler.valueBegin);
            addEdit(edits, anchors.linkHandler.valueBegin,
                    anchors.linkHandler.valueEnd - anchors.linkHandler.valueBegin,
                    tagged(call, handlerOriginal, kHandlerTag));
        } else {
            // No handler in this build: add one above the text member, tagged
            // so turning the option off removes exactly these two lines.
            const int lineStart =
                qml.lastIndexOf(QLatin1Char('\n'), anchors.linkTextBinding.nameBegin - 1) + 1;
            QString indent;
            int i = lineStart;
            while (i < qml.size()
                   && (qml.at(i) == QLatin1Char(' ') || qml.at(i) == QLatin1Char('\t')))
                indent += qml.at(i++);
            addEdit(edits, lineStart, 0,
                    indent + kHandlerMarker + nl + indent
                        + QStringLiteral("onLinkActivated: ") + call + nl);
        }
    } else if (!sameObject(anchors.linksBlock, anchors.aboutBlock)) {
        // Text only: no custom link, and no stock links either.
        hideObject(qml, anchors.linksBlock, edits);
    } else {
        for (const auto &id : { QStringLiteral("iSupportEmailText"), QStringLiteral("iSupportText"),
                                QStringLiteral("iWebsiteText") }) {
            QmlText::Object row;
            if (findObjectById(qml, masked, id, &row)) hideObject(qml, row, edits);
        }
        for (const auto &row : anchors.qqRows) hideObject(qml, row, edits);
    }

    // ---- the update button and its "More info" links: never offered again -
    if (anchors.updateButton.open >= 0) hideObject(qml, anchors.updateButton, edits);
    for (const auto &row : anchors.updateInfoRows) hideObject(qml, row, edits);

    addEdit(edits, qml.size(), 0, nl + configLine(config));
    return true;
}

bool planDisable(const QString &qml, QList<Edit> &edits, QString *error)
{
    const QString masked = QmlText::mask(qml);
    const Anchors anchors = findAnchors(qml, masked, error);
    if (!anchors.ok) return false;

    restoreShown(qml, anchors.aboutBlock, edits);
    if (!sameObject(anchors.linksBlock, anchors.aboutBlock))
        restoreShown(qml, anchors.linksBlock, edits);
    // Sections hidden when only one of text/link was configured.
    showObject(qml, anchors.aboutBlock, edits);
    if (!sameObject(anchors.linksBlock, anchors.aboutBlock))
        showObject(qml, anchors.linksBlock, edits);

    for (const auto &id : { QStringLiteral("iSupportEmailText"), QStringLiteral("iSupportText"),
                            QStringLiteral("iWebsiteText") }) {
        QmlText::Object row;
        if (findObjectById(qml, masked, id, &row)) showObject(qml, row, edits);
    }
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
    // The custom link text and its handler.
    QmlText::Binding link;
    if (directBinding(masked, anchors.linkText.open, anchors.linkText.end,
                      QStringLiteral("text"), &link)) {
        QString original;
        int tagEnd = -1;
        if (taggedAfter(qml, link.valueBegin, kTextTag, &original, &tagEnd))
            addEdit(edits, link.valueBegin, tagEnd - link.valueBegin, original);
    }
    QmlText::Binding handler;
    if (directBinding(masked, anchors.linkText.open, anchors.linkText.end,
                      QStringLiteral("onLinkActivated"), &handler)) {
        QString original;
        int tagEnd = -1;
        if (taggedAfter(qml, handler.valueBegin, kHandlerTag, &original, &tagEnd))
            addEdit(edits, handler.valueBegin, tagEnd - handler.valueBegin, original);
    }
    const int marker = qml.indexOf(kHandlerMarker);
    if (marker >= 0) {
        const int start = qml.lastIndexOf(QLatin1Char('\n'), marker - 1) + 1;
        const int firstEnd = qml.indexOf(QLatin1Char('\n'), marker);
        int stop = firstEnd < 0 ? qml.size() : qml.indexOf(QLatin1Char('\n'), firstEnd + 1);
        if (stop < 0) stop = qml.size();
        else ++stop;
        addEdit(edits, start, stop - start, QString());
    }

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
    if (readConfig(doc.qml, &state.config)) {
        state.applied = true;
        state.detail = QStringLiteral("Custom About text and link are applied; the update button is disabled.");
    } else {
        state.detail = QStringLiteral("Stock About page. Apply to replace the stock texts and links "
                                      "and disable the update button.");
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
    if (enabled && config.text.isEmpty() && config.linkUrl.isEmpty())
        return refuse(QStringLiteral("Enter the text or the link to show on the About page first."));

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
        ? QStringLiteral("About page updated: the stock texts are replaced by yours, the stock links "
                         "are replaced by your single link and the update button is disabled. "
                         "Restart BlueStacks.")
        : QStringLiteral("Stock About page restored (texts, links and update button). "
                         "Restart BlueStacks.");
    if (caches > 0)
        msg += QStringLiteral(" Cleared the player QML disk cache so BlueStacks reloads the source.");
    return { true, msg };
}

} // namespace AboutPatcher
