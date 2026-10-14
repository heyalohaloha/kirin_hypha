# Same automatic lifecycle as the real-storage guard, including filtered and failed runs.
set(kirin_path_budget "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/native_path_budget.mjs")
set(kirin_path_root "${CMAKE_CURRENT_BINARY_DIR}/t")
set(kirin_path_ledger "${CMAKE_CURRENT_BINARY_DIR}/path-ledger")
foreach(phase setup cleanup)
    add_test(NAME kirin_native_path_budget_${phase}
        COMMAND "${KIRIN_HYPHA_NODE_EXECUTABLE}" "${kirin_path_budget}" ${phase}
            "${kirin_path_root}" "${kirin_path_ledger}")
    set_tests_properties(kirin_native_path_budget_${phase} PROPERTIES RUN_SERIAL TRUE TIMEOUT 120)
endforeach()
set_tests_properties(kirin_native_path_budget_setup PROPERTIES FIXTURES_SETUP kirin_path_budget)
set_tests_properties(kirin_native_path_budget_cleanup PROPERTIES FIXTURES_CLEANUP kirin_path_budget)
add_test(NAME kirin_native_path_budget_contract COMMAND "${KIRIN_HYPHA_NODE_EXECUTABLE}" --test
    "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/native_path_budget.test.mjs")
set_tests_properties(kirin_native_path_budget_contract PROPERTIES TIMEOUT 120)
list(APPEND kirin_hypha_native_tests kirin_native_path_budget_contract)
set_property(TEST ${kirin_hypha_native_tests} APPEND PROPERTY FIXTURES_REQUIRED kirin_path_budget)
set_property(TEST ${kirin_hypha_native_tests} APPEND PROPERTY ENVIRONMENT
    "KIRIN_HYPHA_PATH_BUDGET_ROOT=${kirin_path_root}" "KIRIN_HYPHA_PATH_LEDGER=${kirin_path_ledger}")
