#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include <cstdio>

#include "core/Backend.h"
#include "core/SettingsManager.h"
#include "core/SysInfo.h"
#include "core/ThemeManager.h"

// Last, so none of the windows.h macros (min/max/interface/...) leak into the
// Qt or project headers above.
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Implemented in selftest.cpp
int runSelfTest();

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

    // Headless verification of the patcher engines + theme chain.
    // Used locally and by the GitHub Actions release workflow.
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest"))) {
        ensureConsoleOutput();
        return runSelfTest();
    }

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    ThemeManager themes;
    SettingsManager settings;
    Backend backend(&settings);
    SysInfo sysinfo;

    QQmlApplicationEngine engine;

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

    return app.exec();
}
