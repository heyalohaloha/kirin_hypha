# Reuse the existing component executable; include before NativeStorageGuard.cmake.
if(TARGET KirinReferenceAuditionComponentTests)
    add_test(NAME kirin_reference_balance_alignment
        COMMAND KirinReferenceAuditionComponentTests --balance-alignment-only)
    add_test(NAME kirin_reference_balance_overlay
        COMMAND KirinReferenceAuditionComponentTests --reference-overlay-only)
    add_test(NAME kirin_reference_balance_approval
        COMMAND KirinReferenceAuditionComponentTests --reference-approval-only)
    add_test(NAME kirin_reference_balance_target_sizes
        COMMAND KirinReferenceAuditionComponentTests --target-balance-sizes-only)
    add_test(NAME kirin_reference_balance_compact_fit
        COMMAND KirinReferenceAuditionComponentTests --compact-balance-fit-only)
    add_test(NAME kirin_reference_balance_follow
        COMMAND KirinReferenceAuditionComponentTests --reference-follow-only)
    add_test(NAME kirin_reference_balance_range_label
        COMMAND KirinReferenceAuditionComponentTests --range-label-only)
    set_tests_properties(
        kirin_reference_balance_alignment
        kirin_reference_balance_overlay
        kirin_reference_balance_approval
        kirin_reference_balance_target_sizes
        kirin_reference_balance_compact_fit
        kirin_reference_balance_follow
        kirin_reference_balance_range_label
        PROPERTIES TIMEOUT 120)
endif()
