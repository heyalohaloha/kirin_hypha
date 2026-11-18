# Register before NativeStorageGuard.cmake so both tests receive storage/path fixtures.
add_test(NAME kirin_reference_audition_component
    COMMAND KirinReferenceAuditionComponentTests)
add_test(NAME kirin_reference_change_notes_ui
    COMMAND KirinReferenceAuditionComponentTests --change-notes-ui-only)
set_tests_properties(kirin_reference_change_notes_ui PROPERTIES
    TIMEOUT 120 LABELS "reference;change-notes;ui")
