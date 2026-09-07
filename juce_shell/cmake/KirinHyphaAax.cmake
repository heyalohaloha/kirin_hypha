# Kirin Hypha AAX is opt-in because the AAX SDK is licensed separately and must stay outside this
# GPL repository.  Keep every gate in this module so the default AU/VST3 configuration is unchanged.

set(KIRIN_HYPHA_AAX_SDK_PATH "" CACHE PATH
    "External AAX SDK root (must contain Interfaces/ACF and must not be stored in this repository)")
option(KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED
    "Confirm that use of the external AAX SDK is covered by an appropriate Avid license" OFF)
option(KIRIN_HYPHA_REQUIRE_AAX
    "Fail configure unless an external, licensed AAX SDK is enabled" OFF)

function(kirin_hypha_configure_aax OUT_ENABLED)
    set(_enabled OFF)

    if(KIRIN_HYPHA_AAX_SDK_PATH)
        if(NOT KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED)
            message(FATAL_ERROR
                "AAX SDK path supplied without KIRIN_HYPHA_AAX_SDK_LICENSE_CONFIRMED=ON")
        endif()

        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin" AND
           NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
            message(FATAL_ERROR "Kirin Hypha AAX builds are supported only on macOS and Windows")
        endif()

        if(NOT IS_DIRECTORY "${KIRIN_HYPHA_AAX_SDK_PATH}" OR
           NOT IS_DIRECTORY "${KIRIN_HYPHA_AAX_SDK_PATH}/Interfaces" OR
           NOT IS_DIRECTORY "${KIRIN_HYPHA_AAX_SDK_PATH}/Interfaces/ACF")
            message(FATAL_ERROR
                "AAX SDK path is invalid; expected an external SDK root containing Interfaces/ACF: "
                "${KIRIN_HYPHA_AAX_SDK_PATH}")
        endif()

        set(_sdk_supplied "${KIRIN_HYPHA_AAX_SDK_PATH}")
        cmake_path(ABSOLUTE_PATH _sdk_supplied BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
                   NORMALIZE OUTPUT_VARIABLE _sdk_absolute)
        file(REAL_PATH "${_sdk_absolute}" _sdk_real)
        file(REAL_PATH "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../.." _repository_real)
        cmake_path(IS_PREFIX _repository_real "${_sdk_absolute}" NORMALIZE _sdk_path_is_in_repository)
        cmake_path(IS_PREFIX _repository_real "${_sdk_real}" NORMALIZE _sdk_is_in_repository)
        if(_sdk_path_is_in_repository OR _sdk_is_in_repository)
            message(FATAL_ERROR
                "AAX SDK path and resolved contents must remain outside the Kirin Hypha repository: "
                "${_sdk_absolute}")
        endif()

        set(_enabled ON)
    elseif(KIRIN_HYPHA_REQUIRE_AAX)
        message(FATAL_ERROR
            "KIRIN_HYPHA_REQUIRE_AAX=ON requires KIRIN_HYPHA_AAX_SDK_PATH")
    endif()

    set(${OUT_ENABLED} ${_enabled} PARENT_SCOPE)
endfunction()
