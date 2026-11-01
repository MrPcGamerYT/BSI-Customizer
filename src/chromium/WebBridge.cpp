#include <cmath>
#include "WebBridge.h"

#include "core/AuthManager.h"
#include "core/Backend.h"
#include "core/SettingsManager.h"
#include "core/ThemeManager.h"
#include "core/UpdateChecker.h"
#include <QColor>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QTimer>
#include <cmath>
#include <exception>
#include <new>

namespace {
QVariantMap failure(const QString &message) { return {{"ok", false}, {"error", message}}; }
QVariantMap success(const QVariant &value = QVariant()) { return {{"ok", true}, {"value", value}}; }
bool fileRole(const QString &role) {
    return role == QLatin1String("exe") || role == QLatin1String("dll")
        || role == QLatin1String("conf") || role == QLatin1String("replace") || role == QLatin1String("extract")
        || role == QLatin1String("profile-open") || role == QLatin1String("profile-save")
        || role == QLatin1String("report-save") || role == QLatin1String("image-open")
        || role == QLatin1String("image-save") || role == QLatin1String("video-open")
        || role == QLatin1String("disk-open") || role == QLatin1String("disk-source")
        || role == QLatin1String("disk-extract") || role == QLatin1String("disk-folder");
}
}

WebBridge::WebBridge(Backend &backend, AuthManager &auth, SettingsManager &settings,
                     ThemeManager &themes, UpdateChecker &updates, QObject *parent)
    : QObject(parent), m_backend(backend), m_auth(auth), m_settings(settings), m_themes(themes), m_updates(updates)
{
    connect(&auth, &AuthManager::authorizedChanged, this, &WebBridge::publish);
    connect(&backend, &Backend::busyChanged, this, &WebBridge::publish);
    connect(&backend, &Backend::exePathChanged, this, &WebBridge::publish);
    connect(&backend, &Backend::dllPathChanged, this, &WebBridge::publish);
    connect(&backend, &Backend::confPathChanged, this, &WebBridge::publish);
    connect(&backend, &Backend::themeChanged, this, &WebBridge::publish);
    connect(&backend, &Backend::resourcesChanged, this, [this] { ++m_resourceRevision; publish(); });
    connect(&backend, &Backend::installsChanged, this, &WebBridge::publish);
    connect(&updates, &UpdateChecker::stateChanged, this, &WebBridge::publish);
    connect(&backend, &Backend::workspaceChanged, this, [this] { ++m_workspaceRevision; publish(); });
    connect(&backend, &Backend::customizationProfileChanged, this, &WebBridge::publish);
    connect(&updates, &UpdateChecker::progressChanged, this, &WebBridge::publish);
    connect(&updates, &UpdateChecker::releaseChanged, this, &WebBridge::publish);
    connect(&updates, &UpdateChecker::autoCheckChanged, this, &WebBridge::publish);
    connect(&themes, &ThemeManager::effectiveThemeChanged, this, &WebBridge::publish);
    connect(&themes, &ThemeManager::accentColorChanged, this, &WebBridge::publish);
    connect(&backend, &Backend::log, this, [this](const QString &message, const QString &color) {
        if (m_auth.isAuthorized()) emit logMessage(message, color);
    });
}

QVariantMap WebBridge::snapshot() const
{
    QVariantMap state{{"protocol", 1}, {"authorized", m_auth.isAuthorized()}};
    if (!m_auth.isAuthorized()) return state;
    const QString path = m_backend.exePath();
    state.insert("busy", m_backend.busy() || m_dispatching);
    state.insert("exePath", path);
    state.insert("dllPath", m_backend.dllPath());
    state.insert("confPath", m_backend.confPath());
    state.insert("exeFound", !path.isEmpty() && QFileInfo(path).isFile());
    state.insert("version", SettingsManager::version());
    state.insert("themeMethod", m_backend.resolvedThemeMethod());
    state.insert("themeStatus", m_backend.themeStatus());
    state.insert("themeDetection", m_backend.themeDetection());
    state.insert("themeStale", m_backend.themeStale());
    state.insert("themeWritable", m_backend.themeWritable());
    state.insert("resourceCount", m_backend.resourceInfos().size());
    state.insert("resourceRevision", m_resourceRevision);
    state.insert("installs", m_backend.installs());
    state.insert("appearance", m_themes.theme());
    state.insert("effectiveTheme", m_themes.effectiveTheme());
    state.insert("accent", m_themes.accentColor().name());
    state.insert("workspaceRevision", m_workspaceRevision);
    state.insert("updates", updateStatus());
    state.insert("fpsAutoDetect", m_settings.fpsAutoDetect());
    state.insert("updateState", m_updates.property("state"));
    state.insert("updateNote", m_updates.property("note"));
    return state;
}

QVariantMap WebBridge::updateStatus() const
{
    QVariantMap status;
    // Deliberately do not serialize UpdateChecker config/token/signing data.
    for (const char *name : {"currentVersion", "configured", "autoCheck", "state", "note", "noteKind",
             "checking", "busy", "progress", "available", "latestVersion", "releaseNotes", "releasedAt",
             "downloadSize", "mandatory", "skipped", "trust", "downloadHost", "readyToInstall"})
        status.insert(QString::fromLatin1(name), m_updates.property(name));
    return status;
}

void WebBridge::publish()
{
    if (m_publishPending) return;
    m_publishPending = true;
    QTimer::singleShot(0, this, [this] {
        m_publishPending = false;
        emit stateChanged(snapshot());
    });
}

QVariantMap WebBridge::invoke(const QString &method, const QVariantMap &args)
{
    try {
    // These guards precede EVERY command, including read-only file access,
    // path/settings changes and opening a native page. JS is not the gate.
    if (!m_auth.isAuthorized()) return failure(QStringLiteral("Sign in with Discord to unlock BSI Customizer."));
    if (m_backend.busy() || m_dispatching) return failure(QStringLiteral("An operation is already running. Please wait."));
    QScopedValueRollback<bool> dispatching(m_dispatching, true);
    publish();
    const auto text = [&args](const char *key) { return args.value(QLatin1String(key)).toString(); };
    QVariant value;
    if (method == QLatin1String("detectInstalls")) value = m_backend.detectInstalls();
    else if (method == QLatin1String("useInstall")) {
        bool valid = false; const int index = args.value("index").toInt(&valid);
        if (!valid || index < 0 || index >= m_backend.installs().size()) return failure(QStringLiteral("Invalid installation index."));
        m_backend.useInstall(index);
    } else if (method == QLatin1String("setPaths")) {
        for (const auto &key : {QStringLiteral("exe"), QStringLiteral("dll"), QStringLiteral("conf")}) {
            if (!args.contains(key)) continue;
            const QString path = args.value(key).toString();
            if (key == QLatin1String("exe")) m_backend.setExePath(path);
            else if (key == QLatin1String("dll")) m_backend.setDllPath(path);
            else m_backend.setConfPath(path);
        }
    } else if (method == QLatin1String("loadTheme")) {
        const QString mode = text("mode");
        if (mode != QLatin1String("auto") && mode != QLatin1String("qml") && mode != QLatin1String("hex")) return failure(QStringLiteral("Invalid theme method."));
        m_backend.loadTheme(mode);
    } else if (method == QLatin1String("readTheme")) {
        value = QVariantMap{{"entries", m_backend.themeEntries()}, {"hex", m_backend.hexColors()}, {"presets", m_backend.themePresets()}, {"method", m_backend.resolvedThemeMethod()}};
    } else if (method == QLatin1String("setThemeEdit")) {
        if (m_backend.resolvedThemeMethod() == QLatin1String("hex")) {
            m_backend.setHexColor(text("name"), text("value"));
            m_backend.setHexEnabled(text("name"), args.value("enabled", true).toBool());
        } else m_backend.setThemeEdit(text("name"), args.value("enabled", true).toBool() ? text("value") : QString());
    } else if (method == QLatin1String("applyPreset")) {
        value = m_backend.resolvedThemeMethod() == QLatin1String("hex") ? m_backend.applyHexPreset(text("id")) : m_backend.applyPreset(text("id"));
    } else if (method == QLatin1String("applyTheme")) {
        value = m_backend.resolvedThemeMethod() == QLatin1String("hex") ? m_backend.applyHex() : m_backend.patchThemeQml();
    } else if (method == QLatin1String("scanResources")) {
        // No constructor, navigation handler or snapshot triggers this scan.
        value = m_backend.scanResources(args.value("includeBinary", true).toBool());
    } else if (method == QLatin1String("listResources")) {
        QVariantList metadata;
        for (const auto &item : m_backend.resourceInfos()) {
            auto row = item.toMap();
            row.remove("thumbnail"); // The paginated web list does not render thumbnails.
            metadata << row;
        }
        value = metadata;
    }
    else if (method == QLatin1String("readResource") || method == QLatin1String("saveResource")
             || method == QLatin1String("replaceResource") || method == QLatin1String("extractResource")) {
        bool valid = false; const int index = args.value("index").toInt(&valid);
        if (!valid || index < 0 || index >= m_backend.resourceInfos().size()) return failure(QStringLiteral("Select a scanned resource first."));
        if (method == QLatin1String("readResource")) {
            // Bound the renderer payload before allocating text/base64 copies.
            const auto info = m_backend.resourceInfos().at(index).toMap();
            if (info.value("size").toLongLong() > 8 * 1024 * 1024)
                return failure(QStringLiteral("This resource is too large for an inline preview. Extract it or use the full native tools."));
            value = QVariantMap{
            {"text", m_backend.resourceTextAt(index)}, {"preview", m_backend.resourcePreviewAt(index, 640)},
            {"note", m_backend.resourcePreviewNote(index)}, {"budget", m_backend.resourceBudgetAt(index)}};
        }
        else if (method == QLatin1String("saveResource")) value = m_backend.saveResourceText(index, text("text"));
        else if (method == QLatin1String("replaceResource")) value = m_backend.replaceResourceWithFile(index, text("path"));
        else value = m_backend.extractResource(index, text("path"));
    } else if (method == QLatin1String("fpsDetect")) value = m_backend.fpsDetect();
    else if (method == QLatin1String("fpsAnalyze")) value = m_backend.fpsAnalyze();
    else if (method == QLatin1String("fpsPatch")) {
        bool valid = false; const double limit = args.value("limit").toDouble(&valid);
        if (!valid || !std::isfinite(limit) || limit <= 0) return failure(QStringLiteral("Enter a positive finite FPS limit."));
        value = m_backend.fpsPatch(limit);
    } else if (method == QLatin1String("fpsRestore")) value = m_backend.fpsRestore();
    else if (method == QLatin1String("integrityCheck")) value = m_backend.integrityCheck();
    else if (method == QLatin1String("integrityPatch")) value = m_backend.integrityPatch();
    else if (method == QLatin1String("integrityRestore")) value = m_backend.integrityRestore();
    else if (method == QLatin1String("bootPatch")) {
        bool valid = false; const int hold = args.value("holdMs").toInt(&valid);
        if (!valid || hold < 0 || hold > 600000) return failure(QStringLiteral("Hold duration must be between 0 and 600000 milliseconds."));
        value = m_backend.bootPatch(hold);
    } else if (method == QLatin1String("purgePlayerCache")) value = m_backend.purgePlayerCache();
    else if (method == QLatin1String("restoreBackup")) value = m_backend.restoreBackup(m_backend.exePath());
    else if (method == QLatin1String("checkUpdates")) m_updates.check();
    else if (method == QLatin1String("setAppearance")) {
        bool valid = false; const int theme = args.value("theme").toInt(&valid); const QColor accent(text("accent"));
        if (!valid || theme < 0 || theme > 2 || !accent.isValid()) return failure(QStringLiteral("Invalid appearance settings."));
        m_themes.setTheme(theme); m_themes.setAccentColor(accent);
    } else if (method == QLatin1String("readInterface")) {
        value = QVariantMap{{"removals", m_backend.removalNames()}, {"layout", m_backend.layoutTweaks()},
                            {"about", m_backend.aboutPageState()}};
    } else if (method == QLatin1String("applyRemovals")) {
        QStringList names;
        for (const auto &name : args.value("names").toList()) names << name.toString();
        value = m_backend.applyRemovals(names);
    } else if (method == QLatin1String("applyLayoutTweak")) {
        value = m_backend.applyLayoutTweak(text("id"), args.value("enabled").toBool());
    } else if (method == QLatin1String("applyAboutPage")) {
        value = m_backend.applyAboutPage(text("text"), args.value("links").toList(),
            args.value("showSections", true).toBool(), args.value("enabled", true).toBool());
    } else if (method == QLatin1String("aboutPageReport")) value = m_backend.aboutPageReport();
    else if (method == QLatin1String("rpcidFind")) value = m_backend.rpcidFind(m_backend.exePath(), true);
    else if (method == QLatin1String("rpcidStatus")) value = m_backend.rpcidStatus(m_backend.exePath());
    else if (method == QLatin1String("rpcidNames")) value = m_backend.rpcidNames(m_backend.exePath());
    else if (method == QLatin1String("rpcidUndo")) value = m_backend.rpcidUndo(m_backend.exePath());
    else if (method == QLatin1String("rpcidReplace")) {
        value = m_backend.rpcidReplace(m_backend.exePath(), text("newId"), text("oldId"), args.value("dryRun", false).toBool(), true);
    } else if (method == QLatin1String("rpcidSetName")) {
        value = m_backend.rpcidSetName(m_backend.exePath(), text("text"), text("key"), args.value("dryRun", false).toBool());
    } else if (method == QLatin1String("gifRpcInspect")) value = m_backend.gifRpcInspect(m_backend.exePath());
    else if (method == QLatin1String("gifRpcApply")) {
        value = m_backend.gifRpcApply(m_backend.exePath(), args.value("settings").toMap(), args.value("replaceExisting", false).toBool());
    } else if (method == QLatin1String("gifRpcRestore")) value = m_backend.gifRpcRestore(m_backend.exePath());
    else if (method == QLatin1String("readWorkspace")) {
        QVariantList summary;
        for (const auto &item : m_backend.workspaceOperations()) {
            auto operation = item.toMap();
            operation.remove("data"); // Keep binary replacement payloads native.
            summary << operation;
        }
        value = QVariantMap{{"profile", m_backend.customizationProfile()}, {"operations", summary}};
    } else if (method == QLatin1String("workspace")) {
        const QString action = text("action");
        const QStringList allowed{ "inspect", "theme-dry-run", "diagnostics", "profile-load", "profile-save", "profile-section",
            "clear", "queue-theme", "queue-resource", "dry-run", "apply", "history", "restore", "image", "video", "rpc" };
        if (!allowed.contains(action)) return failure(QStringLiteral("Unknown workspace action."));
        auto response = m_backend.workspace(action, args.value("options").toMap());
        if (action == QLatin1String("video") && response.value("ok").toBool())
            emit videoFileRequested(QUrl::fromLocalFile(args.value("options").toMap().value("file").toString()));
        if (response.contains("operation")) {
            auto operation = response.value("operation").toMap();
            operation.remove("data");
            response["operation"] = operation;
        }
        value = response;
    } else if (method == QLatin1String("loadGradients")) {
        const auto result = m_backend.loadGradientTargets();
        if (!result.value("ok").toBool()) return success(result);
        value = QVariantMap{{"targets", m_backend.gradientTargets()}, {"msg", result.value("msg")}};
    } else if (method == QLatin1String("applyGradient")) {
        if (m_backend.resolvedThemeMethod() == QLatin1String("hex"))
            value = m_backend.applyHexGradient(text("key"), text("first"), text("second"), args.value("horizontal", true).toBool(), args.value("enabled", true).toBool());
        else value = m_backend.applyThemeGradient(text("key"), text("first"), text("second"), args.value("horizontal", true).toBool(), args.value("enabled", true).toBool());
    } else if (method == QLatin1String("resetThemeEdits")) {
        m_backend.clearThemeEdits(); m_backend.resetHexColors();
    } else if (method == QLatin1String("previewHex")) value = m_backend.previewHex();
    else if (method == QLatin1String("recolorTogglesFromPreset")) value = m_backend.recolorTogglesFromPreset(text("id"));
    else if (method == QLatin1String("detectInstances")) value = m_backend.detectInstances();
    else if (method == QLatin1String("extractAllResources")) value = m_backend.extractAllResources();
    else if (method == QLatin1String("measureResource")) {
        bool valid = false; const int index = args.value("index").toInt(&valid);
        if (!valid || index < 0 || index >= m_backend.resourceInfos().size()) return failure(QStringLiteral("Select a scanned resource first."));
        // BOM comes from the selected entry budget; measurement is not a write.
        // Do not re-read the entire EXE for each editor keystroke.
        const bool bom = args.value("bom", false).toBool();
        value = QVariantMap{{"decoded", m_backend.resourceTextSize(text("text"), bom)},
                            {"encoded", m_backend.resourcePayloadSize(index, text("text"), bom)}};
    } else if (method == QLatin1String("setFpsAutoDetect")) m_settings.setFpsAutoDetect(args.value("enabled").toBool());
    else if (method == QLatin1String("openExeFolder")) m_settings.openExeFolder();
    else if (method == QLatin1String("updates")) {
        const QString action = text("action");
        if (action == QLatin1String("read")) value = updateStatus();
        else if (action == QLatin1String("check")) m_updates.check();
        else if (action == QLatin1String("download")) m_updates.download();
        else if (action == QLatin1String("apply")) m_updates.apply();
        else if (action == QLatin1String("skip")) m_updates.skip();
        else if (action == QLatin1String("unskip")) m_updates.unskip();
        else if (action == QLatin1String("cancel")) m_updates.cancel();
        else if (action == QLatin1String("openDownloadPage")) m_updates.openDownloadPage();
        else if (action == QLatin1String("showStagedFile")) m_updates.showStagedFile();
        else if (action == QLatin1String("showHelperLog")) m_updates.showHelperLog();
        else if (action == QLatin1String("diagnostics")) value = m_updates.helperFailure();
        else if (action == QLatin1String("autoCheck")) m_updates.setAutoCheck(args.value("enabled").toBool());
        else return failure(QStringLiteral("Unknown updater action."));
    } else if (method == QLatin1String("videoControl")) {
        const QString action = text("action");
        if (action == QLatin1String("geometry")) {
            QVariantMap rect{{"visible", args.value("visible").toBool()}};
            for (const auto *key : {"x", "y", "width", "height", "clipX", "clipY", "clipWidth", "clipHeight"}) {
                bool ok = false;
                const double coordinate = args.value(QLatin1String(key)).toDouble(&ok);
                if (!ok || !std::isfinite(coordinate)) return failure(QStringLiteral("Invalid preview geometry."));
                rect.insert(QLatin1String(key), qBound(-8192.0, coordinate, 8192.0));
            }
            emit videoControlRequested(action, rect);
        } else if (action == QLatin1String("play") || action == QLatin1String("pause"))
            emit videoControlRequested(action, {});
        else if (action == QLatin1String("crop"))
            emit videoControlRequested(action, {{"crop", args.value("crop").toBool()}});
        else return failure(QStringLiteral("Unknown preview control."));
        value = QVariantMap{{"msg", QStringLiteral("Preview control requested; emulator playback is not verified.")}};
    } else if (method == QLatin1String("maintenance")) {
        // Explicit adapters only: no reflection, script execution or generic
        // QObject access. All authorization, busy and exception guards above
        // apply to every action; the original engines validate every write.
        const QString action = text("action");
        const auto list = [&args](const char *key) {
            QStringList out;
            for (const auto &item : args.value(QLatin1String(key)).toList()) out << item.toString();
            return out;
        };
        if (action == QLatin1String("sidebar-read")) value = m_backend.sidebarButtonRows();
        else if (action == QLatin1String("sidebar-apply")) value = m_backend.applySidebarOrder(list("order"), list("hidden"));
        else if (action == QLatin1String("debloat-read")) value = QVariantMap{{"state", m_backend.debloatState()}, {"options", m_backend.debloatOptions()}};
        else if (action == QLatin1String("debloat-apply")) value = m_backend.debloatApply(list("ids"), text("instance"));
        else if (action == QLatin1String("debloat-restore")) value = m_backend.debloatRestore();
        else if (action == QLatin1String("android-read")) value = m_backend.androidState();
        else if (action == QLatin1String("android-scan")) value = m_backend.androidScan(text("instance"));
        else if (action == QLatin1String("android-apply")) value = m_backend.androidApply(list("packages"), args.value("wipeDownloads").toBool(), args.value("keepCopy", true).toBool(), args.value("clearAppData").toBool(), text("instance"));
        else if (action == QLatin1String("android-restore")) value = m_backend.androidRestore(text("instance"));
        else if (action == QLatin1String("disk-find")) value = QVariantMap{{"path", m_backend.rootvhdFind()}, {"folders", m_backend.rootvhdEngineFolders()}};
        else if (action == QLatin1String("disk-archive")) value = m_backend.rootvhdOpenEngineArchive(text("file"));
        else if (action == QLatin1String("disk-info")) value = m_backend.rootvhdInfo(text("image"));
        else if (action == QLatin1String("disk-enable")) value = m_backend.rootvhdEnableWrite(text("image"));
        else if (action == QLatin1String("disk-list")) value = m_backend.rootvhdList(text("image"), text("innerPath"));
        else if (action == QLatin1String("disk-search")) value = m_backend.rootvhdSearch(text("image"), text("query"));
        else if (action == QLatin1String("disk-extract")) value = m_backend.rootvhdExtract(text("image"), text("innerPath"), text("file"));
        else if (action == QLatin1String("disk-add")) value = m_backend.rootvhdAdd(text("image"), text("innerPath"), text("file"));
        else if (action == QLatin1String("disk-add-folder")) value = m_backend.rootvhdAddFolder(text("image"), text("innerPath"), text("file"));
        else if (action == QLatin1String("disk-replace")) value = m_backend.rootvhdReplace(text("image"), text("innerPath"), text("file"));
        else if (action == QLatin1String("disk-mkdir")) value = m_backend.rootvhdMkdir(text("image"), text("innerPath"), text("name"));
        else if (action == QLatin1String("disk-rename")) value = m_backend.rootvhdRename(text("image"), text("innerPath"), text("name"));
        else if (action == QLatin1String("disk-move")) value = m_backend.rootvhdMove(text("image"), text("innerPath"), text("destination"));
        else if (action == QLatin1String("disk-copy")) value = m_backend.rootvhdCopy(text("image"), text("innerPath"), text("destination"), text("name"));
        else if (action == QLatin1String("disk-remove")) value = m_backend.rootvhdRemove(text("image"), text("innerPath"));
        else if (action == QLatin1String("disk-undo")) value = m_backend.rootvhdUndo(text("image"));
        else if (action == QLatin1String("disk-discard")) value = m_backend.rootvhdDiscardUndo(text("image"));
        else return failure(QStringLiteral("Unknown maintenance action."));
    } else if (method == QLatin1String("openNativeTool")) {
        bool valid = false; const int page = args.value("page").toInt(&valid);
        if (!valid || page < 0 || page > 12) return failure(QStringLiteral("Invalid native tool page."));
        emit nativeToolRequested(page);
    } else if (method == QLatin1String("signOut")) m_auth.signOut();
    else return failure(QStringLiteral("Unknown bridge command: %1").arg(method));
    // Preserve the engine's result, including a refused write; never invent success.
    return success(value);
    } catch (const std::bad_alloc &) {
        return failure(QStringLiteral("Not enough memory for this operation. Close other applications and try a smaller resource."));
    } catch (const std::exception &) {
        return failure(QStringLiteral("The native operation failed unexpectedly. No success was reported; check the target before retrying."));
    } catch (...) {
        return failure(QStringLiteral("The native operation could not complete. Restart before retrying."));
    }
}

void WebBridge::requestFile(const QString &role)
{
    if (m_auth.isAuthorized() && !m_backend.busy() && !m_dispatching && fileRole(role)) emit openFileRequested(role);
    else emit fileSelected(role, QString());
}

void WebBridge::fileChosen(const QString &role, const QUrl &url)
{
    if (!m_auth.isAuthorized() || m_backend.busy() || m_dispatching || !fileRole(role)) return;
    const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
    if (!path.isEmpty()) {
        if (role == QLatin1String("exe")) m_backend.setExePath(path);
        else if (role == QLatin1String("dll")) m_backend.setDllPath(path);
        else if (role == QLatin1String("conf")) m_backend.setConfPath(path);
    }
    emit fileSelected(role, path);
    publish();
}
