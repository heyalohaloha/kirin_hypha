# Each invocation owns its pair, demand and warmup; no CTest ordering or shared fixture state.
# Conservative local estimates (both FREQ and SHARP): linear/recovery/cycle_listen <= 15 s,
# loop/cycle_blind <= 20 s including setup. Six times those estimates gives a slow runner
# room without accumulating every transition under a single 90 s limit. Audio-block budgets
# retain the input duration; TIMEOUT is only the outer watchdog, never an acceptance threshold.
foreach(layout stereo mono)
    foreach(host content au reaper)
        set(scenarios missing omitted included zero)
        if(host STREQUAL "reaper")
            set(scenarios missing)
        endif()
        foreach(scenario IN LISTS scenarios)
            set(phases linear loop recovery)
            if(host STREQUAL "au" AND NOT scenario STREQUAL "included")
                list(APPEND phases cycle_listen cycle_blind)
            endif()
            foreach(phase IN LISTS phases)
                set(test_name kirin_${host}_analysis_${layout}_${scenario}_${phase}_product)
                set(phase_timeout 90)
                if(phase STREQUAL "loop" OR phase STREQUAL "cycle_blind")
                    set(phase_timeout 120)
                endif()
                add_test(NAME ${test_name}
                    COMMAND KirinContentAnalysisProductTests ${layout} ${scenario} ${host} ${phase} ${phase_timeout})
                set_tests_properties(${test_name} PROPERTIES TIMEOUT ${phase_timeout})
                if(host STREQUAL "reaper")
                    # Shared with the virtual-clock test in AnalysisProductTiming.h.
                    # Keep the same assertions and audio budgets with sub-gap callback jitter.
                    set(jitter_test kirin_${host}_analysis_${layout}_${scenario}_${phase}_jitter_product)
                    add_test(NAME ${jitter_test}
                        COMMAND KirinContentAnalysisProductTests ${layout} ${scenario} ${host} ${phase} ${phase_timeout} jitter)
                    set_tests_properties(${jitter_test} PROPERTIES TIMEOUT ${phase_timeout})
                endif()
            endforeach()
        endforeach()
    endforeach()
endforeach()

add_executable(KirinAnalysisProductWaitTests tests/analysis_product_wait_test.cpp)
target_compile_features(KirinAnalysisProductWaitTests PRIVATE cxx_std_17)
target_compile_options(KirinAnalysisProductWaitTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
add_test(NAME kirin_analysis_product_wait COMMAND KirinAnalysisProductWaitTests)
set_tests_properties(kirin_analysis_product_wait PROPERTIES TIMEOUT 10)

add_executable(KirinAnalysisProductTimingTests tests/analysis_product_timing_test.cpp)
target_compile_features(KirinAnalysisProductTimingTests PRIVATE cxx_std_17)
target_compile_options(KirinAnalysisProductTimingTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
add_test(NAME kirin_analysis_product_timing COMMAND KirinAnalysisProductTimingTests)
set_tests_properties(kirin_analysis_product_timing PROPERTIES TIMEOUT 90)
