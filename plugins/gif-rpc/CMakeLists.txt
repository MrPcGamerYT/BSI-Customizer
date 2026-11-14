cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(BsiGifRpcTests LANGUAGES CXX)
endif()
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The version proxy requires MSVC x64")
endif()
enable_language(ASM_MASM)
add_library(BsiGifRpcProxy SHARED proxy.cpp exports.asm version.def)
set_target_properties(BsiGifRpcProxy PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF OUTPUT_NAME version PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gif-rpc"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_features(BsiGifRpcProxy PRIVATE cxx_std_17)
add_executable(BsiGifRpcSmoke smoke.cpp)
set_target_properties(BsiGifRpcSmoke PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    OUTPUT_NAME HD-Player RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gif-rpc-smoke"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_library(BsiGifRpcLoaderTest SHARED loader-test.cpp)
set_target_properties(BsiGifRpcLoaderTest PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gif-rpc-smoke"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(BsiGifRpcLoaderTest PRIVATE BsiGifRpcProxy)
add_dependencies(BsiGifRpcSmoke BsiGifRpcLoaderTest)
target_compile_definitions(BsiGifRpcSmoke PRIVATE BSI_LOADER_TEST="$<TARGET_FILE:BsiGifRpcLoaderTest>")

add_executable(BsiGifRpcStaticTest static-startup-test.cpp)
set_target_properties(BsiGifRpcStaticTest PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    OUTPUT_NAME HD-Player RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gif-rpc-static"
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(BsiGifRpcStaticTest PRIVATE BsiGifRpcProxy)
target_link_options(BsiGifRpcStaticTest PRIVATE /STACK:131072,4096)
add_custom_command(TARGET BsiGifRpcStaticTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:BsiGifRpcProxy>" "$<TARGET_FILE_DIR:BsiGifRpcStaticTest>/version.dll")
