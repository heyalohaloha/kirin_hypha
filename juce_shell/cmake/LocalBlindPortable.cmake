# Shared by the product test configuration and the isolated Windows/macOS CPU runner.
function(kirin_add_local_blind_portable_contracts root)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(KirinLocalBlindCaptureTests "${root}/tests/local_blind_capture_test.cpp")
    add_executable(KirinLocalBlindTrialTests
        "${root}/tests/local_blind_trial_test.cpp" "${root}/src/local_blind/LocalBlindTrial.cpp")
    add_executable(KirinLocalBlindHostContextTests
        "${root}/tests/local_blind_host_context_test.cpp" "${root}/src/local_blind/HostContext.cpp")
    target_include_directories(KirinLocalBlindHostContextTests PRIVATE
        "${root}/JUCE/modules"
        "${root}/JUCE/modules/juce_audio_processors/format_types/VST3_SDK")
    foreach(target KirinLocalBlindCaptureTests KirinLocalBlindTrialTests KirinLocalBlindHostContextTests)
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_link_libraries(${target} PRIVATE Threads::Threads)
        if(MSVC)
            target_compile_options(${target} PRIVATE /utf-8 /W4)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endforeach()
    add_test(NAME kirin_local_blind_capture COMMAND KirinLocalBlindCaptureTests)
    add_test(NAME kirin_local_blind_trial COMMAND KirinLocalBlindTrialTests)
    add_test(NAME kirin_local_blind_host_context COMMAND KirinLocalBlindHostContextTests)
    set_tests_properties(kirin_local_blind_capture kirin_local_blind_trial kirin_local_blind_host_context
        PROPERTIES TIMEOUT 120)
endfunction()
