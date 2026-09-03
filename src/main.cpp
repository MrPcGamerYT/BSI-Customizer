#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "core/Backend.h"
#include "core/SettingsManager.h"
#include "core/SysInfo.h"
#include "core/ThemeManager.h"

// Implemented in selftest.cpp
int runSelfTest();

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MrPcGamerYT"));
    QCoreApplication::setApplicationName(QStringLiteral("BSI Customizer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(BSI_VERSION));

    // Headless verification of the patcher engines + theme chain.
    // Used locally and by the GitHub Actions release workflow.
    if (QCoreApplication::arguments().contains(QStringLiteral("--selftest")))
        return runSelfTest();

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
