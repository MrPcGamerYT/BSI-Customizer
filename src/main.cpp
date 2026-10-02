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

#include "core/Backend.h"
#include "core/SettingsManager.h"
#include "core/SysInfo.h"
#include "core/ThemeEngine.h"
#include "core/ThemeManager.h"

// Last, so none of the windows.h macros (min/max/interface/...) leak into the
// Qt or project headers above.
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Implemented in selftest.cpp
int runSelfTest();
int runThemeRuntimeTest(const QStringList &args);
int runGradientRuntimeTest(const QStringList &args);
// Implemented in cli.cpp
int runRpcIdCli(const QStringList &args);

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
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MrPcGamerYT"));
    QCoreApplication::setApplicationName(QStringLiteral("BSI Customizer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(BSI_VERSION));

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
        std::printf("%s", qPrintable(report));
        std::fflush(stdout);
        return report.contains(QLatin1String("*** NOT FOUND ***"))
                   || report.contains(QLatin1String("*** ZERO ENTRIES ***"))
               ? 1
               : 0;
    }

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

    QQmlApplicationEngine engine;
    QStringList deploymentErrors;
    if (deploymentTest) {
        engine.setInitialProperties({ { QStringLiteral("visible"), false } });
        QObject::connect(&engine, &QQmlEngine::warnings, &app, [&deploymentErrors](const QList<QQmlError> &warnings) {
            for (const auto &warning : warnings)
                deploymentErrors << warning.toString();
        });
    }

    // IMPORTANT: context properties MUST be registered BEFORE engine.load().
    engine.rootContext()->setContextProperty(QStringLiteral("themes"), &themes);
    engine.rootContext()->setContextProperty(QStringLiteral("settings"), &settings);
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.rootContext()->setContextProperty(QStringLiteral("sysinfo"), &sysinfo);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/BSICustomizer/src/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
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
