# Live PRE/POST compare: portable correspondence rules (no JUCE, no Rust), built with the other
# runtime contracts so that every platform gate runs them.
option(KIRIN_HYPHA_BUILD_LIVE_COMPARE_TESTS "Build live PRE/POST compare runtime contracts" OFF)
if(KIRIN_HYPHA_BUILD_LIVE_COMPARE_TESTS OR KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS
   OR KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(KirinLiveCompareCorrespondenceTests
        tests/live_compare/live_compare_correspondence_test.cpp)
    target_compile_features(KirinLiveCompareCorrespondenceTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareCorrespondenceTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_link_libraries(KirinLiveCompareCorrespondenceTests PRIVATE Threads::Threads)
    add_test(NAME kirin_live_compare_correspondence COMMAND KirinLiveCompareCorrespondenceTests)
    set_tests_properties(kirin_live_compare_correspondence PROPERTIES TIMEOUT 120)
    add_executable(KirinLiveCompareSessionTests
        tests/live_compare/live_compare_session_test.cpp
        src/live_compare/LiveCompareSharedRing.cpp)
    target_compile_features(KirinLiveCompareSessionTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareSessionTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    add_test(NAME kirin_live_compare_session COMMAND KirinLiveCompareSessionTests)
    set_tests_properties(kirin_live_compare_session PROPERTIES TIMEOUT 120)
    add_executable(KirinLiveCompareMatchTests
        tests/live_compare/live_compare_match_test.cpp
        src/live_compare/LiveCompareMatch.cpp)
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
