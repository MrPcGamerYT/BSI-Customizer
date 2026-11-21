#include "NativeSettingsModel.h"
#include <QFileInfo>
#include <QRegularExpression>

namespace NativeSettingsModel {
bool literalQmlString(const QString &source, int begin, int end, QString *value)
{
    if (!value || begin < 0 || end < begin || end > source.size()) return false;
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

QmlText::Object locate(const QString &playerText, const QHash<QString, int> &pageCounts,
                       QString *error)
{
    QString ignoredError;
    if (!error) error = &ignoredError;
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
                const auto found = pageCounts.constFind(leaf.toCaseFolded());
                if (found == pageCounts.cend() || found.value() != 1) continue;
                ++resolved;
                if (leaf.compare(QStringLiteral("PlrAboutSetting.qml"), Qt::CaseInsensitive) == 0) hasAbout = true;
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
        return {};
    }

    return models.first();
}
} // namespace NativeSettingsModel
