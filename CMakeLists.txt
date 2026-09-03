cmake_minimum_required(VERSION 3.21)

project(BSICustomizer VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Svg)

qt_standard_project_setup(REQUIRES 6.5)

# Mark Theme.qml as a QML singleton (must happen BEFORE qt_add_qml_module)
set_source_files_properties(src/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(BSICustomizer
    src/main.cpp
    src/selftest.cpp
    src/core/ByteUtils.h
    src/core/PeUtils.h
    src/core/PeUtils.cpp
    src/core/ZlibUtils.h
    src/core/ZlibUtils.cpp
    src/core/ThemeEngine.h
    src/core/ThemeEngine.cpp
    src/core/SvgEngine.h
    src/core/SvgEngine.cpp
    src/core/FpsPatcher.h
    src/core/FpsPatcher.cpp
    src/core/IntegrityPatcher.h
    src/core/IntegrityPatcher.cpp
    src/core/BootVideoPatcher.h
    src/core/BootVideoPatcher.cpp
    src/core/ExtrasPatcher.h
    src/core/ExtrasPatcher.cpp
    src/core/BsDetector.h
    src/core/BsDetector.cpp
    src/core/Backend.h
    src/core/Backend.cpp
    src/core/ThemeManager.h
    src/core/ThemeManager.cpp
    src/core/SettingsManager.h
    src/core/SettingsManager.cpp
    src/core/SysInfo.h
    src/core/SysInfo.cpp
)

# Windows: embed the requireAdministrator manifest (UAC elevation like the
# Python tool's PyInstaller uac_admin=True).
if(WIN32)
    target_sources(BSICustomizer PRIVATE src/app.rc)
endif()

# The QML module generates a "BSICustomizer" folder in the build dir, so the
# executable gets a distinct output name to avoid the collision.
set_target_properties(BSICustomizer PROPERTIES OUTPUT_NAME "BSI-Customizer")

target_compile_definitions(BSICustomizer PRIVATE
    BSI_VERSION="${PROJECT_VERSION}"
)

qt_add_qml_module(BSICustomizer
    URI BSICustomizer
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/Theme.qml
        src/qml/Icons.js
        src/qml/components/SidebarButton.qml
        src/qml/components/Card.qml
        src/qml/components/ActionButton.qml
        src/qml/components/ToggleSwitch.qml
        src/qml/components/Stepper.qml
        src/qml/components/ChoiceChips.qml
        src/qml/components/Toast.qml
        src/qml/components/PathRow.qml
        src/qml/components/ColorField.qml
        src/qml/components/LogPanel.qml
        src/qml/pages/DashboardPage.qml
        src/qml/pages/ThemePage.qml
        src/qml/pages/SvgPage.qml
        src/qml/pages/FpsPage.qml
        src/qml/pages/IntegrityPage.qml
        src/qml/pages/BootVideoPage.qml
        src/qml/pages/ExtrasPage.qml
        src/qml/pages/AppearancePage.qml
        src/qml/pages/AboutPage.qml
)

target_link_libraries(BSICustomizer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
)

install(TARGETS BSICustomizer RUNTIME DESTINATION bin)
