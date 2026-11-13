# Test data only: Qt 5.15.2's Windows x64 QuickTemplates2 DLL. The official
# PySide2 5.15.2.1 wheel is simply a hash-pinned ZIP archive that happens to
# contain it; nothing from it is installed, imported or executed, and no
# interpreter is involved. Only that one DLL is extracted, as data, and Qt 5 is
# never linked into the Qt 6 application.
function(bsi_qt5_fps_reference directory result)
    set(wheel "PySide2-5.15.2.1-5.15.2-cp35.cp36.cp37.cp38.cp39.cp310-none-win_amd64.whl")
    set(url "https://files.pythonhosted.org/packages/ed/5e/d0d540385dcc5eeda877a4288a9a4970e0e7b2282e840c2b08ce5442542e/${wheel}")
    set(wheel_sha "af6b263fe63ba6dea7eaebae80aa7b291491fe66f4f0057c0aafe780cc83da9d")
    set(dll_sha "97ea065e1c033f81e77d63f5182c07f1e6bc2098fd5fc334cc35c827826c23ee")
    set(dll "${directory}/PySide2/Qt5QuickTemplates2.dll")
    if(EXISTS "${dll}")
        file(SHA256 "${dll}" actual)
        if(actual STREQUAL dll_sha)
            set(${result} "${dll}" PARENT_SCOPE)
            return()
        endif()
        message(FATAL_ERROR "Qt 5 FPS test reference has changed. Clear ${directory} and reconfigure.")
    endif()

    file(MAKE_DIRECTORY "${directory}")
    set(archive "${directory}/${wheel}")
    message(STATUS "Acquiring hash-pinned Qt 5 FPS test data (not an application dependency)")
    file(DOWNLOAD "${url}" "${archive}"
        EXPECTED_HASH "SHA256=${wheel_sha}"
        TLS_VERIFY ON
        TIMEOUT 180
        INACTIVITY_TIMEOUT 60
        STATUS status
    )
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Could not acquire the required Qt 5 FPS test data: ${status}")
    endif()
    # Extract only this data file; no code from the archive is ever run.
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${directory}"
        PATTERNS "PySide2/Qt5QuickTemplates2.dll")
    file(SHA256 "${dll}" actual)
    if(NOT actual STREQUAL dll_sha)
        message(FATAL_ERROR "Qt 5 FPS reference DLL hash mismatch")
    endif()
    file(REMOVE "${archive}")
    set(${result} "${dll}" PARENT_SCOPE)
endfunction()
