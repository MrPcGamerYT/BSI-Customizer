#include "TitlebarDetails.h"

#include "QmlText.h"

#include <QByteArray>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace TitlebarDetails {
namespace {

bool ownBinding(const QString &masked, const QmlText::Object &object,
                const QString &name, QmlText::Binding *out);

// Locate the innermost object owning an exact id, not a containing object or a
// string/comment that happens to mention it. More than one match is unsafe.
bool objectById(const QString &source, const QString &masked, const QString &id,
                QmlText::Object *out)
{
    const QRegularExpression rx(QStringLiteral("\\bid\\s*:\\s*")
                                + QRegularExpression::escape(id) + QStringLiteral("\\b"));
    auto matches = rx.globalMatch(masked);
    bool found = false;
    while (matches.hasNext()) {
        const int pos = int(matches.next().capturedStart());
        QVector<int> opens;
        for (int i = 0; i < pos; ++i) {
            if (masked.at(i) == QLatin1Char('{')) opens << i;
            else if (masked.at(i) == QLatin1Char('}') && !opens.isEmpty()) opens.removeLast();
        }
        if (opens.isEmpty()) continue;
        const int open = opens.last();
        int end = open + 1, depth = 1;
        for (; end < masked.size() && depth; ++end) {
            if (masked.at(end) == QLatin1Char('{')) ++depth;
            else if (masked.at(end) == QLatin1Char('}')) --depth;
        }
        QmlText::Binding binding;
        if (depth || !ownBinding(masked, { -1, open, end }, QStringLiteral("id"), &binding)
            || source.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin) != id)
            continue;
        if (found) return false;
        *out = { -1, open, end };
        found = true;
    }
    return found;
}

bool ownBinding(const QString &masked, const QmlText::Object &object,
                const QString &name, QmlText::Binding *out)
{
    for (int at = object.open + 1; at < object.end;) {
        QmlText::Binding binding;
        if (!QmlText::binding(masked, at, object.end, name, &binding)) return false;
        int depth = 0;
        for (int i = object.open + 1; i < binding.nameBegin; ++i) {
            if (masked.at(i) == QLatin1Char('{')) ++depth;
            else if (masked.at(i) == QLatin1Char('}')) --depth;
        }
        if (depth == 0) { *out = binding; return true; }
        at = binding.valueEnd;
    }
    return false;
}

struct Edit { int at, length; QString text; };

QString compactTitleChrome(const QString &source)
{
    const QString masked = QmlText::mask(source);
    QmlText::Object anchor, row;
    if (!objectById(source, masked, QStringLiteral("iTitleLayout"), &anchor)
        && !objectById(source, masked, QStringLiteral("iBstTxt"), &anchor)) return source;
    // Bound the fallback to the title's closest layout ancestor: the native
    // topbar row. Never compact a containing Window/Settings-page subtree.
    for (const auto &type : { QStringLiteral("UiRowLayout"), QStringLiteral("RowLayout"),
                             QStringLiteral("Row"), QStringLiteral("UiColumnLayout"),
                             QStringLiteral("ColumnLayout"), QStringLiteral("Column") }) {
        for (const auto &candidate : QmlText::objects(masked, type))
            if (candidate.open < anchor.open && candidate.end > anchor.end
                && candidate.open > row.open) row = candidate;
    }
    if (row.open < 0) return source;
    QList<QmlText::Object> candidates { row };
    for (const auto &type : { QStringLiteral("Text"), QStringLiteral("Item"), QStringLiteral("Rectangle"),
                             QStringLiteral("UiImageButton"), QStringLiteral("MouseArea"),
                             QStringLiteral("UiRowLayout"), QStringLiteral("UiColumnLayout"),
                             QStringLiteral("RowLayout"), QStringLiteral("ColumnLayout") })
        for (const auto &object : QmlText::objects(masked, type))
            if (object.open > row.open && object.end < row.end) candidates << object;
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.open < b.open; });
    QList<Edit> edits;
    int boundary = -1;
    for (const auto &object : candidates) {
        if (object.open < boundary) continue;
        const QString body = source.mid(object.open, object.end - object.open);
        // Cleanup/recovery markers own exact spellings of their edited fields.
        // Do not reformat those fields and make a later Restore miss them.
        if (body.contains(QLatin1String("BSI CUSTOMIZER"))) continue;
        // The shared scanner preserves literals, comments, operators and line
        // endings. It refuses regex/division/template ambiguity, in which case
        // safe, balanced child objects may still release a little space.
        const QString compact = QmlText::compactWhitespace(body);
        if (body == compact) continue;
        edits << Edit { object.open, object.end - object.open, compact };
        boundary = object.end;
    }
    QString result = source;
    for (auto it = edits.crbegin(); it != edits.crend(); ++it) result.replace(it->at, it->length, it->text);
    return qCompress(result.toUtf8(), 9).size() < qCompress(source.toUtf8(), 9).size() ? result : source;
}

} // namespace

QString hide(const QString &source, QString *error)
{
    const auto refuse = [&](const QString &why) {
        if (error) *error = why;
        return QString();
    };
    const QString masked = QmlText::mask(source);
    QmlText::Object version, instance, name, details, title;
    if (!objectById(source, masked, QStringLiteral("iVersionTxt"), &version)
        || !objectById(source, masked, QStringLiteral("iImageTxt"), &instance)
        || !objectById(source, masked, QStringLiteral("iBstTxt"), &name))
        return refuse(QStringLiteral("The title bar does not have one unambiguous player name, version and instance label; nothing was written."));
    const auto mentionsId = [&](const QString &id) {
        return QRegularExpression(QStringLiteral("\\bid\\s*:\\s*") + QRegularExpression::escape(id)
                                  + QStringLiteral("\\b")).match(masked).hasMatch();
    };
    const bool grouped = objectById(source, masked, QStringLiteral("iVersionImgLayout"), &details);
    if (!grouped && mentionsId(QStringLiteral("iVersionImgLayout")))
        return refuse(QStringLiteral("The title-details row is ambiguous; nothing was written."));
    const auto inside = [](const QmlText::Object &child, const QmlText::Object &parent) {
        return child.open > parent.open && child.end < parent.end;
    };
    if (grouped && (!inside(version, details) || !inside(instance, details) || inside(name, details)))
        return refuse(QStringLiteral("The title-details group also contains unexpected content; nothing was written."));
    if (grouped) {
        QString remainder = masked.mid(details.open + 1, details.end - details.open - 2);
        QList<QmlText::Object> labels { version, instance };
        std::sort(labels.begin(), labels.end(), [](const auto &a, const auto &b) { return a.open > b.open; });
        for (const auto &label : labels) {
            int type = label.open - 1;
            while (type > details.open && masked.at(type).isSpace()) --type;
            while (type > details.open && (masked.at(type).isLetterOrNumber()
                   || masked.at(type) == QLatin1Char('_') || masked.at(type) == QLatin1Char('.'))) --type;
            remainder.replace(type - details.open, label.end - type - 1,
                              QString(label.end - type - 1, QLatin1Char(' ')));
        }
        if (QRegularExpression(QStringLiteral(R"(\b[A-Z][A-Za-z0-9_.]*\s*\{)")).match(remainder).hasMatch())
            return refuse(QStringLiteral("The title-details row contains another control; nothing was written."));
    }
    const bool titled = objectById(source, masked, QStringLiteral("iTitleLayout"), &title);
    if (!titled && mentionsId(QStringLiteral("iTitleLayout")))
        return refuse(QStringLiteral("The player-title layout is ambiguous; nothing was written."));
    if (titled && (!inside(name, title) || (grouped && !inside(details, title))))
        return refuse(QStringLiteral("The player name is not in the expected title layout; nothing was written."));

    QList<Edit> edits;
    QMap<int, QStringList> additions;
    const auto set = [&](const QmlText::Object &object, const QString &property, const QString &value) {
        QmlText::Binding binding;
        if (ownBinding(masked, object, property, &binding)) {
            if (source.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin) != value)
                edits << Edit { binding.valueBegin, binding.valueEnd - binding.valueBegin, value };
        } else {
            additions[object.open + 1] << property + QLatin1Char(':') + value;
        }
    };
    const auto conceal = [&](const QmlText::Object &object) {
        set(object, QStringLiteral("visible"), QStringLiteral("false"));
        // fReloadTopbarElements() assigns visible imperatively on every resize.
        // Keep a native callback if present, but prevent it re-showing details.
        const QString guard = QStringLiteral("if(visible)visible=false");
        QmlText::Binding callback;
        if (!ownBinding(masked, object, QStringLiteral("onVisibleChanged"), &callback)) {
            additions[object.open + 1] << QStringLiteral("onVisibleChanged:") + guard;
        } else {
            QString original = source.mid(callback.valueBegin, callback.valueEnd - callback.valueBegin).trimmed();
            if (original == guard || original.startsWith(QLatin1Char('{') + guard + QLatin1Char(';'))) return;
            if (original.startsWith(QLatin1Char('{')) && original.endsWith(QLatin1Char('}')))
                original = original.mid(1, original.size() - 2);
            else if (original.startsWith(QLatin1String("function")))
                original = QLatin1Char('(') + original + QStringLiteral(")()");
            set(object, QStringLiteral("onVisibleChanged"),
                QLatin1Char('{') + guard + QLatin1Char(';') + original + QLatin1Char('}'));
        }
    };
    if (grouped) conceal(details);
    else { conceal(version); conceal(instance); }
    set(name, QStringLiteral("verticalAlignment"), QStringLiteral("Text.AlignVCenter"));
    set(name, QStringLiteral("Layout.fillHeight"), QStringLiteral("true"));
    set(name, QStringLiteral("Layout.alignment"), QStringLiteral("Qt.AlignVCenter"));
    if (titled) {
        set(title, QStringLiteral("Layout.fillHeight"), QStringLiteral("true"));
        set(title, QStringLiteral("Layout.alignment"), QStringLiteral("Qt.AlignVCenter"));
    }
    const QString nl = source.contains(QLatin1String("\r\n")) ? QStringLiteral("\r\n") : QStringLiteral("\n");
    for (auto it = additions.cbegin(); it != additions.cend(); ++it)
        edits << Edit { it.key(), 0, nl + it.value().join(QLatin1Char(';')) + nl };
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.at > b.at; });
    QString result = source;
    int boundary = source.size();
    for (const auto &edit : edits) {
        if (edit.at < 0 || edit.at + edit.length > boundary)
            return refuse(QStringLiteral("Title-details edits overlap; nothing was written."));
        result.replace(edit.at, edit.length, edit.text);
        boundary = edit.at;
    }
    // Only compact the title's own subtree, and only when it reduces the real
    // compressed document. Other controls, callbacks and all text stay intact.
    QmlText::Object changedTitle;
    if (titled && objectById(result, QmlText::mask(result), QStringLiteral("iTitleLayout"), &changedTitle)) {
        QString compact = result;
        compact.replace(changedTitle.open, changedTitle.end - changedTitle.open,
                        QmlText::compactWhitespace(result.mid(changedTitle.open, changedTitle.end - changedTitle.open)));
        if (qCompress(compact.toUtf8(), 9).size() < qCompress(result.toUtf8(), 9).size()) result = compact;
    }
    // The latest players have a completely full compressed record and no
    // relocation cave. Release only safe whitespace in the owning topbar row
    // if the new source still needs more than the original payload; never grow
    // the EXE or erase label/translation strings to make the edit fit.
    if (qCompress(result.toUtf8(), 9).size() > qCompress(source.toUtf8(), 9).size())
        result = compactTitleChrome(result);
    return result;
}

} // namespace TitlebarDetails
