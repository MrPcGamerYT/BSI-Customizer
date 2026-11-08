#include "SettingsPanelPatcher.h"

#include "BsDetector.h"
#include "CustomizationWorkspace.h"
#include "EmbeddedResources.h"
#include "PatchFile.h"
#include "QmlText.h"
#include "QtResources.h"
#include "ThemeEngine.h"

#include <QCryptographicHash>
#include <QBuffer>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QImageReader>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include <QUrl>

namespace SettingsPanelPatcher {
namespace {
const QString kWindowMarker = QStringLiteral("BSI CUSTOMIZER: redesigned Settings window v1");
const QString kPreviousWindowDesignMarker = QStringLiteral("BSI CUSTOMIZER: Settings v2");
const QString kWindowDesignMarkerV3 = QStringLiteral("BSI Settings v3");
const QString kWindowDesignMarkerV4 = QStringLiteral("BSI Settings v4");
const QString kWindowDesignMarkerV5 = QStringLiteral("BSI Settings v5");
const QString kWindowDesignMarker = QStringLiteral("BSI Settings v6");
const QString kAboutMarker = QStringLiteral("BSI CUSTOMIZER: redesigned About page v1");
const QString kSchema = QStringLiteral("bsi.settings-panel.recovery");
const QString kWindowLeaf = QStringLiteral("UiSettingsWindow.qml");
const QString kAboutLeaf = QStringLiteral("PlrAboutSetting.qml");
const QStringList kWindowUnitIds {
    QStringLiteral("iSettingsWindowShadowContainer"), QStringLiteral("fChangeSelectedSetting")
};
const QStringList kAboutUnitIds {
    QStringLiteral("aboutSettings"), QStringLiteral("Important links"),
    QStringLiteral("About and other info"), QStringLiteral("iSupportEmailText")
};
const QStringList kAboutPatcherMarkers {
    QStringLiteral("BSI CUSTOMIZER: about hidden"),
    QStringLiteral("BSI CUSTOMIZER: about shown:"),
    QStringLiteral("BSI CUSTOMIZER: about text:"),
    QStringLiteral("BSI CUSTOMIZER: about handler:"),
    QStringLiteral("BSI CUSTOMIZER: about-config ")
};
constexpr qint64 kMaxImage = 256LL * 1024 * 1024;
constexpr qint64 kMaxQml = 8LL * 1024 * 1024;

struct Source
{
    QString path;
    QByteArray bytes;
    QString text;
};

struct TargetSources
{
    Source window;
    Source about;
    int pageCount = 0;
    int qmlClosureCount = 0;
    QString aboutLogoSource;
    QString aboutLogoStatus;
    QVariantList tabs;
    QString error;
    bool ok = false;
};

bool hasAboutPatcherEdits(const QString &qml)
{
    for (const QString &marker : kAboutPatcherMarkers)
        if (qml.contains(marker)) return true;
    return false;
}

QByteArray sha256(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

bool validateAssetBytes(const QByteArray &bytes, const QString &kind, QString *mime, QString *error)
{
    const bool icon = kind == QLatin1String("tab-icon");
    const qint64 limit = icon ? 48 * 1024 : 512 * 1024;
    if (bytes.isEmpty() || bytes.size() > limit) {
        *error = QStringLiteral("Choose a non-empty %1 no larger than %2 KB.")
            .arg(icon ? QStringLiteral("SVG tab icon") : QStringLiteral("logo image"))
            .arg(limit / 1024);
        return false;
    }
    const QString text = QString::fromUtf8(bytes);
    const bool isSvg = text.trimmed().startsWith(QLatin1String("<?xml"), Qt::CaseInsensitive)
        ? text.contains(QRegularExpression(QStringLiteral("<\\s*svg\\b"), QRegularExpression::CaseInsensitiveOption))
        : text.trimmed().startsWith(QLatin1String("<svg"), Qt::CaseInsensitive);
    if (isSvg) {
        static const QRegularExpression unsafeSvg(QStringLiteral(
            "<!\\s*(?:DOCTYPE|ENTITY)|<\\s*/?\\s*(?:script|foreignObject|iframe|object|embed)\\b|"
            "\\bon[a-z]+\\s*=|(?:xlink:)?href\\s*=\\s*[\\\"']\\s*(?!#)[^\\\"']*|"
            "url\\s*\\(\\s*[\\\"']?\\s*(?!#)[^)]+|@import|javascript\\s*:"),
            QRegularExpression::CaseInsensitiveOption);
        if (!text.contains(QRegularExpression(QStringLiteral("</\\s*svg\\s*>"), QRegularExpression::CaseInsensitiveOption))
            || unsafeSvg.match(text).hasMatch()) {
            *error = QStringLiteral("That SVG is incomplete or contains scripts, event handlers, or external references. Choose a self-contained SVG.");
            return false;
        }
        if (icon) *mime = QStringLiteral("image/svg+xml");
        else *mime = QStringLiteral("image/svg+xml");
        return true;
    }
    if (icon) {
        *error = QStringLiteral("Tab icons must be SVG files.");
        return false;
    }
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("The selected logo image could not be opened.");
        return false;
    }
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QSize size = reader.size();
    const QByteArray format = reader.format().toLower();
    if (!reader.canRead() || size.width() <= 0 || size.height() <= 0
        || size.width() > 2048 || size.height() > 2048
        || (format != QByteArrayLiteral("png") && format != QByteArrayLiteral("jpeg")
            && format != QByteArrayLiteral("webp"))) {
        *error = QStringLiteral("Choose a valid PNG, JPEG, WebP, or safe SVG logo up to 2048 by 2048 pixels.");
        return false;
    }
    *mime = QStringLiteral("image/%1").arg(QString::fromLatin1(format));
    return true;
}

bool storedAssetSource(const QVariantMap &asset, const QString &kind, QString *source, QString *error)
{
    const QString encoded = asset.value(QStringLiteral("data")).toString();
    const QByteArray decoded = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors).decoded;
    QString mime;
    if (encoded.isEmpty() || decoded.isEmpty() || !validateAssetBytes(decoded, kind, &mime, error)) return false;
    if (asset.value(QStringLiteral("mime")).toString() != mime) {
        *error = QStringLiteral("A saved Settings-panel image has an invalid MIME type; select it again.");
        return false;
    }
    *source = QStringLiteral("data:") + mime + QStringLiteral(";base64,") + QString::fromLatin1(decoded.toBase64());
    return true;
}

QString fallbackLogoDataUri()
{
    static const QByteArray svg = QByteArrayLiteral(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 96 96\">"
        "<rect x=\"5\" y=\"5\" width=\"86\" height=\"86\" rx=\"24\" fill=\"#15283b\" stroke=\"#55b8ff\" stroke-width=\"3\"/>"
        "<path d=\"M28 32h40v9H38v7h24v9H38v7h30v9H28z\" fill=\"#d8f1ff\"/>"
        "<circle cx=\"70\" cy=\"26\" r=\"5\" fill=\"#63e6be\"/></svg>");
    return QStringLiteral("data:image/svg+xml;base64,") + QString::fromLatin1(svg.toBase64());
}

QString sidecarPath(const QString &exePath)
{
    const QString canonical = QFileInfo(exePath).canonicalFilePath();
    if (canonical.isEmpty()) return {};
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/settings-panel-recovery");
    const QString key = QString::fromLatin1(sha256(canonical.toUtf8()));
    return QDir(root).filePath(key + QStringLiteral(".json"));
}

bool readBounded(const QString &path, QByteArray *bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kMaxImage)
        return false;
    *bytes = file.read(kMaxImage + 1);
    return !bytes->isEmpty() && bytes->size() <= kMaxImage
        && file.error() == QFileDevice::NoError;
}

bool saveAtomic(const QString &path, const QByteArray &bytes)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()) || info.isSymLink()) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size()
        && file.commit();
}

QVariantMap fail(const QString &message)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("msg"), message}};
}

bool sourceFor(const QByteArray &image, const QString &exePath, const QString &leaf,
               Source *source, QString *error)
{
    QList<EmbeddedResources::Entry> matches;
    for (const auto &entry : EmbeddedResources::list(image, true)) {
        if (entry.format.compare(QLatin1String("qml"), Qt::CaseInsensitive) == 0
            && entry.leaf.compare(leaf, Qt::CaseInsensitive) == 0)
            matches << entry;
    }
    if (matches.size() != 1) {
        *error = matches.isEmpty()
            ? QStringLiteral("The executable does not contain the editable %1 source resource.").arg(leaf)
            : QStringLiteral("The executable contains %1 possible %2 sources; the mapping is ambiguous.")
                  .arg(matches.size()).arg(leaf);
        return false;
    }
    const auto &entry = matches.first();
    if (entry.scanned) {
        *error = QStringLiteral("The Resources scan can see %1, but it has no verified Qt resource-tree node; the Settings rebuild cannot safely relink it.").arg(leaf);
        return false;
    }
    if (!entry.text || entry.size <= 0 || entry.size > kMaxQml) {
        *error = QStringLiteral("The %1 resource is not bounded editable UTF-8 QML.").arg(leaf);
        return false;
    }
    const QByteArray raw = EmbeddedResources::readEntry(exePath, entry.entryOff, entry.flags,
                                                         kMaxQml);
    const QString text = EmbeddedResources::decodeText(raw);
    if (raw.isEmpty() || text.isEmpty()) {
        *error = QStringLiteral("The %1 resource could not be read as valid UTF-8 QML.").arg(leaf);
        return false;
    }
    source->path = entry.path;
    source->bytes = raw;
    source->text = text;
    return true;
}

bool literalQmlString(const QString &source, int begin, int end, QString *value)
{
    while (begin < end && source.at(begin).isSpace()) ++begin;
    if (begin >= end || (source.at(begin) != QLatin1Char('\"')
                         && source.at(begin) != QLatin1Char('\'')))
        return false;
    const QChar quote = source.at(begin++);
    QString decoded;
    bool closed = false;
    while (begin < end) {
        const QChar ch = source.at(begin++);
        if (ch == quote) { closed = true; break; }
        if (ch == QLatin1Char('\\')) {
            if (begin >= end) return false;
            const QChar escaped = source.at(begin++);
            if (escaped == QLatin1Char('\\') || escaped == quote || escaped == QLatin1Char('/'))
                decoded += escaped;
            else if (escaped == QLatin1Char('n')) decoded += QLatin1Char('\n');
            else if (escaped == QLatin1Char('r')) decoded += QLatin1Char('\r');
            else if (escaped == QLatin1Char('t')) decoded += QLatin1Char('\t');
            else return false;
        } else {
            if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) return false;
            decoded += ch;
        }
    }
    if (!closed || !decoded.endsWith(QLatin1String(".qml"), Qt::CaseInsensitive)) return false;
    *value = decoded;
    return true;
}

bool readSettingsModelPages(const QString &playerText, const QHash<QString, QList<QtResources::File>> &qmlByLeaf,
                            QSet<QString> *pageNames, QVariantList *tabs, QString *error)
{
    const QString masked = QmlText::mask(playerText);
    const QRegularExpression inlineModelDeclaration(
        QStringLiteral("\\bproperty\\s+(?:[A-Za-z_][A-Za-z0-9_]*\\s+)*SettingElementModelList\\s+pSettingsItems\\b"));
    const bool inlineSettingsModel = inlineModelDeclaration.match(masked).hasMatch();
    const QRegularExpression componentSettingsBindingPattern(
        QStringLiteral("\\bpSettingsWindow\\s*(?:=|:)\\s*[A-Za-z_][A-Za-z0-9_]*\\.createObject\\s*\\([^;]*(?:[\\\"']pSettingsItems[\\\"']|\\bpSettingsItems\\b)\\s*:\\s*([A-Za-z_][A-Za-z0-9_]*)"));
    QRegularExpressionMatch componentSettingsBinding = componentSettingsBindingPattern.match(playerText);
    if (!componentSettingsBinding.hasMatch()) {
        // Some MSI builds create UiSettingsWindow first and assign its model
        // immediately afterward instead of passing it in createObject's map.
        const QRegularExpression separateSettingsModelBinding(
            QStringLiteral("\\bpSettingsWindow\\s*(?:\\.\\s*pSettingsItems|\\[\\s*[\\\"']pSettingsItems[\\\"']\\s*\\])\\s*=\\s*([A-Za-z_][A-Za-z0-9_]*)"));
        componentSettingsBinding = separateSettingsModelBinding.match(playerText);
    }
    const bool liveComponentBinding = componentSettingsBinding.hasMatch();
    const QString settingsComponentId = liveComponentBinding
        ? componentSettingsBinding.captured(1) : QString();

    QList<QmlText::Object> settingsComponents;
    if (liveComponentBinding) {
        const QRegularExpression settingsId(
            QStringLiteral("\\bid\\s*:\\s*%1\\b").arg(QRegularExpression::escape(settingsComponentId)));
        for (const auto &component : QmlText::objects(masked, QStringLiteral("Component"))) {
            const QString body = masked.mid(component.open, component.end - component.open);
            if (settingsId.match(body).hasMatch()) settingsComponents << component;
        }
    }

    const auto allModels = QmlText::objects(masked, QStringLiteral("SettingElementModelList"));
    QList<QmlText::Object> models;
    QList<QmlText::Object> inlineSettingsModels;
    for (const auto &model : allModels) {
        bool inlineOwner = false;
        if (inlineSettingsModel) {
            QRegularExpressionMatch nearestDeclaration;
            auto declarations = inlineModelDeclaration.globalMatch(masked.left(model.nameBegin));
            while (declarations.hasNext()) nearestDeclaration = declarations.next();
            if (nearestDeclaration.hasMatch()) {
                const QString between = masked.mid(nearestDeclaration.capturedEnd(),
                                                   model.nameBegin - nearestDeclaration.capturedEnd());
                inlineOwner = QRegularExpression(QStringLiteral("^\\s*:\\s*$"))
                                  .match(between).hasMatch();
            }
        }
        bool componentOwner = false;
        for (const auto &component : settingsComponents) {
            if (model.nameBegin > component.open && model.end <= component.end) {
                componentOwner = true;
                break;
            }
        }
        if (inlineOwner) inlineSettingsModels << model;
        if (inlineOwner || componentOwner) models << model;
    }
    // Some older Player QML packages declare the native model in plr.qml but
    // inherit/compose its element list without a local pSettingElements
    // binding. If there is exactly one model and a real Settings-window
    // connection, it is still unambiguous; scope discovery to that model body.
    if (models.isEmpty() && allModels.size() == 1
        && (inlineSettingsModel || (liveComponentBinding && settingsComponents.size() == 1)))
        models << allModels.first();
    // Older MSI player QML can declare several model objects, but the one
    // directly initialized by the property named pSettingsItems is the model
    // consumed by UiSettingsWindow. If no unique matching Component exists,
    // prefer that unique inline owner rather than rejecting it alongside nested
    // models for subpages.
    if (models.size() > 1 && settingsComponents.size() != 1 && inlineSettingsModels.size() == 1)
        models = inlineSettingsModels;

    // When plr.qml constructs the window with
    // `pSettingsItems: someSettingsItems.createObject(...)`, that Component is
    // the object actually passed to UiSettingsWindow. Prefer its root model
    // (especially an explicitly named pSettingsItems model) over stale inline
    // declarations and nested page-local models elsewhere in plr.qml.
    if (models.size() > 1 && liveComponentBinding) {
        QList<QmlText::Object> namedSettingsModels;
        QList<QmlText::Object> componentRootModels;
        for (const auto &model : models) {
            for (const auto &component : settingsComponents) {
                if (model.nameBegin <= component.open || model.end > component.end) continue;
                int depth = 0;
                for (int i = component.open + 1; i < model.nameBegin; ++i) {
                    if (masked.at(i) == QLatin1Char('{')) ++depth;
                    else if (masked.at(i) == QLatin1Char('}')) --depth;
                }
                if (depth != 0) continue;
                componentRootModels << model;
                QmlText::Binding idBinding;
                if (QmlText::binding(masked, model.open, model.end - 1,
                                     QStringLiteral("id"), &idBinding)
                    && masked.mid(idBinding.valueBegin,
                                  idBinding.valueEnd - idBinding.valueBegin).trimmed()
                        == QLatin1String("pSettingsItems"))
                    namedSettingsModels << model;
            }
        }
        if (namedSettingsModels.size() == 1) models = namedSettingsModels;
        else if (componentRootModels.size() == 1) models = componentRootModels;
    }
    if (models.size() > 1) {
        QList<QmlText::Object> explicitlyNamed;
        for (const auto &candidate : models) {
            QmlText::Binding idBinding;
            if (QmlText::binding(masked, candidate.open, candidate.end - 1,
                                 QStringLiteral("id"), &idBinding)
                && masked.mid(idBinding.valueBegin,
                              idBinding.valueEnd - idBinding.valueBegin).trimmed()
                    == QLatin1String("pSettingsItems"))
                explicitlyNamed << candidate;
        }
        if (explicitlyNamed.size() == 1) models = explicitlyNamed;
    }
    // A live Component can contain several nested SettingElementModelList
    // objects (for page subviews as well as the tab strip). Prefer the unique
    // model whose page list resolves to the About page and the full native tab
    // set; unrelated page-local models must not make the actual Settings panel
    // look unsupported.
    if (models.size() > 1) {
        QList<QmlText::Object> completeModels;
        const QRegularExpression pageSource(QStringLiteral("\\bpQmlSource\\s*:"));
        for (const auto &candidate : models) {
            int begin = candidate.open;
            int end = candidate.end - 1;
            QmlText::Binding list;
            if (QmlText::binding(masked, candidate.open, candidate.end - 1,
                                 QStringLiteral("pSettingElements"), &list)) {
                begin = list.valueBegin;
                end = list.valueEnd;
            }
            int resolved = 0;
            bool hasAbout = false;
            auto sources = pageSource.globalMatch(masked.mid(begin, end - begin));
            while (sources.hasNext()) {
                const auto source = sources.next();
                const int valueBegin = begin + source.capturedEnd();
                QString path;
                if (!literalQmlString(playerText, valueBegin, end, &path)) continue;
                const QString leaf = QFileInfo(path).fileName();
                const auto found = qmlByLeaf.constFind(leaf.toCaseFolded());
                if (found == qmlByLeaf.cend() || found.value().size() != 1) continue;
                ++resolved;
                if (leaf.compare(kAboutLeaf, Qt::CaseInsensitive) == 0) hasAbout = true;
            }
            if (hasAbout && resolved >= 8) completeModels << candidate;
        }
        if (completeModels.size() == 1) models = completeModels;
    }
    if (playerText.isEmpty() || models.size() != 1) {
        if (models.isEmpty()) {
            *error = QStringLiteral("The registered plr.qml does not expose a live native Settings tab model; this player layout is unsupported.");
        } else {
            QStringList candidateIds;
            for (const auto &candidate : models) {
                QmlText::Binding idBinding;
                QString id = QStringLiteral("<anonymous>");
                if (QmlText::binding(masked, candidate.open, candidate.end - 1,
                                     QStringLiteral("id"), &idBinding))
                    id = masked.mid(idBinding.valueBegin,
                                    idBinding.valueEnd - idBinding.valueBegin).trimmed();
                candidateIds << id;
            }
            const QString binding = liveComponentBinding
                ? QStringLiteral("the pSettingsItems binding resolves to object id %1").arg(settingsComponentId)
                : QStringLiteral("no pSettingsItems Component binding was resolved from pSettingsWindow");
            *error = QStringLiteral("The registered plr.qml exposes multiple possible live Settings tab models (%1); refusing to guess. %2.")
                         .arg(candidateIds.join(QStringLiteral(", ")), binding);
        }
        return false;
    }

    const auto &model = models.first();
    int sourceBegin = model.open;
    int sourceEnd = model.end - 1;
    QmlText::Binding elements;
    if (QmlText::binding(masked, model.open, model.end - 1,
                         QStringLiteral("pSettingElements"), &elements)) {
        sourceBegin = elements.valueBegin;
        sourceEnd = elements.valueEnd;
    }
    const QRegularExpression pageSource(QStringLiteral("\\bpQmlSource\\s*:"));
    auto matches = pageSource.globalMatch(masked.mid(sourceBegin, sourceEnd - sourceBegin));
    while (matches.hasNext()) {
        const auto match = matches.next();
        const int colon = sourceBegin + match.capturedEnd() - 1;
        const int valueBegin = colon + 1;
        if (valueBegin >= sourceEnd) break;
        QString page;
        if (!literalQmlString(playerText, valueBegin, sourceEnd, &page)) {
            *error = QStringLiteral("A page in the live Settings tab model has a dynamic or malformed pQmlSource; it cannot be resolved safely.");
            return false;
        }
        const QString leaf = QFileInfo(page).fileName();
        const QString key = leaf.toCaseFolded();
        if (leaf.isEmpty() || pageNames->contains(key)) {
            *error = QStringLiteral("The live Settings tab model contains an empty or duplicate QML page path; refusing to rebuild an ambiguous panel.");
            return false;
        }
        const auto found = qmlByLeaf.constFind(leaf.toCaseFolded());
        if (found == qmlByLeaf.cend() || found.value().size() != 1) {
            *error = QStringLiteral("The Settings tab QML page %1 is missing or ambiguous in the registered player resources.").arg(leaf);
            return false;
        }
        pageNames->insert(key);
        const int matchStart = sourceBegin + match.capturedStart();
        const int rowOpen = masked.lastIndexOf(QLatin1Char('{'), matchStart);
        const int rowClose = rowOpen >= 0 ? masked.indexOf(QLatin1Char('}'), matchStart) : -1;
        QString label = QFileInfo(leaf).completeBaseName();
        label.remove(QRegularExpression(QStringLiteral("Setting$"), QRegularExpression::CaseInsensitiveOption));
        if (rowOpen >= sourceBegin && rowClose > matchStart) {
            const QString row = playerText.mid(rowOpen, rowClose - rowOpen + 1);
            static const QRegularExpression labelPattern(QStringLiteral(
                "\\bpButtonText\\s*:\\s*(?:(?:qsTranslate\\s*\\([^,]+,\\s*)|(?:qsTr\\s*\\(\\s*))?['\\\"]([^'\\\"]+)['\\\"]"));
            const auto labelMatch = labelPattern.match(row);
            if (labelMatch.hasMatch()) label = labelMatch.captured(1).trimmed();
        }
        tabs->append(QVariantMap{{QStringLiteral("id"), key},
                                 {QStringLiteral("page"), leaf},
                                 {QStringLiteral("source"), page},
                                 {QStringLiteral("label"), label},
                                 {QStringLiteral("index"), tabs->size()}});
    }
    if (pageNames->isEmpty()) {
        *error = QStringLiteral("The live Settings tab model contains no literal QML page paths.");
        return false;
    }
    return true;
}

TargetSources readSources(const QString &exePath, const QByteArray &image)
{
    TargetSources out;
    if (!sourceFor(image, exePath, kWindowLeaf, &out.window, &out.error)) return out;
    if (!sourceFor(image, exePath, kAboutLeaf, &out.about, &out.error)) return out;
    if (out.window.path == out.about.path) {
        out.error = QStringLiteral("The Settings window and About page resolve to the same resource; refusing an unsafe patch.");
        return out;
    }

    // The in-emulator Settings screen is a resource graph, not one document:
    // plr.qml declares the native tab model, UiSettingsWindow.qml loads each
    // pQmlSource dynamically, and those pages in turn use shared QML controls.
    // Validate that graph against the actual registered pool before offering a
    // rebuild, so a matching leaf from a detached/ambiguous resource is not
    // mistaken for the component the player actually loads.
    QHash<QString, QList<QtResources::File>> qmlByLeaf;
    for (const auto &entry : EmbeddedResources::list(image, true)) {
        if (entry.scanned || entry.format.compare(QLatin1String("qml"), Qt::CaseInsensitive) != 0)
            continue;
        qmlByLeaf[entry.leaf.toCaseFolded()] << QtResources::File {
            entry.leaf, entry.entryOff, entry.flags, entry.path
        };
    }
    const auto uniqueQml = [&qmlByLeaf](const QString &leaf) -> const QtResources::File * {
        const auto it = qmlByLeaf.constFind(leaf.toCaseFolded());
        return it != qmlByLeaf.cend() && it.value().size() == 1 ? &it.value().first() : nullptr;
    };
    const QtResources::File *playerRoot = uniqueQml(QStringLiteral("plr.qml"));
    if (!playerRoot) {
        out.error = QStringLiteral("The registered player root plr.qml is missing or ambiguous; Settings tabs cannot be resolved safely.");
        return out;
    }
    const QByteArray playerBytes = QtResources::readFile(image, *playerRoot, kMaxQml);
    const QString playerText = EmbeddedResources::decodeText(playerBytes);
    // Only inspect literal page sources in the live SettingElementModelList.
    // plr.qml also contains pQmlSource strings for dialogs, hidden prototypes
    // and unrelated views; scanning the whole document counted those as tabs,
    // and could reject an otherwise valid model for duplicate paths that are
    // not Settings pages.
    QSet<QString> pageNames;
    if (!readSettingsModelPages(playerText, qmlByLeaf, &pageNames, &out.tabs, &out.error))
        return out;
    bool hasAboutPage = false;
    for (const QString &path : pageNames)
        if (QFileInfo(path).fileName().compare(kAboutLeaf, Qt::CaseInsensitive) == 0)
            hasAboutPage = true;
    if (pageNames.size() < 8 || !hasAboutPage) {
        out.error = QStringLiteral("The live player Settings model exposes only %1 page(s), or no About page; the native tab graph is incomplete.").arg(pageNames.size());
        return out;
    }
    out.pageCount = pageNames.size();
    const bool retainsLoadedComponent = out.window.text.contains(QLatin1String("model.modelData.pComponentObj = item"))
        || out.window.text.contains(QLatin1String("model.modelData.pComponentObj=i"));
    if (!out.window.text.contains(QLatin1String("pSettingsItems.pSettingElements"))
        || !out.window.text.contains(QLatin1String("setSource(model.modelData.pQmlSource"))
        || !retainsLoadedComponent
        || !out.window.text.contains(QLatin1String("fChangeSelectedSetting"))) {
        out.error = QStringLiteral("UiSettingsWindow.qml no longer loads and retains the player’s native Settings pages; this layout cannot be safely replaced.");
        return out;
    }

    // Resolve the QML-backed transitive component closure for the shell and
    // all tab roots, rather than only checking UiSettingsWindow/About leaves.
    // The generated panel uses the same native controls and these untouched
    // pages rely on them remaining registered in this pool.
    QSet<QString> pending { kWindowLeaf.toCaseFolded() };
    for (const QString &page : pageNames) pending.insert(page);
    QSet<QString> visited;
    const QRegularExpression componentType(QStringLiteral("\\b([A-Z][A-Za-z0-9_]*)\\s*\\{"));
    const QRegularExpression qmlPath(QStringLiteral("['\\\"](?:qrc:/)?([^'\\\"]+\\.qml)['\\\"]"), QRegularExpression::CaseInsensitiveOption);
    while (!pending.isEmpty()) {
        const QString key = *pending.cbegin();
        pending.remove(key);
        if (visited.contains(key)) continue;
        const auto it = qmlByLeaf.constFind(key);
        if (it == qmlByLeaf.cend() || it.value().size() != 1) continue;
        visited.insert(key);
        const QString text = EmbeddedResources::decodeText(QtResources::readFile(image, it.value().first(), kMaxQml));
        if (text.isEmpty()) {
            out.error = QStringLiteral("The registered Settings dependency %1 could not be decoded as QML.").arg(it.value().first().name);
            return out;
        }
        auto types = componentType.globalMatch(text);
        while (types.hasNext()) {
            const QString component = types.next().captured(1).toCaseFolded() + QStringLiteral(".qml");
            const auto found = qmlByLeaf.constFind(component);
            if (found != qmlByLeaf.cend()) {
                if (found.value().size() != 1) {
                    out.error = QStringLiteral("The Settings dependency %1 is ambiguous in the registered player resources.").arg(component);
                    return out;
                }
                pending.insert(component);
            }
        }
        auto paths = qmlPath.globalMatch(text);
        while (paths.hasNext()) {
            const QString path = QFileInfo(paths.next().captured(1)).fileName().toCaseFolded();
            if (qmlByLeaf.contains(path)) {
                if (qmlByLeaf.value(path).size() != 1) {
                    out.error = QStringLiteral("The Settings QML path %1 is ambiguous in the registered player resources.").arg(path);
                    return out;
                }
                pending.insert(path);
            }
        }
    }
    out.qmlClosureCount = visited.size();

    const auto assets = EmbeddedResources::list(image, true);
    const auto hasAsset = [&assets](const QString &path) {
        for (const auto &entry : assets)
            if (entry.path.compare(path, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    if (hasAsset(QStringLiteral("qrcAssets/BlueStacks5Beta.svg"))) {
        out.aboutLogoSource = QStringLiteral("qrc:/qrcAssets/BlueStacks5Beta.svg");
        out.aboutLogoStatus = QStringLiteral("Using the player’s registered SVG logo.");
    } else if (hasAsset(QStringLiteral("qrcAssets/BlueStacks5Beta.png"))) {
        out.aboutLogoSource = QStringLiteral("qrc:/qrcAssets/BlueStacks5Beta.png");
        out.aboutLogoStatus = QStringLiteral("Using the player’s registered PNG logo.");
    } else {
        // The rebuilt About page does not use the old native spinner resource.
        // Some Valora/MSI versions also register their logo under another name
        // or omit it from this pool; neither condition makes the Settings graph
        // unsafe. A self-contained fallback prevents a broken-image icon.
        out.aboutLogoSource = fallbackLogoDataUri();
        out.aboutLogoStatus = QStringLiteral("The expected BlueStacks logo resource is absent; a built-in fallback image is ready. You can replace it with your own logo below.");
    }
    out.ok = true;
    return out;
}

QByteArray utf8PreservingBom(const QString &text, const QByteArray &original)
{
    QByteArray bytes = text.toUtf8();
    if (original.startsWith(QByteArray::fromHex("efbbbf")))
        bytes.prepend(QByteArray::fromHex("efbbbf"));
    return bytes;
}

QString qmlString(const QString &value)
{
    QString out = QStringLiteral("\"");
    for (const QChar ch : value) {
        const ushort u = ch.unicode();
        if (ch == QLatin1Char('\\')) out += QStringLiteral("\\\\");
        else if (ch == QLatin1Char('"')) out += QStringLiteral("\\\"");
        else if (ch == QLatin1Char('\n')) out += QStringLiteral("\\n");
        else if (ch == QLatin1Char('\r')) out += QStringLiteral("\\r");
        else if (ch == QLatin1Char('\t')) out += QStringLiteral("\\t");
        else if (u < 0x20 || u == 0x2028 || u == 0x2029)
            out += QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
        else out += ch;
    }
    out += QLatin1Char('"');
    return out;
}

QString buildWindow(const QString &stock, const QStringList &hiddenTabs,
                    const QVariantMap &tabIcons)
{
    const QString masked = QmlText::mask(stock);
    const auto objects = QmlText::objects(masked, QStringLiteral("UiShadowContainer"));
    QmlText::Object target;
    bool found = false;
    for (const auto &object : objects) {
        const QString body = masked.mid(object.open, object.end - object.open);
        if (body.contains(QLatin1String("id: iSettingsWindowShadowContainer"))) {
            target = object;
            found = true;
            break;
        }
    }
    if (!found) return {};

    QString hiddenLiteral = QStringLiteral("[");
    for (const QString &page : hiddenTabs) {
        if (hiddenLiteral.size() > 1) hiddenLiteral += QLatin1Char(',');
        hiddenLiteral += qmlString(page.toCaseFolded());
    }
    hiddenLiteral += QLatin1Char(']');
    QString iconLiteral = QStringLiteral("({");
    for (auto it = tabIcons.cbegin(); it != tabIcons.cend(); ++it) {
        if (iconLiteral.size() > 2) iconLiteral += QLatin1Char(',');
        iconLiteral += qmlString(it.key().toCaseFolded()) + QLatin1Char(':') + qmlString(it.value().toString());
    }
    iconLiteral += QStringLiteral("})");

    // Keep the native root Window, prompts, callbacks and page model; resize
    // that window to a compact centered footprint and constrain its page loader.
    QString replacement = QStringLiteral(R"QML(
UiShadowContainer{
// BSI Settings v6
id:iSettingsWindowShadowContainer;anchors.fill:parent
property var h:@@BSI_HIDDEN_TABS@@
property var c:@@BSI_TAB_ICONS@@
function k(v){return v.toLowerCase().split("/").pop()}
Component.onCompleted:{var w=iSettingsWindow.width,h=iSettingsWindow.height;iSettingsWindow.width=Math.min(w*.60,1100);iSettingsWindow.height=Math.min(h*.68,820);iSettingsWindow.x+=(w-iSettingsWindow.width)/2;iSettingsWindow.y+=(h-iSettingsWindow.height)/2}
MouseArea{width:parent.width-42;height:32;onPressed:iSettingsWindow.startSystemMove()}
UiImageButton{asset:"CloseIcon";anchors.right:parent.right;onClicked:iSettingsWindow.close()}
ListView{width:parent.width;height:72;y:32;clip:true;orientation:ListView.Horizontal;model:pSettingsItems.pSettingElements;currentIndex:pCurrentTabIndex
delegate:Item{property int p:index;property string k:iSettingsWindowShadowContainer.k(modelData.pQmlSource);width:iSettingsWindowShadowContainer.h.indexOf(k)<0?92:0;height:64;visible:width>0
UiSettingsButton{anchors.fill:parent;pSettingBtnText:modelData.pButtonText;pSelected:index===pCurrentTabIndex;onSettingClicked:fChangeSelectedSetting(parent.p)}
Image{source:iSettingsWindowShadowContainer.c[parent.k]||"";width:18;height:18;anchors.horizontalCenter:parent.horizontalCenter}}
}
StackLayout{width:Math.min(parent.width*.90,600);anchors.horizontalCenter:parent.horizontalCenter;anchors.top:parent.top;anchors.bottom:parent.bottom;anchors.topMargin:108;currentIndex:pCurrentTabIndex
Repeater{model:pSettingsItems.pSettingElements;delegate:Item{Loader{width:Math.min(parent.width,520);height:parent.height;anchors.centerIn:parent;Component.onCompleted:setSource(model.modelData.pQmlSource,model.modelData.pComponentArgs||({}));onLoaded:{var i=item;model.modelData.pComponentObj=i;i.fOnQmlLoaded&&i.fOnQmlLoaded()}}}}}
}
)QML");
    replacement.replace(QStringLiteral("@@BSI_HIDDEN_TABS@@"), hiddenLiteral);
    replacement.replace(QStringLiteral("@@BSI_TAB_ICONS@@"), iconLiteral);
    QString out = stock;
    out.replace(target.nameBegin, target.end - target.nameBegin, replacement);
    if (!out.contains(kWindowDesignMarker)) return {};
    return out;
}

QString buildAbout(const QString &developerName, const QString &tagline,
                   const QVariantList &links, const QString &logoSource)
{
    QString rows;
    for (const QVariant &entry : links) {
        const QVariantMap link = entry.toMap();
        const QString label = link.value(QStringLiteral("title")).toString().trimmed();
        const QString url = link.value(QStringLiteral("url")).toString().trimmed();
        if (label.isEmpty() || url.isEmpty()) continue;
        rows += QStringLiteral("\n        { title: %1, url: %2 },")
            .arg(qmlString(label), qmlString(url));
    }
    const QString qDeveloper = qmlString(developerName.trimmed());
    const QString qTagline = qmlString(tagline.trimmed());
    QString generated = QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    // BSI CUSTOMIZER: redesigned About page v1
    objectName: "aboutSettings"
    property bool pUpdateAvailable: false
    property string pUpdateVersion: ""
    property string pMoreInfoURL: ""
    property string pDownloadURL: ""
    property bool pIsInCompatibleUpdateVersion: false
    property string pParentWindow: ""
    property string pShortDisplayName: ""
    property string pDeveloperName: @@BSI_DEVELOPER@@
    property string pDeveloperTagline: @@BSI_TAGLINE@@
    property var pDeveloperLinks: [@@BSI_LINKS@@
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { color: "transparent" }
            contentWidth: iBsiAboutContent.width
            contentHeight: iBsiAboutContent.implicitHeight + 48
            clip: true

            ColumnLayout {
                id: iBsiAboutContent
                width: Math.min(440, parent.width - 24)
                Layout.alignment: Qt.AlignHCenter | Qt.AlignTop
                spacing: 16

                Item { Layout.fillWidth: true; Layout.preferredHeight: 28 }
                Image {
                    objectName: "bsiAboutEmulatorLogo"
                    source: @@BSI_LOGO_SOURCE@@
                    width: 160
                    height: 60
                    fillMode: Image.PreserveAspectFit
                    Layout.alignment: Qt.AlignHCenter
                }
                Text {
                    objectName: "bsiAboutDeveloperName"
                    text: pDeveloperName
                    font: UiTheme.fonts.bodyLarge
                    color: UiTheme.colors.primary10
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Text {
                    objectName: "bsiAboutDeveloperTagline"
                    visible: pDeveloperTagline.length > 0
                    text: pDeveloperTagline
                    font: UiTheme.fonts.bodySmall
                    color: UiTheme.colors.primary30
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: UiTheme.colors.primary60
                }
                Repeater {
                    model: pDeveloperLinks
                    delegate: Rectangle {
                        objectName: "bsiAboutDeveloperLink"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        property bool hovered: false
                        radius: 10
                        color: hovered
                            ? Qt.rgba(UiTheme.colors.accentNormal.r, UiTheme.colors.accentNormal.g,
                                      UiTheme.colors.accentNormal.b, 0.16)
                            : Qt.rgba(UiTheme.colors.primary60.r, UiTheme.colors.primary60.g,
                                      UiTheme.colors.primary60.b, 0.42)
                        border.width: 1
                        border.color: hovered ? UiTheme.colors.accentNormal : UiTheme.colors.primary60
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 14
                            anchors.rightMargin: 38
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: parent.hovered ? UiTheme.colors.accentNormal : UiTheme.colors.primary10
                            font: UiTheme.fonts.bodyMedium
                            elide: Text.ElideRight
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: "↗"
                            color: UiTheme.colors.accentNormal
                            font.pixelSize: 16
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: parent.hovered = true
                            onExited: parent.hovered = false
                            onClicked: {
                                var target = String(modelData.url || "")
                                if (/^https?:\/\//i.test(target)) Qt.openUrlExternally(target)
                            }
                        }
                    }
                }
                Text {
                    visible: pDeveloperLinks.length === 0
                    text: qsTranslate("QObject", "Add up to four developer links in BSI Customizer.")
                    color: UiTheme.colors.primary30
                    font: UiTheme.fonts.bodySmall
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                Item { Layout.fillWidth: true; Layout.preferredHeight: 28 }
            }
        }
    }

    // Retain the native About-page callback surface for parent QML connections.
    function fCheckForUpdateCallback(apiCallStatus, jsonData) {
        if (apiCallStatus === 0) {
            var jsonObj = JSON.parse(jsonData)
            pUpdateAvailable = String(jsonObj.update_available).toLowerCase() === "true"
            pIsInCompatibleUpdateVersion = pUpdateAvailable
                && String(jsonObj.update_details.is_build_compatible).toLowerCase() !== "true"
            if (pUpdateAvailable) {
                pUpdateVersion = jsonObj.update_details.client_version
                pMoreInfoURL = jsonObj.update_details.detailed_changelogs_url
                pDownloadURL = jsonObj.update_details.download_url
            }
        } else {
            pUpdateAvailable = false
            pIsInCompatibleUpdateVersion = false
        }
    }
    function fUpdateShortDisplayName(displayName) {
        pShortDisplayName = String(displayName || "")
    }
}
)QML");
    generated.replace(QStringLiteral("@@BSI_DEVELOPER@@"), qDeveloper);
    generated.replace(QStringLiteral("@@BSI_TAGLINE@@"), qTagline);
    generated.replace(QStringLiteral("@@BSI_LINKS@@"), rows);
    generated.replace(QStringLiteral("@@BSI_LOGO_SOURCE@@"), qmlString(logoSource));
    return generated;
}

QVariantMap readRecovery(const QString &path, const QString &exePath)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 12 * 1024 * 1024)
        return {};
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const QVariantMap map = doc.toVariant().toMap();
    if (map.value(QStringLiteral("schema")).toString() != kSchema
        || map.value(QStringLiteral("version")).toInt() != 1
        || map.value(QStringLiteral("target")).toString() != QFileInfo(exePath).canonicalFilePath())
        return {};
    return map;
}

QVariantList sourceRecords(const TargetSources &sources, const QMap<QString, QByteArray> &originals,
                           const QMap<QString, QByteArray> &patched = {})
{
    QVariantList records;
    for (const Source *source : {&sources.window, &sources.about}) {
        const QByteArray original = originals.value(source->path);
        const QByteArray replacement = patched.value(source->path);
        QVariantList acceptedHashes;
        const QString currentHash = QString::fromLatin1(sha256(source->bytes));
        const QString replacementHash = QString::fromLatin1(sha256(replacement));
        acceptedHashes << currentHash;
        if (replacementHash != currentHash) acceptedHashes << replacementHash;
        records << QVariantMap {
            {QStringLiteral("path"), source->path},
            {QStringLiteral("original"), QString::fromLatin1(original.toBase64())},
            {QStringLiteral("originalSha256"), QString::fromLatin1(sha256(original))},
            {QStringLiteral("acceptedSha256"), acceptedHashes},
        };
    }
    return records;
}

QMap<QString, QByteArray> originalsFromRecovery(const QVariantMap &recovery, QString *error)
{
    QMap<QString, QByteArray> originals;
    const QVariantList records = recovery.value(QStringLiteral("sources")).toList();
    if (records.size() != 2) {
        *error = QStringLiteral("The Settings-panel recovery snapshot is incomplete; restore is blocked.");
        return {};
    }
    for (const QVariant &value : records) {
        const QVariantMap record = value.toMap();
        const QString path = record.value(QStringLiteral("path")).toString();
        const QByteArray raw = QByteArray::fromBase64(record.value(QStringLiteral("original")).toByteArray());
        if (path.isEmpty() || raw.isEmpty() || raw.size() > kMaxQml
            || QString::fromLatin1(sha256(raw)) != record.value(QStringLiteral("originalSha256")).toString()
            || originals.contains(path)) {
            *error = QStringLiteral("The Settings-panel recovery snapshot failed its integrity check.");
            return {};
        }
        originals.insert(path, raw);
    }
    return originals;
}

bool recoveryAcceptsCurrent(const QVariantMap &record, const QByteArray &current)
{
    const QString digest = QString::fromLatin1(sha256(current));
    const QVariantList accepted = record.value(QStringLiteral("acceptedSha256")).toList();
    if (!accepted.isEmpty()) {
        for (const QVariant &hash : accepted)
            if (hash.toString() == digest) return true;
        return false;
    }
    // Read the first, not-yet-deployed schema during an in-place upgrade.
    return record.value(QStringLiteral("patchedSha256")).toString() == digest;
}

QVariantList operationsFor(const QMap<QString, QByteArray> &replacement)
{
    QVariantList operations;
    for (auto it = replacement.cbegin(); it != replacement.cend(); ++it) {
        operations << QVariantMap {
            {QStringLiteral("kind"), QStringLiteral("resource")},
            {QStringLiteral("path"), it.key()},
            {QStringLiteral("data"), QString::fromLatin1(it.value().toBase64())},
            {QStringLiteral("sha256"), QString::fromLatin1(sha256(it.value()))},
        };
    }
    return operations;
}

bool validateProfile(const QString &developerName, const QVariantList &links, QString *error)
{
    if (developerName.trimmed().isEmpty() || developerName.trimmed().size() > 80
        || developerName.contains(QChar(0)) || links.size() > 4) {
        *error = QStringLiteral("Enter a developer name up to 80 characters and no more than four About links.");
        return false;
    }
    for (const QVariant &value : links) {
        const QVariantMap link = value.toMap();
        const QString title = link.value(QStringLiteral("title")).toString().trimmed();
        const QString urlText = link.value(QStringLiteral("url")).toString().trimmed();
        if (title.isEmpty() && urlText.isEmpty()) continue;
        if (title.isEmpty() || title.size() > 80 || urlText.size() > 512
            || QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")).match(urlText).hasMatch()) {
            *error = QStringLiteral("Every About link needs a label up to 80 characters and a valid HTTP(S) URL.");
            return false;
        }
        const QUrl url(urlText, QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
            || (url.scheme().compare(QLatin1String("http"), Qt::CaseInsensitive) != 0
                && url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0)) {
            *error = QStringLiteral("About links must be valid HTTP or HTTPS URLs.");
            return false;
        }
    }
    return true;
}

QVariantMap applyResources(const QString &exePath, const QVariantList &operations)
{
    QByteArray image;
    if (!readBounded(exePath, &image))
        return fail(QStringLiteral("The selected HD-Player.exe is unreadable or exceeds the safe 256 MB limit."));
    const QString inputHash = CustomizationWorkspace::digest(image);
    QByteArray prepared;
    const QVariantMap dryRun = CustomizationWorkspace::simulate(exePath, operations, inputHash, &prepared);
    if (!dryRun.value(QStringLiteral("ok")).toBool()) return dryRun;
    QVariantMap applied = CustomizationWorkspace::apply(exePath, operations, inputHash,
        dryRun.value(QStringLiteral("outputSha256")).toString());
    if (!applied.value(QStringLiteral("ok")).toBool()) return applied;

    // The file-resource writer can invalidate a compiled QML unit by filename
    // or a known structural identifier, but MSI 5 units commonly have an empty
    // sourceFileIndex and omit the filename. Apply this feature's full page-ID
    // set as a second, verified pass. Never tell the user "applied" while
    // bytecode that still draws the original UI remains active.
    QByteArray written;
    if (!readBounded(exePath, &written))
        return fail(QStringLiteral("The patched player could not be read back to verify its compiled Settings QML."));
    const int activeBefore = QtResources::activeUnits(written,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    const int invalidated = QtResources::invalidateUnits(written,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    const int activeAfter = QtResources::activeUnits(written,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    if (activeAfter > 0) {
        QByteArray current;
        QString rollbackError;
        if (readBounded(exePath, &current) && current != image)
            PatchFile::writeVerified(exePath, current, image, &rollbackError);
        return fail(QStringLiteral("The Settings QML source was changed, but %1 active compiled unit(s) could not be disabled. The patch was rolled back where safe; restore the player backup if the rollback warning appears. %2")
            .arg(activeAfter).arg(rollbackError));
    }
    if (written != prepared) {
        QByteArray current;
        QString error;
        if (!readBounded(exePath, &current)
            || !PatchFile::writeVerified(exePath, current, written, &error)) {
            QByteArray afterFailure;
            QString rollbackError;
            if (readBounded(exePath, &afterFailure) && afterFailure != image)
                PatchFile::writeVerified(exePath, afterFailure, image, &rollbackError);
            return fail(QStringLiteral("The embedded compiled Settings units could not be committed safely. %1 %2")
                .arg(error, rollbackError));
        }
        QtResources::clearPlayerQmlCache(exePath);
    }
    if (activeBefore > 0) {
        QString message = applied.value(QStringLiteral("msg")).toString();
        message += QStringLiteral(" Disabled %1 stale compiled Settings/About unit(s); verified the player will compile the rebuilt QML on restart.").arg(invalidated);
        applied.insert(QStringLiteral("msg"), message);
    }
    return applied;
}

bool sourceHasMarker(const QByteArray &bytes, const QString &marker)
{
    return EmbeddedResources::decodeText(bytes).contains(marker);
}

bool hasWindowMarker(const QString &text)
{
    // Keep recognizing v1 so users can update or safely restore a panel built
    // by the previous Settings design.
    return text.contains(kWindowMarker) || text.contains(kPreviousWindowDesignMarker)
        || text.contains(kWindowDesignMarkerV3) || text.contains(kWindowDesignMarkerV4)
        || text.contains(kWindowDesignMarkerV5) || text.contains(kWindowDesignMarker);
}

} // namespace

QVariantMap readCustomAsset(const QString &path, const QString &kind)
{
    if (kind != QLatin1String("tab-icon") && kind != QLatin1String("logo"))
        return fail(QStringLiteral("Unknown Settings-panel asset type."));
    const QFileInfo info(path);
    if (path.isEmpty() || !info.isFile() || info.isSymLink())
        return fail(QStringLiteral("Choose an existing local image file."));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("The selected image could not be read."));
    const qint64 limit = kind == QLatin1String("tab-icon") ? 48 * 1024 : 512 * 1024;
    if (file.size() <= 0 || file.size() > limit)
        return fail(QStringLiteral("The selected file is empty or exceeds the Settings-panel asset size limit."));
    const QByteArray bytes = file.read(limit + 1);
    QString mime;
    QString error;
    if (bytes.isEmpty() || bytes.size() > limit || !validateAssetBytes(bytes, kind, &mime, &error))
        return fail(error.isEmpty() ? QStringLiteral("The selected image is invalid.") : error);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("asset"), QVariantMap{
                {QStringLiteral("mime"), mime},
                {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())}}},
            {QStringLiteral("msg"), QStringLiteral("The safe local image is saved in the Settings-panel profile. It will be written only when you apply the rebuild.")}};
}

State analyze(const QString &exePath)
{
    State state;
    if (exePath.isEmpty() || !QFileInfo(exePath).isFile()) {
        state.detail = QStringLiteral("Select an HD-Player.exe before rebuilding its Settings panel.");
        return state;
    }
    QByteArray image;
    if (!readBounded(exePath, &image)) {
        state.detail = QStringLiteral("The selected executable is unreadable or exceeds the safe 256 MB limit.");
        return state;
    }
    // Preserve a useful inventory even when the strict live-model walk fails.
    // This makes the real GUI/test report distinguish "no QML was indexed" from
    // "QML exists but this build's Settings resource graph uses another shape".
    const auto qmlEntries = EmbeddedResources::list(image, true);
    for (const auto &entry : qmlEntries) {
        if (entry.format.compare(QLatin1String("qml"), Qt::CaseInsensitive) != 0 || entry.scanned)
            continue;
        ++state.embeddedQmlResourceCount;
        if (state.qmlResourceSamples.size() < 32)
            state.qmlResourceSamples << entry.path;
    }
    const TargetSources sources = readSources(exePath, image);
    state.tabs = sources.tabs;
    state.logoFallback = sources.aboutLogoSource.startsWith(QLatin1String("data:image/"));
    state.logoStatus = sources.aboutLogoStatus;
    if (!sources.ok) {
        state.detail = QStringLiteral("%1 Registered QML resources: %2. %3")
            .arg(state.embeddedQmlResourceCount)
            .arg(state.qmlResourceSamples.isEmpty() ? QStringLiteral("none detected") : state.qmlResourceSamples.join(QStringLiteral(", ")))
            .arg(sources.error);
        return state;
    }
    state.supported = true;
    state.settingsPageCount = sources.pageCount;
    state.settingsQmlResourceCount = sources.qmlClosureCount;
    if (!sources.about.text.contains(kAboutMarker) && hasAboutPatcherEdits(sources.about.text)) {
        state.supported = false;
        state.detail = QStringLiteral("The About page already contains separate About-editor changes. Restore the About page to stock before rebuilding the Settings panel, so those links and text are not overwritten.");
        return state;
    }
    const bool windowPatched = hasWindowMarker(sources.window.text);
    const bool aboutPatched = sources.about.text.contains(kAboutMarker);
    state.compiledUnitsDetected = QtResources::matchingUnits(image,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    state.compiledUnitsActive = QtResources::activeUnits(image,
        {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
    state.applied = windowPatched && aboutPatched && state.compiledUnitsActive == 0;
    const QString backup = sidecarPath(exePath);
    const QVariantMap recovery = backup.isEmpty() ? QVariantMap{} : readRecovery(backup, exePath);
    state.restorable = windowPatched && aboutPatched && !recovery.isEmpty();
    if (windowPatched != aboutPatched) {
        state.supported = false;
        state.detail = QStringLiteral("Only part of the custom Settings panel is present. Restore from the feature recovery snapshot before retrying.");
    } else if (windowPatched && aboutPatched && !state.restorable) {
        state.supported = false;
        state.detail = QStringLiteral("The custom Settings/About sources are present, but their feature recovery snapshot is missing or invalid. Do not overwrite this executable.");
    } else if (windowPatched && aboutPatched && state.compiledUnitsActive > 0) {
        state.detail = QStringLiteral("The Settings/About sources are patched and %1 native tab pages were resolved, but %2 matching embedded compiled unit(s) still take precedence. Reapply to invalidate them; the previous source write alone did not change the visible emulator UI.")
            .arg(state.settingsPageCount).arg(state.compiledUnitsActive);
    } else if (state.applied && state.compiledUnitsDetected > 0) {
        state.detail = QStringLiteral("The rebuilt Settings/About sources and all %1 native tab pages were resolved; all %2 matching compiled QML unit(s) are inactive. Restart the player to test the appearance; live rendering has not been verified.")
            .arg(state.settingsPageCount).arg(state.compiledUnitsDetected);
    } else if (state.applied) {
        state.detail = QStringLiteral("The rebuilt Settings/About sources and all %1 native tab pages were resolved across %2 QML resources. This executable has no recognizable embedded QV4 unit for the changed pages, so the visual result still requires a player restart/test.")
            .arg(state.settingsPageCount).arg(state.settingsQmlResourceCount);
    } else {
        state.detail = QStringLiteral("The Settings window has %1 native page roots and a verified %2-resource QML dependency closure. Source files are ready; no emulator UI change is claimed yet.")
            .arg(state.settingsPageCount).arg(state.settingsQmlResourceCount);
    }
    if (!state.logoStatus.isEmpty()) state.detail += QLatin1Char(' ') + state.logoStatus;
    return state;
}

QVariantMap apply(const QString &exePath, const QString &developerName,
                  const QVariantList &links, const QVariantMap &profile, bool enabled)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return fail(QStringLiteral("Close every HD-Player window before rebuilding or restoring the Settings panel."));
    const QString canonical = QFileInfo(exePath).canonicalFilePath();
    if (canonical.isEmpty() || QFileInfo(exePath).suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) != 0)
        return fail(QStringLiteral("Select an existing HD-Player.exe file first."));
    const QString backup = sidecarPath(exePath);
    if (backup.isEmpty()) return fail(QStringLiteral("A safe local recovery path could not be prepared."));

    QByteArray image;
    if (!readBounded(exePath, &image))
        return fail(QStringLiteral("The selected executable is unreadable or exceeds the safe 256 MB limit."));
    const TargetSources current = readSources(exePath, image);
    if (!current.ok) return fail(current.error);
    if (!current.about.text.contains(kAboutMarker) && hasAboutPatcherEdits(current.about.text))
        return fail(QStringLiteral("The About page contains changes from the separate About editor. Restore that page to stock first; the Settings rebuild was not allowed to overwrite its links or text."));
    QVariantMap recovery = readRecovery(backup, exePath);
    const bool hadRecovery = !recovery.isEmpty();

    if (!enabled) {
        if (recovery.isEmpty()) return fail(QStringLiteral("No verified Settings-panel recovery snapshot was found; nothing was changed."));
        QString recoveryError;
        const QMap<QString, QByteArray> originals = originalsFromRecovery(recovery, &recoveryError);
        if (originals.isEmpty()) return fail(recoveryError);
        if (originals.size() != 2 || !originals.contains(current.window.path) || !originals.contains(current.about.path))
            return fail(QStringLiteral("The executable's resource paths no longer match the saved Settings-panel snapshot; restore is blocked."));
        if (!hasWindowMarker(current.window.text) || !current.about.text.contains(kAboutMarker))
            return fail(QStringLiteral("The Settings-panel sources have changed since this patch. Automatic restore is blocked to protect later edits."));
        const QVariantList records = recovery.value(QStringLiteral("sources")).toList();
        for (const QVariant &value : records) {
            const QVariantMap record = value.toMap();
            const Source *source = record.value(QStringLiteral("path")).toString() == current.window.path
                ? &current.window : &current.about;
            if (!recoveryAcceptsCurrent(record, source->bytes))
                return fail(QStringLiteral("A Settings-panel resource was edited after patching; restore is blocked rather than overwriting that change."));
        }
        const auto restored = applyResources(exePath, operationsFor(originals));
        if (!restored.value(QStringLiteral("ok")).toBool()) return restored;
        QFile::remove(backup);
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("msg"), QStringLiteral("The original Settings-window and About-page sources were restored without reverting other executable resources. Restart BlueStacks to see the change.")}};
    }

    QString profileError;
    if (!validateProfile(developerName, links, &profileError)) return fail(profileError);
    for (auto it = profile.cbegin(); it != profile.cend(); ++it)
        if (it.key() != QLatin1String("hiddenTabs") && it.key() != QLatin1String("tabIcons")
            && it.key() != QLatin1String("logo") && it.key() != QLatin1String("tagline"))
            return fail(QStringLiteral("Unknown Settings-panel profile option: %1.").arg(it.key()));
    const QString tagline = profile.value(QStringLiteral("tagline")).toString().trimmed();
    if (tagline.size() > 120 || tagline.contains(QChar(0))
        || QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")).match(tagline).hasMatch())
        return fail(QStringLiteral("The About subtitle must be no longer than 120 characters and contain no control characters."));
    QSet<QString> availableTabs;
    for (const QVariant &tab : current.tabs)
        availableTabs.insert(tab.toMap().value(QStringLiteral("id")).toString().toCaseFolded());
    QStringList hiddenTabs;
    for (const QVariant &value : profile.value(QStringLiteral("hiddenTabs")).toList()) {
        const QString id = value.toString().toCaseFolded();
        if (availableTabs.contains(id) && !hiddenTabs.contains(id)) hiddenTabs << id;
    }
    if (hiddenTabs.size() >= availableTabs.size())
        return fail(QStringLiteral("At least one native Settings tab must remain visible."));
    QVariantMap customTabIcons;
    const QVariantMap storedIcons = profile.value(QStringLiteral("tabIcons")).toMap();
    if (storedIcons.size() > 64)
        return fail(QStringLiteral("The Settings-panel profile contains too many custom tab icons."));
    for (auto it = storedIcons.cbegin(); it != storedIcons.cend(); ++it) {
        const QString id = it.key().toCaseFolded();
        if (!availableTabs.contains(id)) continue; // stale option from another player version
        QString source;
        QString assetError;
        if (!storedAssetSource(it.value().toMap(), QStringLiteral("tab-icon"), &source, &assetError))
            return fail(assetError);
        customTabIcons.insert(id, source);
    }
    QString logoSource = current.aboutLogoSource;
    if (profile.contains(QStringLiteral("logo"))) {
        QString assetError;
        if (!storedAssetSource(profile.value(QStringLiteral("logo")).toMap(),
                               QStringLiteral("logo"), &logoSource, &assetError))
            return fail(assetError);
    }
    const bool hasPatchMarker = hasWindowMarker(current.window.text)
        || current.about.text.contains(kAboutMarker);
    QMap<QString, QByteArray> originals;
    if (!recovery.isEmpty()) {
        QString recoveryError;
        originals = originalsFromRecovery(recovery, &recoveryError);
        if (originals.isEmpty()) return fail(recoveryError);
        if (originals.size() != 2 || !originals.contains(current.window.path) || !originals.contains(current.about.path))
            return fail(QStringLiteral("The executable's resource paths no longer match the saved Settings-panel snapshot; patch is blocked."));
        const QVariantList records = recovery.value(QStringLiteral("sources")).toList();
        for (const QVariant &value : records) {
            const QVariantMap record = value.toMap();
            const Source *source = record.value(QStringLiteral("path")).toString() == current.window.path
                ? &current.window : &current.about;
            if (!recoveryAcceptsCurrent(record, source->bytes))
                return fail(QStringLiteral("A saved Settings-panel resource was changed by another patch. Restore or inspect it manually before rebuilding."));
        }
    } else {
        if (hasPatchMarker)
            return fail(QStringLiteral("A Settings-panel marker exists without its recovery snapshot. The executable was not overwritten."));
        originals.insert(current.window.path, current.window.bytes);
        originals.insert(current.about.path, current.about.bytes);
        recovery = {{QStringLiteral("schema"), kSchema},
                    {QStringLiteral("version"), 1},
                    {QStringLiteral("target"), canonical}};
    }

    const QString originalWindowText = EmbeddedResources::decodeText(originals.value(current.window.path));
    const QString originalAboutText = EmbeddedResources::decodeText(originals.value(current.about.path));
    if (originalWindowText.isEmpty() || originalAboutText.isEmpty())
        return fail(QStringLiteral("A saved original QML source could not be decoded; the executable was not changed."));
    const QString rebuiltWindow = buildWindow(originalWindowText, hiddenTabs, customTabIcons);
    if (rebuiltWindow.isEmpty())
        return fail(QStringLiteral("This BlueStacks build's Settings-window structure is not recognized. No file was changed."));
    const QString rebuiltAbout = buildAbout(developerName, tagline, links, logoSource);
    if (rebuiltAbout.isEmpty())
        return fail(QStringLiteral("The About page could not be generated; no file was changed."));
    QMap<QString, QByteArray> replacements {
        {current.window.path, utf8PreservingBom(rebuiltWindow, originals.value(current.window.path))},
        {current.about.path, utf8PreservingBom(rebuiltAbout, originals.value(current.about.path))},
    };
    if (!hasWindowMarker(EmbeddedResources::decodeText(replacements.value(current.window.path)))
        || !sourceHasMarker(replacements.value(current.about.path), kAboutMarker))
        return fail(QStringLiteral("Generated QML failed its marker check; no file was changed."));
    if (replacements.value(current.window.path) == current.window.bytes
        && replacements.value(current.about.path) == current.about.bytes) {
        const int active = QtResources::activeUnits(image,
            {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
        if (active == 0)
            return {{QStringLiteral("ok"), true},
                    {QStringLiteral("msg"), QStringLiteral("The requested Settings-panel design is already applied and no stale compiled QML remains active.")}};
        QByteArray invalidated = image;
        QtResources::invalidateUnits(invalidated,
            {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
        if (QtResources::activeUnits(invalidated,
                {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds) > 0)
            return fail(QStringLiteral("The Settings/About sources already match, but their embedded compiled QML is still active and could not be disabled."));
        QLockFile lock(canonical + QStringLiteral(".bsi-settings-panel.lock"));
        if (!lock.tryLock(0))
            return fail(QStringLiteral("Another Settings-panel writer is using this executable; retry after it finishes."));
        QString error;
        if (!ThemeEngine::backup(exePath) || !PatchFile::writeVerified(exePath, image, invalidated, &error))
            return fail(QStringLiteral("Could not safely disable stale compiled Settings/About QML. %1").arg(error));
        QtResources::clearPlayerQmlCache(exePath);
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("msg"), QStringLiteral("The Settings/About QML sources were already rebuilt; %1 stale compiled unit(s) were invalidated and verified. Restart BlueStacks to load the source.").arg(active)}};
    }

    recovery.insert(QStringLiteral("sources"), sourceRecords(current, originals, replacements));
    recovery.insert(QStringLiteral("developerName"), developerName.trimmed());
    if (!saveAtomic(backup, QJsonDocument::fromVariant(recovery).toJson(QJsonDocument::Compact)))
        return fail(QStringLiteral("Could not save the feature-specific recovery snapshot. No executable resources were changed."));

    const QVariantMap result = applyResources(exePath, operationsFor(replacements));
    if (!result.value(QStringLiteral("ok")).toBool()) {
        // Preserve an existing valid recovery file; remove only the one created
        // by this attempt, so a failed first apply cannot masquerade as active.
        if (!hadRecovery) QFile::remove(backup);
        return result;
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("msg"), QStringLiteral("The compact, draggable Settings window, icon tabs and customizable About page were applied. A selective recovery snapshot was saved; restart BlueStacks to see it.")},
            {QStringLiteral("recovery"), result.value(QStringLiteral("id"))}};
}

} // namespace SettingsPanelPatcher
