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
    src/theme_runtime_test.cpp
    src/patch_regression_test.cpp
    src/core/PatchFile.h
    src/core/PatchFile.cpp
    src/core/QtResources.h
    src/core/QtResources.cpp
    src/core/ByteUtils.h
    src/core/PeUtils.h
    src/core/PeUtils.cpp
    src/core/ZlibUtils.h
    src/core/ZlibUtils.cpp
    src/core/ThemeEngine.h
    src/core/ThemeEngine.cpp
    src/core/ThemePresets.h
    src/core/ThemePresets.cpp
    src/core/SvgEngine.h
    src/core/SvgEngine.cpp
    src/core/FpsPatcher.h
    src/core/FpsPatcher.cpp
    src/core/X64Prologue.h
    src/core/X64Prologue.cpp
    src/third_party/hde64/hde64.cpp
    src/third_party/hde64/hde64.h
    src/third_party/hde64/table64.h
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

    # Re-run rc.exe whenever the manifest XML itself changes (Makefile/Ninja
    # generators; Visual Studio tracks resource dependencies on its own).
    set_source_files_properties(src/app.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest")

    if(MSVC)
        # src/app.rc provides the RT_MANIFEST resource with id 1 itself. MSVC
        # *also* generates a default application manifest and hands it to the
        # linker, which asks cvtres to merge the two -> the build dies with
        #
        #   CVTRES : fatal error CVT1100: duplicate resource.
        #            type:MANIFEST, name:1, language:0x0409
        #   LINK   : fatal error LNK1123: failure during conversion to COFF
        #
        # src/app.manifest is the only manifest we want, so stop the toolchain
        # from producing a second one. /MANIFEST:NO covers every MSVC generator
        # (Visual Studio, Ninja, NMake); the VS global additionally skips
        # MSBuild's manifest-tool step so nothing else touches the image.
        target_link_options(BSICustomizer PRIVATE "/MANIFEST:NO")
        set_property(TARGET BSICustomizer APPEND PROPERTY VS_GLOBALS
            "GenerateManifest=false")
    endif()
endif()

# The QML module generates a "BSICustomizer" folder in the build dir, so the
# executable gets a distinct output name to avoid the collision.
#
# WIN32_EXECUTABLE -> GUI subsystem, i.e. no console window flashes behind the
# UI when the released exe is launched from Explorer. Qt supplies the WinMain
# entry point that calls our plain main(). (Same as the official Qt Quick
# application template.)
set_target_properties(BSICustomizer PROPERTIES
    OUTPUT_NAME "BSI-Customizer"
    WIN32_EXECUTABLE TRUE
)

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

# Retain the decoder's BSD notices next to built/installed executables.
add_custom_command(TARGET BSICustomizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/hde64/LICENSE.txt"
        "$<TARGET_FILE_DIR:BSICustomizer>/THIRD-PARTY-NOTICES.txt"
)
install(FILES src/third_party/hde64/LICENSE.txt DESTINATION bin RENAME THIRD-PARTY-NOTICES.txt)
