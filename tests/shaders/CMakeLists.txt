cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(BSIShaderPipelineTests LANGUAGES C CXX)
    enable_testing()
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(shader_dir "${CMAKE_CURRENT_LIST_DIR}/../../src/shaders")
option(BSI_TEST_NATIVE_SETTINGS "Require the Qt Core native Settings source/image tests" OFF)
if(BSI_TEST_NATIVE_SETTINGS AND NOT TARGET Qt${QT_VERSION_MAJOR}::Core)
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)
endif()
if(TARGET Qt${QT_VERSION_MAJOR}::Core)
    add_executable(shader-preferences-test shader_preferences_test.cpp
        "${shader_dir}/ShaderPreferences.cpp" "${shader_dir}/NativeDisplayBinding.cpp")
    set_target_properties(shader-preferences-test PROPERTIES AUTOMOC ON)
    target_include_directories(shader-preferences-test PRIVATE "${shader_dir}")
    target_link_libraries(shader-preferences-test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    add_test(NAME shader-native-potato-preferences COMMAND shader-preferences-test)
    if(NOT TARGET bsi_zopfli)
        set(BSI_ZOPFLI_TESTING ON)
    endif()
    add_subdirectory("${shader_dir}/../third_party/zopfli" "${CMAKE_CURRENT_BINARY_DIR}/zopfli")
    set(core_dir "${shader_dir}/../core")
    add_executable(shader-settings-source-test settings_source_test.cpp
        "${core_dir}/ShaderSettingsSource.cpp" "${core_dir}/NativeSettingsModel.cpp" "${core_dir}/QmlText.cpp")
    target_include_directories(shader-settings-source-test PRIVATE "${core_dir}")
    target_link_libraries(shader-settings-source-test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    add_test(NAME shader-native-settings-source COMMAND shader-settings-source-test)
    add_executable(shader-settings-image-test settings_image_test.cpp
        "${core_dir}/ShaderSettingsImage.cpp" "${core_dir}/ShaderSettingsSource.cpp"
        "${core_dir}/NativeSettingsModel.cpp" "${core_dir}/QmlText.cpp"
        "${core_dir}/QtResources.cpp" "${core_dir}/QtResourcePatching.cpp"
        "${core_dir}/QtResourceRegistration.cpp" "${core_dir}/PeUtils.cpp" "${core_dir}/ZlibUtils.cpp")
    target_include_directories(shader-settings-image-test PRIVATE "${core_dir}")
    target_link_libraries(shader-settings-image-test PRIVATE Qt${QT_VERSION_MAJOR}::Core bsi_zopfli)
    add_test(NAME shader-native-settings-image COMMAND shader-settings-image-test)
    add_executable(phone-profiles-test ../phone_profiles_test.cpp
        "${core_dir}/PhoneProfiles.cpp" "${core_dir}/PhoneProfilesInstall.cpp"
        "${core_dir}/PatchFile.cpp" "${core_dir}/QmlText.cpp"
        "${core_dir}/QtResources.cpp" "${core_dir}/QtResourcePatching.cpp"
        "${core_dir}/QtResourceRegistration.cpp" "${core_dir}/PeUtils.cpp" "${core_dir}/ZlibUtils.cpp")
    target_include_directories(phone-profiles-test PRIVATE "${core_dir}" "${CMAKE_CURRENT_LIST_DIR}/..")
    target_link_libraries(phone-profiles-test PRIVATE Qt${QT_VERSION_MAJOR}::Core bsi_zopfli)
    add_test(NAME phone-profiles-native-contract COMMAND phone-profiles-test)
    add_executable(shader-resource-compression-test resource_compression_test.cpp "${core_dir}/ZlibUtils.cpp")
    target_include_directories(shader-resource-compression-test PRIVATE "${core_dir}")
    target_link_libraries(shader-resource-compression-test PRIVATE Qt${QT_VERSION_MAJOR}::Core bsi_zopfli)
    add_test(NAME shader-resource-compression COMMAND shader-resource-compression-test)
    if(BSI_ZOPFLI_TESTING)
        add_executable(shader-bounded-compression-test bounded_zopfli_test.c)
        set_target_properties(shader-bounded-compression-test PROPERTIES
            C_STANDARD 99 C_STANDARD_REQUIRED ON AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(shader-bounded-compression-test PRIVATE "${core_dir}")
        target_compile_definitions(shader-bounded-compression-test PRIVATE BSI_ZOPFLI_TESTING)
        target_link_libraries(shader-bounded-compression-test PRIVATE bsi_zopfli)
        add_test(NAME shader-bounded-compression COMMAND shader-bounded-compression-test)
    endif()
    add_executable(shader-compression-failure-test compression_failure_test.cpp "${core_dir}/ZlibUtils.cpp")
    target_include_directories(shader-compression-failure-test PRIVATE "${core_dir}")
    target_link_libraries(shader-compression-failure-test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    add_test(NAME shader-compression-failure COMMAND shader-compression-failure-test)
    add_executable(shader-package-test package_test.cpp "${core_dir}/ShaderPackage.cpp")
    target_include_directories(shader-package-test PRIVATE "${core_dir}")
    target_link_libraries(shader-package-test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    add_test(NAME shader-package-validation COMMAND shader-package-test)
    if(WIN32 AND TARGET BSIShaders)
        add_test(NAME shader-actual-windows-package COMMAND shader-package-test
            "${module_dir}" "$<TARGET_FILE_DIR:Qt${QT_VERSION_MAJOR}::Core>")
    endif()
    add_executable(shader-bundle-test bundle_test.cpp "${core_dir}/ShaderBundle.cpp" "${core_dir}/ShaderPackage.cpp")
    target_include_directories(shader-bundle-test PRIVATE "${core_dir}")
    target_link_libraries(shader-bundle-test PRIVATE Qt${QT_VERSION_MAJOR}::Core)
    add_test(NAME shader-bundle-validation COMMAND shader-bundle-test)
    add_test(NAME shader-bundle-cmake COMMAND ${CMAKE_COMMAND}
        "-DTEST_EXE=$<TARGET_FILE:shader-bundle-test>" "-DSOURCE_ROOT=${shader_dir}/../.."
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/bundle-cmake-fixtures" -P "${CMAKE_CURRENT_LIST_DIR}/bundle_cmake_test.cmake")
    add_executable(shader-install-test install_test.cpp "${core_dir}/ShaderInstall.cpp"
        "${core_dir}/ShaderActions.cpp" "${core_dir}/ShaderBundle.cpp"
        "${core_dir}/ShaderPackage.cpp" "${core_dir}/ShaderSettingsImage.cpp"
        "${core_dir}/ShaderSettingsSource.cpp" "${core_dir}/NativeSettingsModel.cpp" "${core_dir}/QmlText.cpp"
        "${core_dir}/QtResources.cpp" "${core_dir}/QtResourcePatching.cpp"
        "${core_dir}/QtResourceRegistration.cpp" "${core_dir}/PeUtils.cpp" "${core_dir}/ZlibUtils.cpp")
    target_include_directories(shader-install-test PRIVATE "${core_dir}")
    target_compile_definitions(shader-install-test PRIVATE BSI_SHADER_INSTALL_TESTING)
    target_link_libraries(shader-install-test PRIVATE Qt${QT_VERSION_MAJOR}::Core bsi_zopfli)
    add_test(NAME shader-install-recovery COMMAND shader-install-test)
    set_tests_properties(shader-install-recovery PROPERTIES TIMEOUT 300)
endif()
add_executable(shader-renderer-routing-test renderer_capabilities_test.cpp runtime_hooks_stub.cpp "${shader_dir}/Runtime.cpp")
target_include_directories(shader-renderer-routing-test PRIVATE "${shader_dir}")
if(WIN32)
    target_link_libraries(shader-renderer-routing-test PRIVATE user32)
endif()
add_test(NAME shader-renderer-routing COMMAND shader-renderer-routing-test)
# Experimental descriptor policy only: deliberately not wired to player hooks.
add_executable(shader-texture-quality-test texture_quality_test.cpp)
target_include_directories(shader-texture-quality-test PRIVATE "${shader_dir}" "${shader_dir}/../third_party/vulkan/include")
if(WIN32)
    target_compile_definitions(shader-texture-quality-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()
add_test(NAME shader-texture-quality-policy COMMAND shader-texture-quality-test)
add_executable(shader-vulkan-present-test vulkan_present_test.cpp)
target_include_directories(shader-vulkan-present-test PRIVATE "${shader_dir}" "${shader_dir}/../third_party/vulkan/include")
add_test(NAME shader-vulkan-present-contract COMMAND shader-vulkan-present-test)
add_executable(shader-vulkan-pixels-test vulkan_pixels_test.cpp "${shader_dir}/VulkanPipeline.cpp")
target_include_directories(shader-vulkan-pixels-test PRIVATE "${shader_dir}" "${shader_dir}/../third_party/vulkan/include")
target_link_libraries(shader-vulkan-pixels-test PRIVATE ${CMAKE_DL_LIBS})
# Vulkan is mandatory in the software/API CI job, not treated as a skipped pass
# when a Windows host has no ICD. That job records an explicit driver setup.
option(BSI_TEST_VULKAN "Run native Vulkan pixels (requires a working ICD)" ON)
if(BSI_TEST_VULKAN)
    add_test(NAME shader-vulkan-pixels COMMAND shader-vulkan-pixels-test)
endif()
if(WIN32)
    add_executable(shader-d3d-pixels-test d3d_pixels_test.cpp "${shader_dir}/D3d11Pipeline.cpp" "${shader_dir}/D3d12Pipeline.cpp")
    target_include_directories(shader-d3d-pixels-test PRIVATE "${shader_dir}")
    target_compile_definitions(shader-d3d-pixels-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(shader-d3d-pixels-test PRIVATE d3d11 d3d12 dxgi)
    add_test(NAME shader-d3d11-warp-pixels COMMAND shader-d3d-pixels-test d3d11)
    add_test(NAME shader-d3d12-warp-pixels COMMAND shader-d3d-pixels-test d3d12)
    set(minhook_dir "${shader_dir}/../third_party/minhook")
    add_library(shader-presentation-test-code OBJECT
        "${shader_dir}/Runtime.cpp" "${shader_dir}/PresentationHooks.cpp" "${shader_dir}/DxgiHooks.cpp"
        "${shader_dir}/VulkanHooks.cpp" "${shader_dir}/GlPipeline.cpp" "${shader_dir}/VulkanPipeline.cpp"
        "${shader_dir}/D3d11Pipeline.cpp" "${shader_dir}/D3d12Pipeline.cpp"
        "${minhook_dir}/src/buffer.c" "${minhook_dir}/src/hook.c" "${minhook_dir}/src/trampoline.c" "${minhook_dir}/src/hde/hde64.c")
    target_include_directories(shader-presentation-test-code PUBLIC "${shader_dir}" "${minhook_dir}/include" "${shader_dir}/../third_party/vulkan/include")
    target_compile_definitions(shader-presentation-test-code PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(shader-presentation-test-code PUBLIC user32 gdi32 psapi)
    add_executable(shader-dxgi-hooks-test dxgi_hooks_test.cpp)
    target_link_libraries(shader-dxgi-hooks-test PRIVATE shader-presentation-test-code d3d11 dxgi)
    add_test(NAME shader-dxgi-presentation-hooks COMMAND shader-dxgi-hooks-test)
    # No ICD is available on every Windows runner. These clearly labelled
    # dispatch doubles exercise the actual MinHook/provider/HWND lifecycle;
    # they never substitute for the separate real Vulkan pipeline pixel tests.
    foreach(provider A B)
        add_library(shader-vulkan-fixture-${provider} SHARED vulkan_loader_fixture.cpp)
        target_include_directories(shader-vulkan-fixture-${provider} PRIVATE "${shader_dir}/../third_party/vulkan/include")
        target_compile_definitions(shader-vulkan-fixture-${provider} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        set_target_properties(shader-vulkan-fixture-${provider} PROPERTIES PREFIX "" OUTPUT_NAME "BsiVulkanFixture${provider}")
    endforeach()
    add_executable(shader-vulkan-hooks-test vulkan_hooks_test.cpp)
    target_link_libraries(shader-vulkan-hooks-test PRIVATE shader-presentation-test-code)
    add_dependencies(shader-vulkan-hooks-test shader-vulkan-fixture-A shader-vulkan-fixture-B)
    add_test(NAME shader-vulkan-loader-hooks-contract COMMAND shader-vulkan-hooks-test
        "$<TARGET_FILE:shader-vulkan-fixture-A>" "$<TARGET_FILE:shader-vulkan-fixture-B>")
endif()
