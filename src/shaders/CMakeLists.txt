cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(BSIShaders VERSION 1.0.0 LANGUAGES C CXX)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(BSI_SHADERS_QT_MAJOR "" CACHE STRING "Explicit target Qt major (5 or 6); never choose the Customizer's Qt for another host")
if(BSI_SHADERS_QT_MAJOR STREQUAL "5" OR BSI_SHADERS_QT_MAJOR STREQUAL "6")
    set(QT_VERSION_MAJOR "${BSI_SHADERS_QT_MAJOR}")
elseif(BSI_SHADERS_QT_MAJOR STREQUAL "")
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Gui Qml)
else()
    message(FATAL_ERROR "BSI_SHADERS_QT_MAJOR must be 5 or 6")
endif()
if(QT_VERSION_MAJOR EQUAL 5)
    set(bsi_qt_minimum 5.15.2)
else()
    set(bsi_qt_minimum 6.2.4)
endif()
# Build the small public-API bridge on the oldest supported SDK, not on the
# Customizer's newest SDK. Qt 5/6 are still separate ABI families.
find_package(Qt${QT_VERSION_MAJOR} ${bsi_qt_minimum} REQUIRED COMPONENTS Core Gui Qml)
if(WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The shader plugin currently supports x64 players only")
endif()
add_library(BSIShaders MODULE ShaderPlugin.cpp ShaderBridge.cpp ShaderBridge.h
    ShaderPreferences.cpp ShaderPreferences.h NativeDisplayBinding.cpp NativeDisplayBinding.h PotatoResolution.h Runtime.cpp
    Runtime.h RendererCapabilities.h Parameters.h ShaderSources.h GlPipeline.cpp GlPipeline.h
    GlTextureOverride.cpp GlTextureOverride.h GlDrawCalls.h TextureQuality.h GlTextureQuality.h
    VulkanPipeline.cpp VulkanPipeline.h VulkanSpirv.h PresentationHooks.cpp shaders.qrc)
target_include_directories(BSIShaders PRIVATE ../third_party/vulkan/include)
target_link_libraries(BSIShaders PRIVATE Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Qml)
set(BSI_SHADERS_EXPECT_QT_VERSION "" CACHE STRING "Optional packed QT_VERSION required by the payload builder; never a descriptor override")
if(NOT BSI_SHADERS_EXPECT_QT_VERSION STREQUAL "")
    if(NOT BSI_SHADERS_EXPECT_QT_VERSION MATCHES "^[0-9]+$")
        message(FATAL_ERROR "BSI_SHADERS_EXPECT_QT_VERSION must be a packed QT_VERSION integer")
    endif()
    target_compile_definitions(BSIShaders PRIVATE BSI_SHADERS_EXPECT_QT_VERSION=${BSI_SHADERS_EXPECT_QT_VERSION})
endif()
if(WIN32)
    target_sources(BSIShaders PRIVATE D3d11Pipeline.cpp D3d11Pipeline.h D3d12Pipeline.cpp D3d12Pipeline.h HlslSources.h
        DxgiHooks.cpp VulkanHooks.cpp PresentationHooks.h
        ../third_party/minhook/src/buffer.c ../third_party/minhook/src/hook.c
        ../third_party/minhook/src/trampoline.c ../third_party/minhook/src/hde/hde64.c)
    target_include_directories(BSIShaders PRIVATE ../third_party/minhook/include)
    target_link_libraries(BSIShaders PRIVATE user32 gdi32 psapi)
    target_compile_definitions(BSIShaders PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        # VS 2022's newer constexpr std::mutex initialization crashes inside
        # the older MSVCP140.dll bundled with Qt 5/player installations. Use
        # MSVC's supported legacy initialization path; never overwrite the
        # player's CRT DLLs or require a global runtime upgrade just for this.
        target_compile_definitions(BSIShaders PRIVATE _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    endif()
endif()
set(module_dir "${CMAKE_CURRENT_BINARY_DIR}/module/BSIShaders")
set_target_properties(BSIShaders PROPERTIES OUTPUT_NAME BSIShaders
    LIBRARY_OUTPUT_DIRECTORY "${module_dir}" RUNTIME_OUTPUT_DIRECTORY "${module_dir}")
if(WIN32)
    set_target_properties(BSIShaders PROPERTIES PREFIX "")
endif()
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(BSIShaders PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${config} "${module_dir}"
        RUNTIME_OUTPUT_DIRECTORY_${config} "${module_dir}")
endforeach()
file(MAKE_DIRECTORY "${module_dir}")
configure_file(qmldir "${module_dir}/qmldir" COPYONLY)
install(TARGETS BSIShaders LIBRARY DESTINATION BSIShaders RUNTIME DESTINATION BSIShaders)
install(FILES qmldir DESTINATION BSIShaders)
option(BSI_SHADERS_BUILD_TESTS "Build isolated shader pipeline tests" OFF)
if(BSI_SHADERS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(../../tests/shaders "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()
