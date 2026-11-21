#include "ShaderSettingsSource.h"
#include "NativeSettingsModel.h"
#include "settings_fixture.h"
#include "legacy_source_fixture.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace ShaderSettingsFixture;
void require(bool condition, const QString &message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", qPrintable(message)); std::exit(1); }
}
void write(const QString &path, const QString &text)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(text.toUtf8()) == text.toUtf8().size(), QStringLiteral("write fixture %1").arg(path));
}
QString read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly) && file.size() < 8 * 1024 * 1024, QStringLiteral("read fixture %1").arg(path));
    return QString::fromUtf8(file.readAll());
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto counts = pageCounts();
    const auto baseWindow = window();
    for (bool persistentModel : {false, true})
    for (bool list : {false, true}) {
        for (bool crlf : {false, true}) {
            QString original = player(list, persistentModel);
            if (crlf) original.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
            const auto plan = ShaderSettingsSource::prepare(original, baseWindow, counts);
            require(plan.ok && !plan.alreadyApplied && plan.tabIndex == 12, QStringLiteral("native row/bootstrap plan: ") + plan.error);
            require(!plan.player.contains(QStringLiteral("import BSIShaders 1.0\n")), QStringLiteral("native root cannot acquire a mandatory shader dependency"));
            const auto repeat = ShaderSettingsSource::prepare(plan.player, plan.window, counts);
            require(repeat.ok && repeat.alreadyApplied && repeat.player == plan.player && repeat.window == plan.window,
                    QStringLiteral("source plan idempotence: ") + repeat.error);
            QString base, error;
            require(ShaderSettingsSource::removePlayer(plan.player, &base, &error) && base == original, QStringLiteral("exact root/source/line-ending restore: ") + error);
            require(ShaderSettingsSource::removeWindow(plan.window, &base, &error) && base == baseWindow, QStringLiteral("exact navigation/dirty/visibility restore: ") + error);
            QString later = plan.player;
            later.replace(QStringLiteral("width:1440"), QStringLiteral("width:1280"));
            QString wanted = original;
            wanted.replace(QStringLiteral("width:1440"), QStringLiteral("width:1280"));
            require(ShaderSettingsSource::removePlayer(later, &base, &error) && base == wanted, QStringLiteral("shader removal must preserve compatible later root edits"));
            QString damaged = plan.player;
            damaged.replace(QStringLiteral("'bsiTabIndex':12"), QStringLiteral("'bsiTabIndex':11"));
            require(!ShaderSettingsSource::prepare(damaged, plan.window, counts).ok, QStringLiteral("live row index conflicts cannot be overwritten"));
            damaged = plan.player;
            damaged.replace(QStringLiteral("pIsVisible:iAppwin.bsiShaderReady"), QStringLiteral("pIsVisible:true"));
            require(!ShaderSettingsSource::removePlayer(damaged, &base, &error), QStringLiteral("manual integration edits block removal"));
            require(!ShaderSettingsSource::prepare(plan.player, baseWindow, counts).ok, QStringLiteral("partial installation is not an idempotent success"));
            require(!ShaderSettingsSource::removeWindow(plan.window + QStringLiteral("\nproperty bool bsiShaderUnknown:true"), &base, &error), QStringLiteral("unknown owned identifiers block removal"));
            for (const auto &fragment : {QStringLiteral("property bool bsiShaderDirty2:false"), QStringLiteral("||bsiShaderDirty2"),
                                        QStringLiteral("active:modelData.pIsVisible!==false;"), QStringLiteral("modelData.pIsVisible===false?'':"),
                                        QStringLiteral(";bsiShaderDirty2=false;")}) {
                damaged = plan.window;
                const int at = damaged.indexOf(fragment);
                require(at >= 0, QStringLiteral("compact owned-fragment fixture must exist: ") + fragment);
                damaged.replace(at, fragment.size(), fragment.left(fragment.size() - 1));
                require(!ShaderSettingsSource::removeWindow(damaged, &base, &error), QStringLiteral("an edited compact owned fragment cannot be removed: ") + fragment);
                require(!ShaderSettingsSource::removeWindow(plan.window + fragment, &base, &error), QStringLiteral("duplicate compact owned fragments cannot be removed: ") + fragment);
            }
        }
    }
    {
        // v18 changes the material, not the native icon/Loader/dirty-state
        // anchors. Retain v17 compatibility and exact removal on both designs.
        QString glass = baseWindow;
        glass.replace(QStringLiteral("//BSI v17"), QStringLiteral("//BSI v18"));
        const auto plan = ShaderSettingsSource::prepare(player(), glass, counts);
        require(plan.ok && plan.tabIndex == 12, QStringLiteral("glass Settings shader integration: ") + plan.error);
        QString restored, error;
        require(ShaderSettingsSource::removeWindow(plan.window, &restored, &error) && restored == glass,
                QStringLiteral("exact v18 glass window removal: ") + error);
        const auto repeat = ShaderSettingsSource::prepare(plan.player, plan.window, counts);
        require(repeat.ok && repeat.alreadyApplied && repeat.window == plan.window,
                QStringLiteral("v18 glass shader idempotence: ") + repeat.error);
        glass.replace(QStringLiteral("//BSI v18"), QStringLiteral("//BSI v19"));
        const auto current = ShaderSettingsSource::prepare(player(), glass, counts);
        require(current.ok && current.tabIndex == 12, QStringLiteral("safe-material v19 shader integration: ") + current.error);
        require(ShaderSettingsSource::removeWindow(current.window, &restored, &error) && restored == glass,
                QStringLiteral("exact v19 shader removal: ") + error);
        const auto currentRepeat = ShaderSettingsSource::prepare(current.player, current.window, counts);
        require(currentRepeat.ok && currentRepeat.alreadyApplied && currentRepeat.window == current.window,
                QStringLiteral("v19 shader idempotence: ") + currentRepeat.error);
        glass.replace(QStringLiteral("//BSI v19"), QStringLiteral("//BSI v20"));
        const auto isolated = ShaderSettingsSource::prepare(player(), glass, counts);
        require(isolated.ok && isolated.tabIndex == 12, QStringLiteral("page-isolated v20 shader integration: ") + isolated.error);
        require(ShaderSettingsSource::removeWindow(isolated.window, &restored, &error) && restored == glass,
                QStringLiteral("exact v20 shader removal: ") + error);
        const auto isolatedRepeat = ShaderSettingsSource::prepare(isolated.player, isolated.window, counts);
        require(isolatedRepeat.ok && isolatedRepeat.alreadyApplied && isolatedRepeat.window == isolated.window,
                QStringLiteral("v20 shader idempotence: ") + isolatedRepeat.error);
        glass.replace(QStringLiteral("//BSI v20"), QStringLiteral("//BSI v21"));
        const auto flat = ShaderSettingsSource::prepare(player(), glass, counts);
        require(flat.ok && flat.tabIndex == 12, QStringLiteral("flat-native-theme v21 shader integration: ") + flat.error);
        require(ShaderSettingsSource::removeWindow(flat.window, &restored, &error) && restored == glass,
                QStringLiteral("exact v21 shader removal: ") + error);
        const auto flatRepeat = ShaderSettingsSource::prepare(flat.player, flat.window, counts);
        require(flatRepeat.ok && flatRepeat.alreadyApplied && flatRepeat.window == flat.window,
                QStringLiteral("v21 shader idempotence: ") + flatRepeat.error);
        glass.replace(QStringLiteral("//BSI v21"), QStringLiteral("//BSI v22"));
        require(!ShaderSettingsSource::prepare(player(), glass, counts).ok,
                QStringLiteral("unknown future Settings designs must not be guessed"));
    }
    {
        QString restored, error;
        require(ShaderSettingsSource::hasPlayer(ShaderSettingsLegacyFixture::player)
                    && ShaderSettingsSource::hasWindow(ShaderSettingsLegacyFixture::window),
                QStringLiteral("the prior planning format remains detectable"));
        require(ShaderSettingsSource::removePlayer(ShaderSettingsLegacyFixture::player, &restored, &error) && restored == player(),
                QStringLiteral("exact prior-format root removal: ") + error);
        require(ShaderSettingsSource::removeWindow(ShaderSettingsLegacyFixture::window, &restored, &error) && restored == baseWindow,
                QStringLiteral("exact prior-format window removal: ") + error);
        require(!ShaderSettingsSource::prepare(ShaderSettingsLegacyFixture::player, ShaderSettingsLegacyFixture::window, counts).ok,
                QStringLiteral("prior planning versions must be removed explicitly before a v2 install, not silently migrated"));
    }
    auto missing = counts;
    missing.remove(QStringLiteral("uigraphicssetting.qml"));
    require(!ShaderSettingsSource::prepare(player(), baseWindow, missing).ok, QStringLiteral("missing native pages are refused"));
    auto duplicate = counts;
    duplicate[QStringLiteral("plraboutsetting.qml")] = 2;
    require(!ShaderSettingsSource::prepare(player(), baseWindow, duplicate).ok, QStringLiteral("ambiguous registered pages are refused"));
    QString wrong = player();
    wrong.replace(QStringLiteral("pQmlSource:'UiGraphicsSetting.qml'"), QStringLiteral("pQmlSource:someRuntimeExpression"));
    require(!ShaderSettingsSource::prepare(wrong, baseWindow, counts).ok, QStringLiteral("dynamic native page paths are refused"));
    wrong = player();
    wrong.replace(QStringLiteral("Component.onCompleted:{pCompLoaded=true}"), QStringLiteral("Component.onCompleted:pCompLoaded=true"));
    require(!ShaderSettingsSource::prepare(wrong, baseWindow, counts).ok, QStringLiteral("unrecognized startup lifecycle is refused"));
    QString withDecoy = player();
    withDecoy.insert(withDecoy.lastIndexOf(QLatin1Char('}')), QStringLiteral("\nComponent{id:unrelated;SettingElementModelList{SettingElementModel{pQmlSource:'NotASettingsPage.qml'}}}\n"));
    require(ShaderSettingsSource::prepare(withDecoy, baseWindow, counts).ok, QStringLiteral("detached model prototypes cannot capture the shader row"));
    QString nested = player();
    nested.replace(QStringLiteral("pQmlSource:'UiGraphicsSetting.qml'"),
                   QStringLiteral("pQmlSource:'UiGraphicsSetting.qml';property QtObject preview:SettingElementModel{pQmlSource:'NotASettingsPage.qml'}"));
    const auto nestedPlan = ShaderSettingsSource::prepare(nested, baseWindow, counts);
    require(nestedPlan.ok && nestedPlan.tabIndex == 12, QStringLiteral("nested page prototypes are not extra live tab rows: ") + nestedPlan.error);
    nested = player();
    nested.replace(QStringLiteral("id:iAppwin"), QStringLiteral("Item{id:iAppwin}"));
    require(!ShaderSettingsSource::prepare(nested, baseWindow, counts).ok, QStringLiteral("a child id cannot impersonate the root window"));
    nested = player();
    nested.replace(QStringLiteral("Component.onCompleted:{pCompLoaded=true}"), QStringLiteral("Item{Component.onCompleted:{pCompLoaded=true}}"));
    require(!ShaderSettingsSource::prepare(nested, baseWindow, counts).ok, QStringLiteral("a nested completion handler cannot bootstrap native player startup"));
    nested = player();
    nested.replace(QStringLiteral("Component.onCompleted:{pCompLoaded=true}"), QStringLiteral("Component.onCompleted:{pCompLoaded=true}\nComponent.onCompleted:{}"));
    require(!ShaderSettingsSource::prepare(nested, baseWindow, counts).ok, QStringLiteral("duplicate direct completion handlers are refused"));
    nested = player();
    nested.replace(QStringLiteral("pQmlSource:'UiGraphicsSetting.qml'"), QStringLiteral("property QtObject preview:QtObject{property string pQmlSource:'UiGraphicsSetting.qml'}"));
    require(!ShaderSettingsSource::prepare(nested, baseWindow, counts).ok, QStringLiteral("a nested source cannot impersonate its containing native row"));
    nested = player();
    nested.insert(nested.lastIndexOf(QLatin1Char('}')), QStringLiteral("property string templateText:`unparsed ${Object.keys({})}`\n"));
    require(!ShaderSettingsSource::prepare(nested, baseWindow, counts).ok, QStringLiteral("unsupported template-literal grammar cannot confuse native object bounds"));
    QString array = player(true);
    array.replace(QStringLiteral("pSettingElements:[\n"), QStringLiteral("pSettingElements:[\nunknownRuntimeRow,\n"));
    require(!ShaderSettingsSource::prepare(array, baseWindow, counts).ok, QStringLiteral("array expressions cannot silently change the appended row index"));
    array = player(true);
    array.replace(QStringLiteral("]\n}}"), QStringLiteral(",]\n}}"));
    require(!ShaderSettingsSource::prepare(array, baseWindow, counts).ok, QStringLiteral("an unsupported trailing separator is refused rather than emitting invalid QML"));
    QString corruptWindow = baseWindow;
    corruptWindow.replace(QStringLiteral("onClosing:{if(uiBackend.confIsModified())prompt()}"), QStringLiteral("Item{onClosing:{if(uiBackend.confIsModified())prompt()}}"));
    require(!ShaderSettingsSource::prepare(player(), corruptWindow, counts).ok, QStringLiteral("a nested close handler cannot capture the dirty predicate"));
    corruptWindow = baseWindow;
    corruptWindow.replace(QStringLiteral("function fChangeSelectedSetting(index){if(uiBackend.confIsModified())prompt()}"),
                          QStringLiteral("function fChangeSelectedSetting(index){}\nfunction unrelated(){if(uiBackend.confIsModified())prompt()}"));
    require(!ShaderSettingsSource::prepare(player(), corruptWindow, counts).ok, QStringLiteral("dirty predicates outside the native navigation function are refused"));
    corruptWindow = baseWindow;
    corruptWindow.replace(QStringLiteral("function fChangeSelectedSetting(index)"), QStringLiteral("function fChangeSelectedSetting(index){}\nfunction fChangeSelectedSetting(index)"));
    require(!ShaderSettingsSource::prepare(player(), corruptWindow, counts).ok, QStringLiteral("duplicate native navigation methods cannot be patched by first-match guessing"));
    corruptWindow = baseWindow;
    corruptWindow.replace(QStringLiteral("property var c:iSettingsWindowShadowContainer.bIcons[index]"),
                          QStringLiteral("/*property var c:iSettingsWindowShadowContainer.bIcons[index]*/"));
    require(!ShaderSettingsSource::prepare(player(), corruptWindow, counts).ok, QStringLiteral("commented-out shell anchors cannot impersonate actual bindings"));
    QString decoded;
    require(!NativeSettingsModel::literalQmlString(QStringLiteral("'A.qml'"), -1, 7, &decoded)
                && !NativeSettingsModel::literalQmlString(QStringLiteral("'A.qml'"), 0, 100, &decoded)
                && !NativeSettingsModel::literalQmlString(QStringLiteral("'A.qml'"), 0, 7, nullptr),
            QStringLiteral("shared literal reader refuses invalid bounds and missing output"));
    require(NativeSettingsModel::locate(QString(), counts, nullptr).nameBegin < 0, QStringLiteral("optional discovery diagnostics cannot crash on an unsupported model"));
    if (app.arguments().size() == 4 && (app.arguments().at(1) == QLatin1String("--emit")
                                      || app.arguments().at(1) == QLatin1String("--emit-persistent"))) {
        const QString directory = app.arguments().at(2);
        const auto nativeWindow = read(app.arguments().at(3));
        const auto nativePlayer = player(false, app.arguments().at(1) == QLatin1String("--emit-persistent"));
        const auto plan = ShaderSettingsSource::prepare(nativePlayer, nativeWindow, counts);
        require(plan.ok, QStringLiteral("complete native Settings-window integration: ") + plan.error);
        require(QDir().mkpath(directory), QStringLiteral("create integration fixture directory"));
        write(QDir(directory).filePath(QStringLiteral("Player.qml")), plan.player);
        write(QDir(directory).filePath(QStringLiteral("UiSettingsWindow.qml")), plan.window);
        write(QDir(directory).filePath(QStringLiteral("PlayerBase.qml")), nativePlayer);
    }
    std::puts("PASS: shared live native-model discovery, optional normal-DLL bootstrap, appended shader-only row, native discard/close predicates, hidden/unloaded missing-module path, idempotence and exact reversible source surgery (LF/CRLF and both model forms)");
    std::puts("LIMIT: source planning and Qt contract fixtures are not a running player, DLL deployment or physical GPU certification");
    return 0;
}
