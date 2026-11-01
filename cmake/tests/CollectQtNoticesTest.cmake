# Offline self-test for cmake/CollectQtNotices.cmake.
#
#   cmake [-DWORK_DIR=<dir>] -P cmake/tests/CollectQtNoticesTest.cmake
#
# Builds a synthetic "Qt module" archive, serves it over file:// and checks
# that the packaging step collects exactly the licence data it must: the
# module's SPDX texts, third-party attribution entries, the licence files they
# reference in either of Qt's two conventions - and nothing else. No network
# access, no interpreter and no Qt installation are involved.

cmake_minimum_required(VERSION 3.21)

get_filename_component(repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT DEFINED WORK_DIR OR WORK_DIR STREQUAL "")
    set(WORK_DIR "${repo}/build/qt-notices-test")
endif()
file(TO_CMAKE_PATH "${WORK_DIR}" WORK_DIR)

set(failures 0)
function(expect message)
    if(${ARGN})
        message(STATUS "  PASS  ${message}")
    else()
        message(STATUS "  FAIL  ${message}")
        math(EXPR failures "${failures} + 1")
        set(failures "${failures}" PARENT_SCOPE)
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(modules qtbase qtdeclarative qtsvg qtimageformats qtshadertools qtmultimedia)
set(version 9.9.9)

foreach(module IN LISTS modules)
    set(root "${WORK_DIR}/src/${module}-${version}")
    file(WRITE "${root}/LICENSES/LGPL-3.0-only.txt" "LGPL text for ${module}\n")
    file(WRITE "${root}/LICENSES/BSD-3-Clause.txt" "BSD text for ${module}\n")
    # LicenseFile relative to the attribution file ...
    file(WRITE "${root}/src/corelib/3rdparty/zlib/qt_attribution.json"
         "{\n  \"Id\": \"zlib\",\n  \"License\": \"Zlib\",\n  \"LicenseFile\": \"LICENSE\"\n}\n")
    file(WRITE "${root}/src/corelib/3rdparty/zlib/LICENSE" "ZLIB LICENSE TEXT for ${module}\n")
    # ... and relative to the module root, as a JSON array of entries.
    file(WRITE "${root}/src/gui/qt_attribution.json"
         "[\n  { \"Id\": \"foo\", \"LicenseFile\": \"src/3rdparty/foo/LICENSE\" },\n  { \"Id\": \"bar\", \"License\": \"MIT\" }\n]\n")
    file(WRITE "${root}/src/3rdparty/foo/LICENSE" "FOO LICENSE TEXT for ${module}\n")
    # Must be ignored: not part of the shipped sources.
    file(WRITE "${root}/tests/qt_attribution.json" "{ \"Id\": \"t\", \"LicenseFile\": \"missing\" }\n")
    file(WRITE "${root}/src/secret_source.cpp" "int main() { return 0; }\n")

    set(served "${WORK_DIR}/www/${module}/zip/refs/tags")
    file(MAKE_DIRECTORY "${served}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${served}/v${version}"
                        --format=zip -- "${module}-${version}"
                    WORKING_DIRECTORY "${WORK_DIR}/src"
                    RESULT_VARIABLE packed OUTPUT_QUIET)
    if(NOT packed EQUAL 0)
        message(FATAL_ERROR "Could not build the test archive for ${module}")
    endif()
    file(REMOVE_RECURSE "${root}")
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DQT_VERSION=${version}"
        "-DOUTPUT_DIR=${WORK_DIR}/out"
        "-DCACHE_DIR=${WORK_DIR}/cache"
        "-DSOURCE_BASE_URL=file://${WORK_DIR}/www"
        -P "${repo}/cmake/CollectQtNotices.cmake"
    RESULT_VARIABLE collected
    OUTPUT_VARIABLE log
    ERROR_VARIABLE log
)
if(NOT collected EQUAL 0)
    message("${log}")
    message(FATAL_ERROR "Collecting the notices failed")
endif()

expect("QT-NOTICES.txt is produced" EXISTS "${WORK_DIR}/out/QT-NOTICES.txt")
expect("SOURCES.json is produced" EXISTS "${WORK_DIR}/out/SOURCES.json")
file(READ "${WORK_DIR}/out/QT-NOTICES.txt" notices)
file(READ "${WORK_DIR}/out/SOURCES.json" sources)

foreach(module IN LISTS modules)
    string(FIND "${notices}" "LGPL text for ${module}" found)
    expect("${module}: the module's own licence text is included" found GREATER -1)
    string(FIND "${notices}" "ZLIB LICENSE TEXT for ${module}" found)
    expect("${module}: a licence referenced next to its attribution file is included" found GREATER -1)
    string(FIND "${notices}" "FOO LICENSE TEXT for ${module}" found)
    expect("${module}: a licence referenced from the module root is included" found GREATER -1)
    string(FIND "${sources}" "\"module\": \"${module}\"" found)
    expect("${module}: the source archive is recorded with its URL and hash" found GREATER -1)
endforeach()

string(FIND "${notices}" "secret_source" found)
expect("source code is never copied into the notices" found EQUAL -1)
string(FIND "${notices}" "\"Id\": \"t\"" found)
expect("attribution files outside src/ are ignored" found EQUAL -1)
string(REGEX MATCHALL "\"sha256\": \"[0-9a-f]+\"" hashes "${sources}")
list(LENGTH hashes hash_count)
list(LENGTH modules module_count)
expect("every module archive is hashed" hash_count EQUAL module_count)

# A referenced licence that is not in the archive must fail loudly instead of
# shipping an incomplete notice file.
set(broken "${WORK_DIR}/broken")
file(WRITE "${broken}/src/qtbase-${version}/LICENSES/MIT.txt" "MIT\n")
file(WRITE "${broken}/src/qtbase-${version}/src/foo/qt_attribution.json"
     "{ \"Id\": \"x\", \"LicenseFile\": \"nowhere/LICENSE\" }\n")
file(MAKE_DIRECTORY "${broken}/www/qtbase/zip/refs/tags")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf
                    "${broken}/www/qtbase/zip/refs/tags/v${version}"
                    --format=zip -- "qtbase-${version}"
                WORKING_DIRECTORY "${broken}/src" OUTPUT_QUIET)
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DQT_VERSION=${version}" "-DOUTPUT_DIR=${broken}/out" "-DCACHE_DIR=${broken}/cache"
        "-DSOURCE_BASE_URL=file://${broken}/www"
        -P "${repo}/cmake/CollectQtNotices.cmake"
    RESULT_VARIABLE rejected OUTPUT_VARIABLE reject_log ERROR_VARIABLE reject_log)
expect("a missing referenced licence is reported, not ignored" NOT rejected EQUAL 0)
string(FIND "${reject_log}" "Missing license text" found)
expect("the failure names the unresolved licence file" found GREATER -1)

# The cached archive is reused: the same run works with the server gone.
file(RENAME "${WORK_DIR}/www" "${WORK_DIR}/www-offline")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DQT_VERSION=${version}" "-DOUTPUT_DIR=${WORK_DIR}/out2" "-DCACHE_DIR=${WORK_DIR}/cache"
        "-DSOURCE_BASE_URL=file://${WORK_DIR}/www"
        -P "${repo}/cmake/CollectQtNotices.cmake"
    RESULT_VARIABLE cached OUTPUT_QUIET ERROR_QUIET)
expect("a cached archive is reused instead of downloaded again" cached EQUAL 0)
if(cached EQUAL 0)
    file(READ "${WORK_DIR}/out2/QT-NOTICES.txt" again)
    expect("a repeated run produces identical notices" again STREQUAL notices)
endif()

# WebEngine-capable packaging adds modules that may have no attribution JSON.
foreach(module qtwebengine qtwebchannel qtpositioning qtserialport)
    set(root "${WORK_DIR}/extra/${module}-${version}")
    file(WRITE "${root}/LICENSES/LGPL-3.0-only.txt" "SPDX-only text for ${module}\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${WORK_DIR}/cache/${module}-${version}.zip"
        --format=zip -- "${module}-${version}" WORKING_DIRECTORY "${WORK_DIR}/extra"
        RESULT_VARIABLE packed OUTPUT_QUIET)
    expect("${module}: SPDX-only fixture archive" packed EQUAL 0)
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DQT_VERSION=${version}" "-DBSI_WITH_WEBENGINE=ON"
    "-DOUTPUT_DIR=${WORK_DIR}/web-out" "-DCACHE_DIR=${WORK_DIR}/cache"
    "-DSOURCE_BASE_URL=file://${WORK_DIR}/www"
    -P "${repo}/cmake/CollectQtNotices.cmake"
    RESULT_VARIABLE web_collected OUTPUT_QUIET ERROR_VARIABLE web_error)
expect("WebEngine modules without attribution JSON collect successfully" web_collected EQUAL 0)
if(web_collected EQUAL 0)
    file(READ "${WORK_DIR}/web-out/QT-NOTICES.txt" web_notices)
    foreach(module qtwebengine qtwebchannel qtpositioning qtserialport)
        string(FIND "${web_notices}" "SPDX-only text for ${module}" found)
        expect("${module}: own licence is retained without attribution files" found GREATER -1)
    endforeach()
else()
    message("${web_error}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
if(failures GREATER 0)
    message(FATAL_ERROR "Qt notice collection self-test: ${failures} failure(s)")
endif()
message(STATUS "Qt notice collection self-test: ALL TESTS PASSED")
