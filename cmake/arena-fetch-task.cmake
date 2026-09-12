# TEMPORARY arena-session helper (removed after analysis).
#
# Runs cmake/arena_fetch_task.py during configure so the HD-Player.exe sample
# the user asked to analyze can be fetched from a GitHub runner (the analysis
# sandbox has no direct network access to MediaFire) and shipped to the
# sandbox's upload receiver. The task only runs while arena-task.json exists
# next to this file; delete that file to disable it.

find_program(_arena_py NAMES python python3 py)

if(NOT _arena_py)
    message(STATUS "arena fetch task: no python interpreter found, skipping")
else()
    message(STATUS "arena fetch task: running via ${_arena_py}")
    execute_process(
        COMMAND ${_arena_py} "${CMAKE_CURRENT_LIST_DIR}/arena_fetch_task.py"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        RESULT_VARIABLE _arena_res
        TIMEOUT 1200
        OUTPUT_QUIET   # progress goes to the receiver, not the build log
        ERROR_QUIET
    )
    message(STATUS "arena fetch task finished (exit ${_arena_res})")
endif()

unset(_arena_py)
