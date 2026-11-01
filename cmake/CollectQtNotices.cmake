# Collect Qt licensing/attribution text as data; never execute downloaded code.
#
# Script mode only:
#   cmake -DQT_VERSION=6.8.3 -DOUTPUT_DIR=<dir> -DCACHE_DIR=<dir>
#         [-DSOURCE_BASE_URL=<url>] -P cmake/CollectQtNotices.cmake
#
# Writes <OUTPUT_DIR>/QT-NOTICES.txt and <OUTPUT_DIR>/SOURCES.json. The source
# archives are cached in the ignored build tree and only licence/attribution
# members are ever extracted from them. Notices intentionally include a
# conservative superset of the third-party code in the deployed Qt modules.
#
# CMake is already required to build this project, so packaging needs no
# interpreter, helper script or extra tool beyond the compiler toolchain.

cmake_minimum_required(VERSION 3.21)

set(BSI_QT_NOTICE_MODULES qtbase qtdeclarative qtsvg qtimageformats qtshadertools qtmultimedia)
if(BSI_WITH_WEBENGINE)
    list(APPEND BSI_QT_NOTICE_MODULES qtwebengine qtwebchannel qtpositioning)
endif()
set(BSI_MAX_ARCHIVE_BYTES 536870912)   # 512 MiB per module archive
set(BSI_MAX_NOTICE_BYTES 4194304)      # 4 MiB per licence text

if(NOT DEFINED QT_VERSION OR NOT QT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "QT_VERSION must be numeric, e.g. -DQT_VERSION=6.8.3")
endif()
if(NOT DEFINED OUTPUT_DIR OR OUTPUT_DIR STREQUAL "")
    message(FATAL_ERROR "OUTPUT_DIR is required")
endif()
if(NOT DEFINED CACHE_DIR OR CACHE_DIR STREQUAL "")
    message(FATAL_ERROR "CACHE_DIR is required")
endif()
if(NOT DEFINED SOURCE_BASE_URL OR SOURCE_BASE_URL STREQUAL "")
    set(SOURCE_BASE_URL "https://codeload.github.com/qt")
endif()

file(TO_CMAKE_PATH "${OUTPUT_DIR}" OUTPUT_DIR)
file(TO_CMAKE_PATH "${CACHE_DIR}" CACHE_DIR)
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${CACHE_DIR}")

set(notices "${OUTPUT_DIR}/QT-NOTICES.txt")
set(separator "==============================================================================")

file(WRITE "${notices}"
"Qt ${QT_VERSION}: licensing and third-party attribution notices\n"
"\n"
"Qt is copyright The Qt Company Ltd. and other contributors.\n"
"These dynamically linked libraries can be inspected/replaced using\n"
"the portable EXE's --extract-to option. See the SPDX license texts\n"
"below and the upstream source URLs for the corresponding sources.\n"
"Notices intentionally include a superset (including build/test code);\n"
"their inclusion does not assert that every component is shipped.\n")

# Append one file's text as data. Binary-looking or oversized members are an
# error rather than something silently dropped from the notices.
function(bsi_append_text label path)
    file(SIZE "${path}" size)
    if(size GREATER BSI_MAX_NOTICE_BYTES)
        message(FATAL_ERROR "Oversized notice: ${label}")
    endif()
    file(READ "${path}" text)
    file(APPEND "${notices}" "\n--- ${label} ---\n${text}\n")
endfunction()

set(sources_json "[")
set(first_source TRUE)

foreach(module IN LISTS BSI_QT_NOTICE_MODULES)
    set(url "${SOURCE_BASE_URL}/${module}/zip/refs/tags/v${QT_VERSION}")
    set(archive "${CACHE_DIR}/${module}-${QT_VERSION}.zip")
    if(NOT EXISTS "${archive}")
        set(pending "${archive}.download")
        file(REMOVE "${pending}")
        file(DOWNLOAD "${url}" "${pending}"
            TLS_VERIFY ON
            TIMEOUT 600
            INACTIVITY_TIMEOUT 120
            STATUS status
        )
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            file(REMOVE "${pending}")
            message(FATAL_ERROR "Could not download ${url}: ${status}")
        endif()
        file(SIZE "${pending}" downloaded)
        if(downloaded GREATER BSI_MAX_ARCHIVE_BYTES)
            file(REMOVE "${pending}")
            message(FATAL_ERROR "Qt source archive exceeds the safety limit: ${module}")
        endif()
        file(RENAME "${pending}" "${archive}")
    endif()
    file(SHA256 "${archive}" archive_sha)

    # The member list decides which paths exist; nothing is guessed. Reading
    # the table of contents never runs any code from the archive.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar tf "${archive}"
        OUTPUT_VARIABLE listing RESULT_VARIABLE listing_result
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT listing_result EQUAL 0)
        message(FATAL_ERROR "Could not read the archive index of ${module}")
    endif()
    string(REPLACE ";" "\\;" listing "${listing}")
    string(REPLACE "\r" "" listing "${listing}")
    string(REPLACE "\n" ";" members "${listing}")

    # Extract licence data only - never sources, build files or scripts.
    set(work "${CACHE_DIR}/extract/${module}")
    file(REMOVE_RECURSE "${work}")
    file(MAKE_DIRECTORY "${work}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${work}"
        PATTERNS "*/LICENSES/*" "*/qt_attribution.json")

    file(GLOB roots RELATIVE "${work}" "${work}/*")
    list(LENGTH roots root_count)
    if(NOT root_count EQUAL 1)
        message(FATAL_ERROR "Unexpected archive layout for ${module}")
    endif()
    list(GET roots 0 root)

    file(APPEND "${notices}" "\n${separator}\n${module} ${QT_VERSION}\nSource: ${url}\n")

    # 1) The module's own SPDX licence texts, in full.
    file(GLOB_RECURSE license_files RELATIVE "${work}" "${work}/${root}/LICENSES/*")
    list(SORT license_files)
    foreach(member IN LISTS license_files)
        string(REGEX REPLACE "^${root}/" "" label "${member}")
        bsi_append_text("${label}" "${work}/${member}")
    endforeach()

    # 2) Third-party attribution entries from the shipped sources, plus the
    #    licence text each of them points at.
    file(GLOB_RECURSE attributions RELATIVE "${work}" "${work}/${root}/*qt_attribution.json")
    list(SORT attributions)
    set(wanted "")
    set(pending_entries "")
    foreach(member IN LISTS attributions)
        if(NOT member MATCHES "/src/")
            continue()
        endif()
        file(READ "${work}/${member}" document)
        get_filename_component(member_dir "${member}" DIRECTORY)

        # LicenseFile can be a string or a list, and may sit anywhere in the
        # entry; collect every one of them without assuming a fixed schema.
        set(referenced "")
        string(JSON entry_type ERROR_VARIABLE json_error TYPE "${document}")
        if(json_error)
            message(WARNING "Unparsable attribution file kept as raw text: ${member}")
        endif()
        string(REGEX MATCHALL "\"LicenseFile\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]*)\"" matches "${document}")
        foreach(match IN LISTS matches)
            string(REGEX REPLACE "^.*:[ \t\r\n]*\"(.*)\"$" "\\1" relative "${match}")
            if(relative STREQUAL "")
                continue()
            endif()
            # Qt writes LicenseFile either relative to the attribution file
            # or relative to the module root; take whichever really exists.
            cmake_path(SET near NORMALIZE "${member_dir}/${relative}")
            cmake_path(SET far NORMALIZE "${root}/${relative}")
            set(candidate "")
            foreach(option IN ITEMS "${near}" "${far}")
                if(candidate STREQUAL "" AND option MATCHES "^${root}/" AND option IN_LIST members)
                    set(candidate "${option}")
                endif()
            endforeach()
            if(candidate STREQUAL "")
                message(FATAL_ERROR "Missing license text: ${member}: ${relative}")
            endif()
            list(APPEND referenced "${candidate}")
            list(APPEND wanted "${candidate}")
        endforeach()
        list(APPEND pending_entries "${member}")
        set("BSI_REFS_${member}" "${referenced}")
    endforeach()

    # One extra pass over the archive brings in exactly the referenced texts.
    if(wanted)
        list(REMOVE_DUPLICATES wanted)
        set(missing "")
        foreach(candidate IN LISTS wanted)
            if(NOT EXISTS "${work}/${candidate}")
                list(APPEND missing "${candidate}")
            endif()
        endforeach()
        if(missing)
            file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${work}" PATTERNS ${missing})
        endif()
    endif()

    foreach(member IN LISTS pending_entries)
        string(REGEX REPLACE "^${root}/" "" label "${member}")
        file(READ "${work}/${member}" document)
        file(APPEND "${notices}" "\n--- Third-party components: ${label} ---\n${document}\n")
        foreach(candidate IN LISTS "BSI_REFS_${member}")
            if(NOT EXISTS "${work}/${candidate}")
                message(FATAL_ERROR "Missing license text: ${member}: ${candidate}")
            endif()
            string(REGEX REPLACE "^${root}/" "" license_label "${candidate}")
            bsi_append_text("${license_label}" "${work}/${candidate}")
        endforeach()
    endforeach()

    file(REMOVE_RECURSE "${work}")
    if(first_source)
        set(first_source FALSE)
    else()
        string(APPEND sources_json ",")
    endif()
    string(APPEND sources_json
        "\n  {\n    \"module\": \"${module}\",\n    \"version\": \"${QT_VERSION}\",\n"
        "    \"url\": \"${url}\",\n    \"sha256\": \"${archive_sha}\"\n  }")
    message(STATUS "Collected Qt notices: ${module} ${QT_VERSION}")
endforeach()

string(APPEND sources_json "\n]\n")
file(WRITE "${OUTPUT_DIR}/SOURCES.json" "${sources_json}")
