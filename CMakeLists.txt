cmake_minimum_required(VERSION 3.21)

project(BSICustomizer VERSION 2.5.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick QuickControls2 Svg)

qt_standard_project_setup(REQUIRES 6.5)

# Mark Theme.qml as a QML singleton (must happen BEFORE qt_add_qml_module)
set_source_files_properties(src/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(BSICustomizer
    src/main.cpp
    src/cli.cpp
    src/selftest.cpp
    src/theme_runtime_test.cpp
    src/gradient_runtime_test.cpp
    src/patch_regression_test.cpp
    src/core/PatchFile.h
    src/core/PatchFile.cpp
    src/core/QtResources.h
    src/core/QtResources.cpp
    src/core/QtResourcePatching.cpp
    src/core/QtResourceRegistration.h
    src/core/QtResourceRegistration.cpp
    src/core/ByteUtils.h
    src/core/PeUtils.h
    src/core/PeUtils.cpp
    src/core/ZlibUtils.h
    src/core/ZlibUtils.cpp
    src/core/ThemeEngine.h
    src/core/ThemeEngine.cpp
    src/core/GradientPatcher.h
    src/core/GradientPatcher.cpp
    src/core/ThemePresets.h
    src/core/ThemePresets.cpp
    src/core/SvgEngine.h
    src/core/SvgEngine.cpp
    src/core/FpsPatcher.h
    src/core/FpsPatcher.cpp
    src/core/FpsDetection.cpp
    src/core/X64Prologue.h
    src/core/X64Prologue.cpp
    src/third_party/hde64/hde64.cpp
    src/third_party/hde64/hde64.h
    src/third_party/hde64/table64.h
    src/core/IntegrityPatcher.h
    src/core/IntegrityPatcher.cpp
    src/core/RpcIdScan.h
    src/core/RpcIdScan.cpp
    src/core/RpcIdPatcher.h
    src/core/RpcIdPatcher.cpp
    src/core/VersionResource.h
    src/core/VersionResource.cpp
    src/core/BootVideoPatcher.h
    src/core/BootVideoPatcher.cpp
    src/core/EmbeddedImages.h
    src/core/EmbeddedImages.cpp
    src/core/EmbeddedResources.cpp
    src/core/ExtrasPatcher.h
    src/core/ExtrasPatcher.cpp
    src/core/QmlText.h
    src/core/QmlText.cpp
    src/core/LayoutPatcher.h
    src/core/LayoutPatcher.cpp
    src/core/AboutPatcher.h
    src/core/AboutPatcher.cpp
    src/core/BsDetector.h
    src/core/BsDetector.cpp
    src/core/Backend.h
    src/core/Backend.cpp
    src/core/AuthManager.h
    src/core/AuthManager.cpp
    src/core/SessionStore.h
    src/core/SessionStore.cpp
    src/core/UpdateChecker.h
    src/core/UpdateChecker.cpp
    src/core/ThemeManager.h
    src/core/ThemeManager.cpp
    src/core/SettingsManager.h
    src/core/SettingsManager.cpp
    src/core/SysInfo.h
    src/core/SysInfo.cpp
)

# Windows: embed the requireAdministrator manifest, so patching an
# installation under Program Files never fails on a permission error.
if(WIN32)
    target_sources(BSICustomizer PRIVATE src/app.rc)

    # Re-run rc.exe whenever the manifest XML itself changes (Makefile/Ninja
    # generators; Visual Studio tracks resource dependencies on its own).
    # Also rebuild on icon changes and point RC at the repo root, where the
    # brand icon lives ("BSI CUSTOMIZER.ico").
    set_source_files_properties(src/app.rc PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest;${CMAKE_CURRENT_SOURCE_DIR}/BSI CUSTOMIZER.ico"
        INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")

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

# ---------------------------------------------------------------------------
# Optional build-time auth embed ("zip auth json"): the whole auth.json blob
# comes from the ZLP_AUTH_JSON repository secret (BSIC_AUTH_JSON accepted as
# an alias) and is baked into the EXE as :/bsic_auth/auth.json, so released
# builds sign in with no sidecar file. At runtime an auth.json next to the
# EXE - or a ZLP_AUTH_JSON env blob - still overrides it. No secret -> the
# build simply carries no embed and the file/env paths work as before.
# ---------------------------------------------------------------------------
set(BSI_AUTH_EMBED_DIR "${CMAKE_BINARY_DIR}/generated")
set(BSI_AUTH_EMBED_JSON "${BSI_AUTH_EMBED_DIR}/auth.json")
set(BSI_AUTH_BLOB "$ENV{ZLP_AUTH_JSON}")
if(BSI_AUTH_BLOB STREQUAL "")
    set(BSI_AUTH_BLOB "$ENV{BSIC_AUTH_JSON}")
endif()
if(NOT BSI_AUTH_BLOB STREQUAL "")
    file(MAKE_DIRECTORY "${BSI_AUTH_EMBED_DIR}")
    file(WRITE "${BSI_AUTH_EMBED_JSON}" "${BSI_AUTH_BLOB}")
    string(LENGTH "${BSI_AUTH_BLOB}" BSI_AUTH_BLOB_LEN)
    message(STATUS "Embedding auth.json from ZLP_AUTH_JSON (${BSI_AUTH_BLOB_LEN} bytes)")
    qt_add_resources(BSICustomizer "bsic_auth_embedded"
        BASE "${BSI_AUTH_EMBED_DIR}"
        PREFIX "/bsic_auth"
        FILES "${BSI_AUTH_EMBED_JSON}")
else()
    message(STATUS "No ZLP_AUTH_JSON in the environment: building without an embedded auth.json")
endif()

# Qt 6 is the application's build/runtime dependency, not an FPS target restriction.
# Require a real Qt 5 reference in Windows CI; ordinary/offline builds stay network-free.
set(_bsi_qt5_reference_default OFF)
if(WIN32 AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    set(_bsi_qt5_reference_default ON)
endif()
option(BSI_TEST_QT5_REFERENCE "Acquire and require a real Qt 5 DLL for FPS self-tests" ${_bsi_qt5_reference_default})
if(BSI_TEST_QT5_REFERENCE)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "BSI_TEST_QT5_REFERENCE requires a 64-bit Windows build")
    endif()
    include(cmake/Qt5FpsReference.cmake)
    bsi_qt5_fps_reference("${CMAKE_CURRENT_BINARY_DIR}/fps-reference/qt5" _bsi_qt5_dll)
    target_compile_definitions(BSICustomizer PRIVATE BSI_QT5_REFERENCE_DLL="${_bsi_qt5_dll}")
endif()

# Optional user-supplied diagnostic input. It is read as data and only disposable
# copies are patched by --selftest; its executable code/QML is never run.
set(BSI_EXE_TEST_FILE "" CACHE FILEPATH "Optional HD-Player.exe for read-only reference regression tests")
if(NOT BSI_EXE_TEST_FILE AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true"
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/HD-Player.exe")
    set(BSI_EXE_TEST_FILE "${CMAKE_CURRENT_SOURCE_DIR}/HD-Player.exe")
endif()
if(BSI_EXE_TEST_FILE)
    if(NOT EXISTS "${BSI_EXE_TEST_FILE}")
        message(FATAL_ERROR "BSI_EXE_TEST_FILE does not exist")
    endif()
    file(TO_CMAKE_PATH "${BSI_EXE_TEST_FILE}" _bsi_exe_reference)
    target_compile_definitions(BSICustomizer PRIVATE BSI_EXE_TEST_FILE="${_bsi_exe_reference}")
endif()

qt_add_qml_module(BSICustomizer
    URI BSICustomizer
    VERSION 1.0
    QML_FILES
        src/qml/Main.qml
        src/qml/Theme.qml
        src/qml/Icons.js
        src/qml/LoginGate.qml
        src/qml/UpdateBanner.qml
        src/qml/components/SidebarButton.qml
        src/qml/components/WinControls.qml
        src/qml/components/Card.qml
        src/qml/components/ActionButton.qml
        src/qml/components/ToggleSwitch.qml
        src/qml/components/Stepper.qml
        src/qml/components/ChoiceChips.qml
        src/qml/components/Toast.qml
        src/qml/components/PathRow.qml
        src/qml/components/ColorField.qml
        src/qml/components/GradientEditor.qml
        src/qml/components/LogPanel.qml
        src/qml/pages/DashboardPage.qml
        src/qml/pages/ThemePage.qml
        src/qml/pages/ResourcesPage.qml
        src/qml/pages/FpsPage.qml
        src/qml/pages/IntegrityPage.qml
        src/qml/pages/BootVideoPage.qml
        src/qml/pages/UiPage.qml
        src/qml/pages/RpcidPage.qml
        src/qml/pages/AppearancePage.qml
        src/qml/pages/AboutPage.qml
    RESOURCES
        "BSI CUSTOMIZER.ico"
        src/qml/img/app-icon.png
        src/qml/img/discord-mark.svg
        src/qml/img/orb-cyan.png
        src/qml/img/orb-blue.png
        src/qml/img/orb-green.png
        src/qml/img/orb-violet.png
)

# Short, space-free qrc path used by main.cpp for the window/taskbar icon:
#   QIcon(QStringLiteral(":/app.ico"))
set_source_files_properties("BSI CUSTOMIZER.ico" PROPERTIES
    QT_RESOURCE_ALIAS "app.ico")

target_link_libraries(BSICustomizer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
)

install(TARGETS BSICustomizer RUNTIME DESTINATION bin)
# Discord Rich Presence discovery/patching is compiled into the application,
# UI and command line alike (BSI-Customizer.exe --rpcid ...). The build and
# the finished program need no interpreter or sidecar script of any kind.

# Retain the decoder's BSD notices next to built/installed executables.
add_custom_command(TARGET BSICustomizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/hde64/LICENSE.txt"
        "$<TARGET_FILE_DIR:BSICustomizer>/THIRD-PARTY-NOTICES.txt"
)
install(FILES src/third_party/hde64/LICENSE.txt DESTINATION bin RENAME THIRD-PARTY-NOTICES.txt)

# Packaging belongs to the build graph as well as the release workflow. This
# allows the existing Windows CI build to validate the one-file artifact.
set(_bsi_portable_default OFF)
if(WIN32 AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true")
    set(_bsi_portable_default ON)
endif()
option(BSI_BUILD_PORTABLE "Build and smoke-test the single-file portable EXE" ${_bsi_portable_default})
if(BSI_BUILD_PORTABLE)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "BSI_BUILD_PORTABLE requires Windows x64 and MSVC")
    endif()
    find_program(BSI_PWSH pwsh REQUIRED)
    get_filename_component(_bsi_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    set(BSI_PORTABLE_EXE "${CMAKE_CURRENT_BINARY_DIR}/artifacts/BSI-Customizer-${PROJECT_VERSION}-win64.exe")
    add_custom_target(BSI-Portable ALL
        COMMAND "${BSI_PWSH}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Build-SingleExe.ps1"
            -AppExecutable "$<TARGET_FILE:BSICustomizer>" -QtPrefix "${_bsi_qt_prefix}"
            -Version "${PROJECT_VERSION}" -BuildRoot "${CMAKE_CURRENT_BINARY_DIR}/portable"
            -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}/artifacts"
        COMMAND "${BSI_PWSH}" -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/Test-SingleExe.ps1"
            -ExecutablePath "${BSI_PORTABLE_EXE}"
        DEPENDS BSICustomizer
        BYPRODUCTS "${BSI_PORTABLE_EXE}"
        USES_TERMINAL VERBATIM
    )
endif()
