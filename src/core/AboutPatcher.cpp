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

const QString kHiddenWrap = QStringLiteral(
    "false /* BSI CUSTOMIZER: about hidden */ && (");
const QString kHiddenLine = QStringLiteral(
    "visible: false // BSI CUSTOMIZER: about hidden");
const QString kBlockBegin = QStringLiteral("// BSI CUSTOMIZER: about-block-begin");
const QString kBlockEnd = QStringLiteral("// BSI CUSTOMIZER: about-block-end");
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
    int depth = 0;
    int open = -1;
    for (int i = 0; i < pos && i < masked.size(); ++i) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('{')) { open = i; ++depth; }
        else if (c == QLatin1Char('}')) {
            if (depth == 0) return false;
            --depth;
        }
    }
    if (open < 0) return false;
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

// Every object declared directly inside `parent`, in source order. The token
// before each `{` must look like a QML type (a capitalised, possibly
// qualified name); function bodies, JS object literals and `Behavior on x`
// blocks are skipped instead of being mistaken for children.
QList<QmlText::Object> childObjects(const QString &masked, const QmlText::Object &parent)
{
    QList<QmlText::Object> out;
    int depth = 0;
    int i = parent.open;
    while (i < parent.end) {
        const QChar c = masked.at(i);
        if (c == QLatin1Char('{')) {
            ++depth;
            if (depth == 2) {
                int nameEnd = i;
                while (nameEnd > parent.open + 1 && masked.at(nameEnd - 1).isSpace()) --nameEnd;
                int nameBegin = nameEnd;
                while (nameBegin > parent.open + 1
                       && (masked.at(nameBegin - 1).isLetterOrNumber()
                           || masked.at(nameBegin - 1) == QLatin1Char('_')
                           || masked.at(nameBegin - 1) == QLatin1Char('.')))
                    --nameBegin;
                const int close = matchingBrace(masked, i);
                if (close < 0) return {};
                if (nameBegin < nameEnd && masked.at(nameBegin).isUpper())
                    out << QmlText::Object { nameBegin, i, close };
                i = close;
                depth = 1;
                continue;
            }
        } else if (c == QLatin1Char('}')) {
            --depth;
        }
        ++i;
    }
    return out;
}

struct Anchors
{
    QmlText::Object content;
    QList<QmlText::Object> children;
    bool ok = false;
};

Anchors findAnchors(const QString &qml, const QString &masked, QString *why)
{
    Anchors anchors;
    if (!findObjectById(qml, masked, QStringLiteral("iContentLayout"), &anchors.content)) {
        if (why) *why = QStringLiteral("This player's About page has no replaceable content column.");
        return anchors;
    }
    anchors.children = childObjects(masked, anchors.content);
    if (anchors.children.isEmpty()) {
        if (why) *why = QStringLiteral("The About page's content column has no replaceable blocks.");
        return anchors;
    }
    anchors.ok = true;
    return anchors;
}

// Where the custom block is inserted: the start of the line that holds the
// content column's closing brace. Refused when that brace shares a line with
// content, so removing the block later is byte-exact.
bool insertionPoint(const QString &qml, const QmlText::Object &content, int *at, QString *why)
{
    const int close = content.end - 1;
    if (close <= content.open) {
        if (why) *why = QStringLiteral("The About page's content column is unbalanced.");
        return false;
    }
    const int lineStart = qml.lastIndexOf(QLatin1Char('\n'), close - 1) + 1;
    for (int i = lineStart; i < close; ++i) {
        if (!qml.at(i).isSpace()) {
            if (why) *why = QStringLiteral("The About page's content column is not written one "
                                           "block per line; nothing would be written.");
            return false;
        }
    }
    *at = lineStart;
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

// The single custom block that replaces the whole stock About content: one
// wrapping rich-text line, plus a link handler when a URL is configured.
QString blockText(const Config &config, const QString &nl)
{
    const QString indent = QStringLiteral("                ");   // inside iContentLayout
    const QString member = QStringLiteral("                    ");
    QString html = escapeHtml(config.text);
    QString handler;
    if (!config.linkUrl.isEmpty()) {
        const QString label = config.linkLabel.isEmpty() ? config.linkUrl : config.linkLabel;
        if (!html.isEmpty()) html += QLatin1Char(' ');
        html += QStringLiteral("<a href=\"") + escapeHtml(config.linkUrl) + QStringLiteral("\">")
            + escapeHtml(label) + QStringLiteral("</a>");
        handler = nl + member + QStringLiteral("onLinkActivated: Qt.openUrlExternally(\"")
            + escapeQml(config.linkUrl) + QStringLiteral("\")");
    }
    QString out;
    out += indent + kBlockBegin + nl;
    out += indent + QStringLiteral("Text {") + nl;
    out += member + QStringLiteral("Layout.fillWidth: true") + nl;
    out += member + QStringLiteral("wrapMode: Text.Wrap") + nl;
    out += member + QStringLiteral("textFormat: Text.RichText") + nl;
    out += member + QStringLiteral("font: UiTheme.fonts.bodyMedium") + nl;
    out += member + QStringLiteral("color: UiTheme.colors.primary20") + nl;
    out += member + QStringLiteral("text: \"") + escapeQml(html) + QStringLiteral("\"") + handler + nl;
    out += indent + QStringLiteral("}") + nl;
    out += indent + kBlockEnd + nl;
    return out;
}

bool planEnable(const QString &qml, const Config &config, QList<Edit> &edits, QString *error)
{
    const QString masked = QmlText::mask(qml);
    const Anchors anchors = findAnchors(qml, masked, error);
    if (!anchors.ok) return false;
    int insert = -1;
    if (!insertionPoint(qml, anchors.content, &insert, error)) return false;
    const QString nl = lineEnding(qml);

    for (const auto &child : anchors.children) {
        QmlText::Binding visible;
        if (directBinding(masked, child.open, child.end, QStringLiteral("visible"), &visible)) {
            const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
            if (value.startsWith(kHiddenWrap)) continue; // already hidden by us
            addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                    kHiddenWrap + value + QStringLiteral(")"));
        } else {
            addEdit(edits, child.open + 1, 0,
                    nl + QStringLiteral("        ") + kHiddenLine + nl);
        }
    }
    addEdit(edits, insert, 0, blockText(config, nl));
    addEdit(edits, qml.size(), 0, nl + configLine(config));
    return true;
}

bool planDisable(const QString &qml, QList<Edit> &edits, QString *error)
{
    const QString masked = QmlText::mask(qml);
    const Anchors anchors = findAnchors(qml, masked, error);
    if (!anchors.ok) return false;
    const QString nl = lineEnding(qml);

    // Remove the custom block, from the start of its first marker line to the
    // end of the line that holds the closing marker.
    const int begin = qml.indexOf(kBlockBegin);
    if (begin >= 0) {
        const int start = qml.lastIndexOf(QLatin1Char('\n'), begin - 1) + 1;
        const int endMarker = qml.indexOf(kBlockEnd, begin);
        int stop = endMarker >= 0 ? qml.indexOf(QLatin1Char('\n'), endMarker) : -1;
        if (stop < 0) stop = qml.size();
        else ++stop;
        if (start < stop) addEdit(edits, start, stop - start, QString());
    }

    for (const auto &child : anchors.children) {
        const QString inserted = nl + QStringLiteral("        ") + kHiddenLine + nl;
        // Only the line this patcher wrote counts, never a marker further down
        // inside the same block.
        const int at = child.open + 1;
        if (qml.mid(at, inserted.size()) == inserted) {
            addEdit(edits, at, inserted.size(), QString());
            continue;
        }
        QmlText::Binding visible;
        if (!directBinding(masked, child.open, child.end, QStringLiteral("visible"), &visible))
            continue;
        const QString value = qml.mid(visible.valueBegin, visible.valueEnd - visible.valueBegin);
        if (value.startsWith(kHiddenWrap) && value.endsWith(QLatin1Char(')')))
            addEdit(edits, visible.valueBegin, visible.valueEnd - visible.valueBegin,
                    value.mid(kHiddenWrap.size(), value.size() - kHiddenWrap.size() - 1));
    }

    const int at = qml.indexOf(kConfigBegin);
    if (at >= 0) {
        int start = at;
        if (start > 0 && qml.at(start - 1) == QLatin1Char('\n')) {
            --start;
            if (start > 0 && qml.at(start - 1) == QLatin1Char('\r')) --start;
        }
        int stop = qml.indexOf(QLatin1Char('\n'), at);
        if (stop < 0) stop = qml.size();
        addEdit(edits, start, stop - start, QString());
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
        state.detail = QStringLiteral("Custom About text and link are applied.");
    } else {
        state.detail = QStringLiteral("Stock About page. Apply to remove the stock content "
                                      "and show your own text.");
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
    if (enabled && config.text.isEmpty())
        return refuse(QStringLiteral("Enter the text to show on the About page first."));

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
            // Re-enable with different text: build the stock document first,
            // then plan the new content against it.
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
        ? QStringLiteral("About page replaced: the stock content is hidden and your text/link is shown. "
                         "Restart BlueStacks.")
        : QStringLiteral("Stock About page restored. Restart BlueStacks.");
    if (caches > 0)
        msg += QStringLiteral(" Cleared the player QML disk cache so BlueStacks reloads the source.");
    return { true, msg };
}

} // namespace AboutPatcher
