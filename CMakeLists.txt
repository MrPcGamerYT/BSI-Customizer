cmake_minimum_required(VERSION 3.15)
project(BS_THEME_CHANGER)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGui 1.91.9b - same as original
# Download: https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../imgui-1.91.9b)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(BS_THEME_CHANGER
    main.cpp
    patcher.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(BS_THEME_CHANGER PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(BS_THEME_CHANGER PRIVATE
    d3d11
    d3dcompiler
    dwmapi
    comdlg32
    shell32
)

# Manifest - same as original ThemeStudio requireAdministrator
if(MSVC)
    target_link_options(BS_THEME_CHANGER PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    target_sources(BS_THEME_CHANGER PRIVATE ../ThemeStudio.manifest)
endif()

# zlib 1.3.2
find_package(ZLIB REQUIRED)
target_link_libraries(BS_THEME_CHANGER PRIVATE ZLIB::ZLIB)

# Set output name same as original
set_target_properties(BS_THEME_CHANGER PROPERTIES OUTPUT_NAME "BS_THEME_CHANGER")

# Same subsystem as original (GUI)
if(MSVC)
    set_target_properties(BS_THEME_CHANGER PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
