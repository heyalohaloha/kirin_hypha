# Keep this include last: every native test, including future additions and filtered -R runs,
# requires a read-only before/after comparison of the user's Kirin OS directories.
get_property(kirin_hypha_native_tests DIRECTORY PROPERTY TESTS)
if(kirin_hypha_native_tests)
    if(TARGET KirinHyphaPOST)
        include(${CMAKE_CURRENT_LIST_DIR}/StoragePathsTests.cmake)
        get_property(kirin_hypha_native_tests DIRECTORY PROPERTY TESTS)
    endif()
    find_program(KIRIN_HYPHA_NODE_EXECUTABLE NAMES node node.exe REQUIRED)
    set(kirin_storage_guard "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/native_storage_guard.mjs")
    set(kirin_storage_baseline "${CMAKE_CURRENT_BINARY_DIR}/real-kirin-os-before.json")
    add_test(NAME kirin_native_storage_guard_setup
        COMMAND "${KIRIN_HYPHA_NODE_EXECUTABLE}" "${kirin_storage_guard}" setup "${kirin_storage_baseline}")
    add_test(NAME kirin_native_storage_guard_cleanup
        COMMAND "${KIRIN_HYPHA_NODE_EXECUTABLE}" "${kirin_storage_guard}" cleanup "${kirin_storage_baseline}")
    add_test(NAME kirin_native_storage_guard_contract
        COMMAND "${KIRIN_HYPHA_NODE_EXECUTABLE}" --test
            "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/native_storage_guard.test.mjs")
    list(APPEND kirin_hypha_native_tests kirin_native_storage_guard_contract)
    set_tests_properties(kirin_native_storage_guard_setup PROPERTIES
        FIXTURES_SETUP kirin_real_storage RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(kirin_native_storage_guard_cleanup PROPERTIES
        FIXTURES_CLEANUP kirin_real_storage RUN_SERIAL TRUE TIMEOUT 120)
    set_tests_properties(kirin_native_storage_guard_contract PROPERTIES
        TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_property(TEST ${kirin_hypha_native_tests} APPEND PROPERTY FIXTURES_REQUIRED kirin_real_storage)
    # Rust and JUCE Kirin OS transports follow this root. Per-test sandboxes replace it with
    # their own root and keep HOME/APPDATA/TMPDIR in the same platform layout.
    set_property(TEST ${kirin_hypha_native_tests} APPEND PROPERTY ENVIRONMENT
        "KIRIN_HYPHA_TEST_STORAGE_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-kirin-os")
endif()
