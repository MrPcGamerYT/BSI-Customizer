#include "SettingsPanelPatcher.h"

#include "BsDetector.h"
#include "CustomizationWorkspace.h"
#include "EmbeddedResources.h"
#include "PatchFile.h"
#include "PeUtils.h"
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
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include <QUrl>
#include <QXmlStreamReader>

namespace SettingsPanelPatcher {
namespace {
const QString kWindowMarker = QStringLiteral("BSI CUSTOMIZER: redesigned Settings window v1");
const QString kPreviousWindowDesignMarker = QStringLiteral("BSI CUSTOMIZER: Settings v2");
const QString kWindowDesignMarkerV3 = QStringLiteral("BSI Settings v3");
const QString kWindowDesignMarkerV4 = QStringLiteral("BSI Settings v4");
const QString kWindowDesignMarkerV5 = QStringLiteral("BSI Settings v5");
const QString kWindowDesignMarkerV6 = QStringLiteral("BSI6");
// v8 was the full-bleed shell that left the native fixed-width pages hugging
// the left edge; v9 bounds the page region inside a centred frame.
const QString kWindowDesignMarkerV8 = QStringLiteral("BSI Settings v8");
const QString kWindowDesignMarkerV9 = QStringLiteral("BSI Settings v9");
const QString kWindowDesignMarkerV10 = QStringLiteral("BSI Settings v10");
const QString kWindowDesignMarkerV12 = QStringLiteral("BSI Settings v12");
const QString kWindowDesignMarkerV13 = QStringLiteral("BSI Settings v13");
const QString kWindowDesignMarkerV14 = QStringLiteral("BSI Settings v14");
const QString kWindowDesignMarkerV15 = QStringLiteral("BSI v15");
const QString kWindowDesignMarker = QStringLiteral("BSI v16");
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
        QXmlStreamReader xml(bytes);
        while (!xml.atEnd()) xml.readNext();
        if (xml.hasError() || !QSvgRenderer(bytes).isValid()) {
            *error = QStringLiteral("That SVG is malformed or cannot be rendered. Choose a valid self-contained SVG.");
            return false;
        }
        *mime = QStringLiteral("image/svg+xml");
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
    if (encoded.isEmpty() || decoded.isEmpty()) {
        *error = QStringLiteral("A saved Settings-panel image is empty or damaged; select it again.");
        return false;
    }
    if (!validateAssetBytes(decoded, kind, &mime, error)) return false;
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

// Referenced artwork belongs in the app's data folder, not the player's
// installation. File URLs avoid Qt 5's data-URI limitation and the fixed About
// resource-slot budget. Runtime copies are PNGs so they do not depend on the
// player's optional SVG image plugin; the editable profile keeps the SVG.
QString assetDirectory(const QString &exePath)
{
    const QString canonical = QFileInfo(exePath).canonicalFilePath();
    if (canonical.isEmpty()) return {};
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/settings-panel-assets");
    return QDir(root).filePath(QString::fromLatin1(sha256(canonical.toUtf8())));
}

// Only files created by an unsuccessful attempt may be removed. Older pages
// may still reference existing files, including after a failed update.
struct StagedAssetFiles
{
    QStringList created;
    bool committed = false;
    ~StagedAssetFiles()
    {
        if (!committed)
            for (const QString &path : created) QFile::remove(path);
    }
};

// Immutable, content-addressed names also invalidate QML's image cache when an
// icon is replaced. Never overwrite a live icon before the EXE write succeeds.
QString writeAssetFile(const QString &exePath, const QByteArray &bytes,
                       StagedAssetFiles *staged, QString *error)
{
    const QString dir = assetDirectory(exePath);
    if (dir.isEmpty() || bytes.isEmpty() || !QDir().mkpath(dir)) {
        *error = QStringLiteral("The Settings-panel artwork folder could not be prepared.");
        return {};
    }
    const QString file = QDir(dir).filePath(QString::fromLatin1(sha256(bytes).left(24))
                                           + QStringLiteral(".png"));
    const QFileInfo info(file);
    if (info.exists() || info.isSymLink()) {
        QByteArray existing;
        if (info.isSymLink() || !readBounded(file, &existing) || existing != bytes) {
            *error = QStringLiteral("A saved Settings-panel artwork file failed verification; nothing was overwritten.");
            return {};
        }
    } else {
        if (!saveAtomic(file, bytes)) {
            *error = QStringLiteral("Settings-panel artwork could not be saved in the app's data folder.");
            return {};
        }
        staged->created << file;
    }
    // Hand-built URLs turn '#' into a fragment and '%' into an escape, and
    // produce the wrong slash count on non-Windows paths.
    return QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded);
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
    // The rebuilt shell's Repeater delegate sees the model data directly, so it
    // stores the loaded component as `modelData.pComponentObj=item`; the stock
    // shell spells the same assignment `model.modelData....`. The shorter needle
    // matches both, because it is a suffix of the longer one.
    const bool retainsLoadedComponent = out.window.text.contains(QLatin1String("modelData.pComponentObj=item"))
        || out.window.text.contains(QLatin1String("model.modelData.pComponentObj = item"))
        || out.window.text.contains(QLatin1String("model.modelData.pComponentObj=i"));
    // The stock shell reaches the page through `model.modelData`, while the
    // rebuilt shell's Repeater delegate has the model data directly in scope and
    // uses `modelData`. Both feed the same pQmlSource to setSource; accept either
    // so re-applying over an already-patched player is not mistaken for damage.
    const bool loadsNativePage = out.window.text.contains(QLatin1String("setSource(model.modelData.pQmlSource"))
        || out.window.text.contains(QLatin1String("setSource(modelData.pQmlSource"));
    if (!out.window.text.contains(QLatin1String("pSettingsItems.pSettingElements"))
        || !loadsNativePage
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

QString buildWindow(const QString &stock)
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

    // Retain the native model, page callbacks, selection/discard handling and
    // theme, but own the tab labels: UiSettingsButton's private column is
    // left-aligned and uses UiTheme.fonts.bodyLarge, not the application font.
    // Moving/sizing that opaque button cannot reliably centre its text.
    //
    // TextMetrics measures the *same* font/text as the actual Text. Give its
    // ink bounds 11 px on either side and cancel the left glyph bearing, so
    // both the hit targets (4 px) and visible labels (26 px) have uniform gaps.
    // The red dot stays inside that padding; icons sit below the centred label.
    // A hidden sentinel removes its hit target, label, icon and one spacing.
    // UiShadowContainer redirects its default children into a private item;
    // address bIcons by the public shell id, never by r.parent.
    //
    // Keep the native 568 px page column, footer clearance and loader callbacks.
    // Only the selected tab gains a soft themed highlight; hover is quieter.
    // A public Controls ToolButton paints/tints each PNG icon, with no optional
    // SVG decoder, GraphicalEffects or private Controls.impl import. Its disabled,
    // background-free surface leaves all input to the enclosing tab MouseArea.
    // Transparent icon.color preserves custom artwork when it is not selected.
    // The unused bHidden channel is gone: hidden tabs still use bIcons' empty
    // sentinel, published by About before the user can interact with the strip.
    // Inset the page stack 12 px below the tab strip without moving the footer
    // or adding space between the title and tabs.
    // StackLayout fills its Loader children by default; anchoring those children
    // as well conflicts with its geometry and produces native Qt warnings.
    // Keep the template small, but the *whole* compressed Window (including
    // native callbacks, copyright header and geometry helper) must fit the slot.
    // Template length alone cannot predict that compression budget.
    QString replacement = QStringLiteral(R"QML(UiShadowContainer{//BSI v16
id:iSettingsWindowShadowContainer;anchors.fill:parent
property var bIcons:({})
Rectangle{id:r;property var q:UiTheme.colors;anchors.fill:parent;radius:14;color:r.q.primary80
Column{Item{width:r.width;height:56
MouseArea{anchors.fill:parent;onPressed:iSettingsWindow.startSystemMove()}Text{id:h;text:qsTr('Settings');color:r.q.primary10;font:UiTheme.fonts.bodyLarge;anchors.centerIn:parent}UiImageButton{asset:'CloseIcon';x:r.width-32;y:12;onClicked:iSettingsWindow.close()}}ListView{width:Math.max(0,Math.min(contentWidth,r.width-48));height:44;x:(r.width-width)/2;clip:true;orientation:Qt.Horizontal;model:pSettingsItems.pSettingElements;spacing:4
delegate:MouseArea{hoverEnabled:true;visible:c!=='';property var c:iSettingsWindowShadowContainer.bIcons[index];width:c===''?-4:m.boundingRect.width+22;height:44;onClicked:{fChangeSelectedSetting(index);forceActiveFocus()}
Rectangle{anchors.fill:parent;radius:6;color:t.color;opacity:index==pCurrentTabIndex?.2:parent.containsMouse?.1:0}Text{id:t;text:modelData.pButtonText;font:h.font;x:11-m.boundingRect.x;y:c?0:22-height/2;color:r.q[index==pCurrentTabIndex?'subWindowHeaderClick':'primary20']}TextMetrics{id:m;text:t.text;font:t.font}Rectangle{visible:modelData.pShowRedDot;x:parent.width-8;y:t.y+6;width:6;height:6;radius:3;color:r.q.dangerNormal}ToolButton{width:18;height:18;x:parent.width/2-9;y:24;enabled:false;padding:0;background:null;icon.source:c||'';icon.color:index==pCurrentTabIndex?t.color:'transparent';icon.width:18;icon.height:18}}}Item{width:Math.min(r.width-48,568);x:(r.width-width)/2;height:r.height-118
StackLayout{id:iSelectedSettingControl;anchors.fill:parent;currentIndex:pCurrentTabIndex;anchors.topMargin:12
Repeater{model:pSettingsItems.pSettingElements;delegate:Loader{Component.onCompleted:setSource(modelData.pQmlSource,modelData.pComponentArgs||{});onLoaded:{item.anchors.fill=item.parent;modelData.pComponentObj=item;item.fOnQmlLoaded&&item.fOnQmlLoaded()}}}}}}}}
)QML");
    // Older MSI/BlueStacks sources have a 2468-byte compressed slot. Retaining
    // the native callback prefix is essential, but its indentation is not.
    // Compact only that prefix (the generated shell is already compact), keeping
    // literals, copyright notices, line endings and JS statement breaks intact.
    QString out = QmlText::compactWhitespace(stock.left(target.nameBegin));
    out += replacement;
    out += stock.mid(target.end);
    // Latest 1301x665 reference: visible border x=140..1177, y=49..629.
    // Match its 1038x581 frame plus the unchanged 15 px shadow: 1068x611.
    // Retain viewport guards, centering, theme, controls and native page sizes.
    const QString helper = QStringLiteral(R"QML(onVisibleChanged:if(visible&&transientParent){var t=transientParent;width=Math.min(t.width*.96,1068);height=Math.min(t.height*.94,611);x=t.x+t.width/2-width/2;y=t.y+t.height/2-height/2})QML");
    const int rootClose = out.lastIndexOf(QLatin1String("\n}"));
    if (rootClose < 0) return {};
    out.insert(rootClose, helper);
    if (!out.contains(kWindowDesignMarker)) return {};
    return out;
}

// The player delegate indexes this map with its native model index. Keep the
// conversion shared by embedded icons, file references and hidden sentinels.
QVariantMap byTabIndex(const QVariantMap &byId, const QVariantList &tabs)
{
    QVariantMap out;
    for (const QVariant &entry : tabs) {
        const QVariantMap tab = entry.toMap();
        const QString id = tab.value(QStringLiteral("id")).toString().toCaseFolded();
        if (byId.contains(id))
            out.insert(QString::number(tab.value(QStringLiteral("index")).toInt()), byId.value(id));
    }
    return out;
}

QString buildAbout(const QString &developerName, const QString &tagline,
                   const QVariantList &links, const QString &logoSource,
                   const QStringList &hiddenTabs, const QVariantMap &tabIcons)
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
    // A hidden tab must disappear from the strip, and the only channel the small
    // About resource slot has for telling the shell that is the map the shell
    // already reads per delegate. An empty value is the "hidden" sentinel: the
    // delegate collapses to zero width and paints no icon. Declaring bHidden in
    // the shell and publishing the list into it did nothing, because no strip
    // delegate ever read it.
    for (const QString &page : hiddenTabs) {
        if (iconLiteral.size() > 2) iconLiteral += QLatin1Char(',');
        iconLiteral += qmlString(page.toCaseFolded()) + QStringLiteral(":\"\"");
    }
    iconLiteral += QStringLiteral("})");
    const QString qDeveloper = qmlString(developerName.trimmed());
    const QString qTagline = qmlString(tagline.trimmed());
    // The About page is not free-form: the player's own QML resource slot for
    // PlrAboutSetting.qml has to hold this document, compressed, and the EXE is
    // never allowed to grow. A custom tab icon or logo is embedded in here too,
    // so every unused byte of markup eats into what the user can upload. Keep it
    // compact - whitespace and repeated token spellings are the difference
    // between "the icon applies" and "needs 2495 bytes but its slot only holds
    // 2337".
    // The logo is a ColumnLayout child: plain width/height lose to a decoded
    // image's implicit size. Use preferred AND maximum layout hints so large
    // native/custom artwork stays small without stretching its aspect ratio.
    QString generated = QStringLiteral(R"QML(import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
Item{objectName:"aboutSettings"
property bool pUpdateAvailable:false
property string pUpdateVersion:""
property string pMoreInfoURL:""
property string pDownloadURL:""
property bool pIsInCompatibleUpdateVersion:false
property string pParentWindow:""
property string pShortDisplayName:""
property string pDeveloperName:@@BSI_DEVELOPER@@
property string pDeveloperTagline:@@BSI_TAGLINE@@
property var bsiHiddenTabs:@@BSI_HIDDEN_TABS@@
property var bsiTabIcons:@@BSI_TAB_ICONS@@
property var pDeveloperLinks:[@@BSI_LINKS@@]
// BSI CUSTOMIZER: redesigned About page v1
ColumnLayout{anchors.fill:parent;spacing:0
ScrollView{Layout.fillWidth:true;Layout.fillHeight:true;clip:true
background:Rectangle{color:"transparent"}
contentWidth:availableWidth;contentHeight:iAboutD.implicitHeight+48
ColumnLayout{id:iAboutD;width:Math.min(440,parent.width-24);anchors.horizontalCenter:parent.horizontalCenter
Layout.alignment:Qt.AlignHCenter|Qt.AlignTop;spacing:16
Item{Layout.fillWidth:true;Layout.preferredHeight:28}
Image{objectName:"bsiAboutEmulatorLogo";source:@@BSI_LOGO_SOURCE@@
Layout.preferredWidth:160;Layout.preferredHeight:60;Layout.maximumWidth:160;Layout.maximumHeight:60
fillMode:Image.PreserveAspectFit;Layout.alignment:Qt.AlignHCenter}
Text{objectName:"bsiAboutDeveloperName";text:pDeveloperName;font:UiTheme.fonts.bodyLarge
color:UiTheme.colors.primary10;horizontalAlignment:Text.AlignHCenter
wrapMode:Text.Wrap;Layout.fillWidth:true}
Text{objectName:"bsiAboutDeveloperTagline";visible:pDeveloperTagline.length>0
text:pDeveloperTagline;font:UiTheme.fonts.bodySmall;color:UiTheme.colors.primary30
horizontalAlignment:Text.AlignHCenter;wrapMode:Text.Wrap;Layout.fillWidth:true}
Rectangle{Layout.fillWidth:true;Layout.preferredHeight:1;color:UiTheme.colors.primary60}
Repeater{model:pDeveloperLinks
delegate:Rectangle{objectName:"bsiAboutDeveloperLink"
Layout.fillWidth:true;Layout.preferredHeight:42
property bool hovered:false
radius:10;border.width:1
color:hovered?Qt.rgba(UiTheme.colors.accentNormal.r,UiTheme.colors.accentNormal.g,UiTheme.colors.accentNormal.b,.16)
:Qt.rgba(UiTheme.colors.primary60.r,UiTheme.colors.primary60.g,UiTheme.colors.primary60.b,.42)
border.color:hovered?UiTheme.colors.accentNormal:UiTheme.colors.primary60
Text{anchors.left:parent.left;anchors.right:parent.right;anchors.leftMargin:14
anchors.rightMargin:38;anchors.verticalCenter:parent.verticalCenter
text:modelData.title;font:UiTheme.fonts.bodyMedium;elide:Text.ElideRight
color:parent.hovered?UiTheme.colors.accentNormal:UiTheme.colors.primary10}
Text{anchors.right:parent.right;anchors.rightMargin:14
anchors.verticalCenter:parent.verticalCenter;text:"\u2197"
color:UiTheme.colors.accentNormal;font.pixelSize:16}
MouseArea{anchors.fill:parent;hoverEnabled:true;cursorShape:Qt.PointingHandCursor
onEntered:parent.hovered=true;onExited:parent.hovered=false
onClicked:{var t=String(modelData.url||"");if(/^https?:\/\//i.test(t))Qt.openUrlExternally(t)}}}}
Text{visible:pDeveloperLinks.length===0
text:qsTranslate("QObject","Add custom developer buttons in BSI Customizer.")
color:UiTheme.colors.primary30;font:UiTheme.fonts.bodySmall
horizontalAlignment:Text.AlignHCenter;wrapMode:Text.Wrap;Layout.fillWidth:true}
Item{Layout.fillWidth:true;Layout.preferredHeight:28}}}}

// Publish icons and hidden sentinels by walking up to the owning shell.
function fOnQmlLoaded(){var p=parent;while(p&&p.bIcons===undefined)p=p.parent;if(p)p.bIcons=bsiTabIcons}
function fCheckForUpdateCallback(apiCallStatus,jsonData){
if(apiCallStatus===0){var j=JSON.parse(jsonData)
pUpdateAvailable=String(j.update_available).toLowerCase()==="true"
pIsInCompatibleUpdateVersion=pUpdateAvailable&&String(j.update_details.is_build_compatible).toLowerCase()!=="true"
if(pUpdateAvailable){pUpdateVersion=j.update_details.client_version
pMoreInfoURL=j.update_details.detailed_changelogs_url
pDownloadURL=j.update_details.download_url}}
else{pUpdateAvailable=false;pIsInCompatibleUpdateVersion=false}}
function fUpdateShortDisplayName(n){pShortDisplayName=String(n||"")}
}
)QML");
    generated.replace(QStringLiteral("@@BSI_DEVELOPER@@"), qDeveloper);
    generated.replace(QStringLiteral("@@BSI_TAGLINE@@"), qTagline);
    generated.replace(QStringLiteral("@@BSI_HIDDEN_TABS@@"), hiddenLiteral);
    generated.replace(QStringLiteral("@@BSI_TAB_ICONS@@"), iconLiteral);
    generated.replace(QStringLiteral("@@BSI_LINKS@@"), rows);
    generated.replace(QStringLiteral("@@BSI_LOGO_SOURCE@@"), qmlString(logoSource));
    return generated;
}

// The page as it is generated with no custom artwork, compressed. This is the
// baseline the icon budget is measured against.
int emptyPagePayload()
{
    return int(EmbeddedResources::payloadSizeFor(
        buildAbout(QString(), QString(), {}, QString(), QStringList(), {}).toUtf8(), 1));
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

// Why a saved snapshot cannot be used. The distinction matters: an unusable
// snapshot on a *clean* executable is stale state we can safely replace, while
// the same snapshot on an already-patched executable must block every write.
enum class RecoveryProblem {
    None,
    Incomplete, // no two source records: truncated, half-written, older layout
    Damaged,    // records present but a payload or its hash is unusable
};

QMap<QString, QByteArray> originalsFromRecovery(const QVariantMap &recovery,
                                                RecoveryProblem *problem = nullptr)
{
    QMap<QString, QByteArray> originals;
    if (problem) *problem = RecoveryProblem::None;
    const QVariantList records = recovery.value(QStringLiteral("sources")).toList();
    if (records.size() != 2) {
        if (problem) *problem = RecoveryProblem::Incomplete;
        return {};
    }
    for (const QVariant &value : records) {
        const QVariantMap record = value.toMap();
        const QString path = record.value(QStringLiteral("path")).toString();
        const QByteArray raw = QByteArray::fromBase64(record.value(QStringLiteral("original")).toByteArray());
        if (path.isEmpty() || raw.isEmpty() || raw.size() > kMaxQml
            || QString::fromLatin1(sha256(raw)) != record.value(QStringLiteral("originalSha256")).toString()
            || originals.contains(path)) {
            if (problem) *problem = RecoveryProblem::Damaged;
            originals.clear();
            return originals;
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
        || developerName.contains(QChar(0)) || links.size() > 8) {
        *error = QStringLiteral("Enter a developer name up to 80 characters and no more than eight About buttons.");
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
        || text.contains(kWindowDesignMarkerV5) || text.contains(kWindowDesignMarkerV6)
        || text.contains(kWindowDesignMarkerV8) || text.contains(kWindowDesignMarkerV9)
        || text.contains(kWindowDesignMarkerV10) || text.contains(kWindowDesignMarkerV12)
        || text.contains(kWindowDesignMarkerV13) || text.contains(kWindowDesignMarkerV14)
        || text.contains(kWindowDesignMarkerV15) || text.contains(kWindowDesignMarker);
}

// Standalone Image sources do not inherit the QML/HTML text's currentColor.
// Give uncoloured stroke packs a readable foreground, matching the supplied
// pack's dark preview. An explicit SVG colour or style always wins.
QString standaloneSvg(QString text)
{
    if (!text.contains(QLatin1String("currentColor"), Qt::CaseInsensitive)) return text;
    QXmlStreamReader xml(text);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("svg")) return text;
    const auto attrs = xml.attributes();
    static const QRegularExpression styleColor(QStringLiteral("(?:^|;)\\s*color\\s*:"),
                                               QRegularExpression::CaseInsensitiveOption);
    if (attrs.hasAttribute(QLatin1String("color"))
        || styleColor.match(attrs.value(QLatin1String("style")).toString()).hasMatch()) return text;
    const int end = int(xml.characterOffset()) - 1;
    if (end > 0 && end < text.size() && text.at(end) == QLatin1Char('>'))
        text.insert(text.at(end - 1) == QLatin1Char('/') ? end - 1 : end,
                    QStringLiteral(" color=\"#e7e9f2\""));
    return text;
}

} // namespace

// Keep editable SVGs compact in the profile and, when supported and small
// enough, in the fixed-size About resource. Bundled packs and older players use
// separate PNG runtime copies. Editors can emit KB of comments/metadata for a
// tiny drawing: remove that overhead and insignificant tag spacing without
// dropping self-closing slashes. standaloneSvg also gives currentColor artwork
// a readable default foreground unless the SVG defines its own root colour.
// The caller re-validates the result, rejecting malformed XML, scripts, event
// handlers and external resource references.
QString minifySvg(const QString &source)
{
    QString text = source;
    static const QRegularExpression comment(QStringLiteral("<!--.*?-->"),
                                            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression metadata(
        QStringLiteral("<metadata\\b.*?</metadata\\s*>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression prolog(
        QStringLiteral("^\\s*<\\?xml[^>]*\\?>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression doctype(
        QStringLiteral("<!DOCTYPE[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    text.remove(comment);
    text.remove(metadata);
    text.remove(doctype);
    text.remove(prolog);
    // Whitespace between tags carries no meaning in SVG.
    text.replace(QRegularExpression(QStringLiteral(">\\s+<")), QStringLiteral("><"));
    // Leading/trailing padding inside a tag, and the newlines that remain in
    // attribute lists, are also insignificant.
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    text.replace(QRegularExpression(QStringLiteral("<\\s+")), QStringLiteral("<"));
    // Preserve the slash in an editor's `<path ... />`. Dropping it makes an
    // unclosed element; the old shallow validator accepted that broken SVG and
    // the image loader then painted nothing.
    text.replace(QRegularExpression(QStringLiteral("\\s+(/?>)")), QStringLiteral("\\1"));
    return standaloneSvg(text.trimmed());
}

// Render with the Customizer's SVG decoder, not the player's optional plugin.
// Icons get square tiles; logos can keep their natural aspect ratio so the
// About Image does not letterbox an already-letterboxed picture a second time.
QByteArray rasterizeSvg(const QByteArray &svg, int box, bool square = true)
{
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return {};
    // Keep the drawing's own aspect ratio inside a square tile so a wide or tall
    // icon is never stretched.
    QSizeF natural = renderer.defaultSize();
    if (natural.isEmpty()) natural = QSizeF(box, box);
    const QSizeF scaled = natural.scaled(QSizeF(box, box), Qt::KeepAspectRatio);
    const int side = qMax(8, qRound(qMax(scaled.width(), scaled.height())));
    const QSize canvas = square ? QSize(side, side)
                                : QSize(qMax(1, qRound(scaled.width())),
                                        qMax(1, qRound(scaled.height())));
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF target((image.width() - scaled.width()) / 2.0,
                        (image.height() - scaled.height()) / 2.0,
                        scaled.width(), scaled.height());
    renderer.render(&painter, target);
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly)) return {};
    return image.save(&buffer, "PNG") ? png : QByteArray();
}

// A runtime copy is always PNG, including raster logos whose original format
// (for example WebP) may not be supported by the player's image plugins.
// The saved profile is never changed by this conversion.
QByteArray runtimeAssetPng(const QVariantMap &asset, bool icon)
{
    const QByteArray bytes = QByteArray::fromBase64(
        asset.value(QStringLiteral("data")).toString().toLatin1());
    if (asset.value(QStringLiteral("mime")).toString() == QLatin1String("image/svg+xml"))
        return rasterizeSvg(bytes, icon ? 54 : 320, icon);
    QImage image = QImage::fromData(bytes);
    if (image.isNull()) return {};
    if (image.width() > 320 || image.height() > 320)
        image = image.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return png;
}

// How many bytes of tab artwork the About page can carry. The document is
// compressed into a fixed slot, so this is measured, not guessed: the compressed
// size of the page with no artwork is compared against that player's real slot,
// with a reserve for the logo and for data the compressor cannot pack.
//
// Use the smallest known slot only when this player's actual capacity cannot
// be resolved. Treating every player as that floor needlessly degrades artwork
// on roomier builds and still cannot replace the final document's fit check.
constexpr int kAboutSlotFloor = 2337;
constexpr int kArtworkReserve = 160;

int artworkBudget(int plainPayload, int slotCapacity)
{
    const int slot = slotCapacity > 0 ? slotCapacity : kAboutSlotFloor;
    return qMax(0, slot - plainPayload - kArtworkReserve);
}

// The real capacity of this player's About slot, or -1 when it cannot be
// resolved (the caller then falls back to the known floor).
int aboutSlotCapacity(const QByteArray &image, QString *error)
{
    QList<EmbeddedResources::Entry> matches;
    for (const auto &entry : EmbeddedResources::list(image, true)) {
        if (entry.format.compare(QLatin1String("qml"), Qt::CaseInsensitive) == 0
            && entry.leaf.compare(kAboutLeaf, Qt::CaseInsensitive) == 0)
            matches << entry;
    }
    if (matches.size() != 1 || matches.first().scanned) return -1;
    const EmbeddedResources::Budget room =
        EmbeddedResources::budget(image, matches.first().entryOff, kAboutLeaf);
    if (!room.known) {
        if (error) *error = room.detail;
        return -1;
    }
    return int(room.capacity);
}

// Shrink one stored asset so the page can carry it. The About document is
// compressed into a fixed slot, so artwork has a hard byte budget: the saved
// SVG is minified; only a logo may become a smaller stored PNG. Tab icons
// remain SVGs in the profile (the apply path creates PNG runtime copies).
// Keeping this in one place matters -
// readCustomAsset() runs when an icon is picked (and updates the profile), but
// apply() re-reads every icon from the profile, and artwork saved by an earlier
// build is already in the drawer. Both paths have to shrink, or an icon that
// "saved fine" fails the apply with "needs 2347 bytes but its slot only holds
// 2337" seconds later.
QVariantMap shrinkAsset(QVariantMap asset, const QString &kind, int budget)
{
    const QByteArray bytes = QByteArray::fromBase64(asset.value(QStringLiteral("data")).toString().toLatin1());
    if (bytes.isEmpty() || asset.value(QStringLiteral("mime")).toString() != QLatin1String("image/svg+xml"))
        return asset;
    QString error;
    QString mime = QStringLiteral("image/svg+xml");
    QByteArray stored = bytes;
    const QByteArray minified = minifySvg(QString::fromUtf8(bytes)).toUtf8();
    // A currentColor-only SVG can grow slightly when its standalone colour is
    // made explicit. Keep that readable version even if it is not smaller.
    if (!minified.isEmpty() && validateAssetBytes(minified, kind, &mime, &error))
        stored = minified;
    // Still too big for the slot? Draw it down. Refusing is worse: the SVG is
    // valid, only the container is small.
    //
    // Only the logo may become a raster image. A tab icon is required to be an
    // SVG (validateAssetBytes rejects "image/png" for that kind), so rasterising
    // one here produced an asset that storedAssetSource() then refused, and the
    // icon was dropped from the rebuilt page without any message. For icons the
    // minified SVG is the smallest form we are allowed to embed.
    int uriSize = int(stored.toBase64().size()) + 28;
    if (kind != QLatin1String("tab-icon") && budget > 0 && uriSize > budget) {
        for (int box : {32, 28, 24, 20, 16, 12}) {
            if (uriSize <= budget) break;
            const QByteArray png = rasterizeSvg(stored, box);
            if (png.isEmpty()) break;
            const int pngUri = int(png.toBase64().size()) + 22;
            if (pngUri >= uriSize) break;
            stored = png;
            mime = QStringLiteral("image/png");
            uriSize = pngUri;
        }
    }
    asset.insert(QStringLiteral("mime"), mime);
    asset.insert(QStringLiteral("data"), QString::fromLatin1(stored.toBase64()));
    return asset;
}

QVariantMap defaultTabIcons(const QVariantList &tabs, QString *error)
{
    struct Catalog { QVariantMap assets; QString error; };
    static const Catalog catalog = [] {
        Catalog out;
        const QString root = QStringLiteral(":/settings-panel-icons/");
        const QString damaged = QStringLiteral("The bundled Settings-tab icon pack is missing or damaged. Reinstall the Customizer before applying the rebuild.");
        QFile manifest(root + QStringLiteral("default-icons.json"));
        if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 32 * 1024) {
            out.error = damaged;
            return out;
        }
        const QJsonObject document = QJsonDocument::fromJson(manifest.readAll()).object();
        const QJsonObject pages = document.value(QStringLiteral("pages")).toObject();
        if (document.value(QStringLiteral("version")).toInt() != 1 || pages.isEmpty()) {
            out.error = damaged;
            return out;
        }
        QVariantMap byFile;
        for (auto it = pages.begin(); it != pages.end(); ++it) {
            const QString file = it.value().toString();
            if (QFileInfo(file).fileName() != file || !file.endsWith(QLatin1String(".svg"))) {
                out.error = damaged;
                return out;
            }
            if (!byFile.contains(file)) {
                QFile svg(root + file);
                if (!svg.open(QIODevice::ReadOnly) || svg.size() <= 0 || svg.size() > 48 * 1024) {
                    out.error = damaged;
                    return out;
                }
                const QVariantMap asset = shrinkAsset(
                    {{QStringLiteral("mime"), QStringLiteral("image/svg+xml")},
                     {QStringLiteral("data"), QString::fromLatin1(svg.readAll().toBase64())}},
                    QStringLiteral("tab-icon"), 0);
                QString source, why;
                if (!storedAssetSource(asset, QStringLiteral("tab-icon"), &source, &why)) {
                    out.error = damaged;
                    return out;
                }
                byFile.insert(file, asset);
            }
            out.assets.insert(it.key().toCaseFolded(), byFile.value(file));
        }
        return out;
    }();
    if (error) *error = catalog.error;
    if (!catalog.error.isEmpty()) return {};
    QVariantMap icons;
    for (const QVariant &value : tabs) {
        const QString id = value.toMap().value(QStringLiteral("id")).toString().toCaseFolded();
        if (catalog.assets.contains(id)) icons.insert(id, catalog.assets.value(id));
    }
    return icons;
}

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
    const QVariantMap shrunk = shrinkAsset(
        {{QStringLiteral("mime"), mime},
         {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())}},
        kind, artworkBudget(emptyPagePayload(), 0));
    const QByteArray stored = QByteArray::fromBase64(
        shrunk.value(QStringLiteral("data")).toString().toLatin1());
    mime = shrunk.value(QStringLiteral("mime")).toString();
    QString note;
    if (stored.size() < bytes.size())
        note = QStringLiteral(" Minified and stored at %1 bytes (from %2) to keep the saved artwork compact.")
                   .arg(stored.size()).arg(bytes.size());
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("asset"), QVariantMap{
                {QStringLiteral("mime"), mime},
                {QStringLiteral("data"), QString::fromLatin1(stored.toBase64())}}},
            {QStringLiteral("msg"), QStringLiteral("The safe local image is saved in the Settings-panel profile. It will be written only when you apply the rebuild.") + note}};
}

QString recoveryPathFor(const QString &exePath)
{
    return sidecarPath(exePath);
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

// The About page is one resource record with a fixed slot in the player EXE, and
// a custom tab icon or logo is base64-pasted into that same document. The writer
// can only rewrite a record in place or move it into space that is already free
// inside the file (the EXE never grows), so an oversized About page fails the
// whole apply.
//
// Which Qt the *player* runs on, from HD-Player.exe's own import table.
//
// This decides more than it looks like it does. A QML Image cannot load a
// data: URI on Qt 5 - measured, not assumed: with Qt 5.15 every data: URI gets
// status Error while the identical bytes load fine from a file - and a Qt 5
// player also needs the imageformats plugins to have an SVG decoder at all.
// Both only fail at paint time, silently, which is exactly the "saved fine, then
// never appeared" report. Qt 6 loads data: URIs, so the same document works there.
//
// Returns 5, 6, or 0 when the table cannot be read (then the caller reasons about
// both generations rather than assuming one).
int playerQtMajor(const QByteArray &image)
{
    const auto imports = PeUtils::importedModules(image);
    if (!imports.ok) return 0;
    QSet<int> versions;
    const auto add = [&versions](const QStringList &modules, const QString &suffix) {
        for (const auto &module : modules) {
            for (int major : { 5, 6 }) {
                const auto prefix = QStringLiteral("qt%1").arg(major);
                if ((!suffix.isEmpty() && module.compare(prefix + suffix, Qt::CaseInsensitive) == 0)
                    || (suffix.isEmpty() && module.startsWith(prefix, Qt::CaseInsensitive)
                        && module.endsWith(QLatin1String(".dll"), Qt::CaseInsensitive)))
                    versions.insert(major);
            }
        }
    };
    add(imports.modules, QStringLiteral("quicktemplates2.dll"));
    if (versions.isEmpty()) add(imports.modules, QStringLiteral("core.dll"));
    if (versions.isEmpty()) add(imports.modules, QString());
    return versions.size() == 1 ? *versions.cbegin() : 0;
}

// Does this exact About document fit the player's own resource slot? Split out of
// aboutCapacityProblem() because apply() needs the answer while it is still
// choosing between embedding artwork and pointing at it, not only to complain
// afterwards. Returns an empty string when it fits (or when the slot cannot be
// measured, in which case the write is still verified afterwards).
QString aboutFitProblem(const QByteArray &image, const QByteArray &aboutBytes)
{
    QList<EmbeddedResources::Entry> matches;
    for (const auto &entry : EmbeddedResources::list(image, true)) {
        if (entry.format.compare(QLatin1String("qml"), Qt::CaseInsensitive) == 0
            && entry.leaf.compare(kAboutLeaf, Qt::CaseInsensitive) == 0)
            matches << entry;
    }
    if (matches.size() != 1 || matches.first().scanned) return {};
    const auto &entry = matches.first();
    const auto link = QtResources::findLink(image, entry.entryOff, entry.leaf);
    if (!link.found || link.ambiguous) return {};
    const EmbeddedResources::Budget room = EmbeddedResources::budget(image, entry.entryOff, entry.leaf);
    if (!room.known) return {};
    const qint64 payload = EmbeddedResources::payloadSizeFor(aboutBytes, entry.flags);
    if (payload <= room.capacity) return {};
    QString error;
    if (QtResources::findFreeSpace(image, link, payload, &error) >= 0) return {};
    return QStringLiteral("over");
}

// Measure it first and name the culprits: "needs 2495 bytes but its slot only
// holds 2337" leaves the user with no idea which icon caused it.
QString aboutCapacityProblem(const QByteArray &image, const QByteArray &aboutBytes,
                             const QVariantMap &customTabIcons, bool customLogo)
{
    QList<EmbeddedResources::Entry> matches;
    for (const auto &entry : EmbeddedResources::list(image, true)) {
        if (entry.format.compare(QLatin1String("qml"), Qt::CaseInsensitive) == 0
            && entry.leaf.compare(kAboutLeaf, Qt::CaseInsensitive) == 0)
            matches << entry;
    }
    if (matches.size() != 1 || matches.first().scanned) return {};
    const auto &entry = matches.first();
    const auto link = QtResources::findLink(image, entry.entryOff, entry.leaf);
    if (!link.found || link.ambiguous) return {};
    const EmbeddedResources::Budget room = EmbeddedResources::budget(image, entry.entryOff, entry.leaf);
    if (!room.known) return {};
    const qint64 payload = EmbeddedResources::payloadSizeFor(aboutBytes, entry.flags);
    if (payload <= room.capacity) return {};
    QString error;
    if (QtResources::findFreeSpace(image, link, payload, &error) >= 0) return {};
    QStringList culprits;
    for (auto it = customTabIcons.cbegin(); it != customTabIcons.cend(); ++it)
        culprits << QStringLiteral("%1 (%2 KB)")
                        .arg(it.key())
                        .arg(QString::number(it.value().toString().toLatin1().size() / 1024.0, 'f', 1));
    if (customLogo) culprits << QStringLiteral("the About logo");
    return QStringLiteral(
               "The About page needs %1 bytes but its slot in this player only holds %2, and the "
               "rest of the EXE has no verified free space to move it to. Nothing was written. "
               "The generated page includes your About text, links and artwork sources.%3 "
               "Shorten the About text/links, or use a shorter application-data path, then rebuild.")
        .arg(payload)
        .arg(room.capacity)
        .arg(culprits.isEmpty() ? QString()
                                : QStringLiteral(" Tab/logo sources: %1.").arg(culprits.join(
                                      QStringLiteral(", "))));
}

QVariantMap apply(const QString &exePath, const QString &developerName,
                  const QVariantList &links, const QVariantMap &profile, bool enabled)
{
    if (BsDetector::isProcessRunning(QStringLiteral("HD-Player.exe")))
        return fail(QStringLiteral("Close every HD-Player window before rebuilding or restoring the Settings panel."));
    const QString canonical = QFileInfo(exePath).canonicalFilePath();
    if (canonical.isEmpty() || QFileInfo(exePath).suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) != 0)
        return fail(QStringLiteral("Select an existing HD-Player.exe file first."));
    // Serialize staging as well as the final write: a failed concurrent
    // attempt must not delete a file another successful apply just adopted.
    QLockFile lock(canonical + QStringLiteral(".bsi-settings-panel.lock"));
    if (!lock.tryLock(0))
        return fail(QStringLiteral("Another Settings-panel writer is using this executable; retry after it finishes."));
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
        RecoveryProblem restoreProblem = RecoveryProblem::None;
        const QMap<QString, QByteArray> originals = originalsFromRecovery(recovery, &restoreProblem);
        if (originals.isEmpty()) {
            // Restore has nothing safe to write without a complete snapshot, so
            // this stays blocked - but say which part is unusable.
            return fail(restoreProblem == RecoveryProblem::Incomplete
                ? QStringLiteral("The Settings-panel recovery snapshot is incomplete; restore is blocked. Rebuild the panel to save a fresh snapshot.")
                : QStringLiteral("The Settings-panel recovery snapshot failed its integrity check; restore is blocked."));
        }
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
    const QVariantMap overrides = profile.value(QStringLiteral("tabIcons")).toMap();
    if (overrides.size() > 64)
        return fail(QStringLiteral("The Settings-panel profile contains too many custom tab icons."));
    QString packError;
    const QVariantMap packIcons = defaultTabIcons(current.tabs, &packError);
    if (!packError.isEmpty()) return fail(packError);
    // Defaults live in the executable, not in QSettings. Existing custom SVGs
    // override them; removing an override automatically restores its pack icon.
    QVariantMap storedIcons = packIcons;
    for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
        storedIcons.insert(it.key().toCaseFolded(), it.value());
    const QVariantMap storedLogo = profile.value(QStringLiteral("logo")).toMap();
    const bool hasCustomLogo = profile.contains(QStringLiteral("logo"));
    // The slot this player actually has, not the smallest one any build is known
    // to have. Everything below is sized against it.
    const int slotCapacity = aboutSlotCapacity(image, nullptr);
    const int plainPayload = emptyPagePayload();
    // `logoSrc` is the resolved data URI for the About logo (not the stored
    // asset), so the document can be rebuilt with it on every pass.
    const auto buildAssets = [&](int budget, QVariantMap *icons, QVariantMap *iconFiles,
                                 QVariantMap *logoFiles, QString *logoSrc, QString *error) -> bool {
        icons->clear();
        iconFiles->clear();
        logoFiles->clear();
        QString source;
        for (auto it = storedIcons.cbegin(); it != storedIcons.cend(); ++it) {
            const QString id = it.key().toCaseFolded();
            if (!availableTabs.contains(id)) continue; // stale option from another player version
            // Artwork saved by an earlier build never went through shrinkAsset(),
            // so shrink it again here: the budget is a property of this player's
            // slot, not of when the icon was picked.
            const QVariantMap shrunk = shrinkAsset(it.value().toMap(),
                                                   QStringLiteral("tab-icon"), budget);
            if (!storedAssetSource(shrunk, QStringLiteral("tab-icon"), &source, error))
                return false;
            icons->insert(id, source);
            // Keep the saved bytes too: if this icon cannot fit the document the
            // caller writes a runtime copy in the app's data folder and points at it.
            iconFiles->insert(id, shrunk);
        }
        if (hasCustomLogo) {
            const QVariantMap shrunkLogo = shrinkAsset(storedLogo, QStringLiteral("logo"), budget);
            if (!storedAssetSource(shrunkLogo, QStringLiteral("logo"), logoSrc, error))
                return false;
            logoFiles->insert(QStringLiteral("logo"), shrunkLogo);
        }
        return true;
    };
    // Fit the real document: draw the artwork down until the page it produces
    // actually fits this player's slot. The previous code sized each asset
    // against a fixed reservation and never checked the result, so a page that
    // was 26 bytes over was reported as unfixable instead of being shrunk once
    // more. Each pass halves the allowance, so this converges in a few tries.
    //
    // Every pass keeps the smallest document it has managed to build. Bailing
    // out of this loop used to leave customTabIcons empty, which meant the page
    // was written with no artwork at all: the user's icons silently vanished and
    // no error explained it. A low-fidelity icon is always better than none.
    int budget = artworkBudget(plainPayload, slotCapacity);
    QVariantMap bestIcons;
    QVariantMap bestIconFiles;
    QVariantMap bestLogoFiles;
    QString logoSource = current.aboutLogoSource;
    QString bestLogoSource = logoSource;
    qint64 bestPayload = -1;
    for (int pass = 0; pass < 8; ++pass) {
        QVariantMap icons;
        QVariantMap iconFiles;
        QVariantMap logoFiles;
        QString candidateLogo;
        QString assetError;
        if (!buildAssets(budget, &icons, &iconFiles, &logoFiles, &candidateLogo, &assetError)) {
            if (pass == 0) return fail(assetError);
            break;
        }
        const QString candidateLogoSource = hasCustomLogo ? candidateLogo : current.aboutLogoSource;
        const QString doc = buildAbout(developerName, tagline, links, candidateLogoSource,
                                       hiddenTabs, icons);
        const qint64 payload = EmbeddedResources::payloadSizeFor(doc.toUtf8(), 1);
        if (bestPayload < 0 || payload < bestPayload) {
            bestPayload = payload;
            bestIcons = icons;
            bestIconFiles = iconFiles;
            bestLogoFiles = logoFiles;
            bestLogoSource = candidateLogoSource;
        }
        if (slotCapacity <= 0 || payload <= slotCapacity) break;
        if (budget <= 0) break;
        budget = qMax(0, budget / 2);
    }
    // Keep the best attempt either way: aboutCapacityProblem() below still fails
    // the apply with an exact reason if even that does not fit, so nothing is
    // ever written with a truncated page.
    customTabIcons = bestIcons;
    logoSource = bestLogoSource;
    // Identity for the strip delegate. `pQmlSource` is a QML url, and a url
    // stringifies percent-encoded (qrc:%5CSettings%5CPerformanceSetting.qml), so
    // splitting it on a path separator never yielded a leaf name: the lookup
    // returned undefined for every tab and no custom icon was ever drawn. The
    // delegate already knows its own `index`, so key the published map by that
    // instead. No string surgery happens inside the player, and the keys are
    // shorter than leaf names, which buys back bytes in the About slot too.
    customTabIcons = byTabIndex(customTabIcons, current.tabs);
    // Hidden tabs travel in the same map, with an empty value as the sentinel,
    // so re-key them together: converting the icon map alone left an empty map
    // whenever no icons were set, and every hidden tab reappeared.
    if (!hiddenTabs.isEmpty()) {
        QStringList hiddenById;
        for (const QString &id : hiddenTabs) hiddenById << id;
        const QVariantMap keys = byTabIndex([&hiddenById] {
            QVariantMap map;
            for (const QString &id : hiddenById) map.insert(id, QString());
            return map;
        }(), current.tabs);
        hiddenTabs = keys.keys();
    }
    const bool hasPatchMarker = hasWindowMarker(current.window.text)
        || current.about.text.contains(kAboutMarker);
    QMap<QString, QByteArray> originals;
    bool recoveryMatchesCurrent = false;
    // An unusable snapshot is only fatal once the executable carries our
    // markers; on clean sources it is stale state and the block below replaces
    // it with a verified baseline (previously this aborted every apply with
    // "snapshot is incomplete", including on untouched players).
    RecoveryProblem applyProblem = RecoveryProblem::None;
    if (!recovery.isEmpty()) {
        originals = originalsFromRecovery(recovery, &applyProblem);
        recoveryMatchesCurrent = originals.size() == 2
            && originals.contains(current.window.path) && originals.contains(current.about.path);
        if (recoveryMatchesCurrent) {
            const QVariantList records = recovery.value(QStringLiteral("sources")).toList();
            for (const QVariant &value : records) {
                const QVariantMap record = value.toMap();
                const Source *source = record.value(QStringLiteral("path")).toString() == current.window.path
                    ? &current.window : &current.about;
                if (!recoveryAcceptsCurrent(record, source->bytes)) {
                    recoveryMatchesCurrent = false;
                    break;
                }
            }
        }
        // No early return here: a patched player must still reach the guard
        // below, which separates "another patch changed the source" from
        // "the snapshot itself is unusable" and gives the matching advice.
    }
    if (!recoveryMatchesCurrent) {
        if (hasPatchMarker) {
            // Three different situations reach here and they need different
            // advice: a usable snapshot that disagrees with the file means
            // another patch ran on top; an unusable snapshot means the stock
            // backup is the only way back; and a snapshot for other resource
            // paths means this is a different executable altogether.
            const bool snapshotCoversThisExe = originals.size() == 2
                && originals.contains(current.window.path) && originals.contains(current.about.path);
            if (snapshotCoversThisExe)
                return fail(QStringLiteral("A Settings-panel resource was changed by another patch. Restore or inspect it manually before rebuilding."));
            if (applyProblem != RecoveryProblem::None)
                return fail(QStringLiteral("This player carries a Settings-panel patch, but its recovery snapshot is unusable (%1). The executable was not overwritten; restore the player backup before rebuilding.")
                    .arg(applyProblem == RecoveryProblem::Incomplete
                             ? QStringLiteral("incomplete")
                             : QStringLiteral("failed its integrity check")));
            return fail(QStringLiteral("A Settings-panel marker exists without a matching recovery snapshot. The executable was not overwritten."));
        }
        // Recovery files are keyed by executable path. A clean reinstall can
        // put a different, unpatched HD-Player.exe at that same path, leaving
        // the previous install's hashes in AppLocalData. Do not mistake that
        // stale sidecar for an edit to this executable: snapshot the exact
        // current sources as the new baseline before applying the rebuild.
        originals.clear();
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
    const QString rebuiltWindow = buildWindow(originalWindowText);
    if (rebuiltWindow.isEmpty())
        return fail(QStringLiteral("This BlueStacks build's Settings-window structure is not recognized. No file was changed."));
    // The About page carries URLs, while the saved profile retains the
    // editable SVGs. Qt 5 (and unidentified players) get local PNG copies even
    // when data URIs would fit: the URI scheme and the SVG decoder are separate
    // runtime failure points on older players. Always reference a bundled pack:
    // all twelve native tabs then fit the smallest known About slot on either Qt.
    QVariantMap publishedIcons = customTabIcons;
    QString publishedLogoSource = logoSource;
    StagedAssetFiles stagedAssets;
    int referencedAssets = 0;
    const auto fits = [&](const QVariantMap &icons, const QString &logoSrc) {
        return aboutFitProblem(image, buildAbout(developerName, tagline, links, logoSrc,
                                                 hiddenTabs, icons).toUtf8()).isEmpty();
    };
    // buildAssets stores the logo under its own key, not directly in this map.
    // Reading bestLogoFiles as an asset wrote an empty file for every logo.
    QVariantMap fileLogo = bestLogoFiles.value(QStringLiteral("logo")).toMap();
    const QString fallbackPrefix = QStringLiteral("data:image/svg+xml;base64,");
    if (fileLogo.isEmpty() && logoSource.startsWith(fallbackPrefix))
        fileLogo = {{QStringLiteral("mime"), QStringLiteral("image/svg+xml")},
                    {QStringLiteral("data"), logoSource.mid(fallbackPrefix.size())}};
    const int qtMajor = playerQtMajor(image);
    const bool mustReference = qtMajor != 6 || !packIcons.isEmpty();
    if ((mustReference || !fits(publishedIcons, publishedLogoSource))
        && (!bestIconFiles.isEmpty() || !fileLogo.isEmpty())) {
        QVariantMap byFile;
        QString assetError;
        for (auto it = bestIconFiles.cbegin(); it != bestIconFiles.cend(); ++it) {
            const QByteArray png = runtimeAssetPng(it.value().toMap(), true);
            if (png.isEmpty())
                return fail(QStringLiteral("The tab icon for %1 could not be rendered. Choose that SVG again.").arg(it.key()));
            const QString path = writeAssetFile(exePath, png, &stagedAssets, &assetError);
            if (path.isEmpty()) return fail(assetError);
            byFile.insert(it.key(), path);
            ++referencedAssets;
        }
        // File assets are collected by stable page id. The strip only reads
        // numeric indices: mixing these keys with customTabIcons left the old
        // (unloadable on Qt 5) data URI in the numeric slot and never used the
        // newly written file at all.
        byFile = byTabIndex(byFile, current.tabs);
        QString logoByFile = publishedLogoSource;
        if (!fileLogo.isEmpty()) {
            const QByteArray png = runtimeAssetPng(fileLogo, false);
            if (png.isEmpty())
                return fail(QStringLiteral("The About logo could not be rendered. Choose that image again."));
            logoByFile = writeAssetFile(exePath, png, &stagedAssets, &assetError);
            if (logoByFile.isEmpty()) return fail(assetError);
            ++referencedAssets;
        }
        // Never fall back to data URIs after a required file write fails: that
        // reports success but leaves the player's images blank. Size/capacity
        // failures below also leave both the EXE and its old artwork intact.
        publishedIcons = byFile;
        publishedLogoSource = logoByFile;
    }
    const QString rebuiltAbout = buildAbout(developerName, tagline, links, publishedLogoSource,
                                            hiddenTabs, publishedIcons);
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
        if (active == 0) {
            stagedAssets.committed = true;
            return {{QStringLiteral("ok"), true},
                    {QStringLiteral("msg"), QStringLiteral("The requested Settings-panel design is already applied and no stale compiled QML remains active.")}};
        }
        QByteArray invalidated = image;
        QtResources::invalidateUnits(invalidated,
            {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds);
        if (QtResources::activeUnits(invalidated,
                {kWindowLeaf, kAboutLeaf}, kWindowUnitIds + kAboutUnitIds) > 0)
            return fail(QStringLiteral("The Settings/About sources already match, but their embedded compiled QML is still active and could not be disabled."));
        QString error;
        if (!ThemeEngine::backup(exePath) || !PatchFile::writeVerified(exePath, image, invalidated, &error))
            return fail(QStringLiteral("Could not safely disable stale compiled Settings/About QML. %1").arg(error));
        QtResources::clearPlayerQmlCache(exePath);
        stagedAssets.committed = true;
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("msg"), QStringLiteral("The Settings/About QML sources were already rebuilt; %1 stale compiled unit(s) were invalidated and verified. Restart BlueStacks to load the source.").arg(active)}};
    }

    recovery.insert(QStringLiteral("sources"), sourceRecords(current, originals, replacements));
    recovery.insert(QStringLiteral("developerName"), developerName.trimmed());
    if (!saveAtomic(backup, QJsonDocument::fromVariant(recovery).toJson(QJsonDocument::Compact)))
        return fail(QStringLiteral("Could not save the feature-specific recovery snapshot. No executable resources were changed."));

    const QString capacityProblem = aboutCapacityProblem(
        image, replacements.value(current.about.path), publishedIcons,
        profile.contains(QStringLiteral("logo")));
    if (!capacityProblem.isEmpty()) {
        if (!hadRecovery) QFile::remove(backup);
        return fail(capacityProblem);
    }

    const QVariantMap result = applyResources(exePath, operationsFor(replacements));
    if (!result.value(QStringLiteral("ok")).toBool()) {
        // A commit/verification failure can report an uncertain final EXE.
        // Remove staged files and a new snapshot only when read-back proves the
        // original is intact; otherwise retain both for safe manual recovery.
        QByteArray afterFailure;
        const bool unchanged = readBounded(exePath, &afterFailure) && afterFailure == image;
        stagedAssets.committed = !unchanged;
        if (!hadRecovery && unchanged) QFile::remove(backup);
        return result;
    }
    stagedAssets.committed = true;
    QString assetNote;
    if (referencedAssets > 0) {
        assetNote = QStringLiteral(" %1 artwork file(s) are stored as PNG runtime copies in the app's "
                                   "data folder; keep that folder so the player can load them. "
                                   "Your saved SVGs remain editable in the Customizer.")
                        .arg(referencedAssets);
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("msg"), QStringLiteral("The screenshot-sized Settings window, themed selected tabs/icons and customizable About page were applied. A selective recovery snapshot was saved; restart BlueStacks to see it.") + assetNote},
            {QStringLiteral("recovery"), result.value(QStringLiteral("id"))}};
}

} // namespace SettingsPanelPatcher
