# Standalone CPU/ownership tests, also included by the existing UI-test CI configuration.
option(KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS "Build local PRE/POST Blind runtime contracts" OFF)
option(KIRIN_HYPHA_TIMING_PRODUCT_DIAGNOSTIC "Trace RT-owned clocks in the non-shipping timing fixture (Clang/macOS only)" OFF)
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
    add_test(NAME kirin_local_blind_product_aax COMMAND KirinLocalBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --aax)
    add_test(NAME kirin_local_blind_product_aax_track COMMAND KirinLocalBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --aax --track-mono)
    set_tests_properties(kirin_local_blind_product kirin_local_blind_product_track
        kirin_local_blind_product_aax kirin_local_blind_product_aax_track PROPERTIES TIMEOUT 90)

    # Live compare PIN to Blind (INV-LC15) and the offset warning, end to end through the live ring
    # of each platform (POSIX shared memory on macOS, a named section on Windows).
    add_executable(KirinLiveComparePinProductTests tests/live_compare_pin_product_test.cpp)
    add_executable(KirinLiveBlindProductTests tests/live_blind_product_test.cpp)
    add_executable(KirinLiveTimingProductTests tests/live_compare_timing_product_test.cpp)
    if(APPLE)
        target_sources(KirinLiveTimingProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLiveTimingProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveTimingProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    if(KIRIN_HYPHA_TIMING_PRODUCT_DIAGNOSTIC)
        if(NOT APPLE OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR "Timing product diagnostic requires Clang on macOS")
        endif()
        # Never pass access-control relaxation or instrumentation to a product target.
        target_compile_options(KirinLiveTimingProductTests PRIVATE -fno-access-control)
        target_compile_definitions(KirinLiveTimingProductTests PRIVATE KIRIN_HYPHA_TIMING_PRODUCT_DIAGNOSTIC=1)
    endif()
    target_compile_definitions(KirinLiveTimingProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveTimingProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveTimingProductTests PRIVATE KirinHyphaPOST juce::juce_recommended_warning_flags)
    add_test(NAME kirin_live_timing_product COMMAND KirinLiveTimingProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav")
    set_tests_properties(kirin_live_timing_product PROPERTIES TIMEOUT 90)
    # Clock policy, startup-hole injection and atomic PCM receipts are portable. Optional
    # macOS tracing adds observations only; it must not turn product tests on or supply proof.
    add_test(NAME kirin_live_initial_loop_product COMMAND KirinLiveTimingProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --initial-loop)
    set_tests_properties(kirin_live_initial_loop_product PROPERTIES TIMEOUT 90)
    add_executable(KirinLiveRecoveryProductTests tests/live_compare_recovery_product_test.cpp)
    add_executable(KirinLiveReentryProductTests tests/live_compare_reentry_product_test.cpp tests/reference_rt_probe.cpp)
    if(APPLE)
        target_sources(KirinLiveReentryProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLiveReentryProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveReentryProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLiveReentryProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveReentryProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveReentryProductTests PRIVATE KirinHyphaPOST juce::juce_recommended_warning_flags)
    foreach(scenario IN ITEMS stop seek clock restore end blind-stop compensation seek-odd pending-odd proof-odd pair-clear pair-set clock-blind preparing-clock preparing-stop reuse pending-end restore-blind failure-named preparing-dc preparing-content preparing-seek)
        add_test(NAME kirin_live_reentry_${scenario}_product COMMAND KirinLiveReentryProductTests
            "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" ${scenario})
        set_tests_properties(kirin_live_reentry_${scenario}_product PROPERTIES TIMEOUT 60)
    endforeach()
    if(APPLE)
        target_sources(KirinLiveRecoveryProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLiveRecoveryProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveRecoveryProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLiveRecoveryProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveRecoveryProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveRecoveryProductTests PRIVATE KirinHyphaPOST juce::juce_recommended_warning_flags)
    add_test(NAME kirin_live_recovery_product COMMAND KirinLiveRecoveryProductTests)
    set_tests_properties(kirin_live_recovery_product PROPERTIES TIMEOUT 60)
    add_executable(KirinLiveCompareProcessorBenchmark
        tests/live_compare_processor_benchmark.cpp tests/reference_rt_probe.cpp)
    if(APPLE)
        target_sources(KirinLiveCompareProcessorBenchmark PRIVATE tests/BlindProductMacRunLoop.mm)
        add_library(KirinProcessorHeapProbe SHARED tests/ProcessorHeapProbeMac.cpp)
        target_compile_features(KirinProcessorHeapProbe PRIVATE cxx_std_17)
        target_link_libraries(KirinProcessorHeapProbe PRIVATE Threads::Threads juce::juce_recommended_warning_flags)
        target_link_libraries(KirinLiveCompareProcessorBenchmark PRIVATE KirinProcessorHeapProbe)
    endif()
    target_compile_features(KirinLiveCompareProcessorBenchmark PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareProcessorBenchmark PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLiveCompareProcessorBenchmark PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveCompareProcessorBenchmark PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveCompareProcessorBenchmark PRIVATE KirinHyphaPOST juce::juce_recommended_warning_flags)
    foreach(frames IN ITEMS 64 128 256 512)
        add_test(NAME kirin_live_processor_cpu_${frames} COMMAND KirinLiveCompareProcessorBenchmark ${frames})
        set_tests_properties(kirin_live_processor_cpu_${frames} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE LABELS "processor-cpu")
    endforeach()
    if(APPLE)
        target_sources(KirinLiveBlindProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLiveBlindProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveBlindProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLiveBlindProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveBlindProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveBlindProductTests PRIVATE KirinHyphaPOST juce::juce_recommended_warning_flags)
    add_test(NAME kirin_live_blind_product COMMAND KirinLiveBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav")
    add_test(NAME kirin_live_blind_reuse_product COMMAND KirinLiveBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --reuse)
    set_tests_properties(kirin_live_blind_product kirin_live_blind_reuse_product PROPERTIES TIMEOUT 90)
    add_test(NAME kirin_live_blind_loop_product COMMAND KirinLiveBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --loop)
    # 100 real-time named PRE laps + 100 real-time Blind laps, plus setup/END assertions.
    set_tests_properties(kirin_live_blind_loop_product PROPERTIES TIMEOUT 160)
    add_test(NAME kirin_live_blind_fault_product COMMAND KirinLiveBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --fault)
    set_tests_properties(kirin_live_blind_fault_product PROPERTIES TIMEOUT 90)
    foreach(fault IN ITEMS gap content stop)
        add_test(NAME kirin_live_blind_fault_${fault}_product COMMAND KirinLiveBlindProductTests
            "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --fault-${fault})
        set_tests_properties(kirin_live_blind_fault_${fault}_product PROPERTIES TIMEOUT 90)
    endforeach()
    add_test(NAME kirin_live_blind_approval_product COMMAND KirinLiveBlindProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" --approval)
    set_tests_properties(kirin_live_blind_approval_product PROPERTIES TIMEOUT 90)
    add_executable(KirinLiveCompareLifecycleTests tests/live_compare_lifecycle_product_test.cpp)
    if(APPLE)
        target_sources(KirinLiveCompareLifecycleTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLiveCompareLifecycleTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareLifecycleTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLiveCompareLifecycleTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveCompareLifecycleTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveCompareLifecycleTests PRIVATE KirinHyphaPOST juce::juce_recommended_warning_flags)
    foreach(scenario IN ITEMS restore-active restore-invalid restore-preparing restore-approval
        held finishing out-of-range reuse-held restore-matched-listen)
        add_test(NAME kirin_live_lifecycle_${scenario} COMMAND KirinLiveCompareLifecycleTests
            "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav" ${scenario})
        set_tests_properties(kirin_live_lifecycle_${scenario} PROPERTIES TIMEOUT 60)
    endforeach()
    if(APPLE)
        target_sources(KirinLiveComparePinProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLiveComparePinProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveComparePinProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLiveComparePinProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveComparePinProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveComparePinProductTests PRIVATE KirinHyphaPOST
        juce::juce_recommended_warning_flags)
    add_test(NAME kirin_live_compare_pin_product COMMAND KirinLiveComparePinProductTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav")
    set_tests_properties(kirin_live_compare_pin_product PROPERTIES TIMEOUT 90)

    # The content-offset warning and the hold on a latency jump (INV-LC7, LC10), end to end.
    add_executable(KirinLiveCompareOffsetProductTests tests/live_compare_offset_product_test.cpp)
    if(APPLE)
        target_sources(KirinLiveCompareOffsetProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLiveCompareOffsetProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareOffsetProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLiveCompareOffsetProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveCompareOffsetProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveCompareOffsetProductTests PRIVATE KirinHyphaPOST
        juce::juce_recommended_warning_flags)
    add_test(NAME kirin_live_compare_offset_product COMMAND KirinLiveCompareOffsetProductTests)
    set_tests_properties(kirin_live_compare_offset_product PROPERTIES TIMEOUT 90)

    # AAX mono: a mono track's PRE and POST compare; a multi-mono set never does (INV-LC9).
    add_executable(KirinLiveCompareAaxGroupProductTests tests/live_compare_aax_group_product_test.cpp)
    if(APPLE)
        target_sources(KirinLiveCompareAaxGroupProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinLiveCompareAaxGroupProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinLiveCompareAaxGroupProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinLiveCompareAaxGroupProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinLiveCompareAaxGroupProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinLiveCompareAaxGroupProductTests PRIVATE KirinHyphaPOST
        juce::juce_recommended_warning_flags)
    add_test(NAME kirin_live_compare_aax_group_product COMMAND KirinLiveCompareAaxGroupProductTests)
    set_tests_properties(kirin_live_compare_aax_group_product PROPERTIES TIMEOUT 90)

    add_executable(KirinEditorSurfaceProductTests tests/editor_surface_product_test.cpp)
    if(APPLE)
        target_sources(KirinEditorSurfaceProductTests PRIVATE tests/BlindProductMacRunLoop.mm)
    endif()
    target_compile_features(KirinEditorSurfaceProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinEditorSurfaceProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinEditorSurfaceProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinEditorSurfaceProductTests PRIVATE
        "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinEditorSurfaceProductTests PRIVATE KirinHyphaPOST
        juce::juce_recommended_warning_flags)
    add_test(NAME kirin_editor_surface_product COMMAND KirinEditorSurfaceProductTests)
    set_tests_properties(kirin_editor_surface_product PROPERTIES TIMEOUT 240)
endif()
