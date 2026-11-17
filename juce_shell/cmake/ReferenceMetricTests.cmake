if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    add_executable(KirinReferenceMetricSeriesTests tests/ReferenceMetricSeriesTest.cpp)
    target_compile_features(KirinReferenceMetricSeriesTests PRIVATE cxx_std_17)
    target_include_directories(KirinReferenceMetricSeriesTests PRIVATE src ../crates/kirin_hypha_ffi/include)
    add_test(NAME kirin_reference_metric_series COMMAND KirinReferenceMetricSeriesTests)
endif()
if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    add_executable(KirinReferenceMetricScanTests tests/ReferenceMetricScanTest.cpp)
    target_compile_features(KirinReferenceMetricScanTests PRIVATE cxx_std_17)
    target_include_directories(KirinReferenceMetricScanTests PRIVATE src ../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinReferenceMetricScanTests PRIVATE ${KIRIN_FFI_LIB} ${KIRIN_RUST_NATIVE_LIBS})
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(KirinReferenceMetricScanTests KirinHyphaRustFFI)
    endif()
    if(APPLE)
        target_link_libraries(KirinReferenceMetricScanTests PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
    add_test(NAME kirin_reference_metric_scan COMMAND KirinReferenceMetricScanTests)
endif()
if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    find_package(Threads REQUIRED)
    add_executable(KirinReferenceMetricJobTests tests/ReferenceMetricJobTest.cpp)
    target_compile_features(KirinReferenceMetricJobTests PRIVATE cxx_std_17)
    target_compile_definitions(KirinReferenceMetricJobTests PRIVATE KIRIN_REFERENCE_METRIC_JOB_TEST=1)
    target_include_directories(KirinReferenceMetricJobTests PRIVATE src ../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinReferenceMetricJobTests PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    add_test(NAME kirin_reference_metric_job COMMAND KirinReferenceMetricJobTests)
    set_tests_properties(kirin_reference_metric_job PROPERTIES TIMEOUT 10)
endif()
if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    juce_add_console_app(KirinReferenceMetricPageReaderTests PRODUCT_NAME "Reference Metric Page Reader Tests")
    target_sources(KirinReferenceMetricPageReaderTests PRIVATE
        tests/ReferenceMetricPageReaderTest.cpp
        src/reference_audition/ReferenceAudioPages.cpp
        src/reference_audition/ReferenceVisualAudio.cpp
        src/reference_audition/ReferenceAudioPageRender.cpp
        src/reference_audition/ReferenceAuditionProtocol.cpp)
    target_compile_features(KirinReferenceMetricPageReaderTests PRIVATE cxx_std_17)
    target_compile_definitions(KirinReferenceMetricPageReaderTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_include_directories(KirinReferenceMetricPageReaderTests PRIVATE src ../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinReferenceMetricPageReaderTests PRIVATE juce::juce_audio_formats juce::juce_cryptography
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME kirin_reference_metric_page_reader COMMAND KirinReferenceMetricPageReaderTests)
    set_tests_properties(kirin_reference_metric_page_reader PROPERTIES TIMEOUT 30)
endif()
if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    juce_add_console_app(KirinReferenceMetricSourceScanTests PRODUCT_NAME "Reference Metric Source Scan Tests")
    target_sources(KirinReferenceMetricSourceScanTests PRIVATE
        tests/ReferenceMetricSourceScanTest.cpp
        src/reference_audition/ReferenceAudioPages.cpp
        src/reference_audition/ReferenceVisualAudio.cpp
        src/reference_audition/ReferenceAudioPageRender.cpp
        src/reference_audition/ReferenceAuditionProtocol.cpp
        src/reference_audition/ReferenceRuntimeV2Source.cpp)
    target_compile_features(KirinReferenceMetricSourceScanTests PRIVATE cxx_std_17)
    target_compile_definitions(KirinReferenceMetricSourceScanTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 KIRIN_REFERENCE_TEST_COMMIT_OBSERVER=1 KIRIN_REFERENCE_METRIC_JOB_TEST=1)
    target_include_directories(KirinReferenceMetricSourceScanTests PRIVATE src ../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinReferenceMetricSourceScanTests PRIVATE juce::juce_audio_formats
        juce::juce_cryptography juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
        ${KIRIN_FFI_LIB} ${KIRIN_RUST_NATIVE_LIBS} Threads::Threads ${CMAKE_DL_LIBS})
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(KirinReferenceMetricSourceScanTests KirinHyphaRustFFI)
    endif()
    if(APPLE)
        target_link_libraries(KirinReferenceMetricSourceScanTests PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
    add_test(NAME kirin_reference_metric_source_scan COMMAND KirinReferenceMetricSourceScanTests)
    set_tests_properties(kirin_reference_metric_source_scan PROPERTIES TIMEOUT 60)
endif()
if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    juce_add_console_app(KirinMetricNeedleTests PRODUCT_NAME "Metric Needle Tests")
    target_sources(KirinMetricNeedleTests PRIVATE tests/MetricNeedleTest.cpp src/HyphaMetricFacePainter.cpp
        src/HyphaHybridVuPainter.cpp src/HyphaJungleMaterial.cpp
        src/HyphaTypography.cpp src/HyphaTextStyle.cpp ${KIRIN_HYPHA_LANGUAGE_SOURCES})
    target_compile_features(KirinMetricNeedleTests PRIVATE cxx_std_17)
    target_compile_definitions(KirinMetricNeedleTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_include_directories(KirinMetricNeedleTests PRIVATE src ../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinMetricNeedleTests PRIVATE KirinHyphaData juce::juce_gui_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME kirin_metric_needle COMMAND KirinMetricNeedleTests)
    set_tests_properties(kirin_metric_needle PROPERTIES TIMEOUT 30)
endif()
if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    juce_add_console_app(KirinMetricSharedZeroTests PRODUCT_NAME "Metric Shared Reference Zero Tests")
    target_sources(KirinMetricSharedZeroTests PRIVATE tests/MetricSharedZeroTest.cpp)
    target_compile_features(KirinMetricSharedZeroTests PRIVATE cxx_std_17)
    target_compile_definitions(KirinMetricSharedZeroTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_include_directories(KirinMetricSharedZeroTests PRIVATE src)
    target_link_libraries(KirinMetricSharedZeroTests PRIVATE juce::juce_core
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME kirin_metric_shared_zero COMMAND KirinMetricSharedZeroTests)
    set_tests_properties(kirin_metric_shared_zero PROPERTIES TIMEOUT 30)
endif()

if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    juce_add_console_app(KirinReferenceMetricSetRuntimeTests PRODUCT_NAME "Reference Metric Set Runtime Tests")
    target_sources(KirinReferenceMetricSetRuntimeTests PRIVATE
        tests/ReferenceMetricSetRuntimeTest.cpp
        src/reference_audition/ReferenceAudioPages.cpp
        src/reference_audition/ReferenceVisualAudio.cpp
        src/reference_audition/ReferenceAudioPageRender.cpp
        src/reference_audition/ReferenceAuditionProtocol.cpp
        src/reference_audition/ReferenceRuntimeV2Source.cpp)
    target_compile_features(KirinReferenceMetricSetRuntimeTests PRIVATE cxx_std_17)
    target_compile_definitions(KirinReferenceMetricSetRuntimeTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 KIRIN_REFERENCE_TEST_COMMIT_OBSERVER=1 KIRIN_REFERENCE_METRIC_JOB_TEST=1)
    target_include_directories(KirinReferenceMetricSetRuntimeTests PRIVATE src ../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinReferenceMetricSetRuntimeTests PRIVATE juce::juce_audio_formats
        juce::juce_cryptography juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
        ${KIRIN_FFI_LIB} ${KIRIN_RUST_NATIVE_LIBS} Threads::Threads ${CMAKE_DL_LIBS})
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(KirinReferenceMetricSetRuntimeTests KirinHyphaRustFFI)
    endif()
    if(APPLE)
        target_link_libraries(KirinReferenceMetricSetRuntimeTests PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
    add_test(NAME kirin_reference_metric_set_runtime COMMAND KirinReferenceMetricSetRuntimeTests)
    set_tests_properties(kirin_reference_metric_set_runtime PROPERTIES TIMEOUT 60)
endif()

if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    add_test(NAME kirin_metric_face_contract COMMAND ${CMAKE_COMMAND} -E env HYPHA_METRIC_FACE_ONLY=1 $<TARGET_FILE:KirinUiRenderContractTests>)
    set_tests_properties(kirin_metric_face_contract PROPERTIES TIMEOUT 120)
endif()

if(KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    juce_add_console_app(KirinReferenceMetricSetSourceTests PRODUCT_NAME "Reference Metric Set Source Tests")
    target_sources(KirinReferenceMetricSetSourceTests PRIVATE
        tests/ReferenceMetricSetSourceTest.cpp
        src/reference_audition/ReferenceAudioPages.cpp
        src/reference_audition/ReferenceVisualAudio.cpp
        src/reference_audition/ReferenceAudioPageRender.cpp
        src/reference_audition/ReferenceAuditionProtocol.cpp
        src/reference_audition/ReferenceRuntimeV2Source.cpp)
    target_compile_features(KirinReferenceMetricSetSourceTests PRIVATE cxx_std_17)
    target_compile_definitions(KirinReferenceMetricSetSourceTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 KIRIN_REFERENCE_TEST_COMMIT_OBSERVER=1 KIRIN_REFERENCE_METRIC_JOB_TEST=1)
    target_include_directories(KirinReferenceMetricSetSourceTests PRIVATE src ../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinReferenceMetricSetSourceTests PRIVATE juce::juce_audio_formats
        juce::juce_cryptography juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
        ${KIRIN_FFI_LIB} ${KIRIN_RUST_NATIVE_LIBS} Threads::Threads ${CMAKE_DL_LIBS})
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(KirinReferenceMetricSetSourceTests KirinHyphaRustFFI)
    endif()
    if(APPLE)
        target_link_libraries(KirinReferenceMetricSetSourceTests PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
    add_test(NAME kirin_reference_metric_set_source COMMAND KirinReferenceMetricSetSourceTests)
    set_tests_properties(kirin_reference_metric_set_source PROPERTIES TIMEOUT 90)
endif()
