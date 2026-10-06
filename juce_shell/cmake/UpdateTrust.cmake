# Only this invocation's explicit input can enable networking. A previous CMake
# cache (including a fixture/old key) is NEVER an authority for a new candidate.
set(KIRIN_HYPHA_UPDATE_PUBLIC_KEY "$ENV{KIRIN_HYPHA_UPDATE_PUBLIC_KEY_INPUT}"
    CACHE STRING "Explicit update verification key for this configure invocation" FORCE)
set(KIRIN_HYPHA_UPDATE_KEY_SHA256 "")
set(KIRIN_HYPHA_UPDATE_KEY_MARKER "disabled")
if(NOT KIRIN_HYPHA_UPDATE_PUBLIC_KEY STREQUAL "")
    string(LENGTH "${KIRIN_HYPHA_UPDATE_PUBLIC_KEY}" _update_key_length)
    if(NOT KIRIN_HYPHA_UPDATE_PUBLIC_KEY MATCHES "^10001,[89a-f][0-9a-f]+[13579bdf]$"
        OR NOT _update_key_length EQUAL 518)
        message(FATAL_ERROR "Update key must have canonical exponent 10001 and a 2048-bit odd lowercase-hex modulus")
    endif()
    string(SHA256 KIRIN_HYPHA_UPDATE_KEY_SHA256 "${KIRIN_HYPHA_UPDATE_PUBLIC_KEY}")
    set(KIRIN_HYPHA_UPDATE_KEY_MARKER "${KIRIN_HYPHA_UPDATE_KEY_SHA256}")
endif()
