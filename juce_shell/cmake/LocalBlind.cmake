# Standalone CPU/ownership tests, also included by the existing UI-test CI configuration.
option(KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS "Build local PRE/POST Blind runtime contracts" OFF)
if(KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS OR KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    include(${CMAKE_CURRENT_LIST_DIR}/LocalBlindPortable.cmake)
    kirin_add_local_blind_portable_contracts("${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(KirinLocalBlindPreparationTests
        tests/local_blind_preparation_test.cpp src/local_blind/LocalBlindPreparation.cpp
        src/local_blind/LocalBlindProductSession.cpp src/local_blind/LocalBlindTrial.cpp)
    target_compile_features(KirinLocalBlindPreparationTests PRIVATE cxx_std_17)
    target_compile_options(KirinLocalBlindPreparationTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_link_libraries(KirinLocalBlindPreparationTests PRIVATE Threads::Threads)
    target_include_directories(KirinLocalBlindPreparationTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinLocalBlindPreparationTests PRIVATE ${KIRIN_FFI_LIB} ${KIRIN_RUST_NATIVE_LIBS})
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(KirinLocalBlindPreparationTests KirinHyphaRustFFI)
    endif()
    if(APPLE)
        target_link_libraries(KirinLocalBlindPreparationTests PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
    add_test(NAME kirin_local_blind_preparation COMMAND KirinLocalBlindPreparationTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav")
    set_tests_properties(kirin_local_blind_preparation
        PROPERTIES TIMEOUT 120)

    juce_add_console_app(KirinLocalBlindCaptureServiceTests
        PRODUCT_NAME "Kirin Local Blind Capture Service Tests")
    target_sources(KirinLocalBlindCaptureServiceTests PRIVATE
        tests/local_blind_capture_service_test.cpp
        src/local_blind/LocalBlindCaptureService.cpp
        src/local_blind/CapturePairComparison.cpp)
    target_compile_definitions(KirinLocalBlindCaptureServiceTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(KirinLocalBlindCaptureServiceTests PRIVATE
        juce::juce_core Threads::Threads
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME kirin_local_blind_capture_service
        COMMAND KirinLocalBlindCaptureServiceTests)
    set_tests_properties(kirin_local_blind_capture_service PROPERTIES TIMEOUT 120)

    add_executable(KirinLocalBlindCapturePairComparisonTests
        tests/capture_pair_comparison_test.cpp
        src/local_blind/CapturePairComparison.cpp)
    target_compile_features(KirinLocalBlindCapturePairComparisonTests PRIVATE cxx_std_17)
    target_compile_options(KirinLocalBlindCapturePairComparisonTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    add_test(NAME kirin_local_blind_capture_pair_comparison
        COMMAND KirinLocalBlindCapturePairComparisonTests)
    set_tests_properties(kirin_local_blind_capture_pair_comparison PROPERTIES TIMEOUT 120)

    add_executable(KirinLocalBlindPdcValidationDelayTests
        tests/pdc_validation_delay/fixed_validation_delay_test.cpp)
    target_compile_features(KirinLocalBlindPdcValidationDelayTests PRIVATE cxx_std_17)
    target_compile_options(KirinLocalBlindPdcValidationDelayTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    add_test(NAME kirin_local_blind_pdc_validation_delay
        COMMAND KirinLocalBlindPdcValidationDelayTests)
    set_tests_properties(kirin_local_blind_pdc_validation_delay PROPERTIES TIMEOUT 120)

    add_executable(KirinLocalBlindProductTests tests/local_blind_product_test.cpp)
    if(APPLE)
        target_sources(KirinLocalBlindProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLocalBlindProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinLocalBlindProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLocalBlindProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLocalBlindProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLocalBlindProductTests PRIVATE KirinHyphaPOST
        juce::juce_recommended_warning_flags)
    add_test(NAME kirin_local_blind_product COMMAND KirinLocalBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav")
    add_test(NAME kirin_local_blind_product_track COMMAND KirinLocalBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --track-mono)
    set_tests_properties(kirin_local_blind_product kirin_local_blind_product_track PROPERTIES TIMEOUT 90)
endif()
