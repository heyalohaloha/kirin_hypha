# Build-time (not configure-time) identity. Descriptive, never release attestation.
set(HYPHA_SOURCE_ID "unknown")
set(HYPHA_SOURCE_STATE "unverified source")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${HYPHA_SOURCE_ROOT}" RESULT_VARIABLE _head_result
        OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
        WORKING_DIRECTORY "${HYPHA_SOURCE_ROOT}" RESULT_VARIABLE _status_result
        OUTPUT_VARIABLE _status OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_head_result EQUAL 0 AND _head MATCHES "^[0-9a-f]+$")
        set(HYPHA_SOURCE_ID "${_head}")
        if(_status_result EQUAL 0)
            if(_status STREQUAL "")
                set(HYPHA_SOURCE_STATE "clean source")
            else()
                set(HYPHA_SOURCE_STATE "modified source")
            endif()
        endif()
    endif()
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/HyphaBuildIdentity.h.in"
    "${HYPHA_OUTPUT_DIR}/HyphaBuildIdentity.h" @ONLY)
