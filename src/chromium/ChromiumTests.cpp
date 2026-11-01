#include "WebBridge.h"
#include "LocalContentPolicy.h"
#include "core/AuthManager.h"
#include "core/Backend.h"
#include "core/SettingsManager.h"
#include "core/ThemeManager.h"
#include "core/UpdateChecker.h"
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickWebEngineProfile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

int runWebBridgeAccessTest()
{
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    QCoreApplication::setOrganizationName(QStringLiteral("BSI Web Bridge Test"));
    QCoreApplication::setApplicationName(QStringLiteral("BSI Web Bridge Test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, directory.path());
    QStandardPaths::setTestModeEnabled(true);
    qputenv("BSI_NO_UPDATER", "1");
    SettingsManager settings(nullptr, false);
    Backend backend(&settings);
    AuthManager auth;
    backend.setAuthManager(&auth);
    ThemeManager themes;
    UpdateChecker updates;
    WebBridge bridge(backend, auth, settings, themes, updates);
    const QString original = settings.exePath();
    int fileSignals = 0, nativeSignals = 0;
    QObject::connect(&bridge, &WebBridge::openFileRequested, [&] { ++fileSignals; });
    QObject::connect(&bridge, &WebBridge::nativeToolRequested, [&] { ++nativeSignals; });
    const auto state = bridge.snapshot();
    bool ok = !auth.isAuthorized() && state.size() == 2 && !state.value("authorized").toBool();
#ifdef BSI_ENFORCE_EMBEDDED_AUTH
    // A downloadable Chromium bundle must be sign-in ready without a sidecar
    // auth.json or configure-time environment surviving on the user's PC.
    const auto config = AuthManager::loadConfig();
    const bool policyReady = auth.configured() && config.roleGateConfigured()
        && config.sourceFile.contains(QStringLiteral("embedded"))
        && !AuthManager::embeddedAuthJson().isEmpty() && config.clientSecret.isEmpty();
    if (!policyReady) {
        std::fprintf(stderr, "FAIL: Chromium bundle is missing a valid embedded Discord sign-in policy\n");
        ok = false;
    }
#endif
    const char *commands[] = {
        "aboutPageReport", "applyAboutPage", "applyGradient", "applyLayoutTweak", "applyPreset",
        "applyRemovals", "applyTheme", "bootPatch", "checkUpdates", "detectInstalls",
        "detectInstances", "extractAllResources", "extractResource", "fpsAnalyze", "fpsDetect",
        "fpsPatch", "fpsRestore", "gifRpcApply", "gifRpcInspect", "gifRpcRestore",
        "integrityCheck", "integrityPatch", "integrityRestore", "listResources", "loadGradients",
        "loadTheme", "measureResource", "openExeFolder", "openNativeTool", "previewHex",
        "purgePlayerCache", "readInterface", "readResource", "readTheme", "readWorkspace",
        "recolorTogglesFromPreset", "replaceResource", "resetThemeEdits", "restoreBackup", "rpcidFind",
        "rpcidNames", "rpcidReplace", "rpcidSetName", "rpcidStatus", "rpcidUndo",
        "saveResource", "scanResources", "setAppearance", "setFpsAutoDetect", "setPaths",
        "setThemeEdit", "signOut", "updates", "useInstall", "workspace",
        "maintenance", "videoControl", "binaryLab", "unknown"};
    for (const char *command : commands) {
        const auto result = bridge.invoke(QString::fromLatin1(command), {{"exe", "must-not-write.exe"}, {"page", 1}, {"index", 0}});
        if (result.value("ok").toBool() || !result.contains("error")) {
            std::fprintf(stderr, "FAIL: unauthorized bridge command %s\n", command);
            ok = false;
        }
    }
    bridge.requestFile(QStringLiteral("exe"));
    bridge.fileChosen(QStringLiteral("exe"), QUrl::fromLocalFile(directory.filePath(QStringLiteral("must-not-write.exe"))));
    ok = ok && settings.exePath() == original && fileSignals == 0 && nativeSignals == 0;
    std::printf("WEB BRIDGE ACCESS TEST %s\n", ok ? "PASSED" : "FAILED");
    std::fflush(stdout);
    return ok ? 0 : 3;
}

int runChromiumRuntimeTest(QCoreApplication &app)
{
    // Renderer-only diagnostic. No AuthManager/Backend/WebChannel endpoint is
    // constructed, so this test is NOT an authorization bypass or patch mode.
    auto *profile = QQuickWebEngineProfile::defaultProfile();
    profile->setUrlRequestInterceptor(new LocalContentPolicy(profile));
    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlEngine::exit, &app, [&app](int code) {
        std::printf("CHROMIUM RUNTIME TEST %s\n", code == 0 ? "PASSED" : "FAILED");
        std::fflush(stdout);
        app.exit(code);
    });
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &app, [&app] {
        std::fprintf(stderr, "Chromium renderer smoke test timed out. Check QtWebEngineProcess, resources and locales.\n");
        app.exit(3);
    });
    deadline.start(30000);
    engine.loadData(R"QML(
import QtQuick
import QtQuick.Window
import QtWebEngine
Window {
    width: 1200; height: 800; visible: true
    WebEngineView {
        id: web
        anchors.fill: parent
        url: "qrc:/bsi-web/index.html"
        settings.localContentCanAccessRemoteUrls: false
        settings.localContentCanAccessFileUrls: false
        onLoadingChanged: (request) => {
            if (request.status === WebEngineView.LoadSucceededStatus) verify.start();
            if (request.status === WebEngineView.LoadFailedStatus) Qt.exit(3);
        }
        onRenderProcessTerminated: (status, code) => Qt.exit(3)
    }
    Timer {
        id: verify
        interval: 600
        onTriggered: web.runJavaScript("(() => { const b=document.querySelector('#preview-button'); if(!b || b.hidden) return false; b.click(); return document.querySelectorAll('#spine [data-page]').length === 14 && document.querySelectorAll('[data-action]:enabled').length === 0 && document.styleSheets.length > 0 && typeof QWebChannel === 'function'; })()", function(ok) { Qt.exit(ok === true ? 0 : 3); })
    }
}
)QML", QUrl(QStringLiteral("qrc:/bsi-web-runtime-test.qml")));
    if (engine.rootObjects().isEmpty()) return 3;
    return app.exec();
}
