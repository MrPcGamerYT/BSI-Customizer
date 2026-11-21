#include "ShaderSettingsSource.h"
#include "NativeSettingsModel.h"
#include "QmlText.h"
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>

namespace ShaderSettingsSource {
namespace {
const QString rowStart = QStringLiteral("/*BSI-SHADERS-ROW:1*/");
const QString rowEnd = QStringLiteral("/*BSI-SHADERS-ROW-END*/");
// Retain exact removal of the earlier, never-deployed planning format.
namespace legacy {
const QString bootStart = QStringLiteral("/*BSI-SHADERS-BOOT:1*/");
const QString windowStart = QStringLiteral("/*BSI-SHADERS-WINDOW:1*/");
const QString startCall = QStringLiteral("/*BSI-SHADERS-START:1*/fBsiStartShaders();/*BSI-SHADERS-START-END*/");
const QString dirtySuffix = QStringLiteral("/*BSI-SHADERS-DIRTY:1*/||bsiShaderDirty/*BSI-SHADERS-DIRTY-END*/");
const QString visibilityGate = QStringLiteral("/*BSI-SHADERS-VISIBLE:1*/modelData.pIsVisible===false?'':/*BSI-SHADERS-VISIBLE-END*/");
const QString pageGate = QStringLiteral("/*BSI-SHADERS-PAGE:1*/active:modelData.pIsVisible!==false;/*BSI-SHADERS-PAGE-END*/");
const QString closeReset = QStringLiteral("/*BSI-SHADERS-CLOSE:1*/;bsiShaderDirty=false;/*BSI-SHADERS-CLOSE-END*/");
const QString windowProperty = QStringLiteral("/*BSI-SHADERS-WINDOW:1*/\nproperty bool bsiShaderDirty:false\n/*BSI-SHADERS-WINDOW-END*/\n");
const QString boot = QStringLiteral(R"QML(/*BSI-SHADERS-BOOT:1*/
property bool bsiShaderReady:false
property var bsiShaderBootstrap:null
function fBsiStartShaders(){
try{
bsiShaderBootstrap=Qt.createQmlObject('import BSIShaders 1.0; Bootstrap{hostWindow:iAppwin;instanceName:plrBackend.getVmName();graphicsRenderer:uiBackend.confGetInstanceString("graphics_renderer")}',iAppwin,'BsiShaderBootstrap')
bsiShaderReady=bsiShaderBootstrap!==null
}catch(error){console.warn('BSI shaders unavailable; native player startup continues: '+error)}
}
/*BSI-SHADERS-BOOT-END*/
)QML");
} // namespace legacy
const QString bootStart = QStringLiteral("/*BSI-SHADERS-BOOT:2*/");
// The versioned property is the v2 window marker. Exact fragment/count checks
// below still protect every owned edit. Per-expression comments are redundant
// and consumed scarce fixed slots; no original native token/comment is removed.
const QString windowStart = QStringLiteral("property bool bsiShaderDirty2:false");
const QString windowProperty = windowStart + QLatin1Char('\n');
const QString dirtySuffix = QStringLiteral("||bsiShaderDirty2");
const QString visibilityGate = QStringLiteral("modelData.pIsVisible===false?'':");
const QString pageGate = QStringLiteral("active:modelData.pIsVisible!==false;");
const QString closeReset = QStringLiteral(";bsiShaderDirty2=false;");
const QString boot = bootStart + QStringLiteral("\nproperty bool bsiShaderReady:false\n/*BSI-SHADERS-BOOT-END*/\n");
// Keep this an optional caught import, outside DllMain. Parent ownership keeps
// Bootstrap alive; a duplicate JS reference/function is unnecessary. Missing
// modules leave startup running and the shader row/page inactive. The complete
// original startup block is restored by removing this exact owned insertion.
const QString startCall = QStringLiteral(R"QML(/*BSI-SHADERS-START:2*/try{bsiShaderReady=!!Qt.createQmlObject('import BSIShaders 1.0;Bootstrap{hostWindow:iAppwin;instanceName:plrBackend.getVmName();graphicsRenderer:uiBackend.confGetInstanceString("graphics_renderer")}',iAppwin)}catch(e){console.warn(e)}/*BSI-SHADERS-START-END*/)QML");
QString row(int index, bool list)
{
    return rowStart + (list ? QStringLiteral(",\n") : QStringLiteral("\n"))
        + QStringLiteral("SettingElementModel{pButtonText:qsTr('Shaders');pQmlSource:'qrc:/bsi-shaders/BsiShadersSetting.qml';pIsVisible:iAppwin.bsiShaderReady;pComponentObj:QtObject{}\npComponentArgs:{'pParentWindow':'plr','pVmName':plrBackend.getVmName(),'bsiTabIndex':%1,'bsiPalette':UiTheme.colors,'bsiFont':UiTheme.fonts.bodyMedium}}")
              .arg(index)
        + rowEnd;
}
bool refuse(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool reserved(const QString &text)
{
    return text.contains(QLatin1String("BSI-SHADERS-"))
        || QRegularExpression(QStringLiteral("\\b(?:bsiShader[A-Za-z0-9_]*|fBsiStartShaders)\\b"))
               .match(QmlText::mask(text)).hasMatch();
}
int depthAt(const QString &masked, int begin, int end)
{
    int depth = 0;
    for (int i = begin; i < end; ++i) {
        if (masked.at(i) == QLatin1Char('{')) ++depth;
        else if (masked.at(i) == QLatin1Char('}')) --depth;
    }
    return depth;
}
int blockEnd(const QString &masked, int open)
{
    int depth = 1;
    for (int i = open + 1; i < masked.size(); ++i) {
        if (masked.at(i) == QLatin1Char('{')) ++depth;
        else if (masked.at(i) == QLatin1Char('}') && --depth == 0) return i;
    }
    return -1;
}
bool directBinding(const QString &masked, const QmlText::Object &object,
                   const QString &name, QmlText::Binding *output)
{
    QList<QmlText::Binding> matches;
    QmlText::Binding binding;
    int next = object.open + 1;
    while (QmlText::binding(masked, next, object.end - 1, name, &binding)) {
        if (depthAt(masked, object.open + 1, binding.nameBegin) == 0) matches << binding;
        next = qMax(binding.nameEnd, binding.valueEnd);
    }
    if (matches.size() != 1) return false;
    *output = matches.first();
    return true;
}
bool root(const QString &text, const QStringList &types, const QString &id,
          QmlText::Object *output, QString *error)
{
    const QString masked = QmlText::mask(text);
    QList<QmlText::Object> found;
    for (const auto &type : types) {
        for (const auto &object : QmlText::objects(masked, type)) {
            QmlText::Binding binding;
            if (directBinding(masked, object, QStringLiteral("id"), &binding)
                && masked.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin).trimmed() == id)
                found << object;
        }
    }
    if (found.size() != 1)
        return refuse(error, QStringLiteral("The native window root %1 is missing or ambiguous; no shader integration was prepared.").arg(id));
    *output = found.first();
    return true;
}
bool exactRemove(QString *text, const QString &fragment, int count, QString *error)
{
    if (text->count(fragment) != count)
        return refuse(error, QStringLiteral("A native shader integration fragment is missing, duplicated or edited; refusing to overwrite it."));
    text->remove(fragment);
    return true;
}
// A strict literal reader for native page URLs, including the escaped Windows
// separators used by older players. Expressions/template strings are refused.
bool literal(const QString &text, const QmlText::Binding &binding, QString *url)
{
    QString source = text.mid(binding.valueBegin, binding.valueEnd - binding.valueBegin).trimmed();
    if (source.size() < 2 || (source.front() != QLatin1Char('\'') && source.front() != QLatin1Char('"'))
        || source.back() != source.front()) return false;
    const QChar quote = source.front();
    QString value;
    for (int i = 1; i < source.size() - 1; ++i) {
        QChar c = source.at(i);
        if (c == QLatin1Char('\\')) {
            if (++i >= source.size() - 1) return false;
            c = source.at(i);
            if (c != QLatin1Char('\\') && c != quote && c != QLatin1Char('/')) return false;
        } else if (c == quote || c == QLatin1Char('\n') || c == QLatin1Char('\r')) return false;
        value += c;
    }
    *url = value;
    return true;
}
}
QString pageUrl() { return QStringLiteral("qrc:/bsi-shaders/BsiShadersSetting.qml"); }
bool hasPlayer(const QString &text) { return text.contains(bootStart) || text.contains(legacy::bootStart); }
bool hasWindow(const QString &text) { return text.contains(windowStart) || text.contains(legacy::windowStart); }
bool hasFragments(const QString &text) { return reserved(text); }

bool removePlayer(const QString &text, QString *base, QString *error)
{
    QString output = text;
    if (output.count(rowStart) != 1 || output.count(rowEnd) != 1)
        return refuse(error, QStringLiteral("The native shader Settings row is missing or duplicated."));
    const int begin = output.indexOf(rowStart);
    const int end = output.indexOf(rowEnd, begin) + rowEnd.size();
    if (end <= begin) return refuse(error, QStringLiteral("The native shader Settings row is incomplete."));
    const QString fragment = output.mid(begin, end - begin);
    const auto index = QRegularExpression(QStringLiteral("'bsiTabIndex':([0-9]{1,2}),")).match(fragment);
    if (!index.hasMatch() || (fragment != row(index.captured(1).toInt(), false)
                              && fragment != row(index.captured(1).toInt(), true)))
        return refuse(error, QStringLiteral("The native shader Settings row was edited; automatic removal is blocked."));
    output.remove(begin, end - begin);
    const bool old = text.contains(legacy::bootStart);
    if (!exactRemove(&output, old ? legacy::boot : boot, 1, error)
        || !exactRemove(&output, old ? legacy::startCall : startCall, 1, error)) return false;
    if (reserved(output)) return refuse(error, QStringLiteral("Unexpected shader identifiers remain in the player root; no source was changed."));
    *base = output;
    return true;
}

bool removeWindow(const QString &text, QString *base, QString *error)
{
    QString output = text;
    const bool old = text.contains(legacy::windowStart);
    if (!exactRemove(&output, old ? legacy::windowProperty : windowProperty, 1, error)
        || !exactRemove(&output, old ? legacy::dirtySuffix : dirtySuffix, 2, error)
        || !exactRemove(&output, old ? legacy::visibilityGate : visibilityGate, 1, error)
        || !exactRemove(&output, old ? legacy::pageGate : pageGate, 1, error)
        || !exactRemove(&output, old ? legacy::closeReset : closeReset, 1, error)) return false;
    if (reserved(output)) return refuse(error, QStringLiteral("Unexpected shader identifiers remain in the Settings window; no source was changed."));
    *base = output;
    return true;
}

bool addWindow(const QString &base, QString *output, QString *error)
{
    if (reserved(base)) return refuse(error, QStringLiteral("The Settings window already contains shader identifiers; remove or reconcile that integration first."));
    QmlText::Object window;
    if (!root(base, {QStringLiteral("Window")}, QStringLiteral("iSettingsWindow"), &window, error)) return false;
    const QString masked = QmlText::mask(base);
    // Both native predicates must retain their original meaning. Merely adding
    // a pEnableSave property does not participate in native tab/close prompts.
    QList<QRegularExpressionMatch> navigation;
    auto handlers = QRegularExpression(QStringLiteral("\\bfunction\\s+fChangeSelectedSetting\\s*\\([^)]*\\)\\s*\\{")).globalMatch(masked);
    while (handlers.hasNext()) {
        const auto handler = handlers.next();
        if (handler.capturedStart() > window.open && handler.capturedStart() < window.end
            && depthAt(masked, window.open + 1, handler.capturedStart()) == 0) navigation << handler;
    }
    QmlText::Binding closing;
    if (navigation.size() != 1 || !directBinding(masked, window, QStringLiteral("onClosing"), &closing))
        return refuse(error, QStringLiteral("Native Settings navigation/discard handlers are not recognized."));
    const auto changed = navigation.first();
    const QRegularExpression modified(QStringLiteral("\\buiBackend\\s*\\.\\s*confIsModified\\s*\\(\\s*\\)"));
    QList<int> positions;
    auto calls = modified.globalMatch(masked);
    while (calls.hasNext()) positions << calls.next().capturedEnd();
    if (positions.size() != 2 || positions.first() <= changed.capturedEnd()
        || positions.first() >= blockEnd(masked, changed.capturedEnd() - 1)
        || positions.last() < closing.valueBegin || positions.last() >= closing.valueEnd)
        return refuse(error, QStringLiteral("Native Settings dirty-state checks are not the supported tab/close pair; refusing to guess."));
    const QString discardAnchor = QStringLiteral("uiBackend.confDiscard()");
    const QString iconAnchor = QStringLiteral("property var c:iSettingsWindowShadowContainer.bIcons[index]");
    const QString loaderAnchor = QStringLiteral("delegate:Loader{Component.onCompleted:setSource(modelData.pQmlSource,");
    for (const auto &anchor : {iconAnchor, loaderAnchor, discardAnchor}) {
        const int at = masked.indexOf(anchor);
        if (base.count(anchor) != 1 || masked.count(anchor) != 1 || at <= window.open || at >= window.end)
            return refuse(error, QStringLiteral("The top-tab/page-loading contract is not recognized; native shader startup remains unchanged."));
    }
    QString result = base;
    result.insert(window.end - 1, windowProperty);
    for (auto it = positions.crbegin(); it != positions.crend(); ++it) result.insert(*it, dirtySuffix);
    result.replace(iconAnchor, QStringLiteral("property var c:") + visibilityGate + QStringLiteral("iSettingsWindowShadowContainer.bIcons[index]"));
    result.replace(loaderAnchor, QStringLiteral("delegate:Loader{") + pageGate + QStringLiteral("Component.onCompleted:setSource(modelData.pQmlSource,"));
    result.replace(discardAnchor, discardAnchor + closeReset);
    QString verified;
    if (!removeWindow(result, &verified, error) || verified != base) return false;
    *output = result;
    return true;
}

Plan prepare(const QString &player, const QString &window, const QHash<QString, int> &pageCounts)
{
    Plan plan;
    QString basePlayer = player, baseWindow = window;
    if (hasPlayer(player) || hasWindow(window)) {
        if (!hasPlayer(player) || !hasWindow(window)
            || !removePlayer(player, &basePlayer, &plan.error)
            || !removeWindow(window, &baseWindow, &plan.error)) {
            if (plan.error.isEmpty()) plan.error = QStringLiteral("Only part of the native shader integration is present. No file was changed.");
            return plan;
        }
        plan.alreadyApplied = true;
    }
    if (reserved(basePlayer) || reserved(baseWindow)) {
        plan.error = QStringLiteral("Reserved shader identifiers already exist in the native sources; no file was changed.");
        return plan;
    }
    if (QmlText::mask(basePlayer).contains(QLatin1Char('`')) || QmlText::mask(baseWindow).contains(QLatin1Char('`'))) {
        plan.error = QStringLiteral("JavaScript template literals are outside the supported native QML grammar; no shader integration was prepared.");
        return plan;
    }
    // Keep the approved horizontal native Settings design; do not resurrect the
    // old sidebar, stretch forms or replace Graphics/About with shader controls.
    if ((!baseWindow.contains(QLatin1String("//BSI v17"))
         && !baseWindow.contains(QLatin1String("//BSI v18"))
         && !baseWindow.contains(QLatin1String("//BSI v19"))
         && !baseWindow.contains(QLatin1String("//BSI v20"))
         && !baseWindow.contains(QLatin1String("//BSI v21")))
        || !baseWindow.contains(QLatin1String("property var bIcons:"))) {
        plan.error = QStringLiteral("Apply the current top-tab Settings-panel rebuild before installing the shader category.");
        return plan;
    }
    QmlText::Object app;
    if (!root(basePlayer, {QStringLiteral("ApplicationWindow"), QStringLiteral("Window")}, QStringLiteral("iAppwin"), &app, &plan.error)) return plan;
    const auto model = NativeSettingsModel::locate(basePlayer, pageCounts, &plan.error);
    if (model.nameBegin < 0) return plan;
    if (model.nameBegin <= app.open || model.end >= app.end) {
        plan.error = QStringLiteral("The Settings model is outside the native player root; no shader category was added to a detached window.");
        return plan;
    }
    const QString masked = QmlText::mask(basePlayer);
    QmlText::Binding elements;
    const bool list = directBinding(masked, model, QStringLiteral("pSettingElements"), &elements);
    const int begin = list ? elements.valueBegin : model.open + 1;
    const int end = list ? elements.valueEnd : model.end - 1;
    if (list && (masked.at(begin) != QLatin1Char('[') || masked.at(end - 1) != QLatin1Char(']'))) {
        plan.error = QStringLiteral("The live native Settings list is not a literal object list.");
        return plan;
    }
    int lastRow = -1, count = 0, listCursor = begin + 1;
    bool about = false;
    QSet<QString> seen;
    for (const auto &entry : QmlText::objects(masked, QStringLiteral("SettingElementModel"))) {
        if (entry.nameBegin < begin || entry.end > end || depthAt(masked, begin, entry.nameBegin) != 0) continue;
        if (list && masked.mid(listCursor, entry.nameBegin - listCursor).trimmed()
                != (count ? QStringLiteral(",") : QString())) {
            plan.error = QStringLiteral("The native Settings array contains an unsupported expression or row; no index was guessed.");
            return plan;
        }
        listCursor = entry.end;
        QmlText::Binding source;
        QString url;
        if (!directBinding(masked, entry, QStringLiteral("pQmlSource"), &source)
            || !literal(basePlayer, source, &url)) {
            plan.error = QStringLiteral("A live native Settings row has no unambiguous literal page source.");
            return plan;
        }
        url.replace(QLatin1Char('\\'), QLatin1Char('/'));
        const QString leaf = QFileInfo(url).fileName().toCaseFolded();
        if (pageCounts.value(leaf) != 1 || seen.contains(leaf)) {
            plan.error = QStringLiteral("A live native Settings page is missing, duplicated or ambiguous: %1.").arg(leaf);
            return plan;
        }
        seen.insert(leaf);
        about = about || leaf == QLatin1String("plraboutsetting.qml");
        lastRow = entry.end;
        ++count;
    }
    if (list && !masked.mid(listCursor, end - 1 - listCursor).trimmed().isEmpty()) {
        plan.error = QStringLiteral("The native Settings array has an unsupported trailing expression or separator; no shader row was inserted.");
        return plan;
    }
    if (count < 8 || count > 32 || !about) {
        plan.error = QStringLiteral("The native Settings model is incomplete or oversized; no shader row was added.");
        return plan;
    }
    QmlText::Binding completed;
    if (!directBinding(masked, app, QStringLiteral("Component.onCompleted"), &completed)
        || masked.at(completed.valueBegin) != QLatin1Char('{')) {
        plan.error = QStringLiteral("The native player root has no recognized completion block for safe shader startup.");
        return plan;
    }
    struct Insertion { int position; QString text; };
    QList<Insertion> insertions{{app.end - 1, boot}, {lastRow, row(count, list)}, {completed.valueBegin + 1, startCall}};
    std::sort(insertions.begin(), insertions.end(), [](const Insertion &a, const Insertion &b) { return a.position > b.position; });
    plan.player = basePlayer;
    for (const auto &insertion : insertions) plan.player.insert(insertion.position, insertion.text);
    if (!addWindow(baseWindow, &plan.window, &plan.error)) return plan;
    QString verified;
    if (!removePlayer(plan.player, &verified, &plan.error) || verified != basePlayer) return plan;
    if (plan.alreadyApplied && (plan.player != player || plan.window != window)) {
        plan.error = QStringLiteral("The installed shader integration no longer matches its live native Settings model; no file was changed.");
        return plan;
    }
    plan.tabIndex = count;
    plan.ok = true;
    return plan;
}
} // namespace ShaderSettingsSource
