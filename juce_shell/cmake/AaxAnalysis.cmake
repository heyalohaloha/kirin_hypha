if(KIRIN_HYPHA_BUILD_LIVE_COMPARE_TESTS OR KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS
   OR KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    add_executable(KirinAaxAnalysisClockTests tests/analysis_clock_aax_test.cpp)
    target_compile_features(KirinAaxAnalysisClockTests PRIVATE cxx_std_17)
    target_compile_options(KirinAaxAnalysisClockTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    add_test(NAME kirin_aax_analysis_clock COMMAND KirinAaxAnalysisClockTests)
    add_executable(KirinAaxAnalysisProductTests tests/analysis_product_aax_test.cpp)
    target_compile_features(KirinAaxAnalysisProductTests PRIVATE cxx_std_17)
    target_compile_options(KirinAaxAnalysisProductTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_compile_definitions(KirinAaxAnalysisProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
    target_include_directories(KirinAaxAnalysisProductTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
    target_link_libraries(KirinAaxAnalysisProductTests PRIVATE KirinHyphaPOST juce::juce_recommended_warning_flags)
    # Independent scenarios keep real-time waits from accumulating into one opaque timeout.
    # Each condition retains its 12-second deadline and every audio/clock assertion still runs.
    foreach(layout stereo mono)
        foreach(scenario certified uncertified loop short_loops per_insert measured_loop)
            add_test(NAME kirin_aax_analysis_${layout}_${scenario}_product
                COMMAND KirinAaxAnalysisProductTests ${layout} ${scenario})
            set_tests_properties(kirin_aax_analysis_${layout}_${scenario}_product PROPERTIES TIMEOUT 90)
        endforeach()
    endforeach()
endif()
