#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QBuffer>
#include <QDir>
#include <QImageReader>
#include <QIcon>
#include <QImage>
#include <QColor>
#include <QQmlError>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <memory>

#include "core/AboutPatcher.h"
#include "core/Backend.h"
#include "core/CrashDiagnostics.h"
#include "core/AuthManager.h"
#include "core/SettingsManager.h"
#include "core/SysInfo.h"
#include "core/ThemeEngine.h"
#include "core/ThemeManager.h"
#include "core/UpdateChecker.h"

#ifdef BSI_CHROMIUM_UI
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QQuickWebEngineProfile>
#include "chromium/LocalContentPolicy.h"
#include "chromium/WebBridge.h"
int runWebBridgeAccessTest();
int runChromiumRuntimeTest(QCoreApplication &app);
#endif


// Last, so none of the windows.h macros (min/max/interface/...) leak into the
// Qt or project headers above.
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Implemented in selftest.cpp
int runSelfTest();
int runThemeRuntimeTest(const QStringList &args);
// Implemented in patch_regression_test.cpp (isolated synthetic QML provider).
int runBootPlayerRuntimeTest(const QStringList &args);
int runGradientRuntimeTest(const QStringList &args);
// Implemented in cli.cpp
int runRpcIdCli(const QStringList &args);
int runUpdateCli(const QStringList &args);

namespace {

// The Windows build is a GUI-subsystem executable (WIN32_EXECUTABLE), so it
// never flashes a console window when double-clicked. The side effect is that
// "--selftest" started from a terminal has nowhere to print its PASS/FAIL
// lines, so re-attach to the parent console in exactly that case.
//
// When stdout is already redirected to a file or a pipe (how CI captures the
// output) the handle type is FILE_TYPE_DISK/PIPE and we leave it alone.
void ensureConsoleOutput()
{
#ifdef Q_OS_WIN
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) != FILE_TYPE_UNKNOWN)
        return; // stdout is usable already (redirected or inherited)
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return; // launched from Explorer: stay silent, as a GUI app should
#ifdef _MSC_VER
    FILE *f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
#else
    std::freopen("CONOUT$", "w", stdout);
    std::freopen("CONOUT$", "w", stderr);
#endif
#endif // Q_OS_WIN
}

} // namespace

int main(int argc, char *argv[])
{
#ifdef BSI_CHROMIUM_UI
    QtWebEngineQuick::initialize();
#endif
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MrPcGamerYT"));
    QCoreApplication::setApplicationName(QStringLiteral("BSI Customizer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(BSI_VERSION));
    installCrashDiagnostics();

    // Brand icon for every window/dialog the app raises (taskbar, Alt-Tab,
    // title bar). The .ico lives at the repo root; CMake maps it into the
    // Qt resource system as :/app.ico. The exe's embedded icon (src/app.rc)
    // covers Explorer shortcuts/UAC prompts.
    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));

    // Headless verification of the patcher engines + theme chain.
    // Used locally and by the GitHub Actions release workflow.
    const QStringList args = QCoreApplication::arguments();
    if (args.contains(QStringLiteral("--gradient-render-test"))) {
        ensureConsoleOutput();
        return runGradientRuntimeTest(args);
    }
    if (args.contains(QStringLiteral("--theme-runtime-test"))) {
        ensureConsoleOutput();
        return runThemeRuntimeTest(args);
    }
    if (args.contains(QStringLiteral("--boot-runtime-test"))) {
        ensureConsoleOutput();
        return runBootPlayerRuntimeTest(args);
    }
    if (args.contains(QStringLiteral("--selftest"))) {
        ensureConsoleOutput();
        return runSelfTest();
    }

    // Scriptable Discord Rich Presence commands, served by the very engines
    // the UI uses. The application is the whole tool: there is no helper
    // script, interpreter or extra download behind this feature.
    //   BSI-Customizer.exe --rpcid status "C:\...\HD-Player.exe" --json
    const int rpcidAt = args.indexOf(QStringLiteral("--rpcid"));
    if (rpcidAt >= 0) {
        ensureConsoleOutput();
        const int code = runRpcIdCli(args.mid(rpcidAt + 1));
        std::fflush(stdout);
        return code;
    }

    // Diagnostics: dump exactly what theme detection sees in a real
    // HD-Player.exe.  Use this when the Theme page reports "not found" or shows
    // no colors - it says which assumption (marker, header layout, content,
    // parse) does not hold instead of leaving it to guesswork.
    //   BSI-Customizer.exe --theme-probe "C:\...\HD-Player.exe"
    const int probeAt = args.indexOf(QStringLiteral("--theme-probe"));
    if (probeAt >= 0) {
        ensureConsoleOutput();
        const QString target = (probeAt + 1 < args.size()) ? args.at(probeAt + 1) : QString();
        if (target.isEmpty()) {
            std::printf("usage: BSI-Customizer --theme-probe <path-to-HD-Player.exe>\n");
            return 2;
        }
        const QString report = ThemeEngine::probeReport(target);
        const QByteArray utf8 = report.toUtf8();
        if (std::fwrite(utf8.constData(), 1, size_t(utf8.size()), stdout) != size_t(utf8.size())
            || std::fflush(stdout) != 0)
            return 2;
        return report.contains(QLatin1String("*** NOT FOUND ***"))
                   || report.contains(QLatin1String("*** ZERO ENTRIES ***"))
               ? 1
               : 0;
    }

    // Diagnostics: dump what the Settings->About editor sees in a real
    // HD-Player.exe - which resource holds PlrAboutSetting.qml, whether the
    // content column, the About line and every stock link row were located,
    // and what the editor would prefill. When the UI reports "not supported",
    // this names the step that failed instead of leaving it to guesswork.
    //   BSI-Customizer.exe --about-probe "C:\...\HD-Player.exe"
    const int aboutProbeAt = args.indexOf(QStringLiteral("--about-probe"));
    if (aboutProbeAt >= 0) {
        ensureConsoleOutput();
        const QString target = (aboutProbeAt + 1 < args.size()) ? args.at(aboutProbeAt + 1) : QString();
        if (target.isEmpty()) {
            std::printf("usage: BSI-Customizer --about-probe <path-to-HD-Player.exe>\n");
            return 2;
        }
        std::printf("%s", qPrintable(AboutPatcher::probeReport(target)));
        std::fflush(stdout);
        return 0;
    }

    // Self-update probe: reads the Discord releases channel the same way the
    // window does, so the bot/channel setup can be verified without a GUI.
    //   BSI-Customizer.exe --check-updates [--json] [--download] [--explain] [--timeout 25]
    const int updatesAt = args.indexOf(QStringLiteral("--check-updates"));
    if (updatesAt >= 0) {
        ensureConsoleOutput();
        const int code = runUpdateCli(args.mid(updatesAt + 1));
        std::fflush(stdout);
        return code;
    }

#ifdef BSI_CHROMIUM_UI
    if (args.contains(QStringLiteral("--web-bridge-test"))) {
        ensureConsoleOutput();
        return runWebBridgeAccessTest();
    }
    if (args.contains(QStringLiteral("--chromium-runtime-test"))) {
        ensureConsoleOutput();
        return runChromiumRuntimeTest(app);
    }
#endif

    // Packaging-only smoke mode: instantiate the real UI without changing
    // the user's preferences or requiring a Qt SDK/development tools.
    const bool deploymentTest = args.contains(QStringLiteral("--deployment-test"));
    std::unique_ptr<QTemporaryDir> deploymentSettings;
    if (deploymentTest) {
        ensureConsoleOutput();
        deploymentSettings = std::make_unique<QTemporaryDir>();
        if (!deploymentSettings->isValid())
            return 2;
        QCoreApplication::setOrganizationName(QStringLiteral("BSI Deployment Test"));
        QCoreApplication::setApplicationName(QStringLiteral("BSI Deployment Test"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, deploymentSettings->path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, deploymentSettings->path());
        QStandardPaths::setTestModeEnabled(true);
    }
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ThemeManager themes;
    SettingsManager settings;
    Backend backend(&settings);
    SysInfo sysinfo;
    // The Discord sign-in gate (Premium-Optimizer login system). Backend
    // refuses its actions until auth verifies a sign-in; the login card in
    // QML is only the UX side of it.
    AuthManager auth;
    backend.setAuthManager(&auth);
    // Self-update through the Discord releases bot (docs/UPDATER.md). Nothing
    // network-facing happens until check() - or the delayed startup check once
    // an updater config is present.
    UpdateChecker updates;
    QObject::connect(&updates, &UpdateChecker::restartRequested, &app, &QGuiApplication::quit);

#ifdef BSI_CHROMIUM_UI
    const bool chromiumUi = !args.contains(QStringLiteral("--native-ui"));
    if (chromiumUi) {
        auto *profile = QQuickWebEngineProfile::defaultProfile();
        profile->setUrlRequestInterceptor(new LocalContentPolicy(profile));
    }
    WebBridge webBridge(backend, auth, settings, themes, updates);
#else
    if (args.contains(QStringLiteral("--web-ui"))) {
        std::fprintf(stderr, "Chromium UI is not compiled in. Reconfigure with -DBSI_CHROMIUM_UI=ON.\n");
        return 2;
    }
#endif

    QQmlApplicationEngine engine;
    QStringList deploymentErrors;
    // Collect QML diagnostics so a failed load can report WHY the root object
    // never appeared (the error text must reach redirected stdio for CI).
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&deploymentErrors](const QList<QQmlError> &warnings) {
        for (const auto &warning : warnings)
            deploymentErrors << warning.toString();
    });
    if (deploymentTest) {
        engine.setInitialProperties({ { QStringLiteral("visible"), false } });
    }

    // IMPORTANT: context properties MUST be registered BEFORE engine.load().
    engine.rootContext()->setContextProperty(QStringLiteral("themes"), &themes);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("sysinfo"), &sysinfo);
    engine.rootContext()->setContextProperty(QStringLiteral("auth"), &auth);
    engine.rootContext()->setContextProperty(QStringLiteral("updates"), &updates);

#ifdef BSI_CHROMIUM_UI
    engine.rootContext()->setContextProperty(QStringLiteral("webBridge"), &webBridge);
    const QUrl mainUrl(chromiumUi
        ? QStringLiteral("qrc:/qt/qml/BSICustomizer/src/qml/ChromiumMain.qml")
        : QStringLiteral("qrc:/qt/qml/BSICustomizer/src/qml/Main.qml"));
#else
    const QUrl mainUrl(QStringLiteral("qrc:/qt/qml/BSICustomizer/src/qml/Main.qml"));
#endif
    engine.load(mainUrl);
    if (engine.rootObjects().isEmpty()) {
        // Print through both CRT streams (Qt's handler may not reach the
        // redirected pipes a GUI subsystem process inherits) so packaging
        // tests capture the actual QML error instead of a bare exit code.
        std::fprintf(stderr, "BSI Customizer: failed to load QML UI (%s)\n",
                     qUtf8Printable(mainUrl.toString()));
        std::printf("BSI Customizer: failed to load QML UI (%s)\n",
                    qUtf8Printable(mainUrl.toString()));
        for (const QString &line : deploymentErrors) {
            std::fprintf(stderr, "%s\n", qUtf8Printable(line));
            std::printf("%s\n", qUtf8Printable(line));
        }
        std::fflush(stderr);
        std::fflush(stdout);
        qCritical("BSI Customizer: failed to load QML UI");
        return -1;
    }

    if (deploymentTest) {
        QTimer::singleShot(400, &app, [&] {
            const QString root = QDir::cleanPath(QCoreApplication::applicationDirPath());
            const QString prefix = QDir::cleanPath(QLibraryInfo::path(QLibraryInfo::PrefixPath));
            bool ok = root.compare(prefix, Qt::CaseInsensitive) == 0;
            if (!ok)
                std::fprintf(stderr, "Qt prefix escaped the bundle: %s\n", qPrintable(prefix));
            for (const auto &path : engine.importPathList()) {
                if (path.startsWith(QLatin1String("qrc:")))
                    continue;
                const auto clean = QDir::cleanPath(path);
                if (clean.compare(root, Qt::CaseInsensitive) != 0
                    && !clean.startsWith(root + QLatin1Char('/'), Qt::CaseInsensitive)) {
                    std::fprintf(stderr, "QML import path escaped the bundle: %s\n", qPrintable(path));
                    ok = false;
                }
            }
            QByteArray svg("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"2\" height=\"2\"><rect width=\"2\" height=\"2\" fill=\"#123456\"/></svg>");
            QBuffer source(&svg);
            source.open(QIODevice::ReadOnly);
            QImageReader reader(&source, "svg");
            const auto image = reader.read();
            if (image.isNull() || image.pixelColor(0, 0) != QColor(QStringLiteral("#123456"))) {
                std::fprintf(stderr, "Bundled SVG image plugin failed: %s\n", qPrintable(reader.errorString()));
                ok = false;
            }
            for (const auto &error : deploymentErrors) {
                std::fprintf(stderr, "%s\n", qPrintable(error));
                if (error.contains(QLatin1String("ReferenceError")) || error.contains(QLatin1String("TypeError"))
                    || error.contains(QLatin1String("not installed")) || error.contains(QLatin1String("is not a type")))
                    ok = false;
            }
            std::printf("DEPLOYMENT_ROOT=%s\n", root.toUtf8().constData());
            std::printf("DEPLOYMENT_PORTABLE=%s\n", qEnvironmentVariable("BSI_PORTABLE", QStringLiteral("0")).toUtf8().constData());
            std::printf("DEPLOYMENT_ARGS=%s\n", QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact).constData());
            if (ok)
                std::printf("DEPLOYMENT TEST PASSED\n");
            std::fflush(stdout);
            app.exit(ok ? 0 : 3);
        });
    }
    return app.exec();
}
