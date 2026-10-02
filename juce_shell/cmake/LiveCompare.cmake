# Live PRE/POST compare: portable correspondence rules (no JUCE, no Rust), built with the other
# runtime contracts so that every platform gate runs them.
option(KIRIN_HYPHA_BUILD_LIVE_COMPARE_TESTS "Build live PRE/POST compare runtime contracts" OFF)
if(KIRIN_HYPHA_BUILD_LIVE_COMPARE_TESTS OR KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS
   OR KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    enable_testing()
    add_executable(KirinHostIdentityTests
        tests/host_identity_test.cpp src/HostExecutableIdentity.cpp)
    target_compile_features(KirinHostIdentityTests PRIVATE cxx_std_17)
    if(WIN32)
        target_sources(KirinHostIdentityTests PRIVATE tests/host_identity_fixture.rc)
    endif()
    target_compile_definitions(KirinHostIdentityTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(KirinHostIdentityTests PRIVATE juce::juce_core
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME kirin_host_identity COMMAND KirinHostIdentityTests)
    foreach(contract IN ITEMS live_blind_session live_compare_completion live_compare_authority)
        add_executable(Kirin_${contract}_Tests tests/live_compare/${contract}_test.cpp)
        target_compile_features(Kirin_${contract}_Tests PRIVATE cxx_std_17)
        target_compile_options(Kirin_${contract}_Tests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
        add_test(NAME kirin_${contract} COMMAND Kirin_${contract}_Tests)
    endforeach()
    find_package(Threads REQUIRED)
    add_executable(KirinLiveCompareTimingTests tests/live_compare/live_compare_timing_test.cpp
        src/live_compare/LiveCompareSharedRing.cpp)
    target_compile_features(KirinLiveCompareTimingTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareTimingTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_link_libraries(KirinLiveCompareTimingTests PRIVATE Threads::Threads)
    add_test(NAME kirin_live_compare_timing COMMAND KirinLiveCompareTimingTests)
    set_tests_properties(kirin_live_compare_timing PROPERTIES TIMEOUT 30)
    add_executable(KirinLiveCompareCorrespondenceTests
        tests/live_compare/live_compare_correspondence_test.cpp)
    target_compile_features(KirinLiveCompareCorrespondenceTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareCorrespondenceTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_link_libraries(KirinLiveCompareCorrespondenceTests PRIVATE Threads::Threads)
    add_test(NAME kirin_live_compare_correspondence COMMAND KirinLiveCompareCorrespondenceTests)
    set_tests_properties(kirin_live_compare_correspondence PROPERTIES TIMEOUT 120)
    # Fast oracle/positive controls only, never a LOOP product acceptance gate. The separate
    # --survey runs the full delay x buffer matrix and may return 2 for unresolved conditions.
    add_executable(KirinLiveCompareLoopFeasibilityTests
        tests/live_compare/live_compare_loop_feasibility_test.cpp)
    target_compile_features(KirinLiveCompareLoopFeasibilityTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareLoopFeasibilityTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    add_test(NAME kirin_live_compare_loop_feasibility COMMAND KirinLiveCompareLoopFeasibilityTests)
    set_tests_properties(kirin_live_compare_loop_feasibility PROPERTIES TIMEOUT 60)
    add_executable(KirinLiveCompareLoopTests tests/live_compare/live_compare_loop_test.cpp)
    target_compile_features(KirinLiveCompareLoopTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareLoopTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    add_test(NAME kirin_live_compare_loop COMMAND KirinLiveCompareLoopTests)
    set_tests_properties(kirin_live_compare_loop PROPERTIES TIMEOUT 60)
    add_executable(KirinLiveCompareSessionTests
        tests/live_compare/live_compare_session_test.cpp
        src/live_compare/LiveComparePin.cpp
        src/live_compare/LiveCompareAaxGroup.cpp
        src/live_compare/LiveCompareSharedRing.cpp)
    target_compile_features(KirinLiveCompareSessionTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareSessionTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    add_test(NAME kirin_live_compare_session COMMAND KirinLiveCompareSessionTests)
    set_tests_properties(kirin_live_compare_session PROPERTIES TIMEOUT 120)
    add_executable(KirinLiveCompareMatchTests
        tests/live_compare/live_compare_match_test.cpp
        tests/live_compare/live_compare_offset_test.cpp
        src/live_compare/LiveCompareMatch.cpp
        src/live_compare/LiveCompareOffset.cpp)
    target_compile_features(KirinLiveCompareMatchTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareMatchTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_include_directories(KirinLiveCompareMatchTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinLiveCompareMatchTests PRIVATE Threads::Threads ${KIRIN_FFI_LIB} ${KIRIN_RUST_NATIVE_LIBS})
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(KirinLiveCompareMatchTests KirinHyphaRustFFI)
    endif()
    if(APPLE)
        target_link_libraries(KirinLiveCompareMatchTests PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
    add_test(NAME kirin_live_compare_match COMMAND KirinLiveCompareMatchTests)
    set_tests_properties(kirin_live_compare_match PROPERTIES TIMEOUT 120)
endif()
