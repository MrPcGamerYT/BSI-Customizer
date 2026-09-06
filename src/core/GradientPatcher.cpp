#include "GradientPatcher.h"
#include "BsDetector.h"
#include "PatchFile.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QFile>
#include <QColor>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace GradientPatcher {
namespace {
struct Object {
    QString type, path, id, objectName;
    int begin = 0, body = 0, end = 0, parent = -1;
};
struct Binding {
    QString name, type;
    int begin = 0, value = 0, end = 0, owner = -1;
};
struct Document {
    QString name, qml, mask, quickPrefix;
    int quickMajor = 0, quickMinor = 0;   // 0/0 = version-less import (assume current)
    QtResources::Link link;
    qint64 entry = -1;
    bool bom = false;
    QList<Object> objects;
    QList<Binding> bindings;
};
struct Site { int object = -1, binding = -1; };
struct Choice { Target target; int document = -1; QList<Site> sites; };
// QQuickGradient::orientation landed in QtQuick 2.12 (Qt 5.12). A Gradient in an
// older UI is valid but always painted top to bottom, so the property is left out
// there instead of writing something that would fail to load.
bool supportsOrientation(const Document &doc)
{
    return doc.quickMajor == 0 || doc.quickMajor > 2
        || (doc.quickMajor == 2 && doc.quickMinor >= 12);
}

struct Catalog {
    QList<Document> documents;
    QList<Choice> choices;
    int candidates = 0, parsed = 0;
};

// Placeholder for one masked string-literal character. It must not be whitespace
// (a binding that ends in a literal has to terminate at the newline that follows
// it), must not be an operator or QML member-list punctuation character, and must
// not be usable inside an identifier so the masked text cannot invent a property,
// type or brace. Comments keep their blank masking: only the "is anything else in
// this member list?" decisions rely on a literal being visible as real content.
constexpr QChar kLiteralMark = QChar::fromLatin1('@');

QString codeMask(const QString &text)
{
    QString result = text;
    QChar quote;
    bool escaped = false, line = false, block = false;
    for (int i = 0; i < text.size(); ++i) {
        const auto c = text[i], next = i + 1 < text.size() ? text[i + 1] : QChar();
        if (line) {
            if (c == QLatin1Char('\n')) line = false;
            else result[i] = QLatin1Char(' ');
        } else if (block) {
            if (c != QLatin1Char('\n')) result[i] = QLatin1Char(' ');
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) { result[++i] = QLatin1Char(' '); block = false; }
        } else if (!quote.isNull()) {
            // Mask newlines inside literals too, so they cannot terminate a binding.
            // Literals are hidden behind kLiteralMark instead of blanks: the
            // character that ends an expression decides whether a binding
            // continues on the next line, and a fully blanked `objectName: "x"`
            // (as BlueStacks writes it, one property per line) leaves the ':'
            // of the property name as the last visible character, which reads as
            // an unfinished ternary/label and swallows the following property.
            result[i] = kLiteralMark;
            if (escaped) escaped = false;
            else if (c == QLatin1Char('\\')) escaped = true;
            else if (c == quote) quote = QChar();
        } else if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            result[i] = result[i + 1] = QLatin1Char(' '); ++i; line = true;
        } else if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            result[i] = result[i + 1] = QLatin1Char(' '); ++i; block = true;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QLatin1Char('`')) {
            result[i] = kLiteralMark; quote = c;
        }
    }
    return result;
}
int expressionEnd(const QString &code, int start, int limit)
{
    int depth = 0, ternaries = 0;
    QChar previous;
    for (int i = start; i < limit; ++i) {
        const auto c = code[i];
        if (c == QLatin1Char('(') || c == QLatin1Char('[') || c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char(')') || c == QLatin1Char(']') || c == QLatin1Char('}')) {
            if (!depth) return i;
            --depth;
        } else if (!depth && c == QLatin1Char('?')) ++ternaries;
        else if (!depth && c == QLatin1Char(':') && ternaries) --ternaries;
        else if (!depth && c == QLatin1Char(';')) return i;
        else if (!depth && (c == QLatin1Char('\n') || c == QLatin1Char('\r'))) {
            int next = i + 1; while (next < limit && code[next].isSpace()) ++next;
            if (!ternaries && !QStringLiteral("?:,+-*/&|=.").contains(previous)
                && (next == limit || !QStringLiteral("?:.&|").contains(code[next]))) return i;
        }
        if (!c.isSpace()) previous = c;
    }
    return limit;
}
QString literal(QString value)
{
    value = value.trimmed();
    if (value.size() >= 2 && (value[0] == QLatin1Char('"') || value[0] == QLatin1Char('\'')) && value.back() == value[0])
        return value.mid(1, value.size() - 2);
    return {};
}
QString expression(const Document &doc, const Binding &binding)
{
    return doc.qml.mid(binding.value, binding.end - binding.value).trimmed();
}
int member(const Document &doc, int owner, const QString &name)
{
    for (int i = 0; i < doc.bindings.size(); ++i)
        if (doc.bindings[i].owner == owner && doc.bindings[i].name == name) return i;
    return -1;
}
bool parse(Document &doc)
{
    doc.mask = codeMask(doc.qml);
    // The document must be a real QtQuick file. BlueStacks (and every Windows
    // build of its UI) writes CRLF line endings, so the import line ends with
    // "\r\n": the terminator has to be accepted before the carriage return as
    // well, otherwise no document qualifies and the whole feature silently
    // discovers zero targets.
    static const QRegularExpression quickImport(QStringLiteral(
            "(?:^|\\n)\\s*import\\s+QtQuick(?:\\s+(\\d+)\\.(\\d+))?"
            "(?:\\s+as\\s+(\\w+))?[ \\t\\r]*(?:;|\\r?$)"), QRegularExpression::MultilineOption);
    const auto quick = quickImport.match(doc.mask);
    if (!quick.hasMatch()) return false;
    // Older BlueStacks builds are Qt 5 based and import e.g. `QtQuick 2.9`, and
    // rejecting anything below 2.12 used to make those documents invisible, which
    // is indistinguishable from "this version has no gradients". Only QtQuick 1.x
    // really cannot be used: its Rectangle gradient has no GradientStop children.
    doc.quickMajor = quick.captured(1).isEmpty() ? 0 : quick.captured(1).toInt();
    doc.quickMinor = quick.captured(2).isEmpty() ? 0 : quick.captured(2).toInt();
    if (doc.quickMajor != 0 && doc.quickMajor < 2) return false;
    if (!quick.captured(3).isEmpty()) doc.quickPrefix = quick.captured(3) + QLatin1Char('.');
    QList<int> braces;
    QMap<int, int> ends;
    QList<int> depths(doc.mask.size() + 1, 0);
    for (int i = 0; i < doc.mask.size(); ++i) {
        depths[i] = braces.size();
        if (doc.mask[i] == QLatin1Char('{')) braces << i;
        else if (doc.mask[i] == QLatin1Char('}')) {
            if (braces.isEmpty()) return false;
            ends[braces.takeLast()] = i + 1;
        }
    }
    if (!braces.isEmpty()) return false;
    static const QRegularExpression types(QStringLiteral("\\b((?:[A-Za-z_][A-Za-z0-9_]*\\.)*[A-Z][A-Za-z0-9_]*)\\s*\\{"));
    auto matches = types.globalMatch(doc.mask);
    QList<int> parents;
    while (matches.hasNext()) {
        const auto m = matches.next(); const int open = m.capturedEnd() - 1;
        if (!ends.contains(open)) return false;
        while (!parents.isEmpty() && doc.objects[parents.last()].end <= m.capturedStart()) parents.removeLast();
        Object object;
        object.type = m.captured(1).section(QLatin1Char('.'), -1);
        object.begin = m.capturedStart(); object.body = open + 1; object.end = ends[open];
        object.parent = parents.isEmpty() ? -1 : parents.last();
        parents << doc.objects.size(); doc.objects << object;
    }
    if (doc.objects.isEmpty()) return false;
    static const QRegularExpression properties(QStringLiteral(
        "\\b(?:(?:readonly\\s+)?property\\s+([A-Za-z_][A-Za-z0-9_.]*)\\s+([A-Za-z_][A-Za-z0-9_]*)|([A-Za-z_][A-Za-z0-9_.]*))\\s*:"));
    QMap<int, int> previousEnd;
    auto bindings = properties.globalMatch(doc.mask);
    while (bindings.hasNext()) {
        const auto m = bindings.next(); int owner = -1;
        for (int i = 0; i < doc.objects.size(); ++i) {
            const auto &o = doc.objects[i];
            if (o.body <= m.capturedStart() && o.end > m.capturedEnd()
                && depths[m.capturedStart()] == depths[o.body]) owner = i;
        }
        if (owner < 0 || m.capturedStart() < previousEnd.value(owner, -1)) continue;
        int start = m.capturedEnd(); while (start < doc.objects[owner].end && doc.qml[start].isSpace()) ++start;
        int end = expressionEnd(doc.mask, start, doc.objects[owner].end - 1);
        // A QML object-valued binding ends at its matching brace, even when
        // another declaration follows on the same line without a semicolon.
        // Do not let replacing a Gradient swallow its following properties.
        for (const auto &object : doc.objects) {
            if (object.parent == owner && object.begin >= start
                && doc.mask.mid(start, object.begin - start).trimmed().isEmpty()) {
                end = object.end;
                break;
            }
        }
        if (end <= start) continue;
        Binding b { m.captured(2).isEmpty() ? m.captured(3) : m.captured(2), m.captured(1),
                    int(m.capturedStart()), start, end, owner };
        previousEnd[owner] = end;
        doc.bindings << b;
    }
    for (int i = 0; i < doc.objects.size(); ++i) {
        auto &o = doc.objects[i];
        const int id = member(doc, i, QStringLiteral("id"));
        const int objectName = member(doc, i, QStringLiteral("objectName"));
        if (id >= 0) o.id = expression(doc, doc.bindings[id]);
        if (objectName >= 0) o.objectName = literal(expression(doc, doc.bindings[objectName]));
        if (o.parent < 0) { o.path = QStringLiteral("root"); continue; }
        QString name;
        for (const auto &b : doc.bindings) {
            if (b.owner == o.parent && b.value <= o.begin && b.end >= o.end
                && doc.mask.mid(b.value, o.begin - b.value).trimmed().isEmpty()) { name = b.name; break; }
        }
        if (name.isEmpty()) name = o.id;
        if (name.isEmpty()) {
            int ordinal = 0;
            for (int j = 0; j < i; ++j)
                if (doc.objects[j].parent == o.parent && doc.objects[j].type == o.type) ++ordinal;
            name = o.type + QStringLiteral("[%1]").arg(ordinal);
        }
        o.path = doc.objects[o.parent].path + QLatin1Char('.') + name;
    }
    return true;
}
QString normalized(const QString &color)
{
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$"));
    return hex.match(color.trimmed()).hasMatch() ? color.trimmed().toUpper() : QString();
}
QString stopColor(const QString &text, const QMap<QString, QString> &palette)
{
    const QString value = literal(text);
    QString color = normalized(value);
    if (!color.isEmpty()) return color;
    if (!value.isEmpty()) {
        const QColor named(value);
        if (named.isValid()) return named.name(named.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb).toUpper();
    }
    static const QRegularExpression ref(QStringLiteral("^(?:UiTheme\\.)?colors\\.([A-Za-z_][A-Za-z0-9_]*)$"));
    const auto m = ref.match(text);
    QString resolved = m.hasMatch() ? palette.value(m.captured(1)) : QString();
    for (int depth = 0; depth < 8 && normalized(resolved).isEmpty() && palette.contains(resolved); ++depth)
        resolved = palette.value(resolved);
    return normalized(resolved);
}
Spec currentSpec(const Document &doc, int binding, const QMap<QString, QString> &palette, int depth = 0)
{
    Spec spec; spec.enabled = false;
    if (binding < 0 || depth > 8) return spec;
    const auto &b = doc.bindings[binding]; const QString value = expression(doc, b);
    spec.enabled = value != QLatin1String("null");
    if (!spec.enabled) return spec;
    for (int i = 0; i < doc.objects.size(); ++i) {
        const auto &gradient = doc.objects[i];
        if (gradient.type != QLatin1String("Gradient") || gradient.begin < b.value || gradient.end > b.end) continue;
        const int orientation = member(doc, i, QStringLiteral("orientation"));
        spec.horizontal = orientation >= 0 && expression(doc, doc.bindings[orientation]).endsWith(QLatin1String(".Horizontal"));
        QList<QPair<double, QString>> stops;
        for (int j = 0; j < doc.objects.size(); ++j) {
            if (doc.objects[j].parent != i || doc.objects[j].type != QLatin1String("GradientStop")) continue;
            const int position = member(doc, j, QStringLiteral("position")), color = member(doc, j, QStringLiteral("color"));
            if (position < 0 || color < 0) continue;
            bool ok = false; const double p = expression(doc, doc.bindings[position]).toDouble(&ok);
            const auto c = stopColor(expression(doc, doc.bindings[color]), palette);
            if (ok && !c.isEmpty()) stops << qMakePair(p, c);
        }
        std::sort(stops.begin(), stops.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        if (stops.size() >= 2) { spec.first = stops.first().second; spec.second = stops.last().second; }
        return spec;
    }
    // Native button-border properties frequently refer to gradients.gradientButton.
    for (int i = 0; i < doc.bindings.size(); ++i) {
        const auto &other = doc.bindings[i];
        if (other.type.section(QLatin1Char('.'), -1) != QLatin1String("Gradient")) continue;
        const QString path = doc.objects[other.owner].path + QLatin1Char('.') + other.name;
        if (path.mid(5) == value) return currentSpec(doc, i, palette, depth + 1); // remove root.
    }
    return spec;
}
QList<QtResources::File> resourceFiles(const QByteArray &image, const ThemeInfo &theme)
{
    auto files = QtResources::registeredFiles(image);
    // A canonical linked palette can still expose its native gradient slots
    // in builds whose registration function was optimized beyond recognition.
    if (theme.hasQrcNode) {
        for (const auto &file : QtResources::filesInPool(image, theme.resource)) {
            bool present = false;
            for (const auto &old : files) if (old.entryOff == file.entryOff && old.name == file.name) present = true;
            if (!present) files << file;
        }
    }
    return files;
}
Catalog catalog(const QByteArray &image)
{
    Catalog out;
    QMap<QString, QString> palette;
    const auto theme = ThemeEngine::findThemeZlib(image);
    for (const auto &entry : ThemeEngine::parseQmlEntries(theme.qml))
        if (!palette.contains(entry.name)) palette[entry.name] = entry.value;
    QSet<qint64> seen;
    for (const auto &file : resourceFiles(image, theme)) {
        if (!file.name.endsWith(QLatin1String(".qml"), Qt::CaseInsensitive) || seen.contains(file.entryOff)) continue;
        const auto bytes = QtResources::readFile(image, file);
        if (bytes.isEmpty()) continue;
        Document doc; doc.name = file.name; doc.entry = file.entryOff;
        doc.bom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
        const auto source = doc.bom ? bytes.mid(3) : bytes;
        doc.qml = QString::fromUtf8(source);
        if (doc.qml.toUtf8() != source) continue; // do not corrupt a different source encoding
        if (!doc.qml.contains(QLatin1String("Gradient")) && !doc.qml.contains(QLatin1String("sidebar"), Qt::CaseInsensitive)
            && !doc.qml.contains(QLatin1String("topbar"), Qt::CaseInsensitive) && file.name != QLatin1String("UiButton.qml")) continue;
        ++out.candidates;
        if (!parse(doc)) continue;
        doc.link = QtResources::findLink(image, doc.entry, doc.name);
        if (!doc.link.found || doc.link.ambiguous) continue;
        seen.insert(file.entryOff);
        const int index = out.documents.size();
        const QString prefix = QString::number(doc.link.nodeOff, 16) + QLatin1Char(':') + doc.name + QLatin1Char(':');
        QSet<int> handled;
        const auto add = [&](const QString &key, const QString &label, const QString &kind, const QList<Site> &sites) {
            if (sites.isEmpty()) return;
            Target target; target.key = prefix + key; target.label = label; target.resource = doc.name; target.kind = kind;
            target.supportsOrientation = supportsOrientation(doc);
            target.current = currentSpec(doc, sites.first().binding, palette);
            out.choices << Choice { target, index, sites };
            for (const auto &site : sites) if (site.binding >= 0) handled.insert(site.binding);
        };
        const auto &root = doc.objects.first();
        if (root.type == QLatin1String("Rectangle")) {
            const bool sidebar = root.objectName.compare(QLatin1String("sidebar"), Qt::CaseInsensitive) == 0
                                 || root.id == QLatin1String("iSidebar");
            const bool topbar = root.objectName.compare(QLatin1String("topbar"), Qt::CaseInsensitive) == 0;
            if (sidebar || topbar)
                add(QStringLiteral("surface.root"), sidebar ? QStringLiteral("Sidebar background") : QStringLiteral("Top bar background"),
                    QStringLiteral("background"), { Site { 0, member(doc, 0, QStringLiteral("gradient")) } });
        }
        if (doc.name == QLatin1String("UiButton.qml")) {
            QList<Site> sites;
            for (int i = 0; i < doc.objects.size(); ++i) {
                if (doc.objects[i].type != QLatin1String("Rectangle")) continue;
                const int color = member(doc, i, QStringLiteral("color"));
                if (color >= 0 && expression(doc, doc.bindings[color]).endsWith(QLatin1String(".pColorState.background")))
                    sites << Site { i, member(doc, i, QStringLiteral("gradient")) };
            }
            add(QStringLiteral("button.background"), QStringLiteral("Button backgrounds (all states)"), QStringLiteral("background"), sites);
        }
        for (int i = 0; i < doc.bindings.size(); ++i) {
            const auto &b = doc.bindings[i];
            if (handled.contains(i)) continue;
            const bool typed = b.type.section(QLatin1Char('.'), -1) == QLatin1String("Gradient");
            if (!typed && b.name != QLatin1String("gradient")) continue;
            // Existing gradient bindings already establish a compatible API.
            // Untyped custom arrays/expressions are not treated as Qt gradients.
            const auto value = expression(doc, b);
            if (!typed && value != QLatin1String("null") && !value.startsWith(doc.quickPrefix + QStringLiteral("Gradient"))) continue;
            const auto path = doc.objects[b.owner].path + QLatin1Char('.') + b.name;
            const QString label = doc.name == QLatin1String("UiThemeObject.qml")
                ? QStringLiteral("Theme · ") + path.mid(5) : doc.name + QStringLiteral(" · ") + path.mid(5);
            add(QStringLiteral("property.") + path, label, typed ? QStringLiteral("gradient property") : QStringLiteral("existing gradient"),
                { Site { b.owner, i } });
        }
        out.documents << doc;
        ++out.parsed;
    }
    return out;
}
QString gradientExpression(const Spec &spec, const QString &prefix, bool withOrientation)
{
    if (!spec.enabled) return QStringLiteral("null");
    const QString body = QStringLiteral("%1GradientStop{position:0;color:\"%2\"}%1GradientStop{position:1;color:\"%3\"}}")
        .arg(prefix, normalized(spec.first), normalized(spec.second));
    if (!withOrientation)
        return QStringLiteral("%1Gradient{").arg(prefix) + body;
    return QStringLiteral("%1Gradient{orientation:%1Gradient.%2;").arg(prefix,
            spec.horizontal ? QStringLiteral("Horizontal") : QStringLiteral("Vertical")) + body;
}
} // namespace

Analysis analyze(const QByteArray &image)
{
    const auto parsed = catalog(image);
    Analysis result;
    for (const auto &choice : parsed.choices) result.targets << choice.target;
    result.candidates = parsed.candidates;
    result.parsed = parsed.parsed;
    // Theme-level and surface targets are what a theme change actually wants; the
    // per-window Gradient properties a full UI contains stay available, but after
    // them, so a long list still starts with the useful entries.
    const auto rank = [](const Target &target) {
        if (target.kind == QLatin1String("background")) return 0;
        if (target.label.startsWith(QLatin1String("Theme · "))) return 1;
        return 2;
    };
    std::stable_sort(result.targets.begin(), result.targets.end(), [rank](const Target &a, const Target &b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        if ((a.kind == QLatin1String("background")) != (b.kind == QLatin1String("background")))
            return a.kind == QLatin1String("background");
        return a.label < b.label;
    });
    result.ok = !result.targets.isEmpty();
    if (!result.ok && parsed.candidates > 0 && parsed.parsed == 0)
        result.message = QStringLiteral("This EXE has %1 QML file(s) with gradient candidates, but none could be parsed, so nothing was edited.").arg(parsed.candidates);
    else if (result.ok)
        result.message = QStringLiteral("%1 compatible gradient target(s) found.").arg(result.targets.size());
    else
        result.message = QStringLiteral("No verified compatible gradient targets were found in this EXE. Solid color fields are not gradient objects.");
    return result;
}

bool apply(QByteArray &image, const QString &key, const Spec &spec, QString *error)
{
    const auto refuse = [&](const QString &message) { if (error) *error = message; return false; };
    if (spec.enabled && (normalized(spec.first).isEmpty() || normalized(spec.second).isEmpty()))
        return refuse(QStringLiteral("Gradient stops must be #RRGGBB or #AARRGGBB colors."));
    const auto parsed = catalog(image);
    const Choice *selected = nullptr;
    for (const auto &choice : parsed.choices) if (choice.target.key == key) {
        if (selected) return refuse(QStringLiteral("The selected gradient target is ambiguous."));
        selected = &choice;
    }
    if (!selected) return refuse(QStringLiteral("The gradient target changed or is unsupported. Reload the gradient list."));
    const auto &doc = parsed.documents[selected->document];
    struct Edit { int begin, length; QString replacement; };
    QList<Edit> edits;
    const bool withOrientation = supportsOrientation(doc);
    const auto value = gradientExpression(spec, doc.quickPrefix, withOrientation);
    // A member whose value is an object literal must not be followed by ';' -
    // Qt's own parser stops on that with "Unexpected token ';'" and the whole
    // QML file then fails to load, so the generated text has to stay unframed.
    const bool objectLiteral = spec.enabled
        && value.startsWith(doc.quickPrefix + QLatin1String("Gradient"));
    for (const auto &site : selected->sites) {
        if (site.binding >= 0) {
            const auto &binding = doc.bindings[site.binding];
            if (expression(doc, binding) != value) {
                auto length = binding.end - binding.value;
                if (objectLiteral) {
                    // A ';' that terminated the previous value would end up behind
                    // the new object literal, and Qt's parser rejects it there.
                    // Swallow it together with the blanks in front of it (never
                    // across a line end): themes write `gradient: null;`, and an
                    // older build of this app wrote the invalid terminator itself.
                    int terminator = binding.end;
                    while (terminator < doc.qml.size()) {
                        const auto c = doc.qml.at(terminator);
                        if (c != QLatin1Char(' ') && c != QLatin1Char('\t') && c != QLatin1Char('\r'))
                            break;
                        ++terminator;
                    }
                    if (terminator < doc.qml.size() && doc.qml.at(terminator) == QLatin1Char(';'))
                        length = terminator + 1 - binding.value;
                }
                // Whatever stays behind the new value must not be a terminator.
                const int behind = binding.value + length;
                if (objectLiteral && behind < doc.qml.size() && doc.qml.at(behind) == QLatin1Char(';'))
                    return refuse(QStringLiteral("The gradient binding is followed by a statement terminator that Qt's QML parser rejects after an object literal."));
                edits << Edit { binding.value, length, value };
            }
        } else if (spec.enabled) {
            edits << Edit { doc.objects[site.object].body, 0,
                            QStringLiteral("\ngradient:") + value + QStringLiteral("\n") };
        }
    }
    if (edits.isEmpty()) return true;
    std::sort(edits.begin(), edits.end(), [](const auto &a, const auto &b) { return a.begin > b.begin; });
    QString qml = doc.qml;
    int boundary = qml.size();
    for (const auto &edit : edits) {
        if (edit.begin < 0 || edit.begin + edit.length > boundary) return refuse(QStringLiteral("Gradient edit ranges overlap."));
        qml.replace(edit.begin, edit.length, edit.replacement); boundary = edit.begin;
    }
    QByteArray changed = image;
    const QByteArray source = (doc.bom ? QByteArray::fromHex("efbbbf") : QByteArray()) + qml.toUtf8();
    if (!QtResources::replaceResource(changed, doc.entry, doc.link, source, error)) return false;
    const auto check = catalog(changed);
    bool verified = false;
    const auto paletteInfo = ThemeEngine::findThemeZlib(changed);
    QMap<QString, QString> palette;
    for (const auto &entry : ThemeEngine::parseQmlEntries(paletteInfo.qml))
        if (!palette.contains(entry.name)) palette[entry.name] = entry.value;
    for (const auto &choice : check.choices) if (choice.target.key == key) {
        verified = choice.sites.size() == selected->sites.size();
        for (const auto &site : choice.sites) {
            const auto current = currentSpec(check.documents[choice.document], site.binding, palette);
            verified &= current.enabled == spec.enabled
                && (!spec.enabled || (current.horizontal == (spec.horizontal && withOrientation)
                    && normalized(current.first) == normalized(spec.first)
                    && normalized(current.second) == normalized(spec.second)));
        }
    }
    if (!verified) return refuse(QStringLiteral("The generated gradient did not pass resource/read-back verification; no file was written."));
    image = changed;
    return true;
}

Result patch(const QString &exePath, const QString &key, const Spec &spec)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return { false, QStringLiteral("Close all BlueStacks instances before applying gradients.") };
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly)) return { false, QStringLiteral("Cannot read the selected EXE.") };
    const auto original = file.readAll(); file.close();
    auto changed = original; QString error;
    if (!apply(changed, key, spec, &error)) return { false, error };
    if (changed == original) return { true, QStringLiteral("This target already has the selected gradient/solid mode.") };
    if (!ThemeEngine::backup(exePath)) return { false, QStringLiteral("Could not preserve the original backup; no EXE was written.") };
    if (!PatchFile::writeVerified(exePath, original, changed, &error)) return { false, error };
    return { true, spec.enabled ? QStringLiteral("Gradient written and verified; resource caches refreshed. Restart BlueStacks.")
                                : QStringLiteral("Gradient disabled. The target's existing solid-color binding is preserved. Restart BlueStacks.") };
}
} // namespace GradientPatcher
