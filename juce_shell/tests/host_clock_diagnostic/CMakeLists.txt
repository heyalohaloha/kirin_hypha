cmake_minimum_required(VERSION 3.22)
project(HyphaClockDiagnostic VERSION 0.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED HYPHA_DIAGNOSTIC_JUCE_ROOT)
    message(FATAL_ERROR "Explicit verified patched JUCE source is required")
endif()
add_subdirectory("${HYPHA_DIAGNOSTIC_JUCE_ROOT}" JUCE)
add_executable(HyphaClockTraceTest clock_trace_test.cpp)
enable_testing()
add_test(NAME HyphaClockTraceTest COMMAND HyphaClockTraceTest)
juce_add_plugin(HyphaClockDiagnostic
    PLUGIN_MANUFACTURER_CODE Kirn PLUGIN_CODE Khck FORMATS VST3
    PRODUCT_NAME "Kirin Clock Diagnostic - Not Product"
    COMPANY_NAME Kirin BUNDLE_ID com.kirinmastering.clock-diagnostic
    VST3_CATEGORIES Fx Tools IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(HyphaClockDiagnostic PRIVATE PluginProcessor.cpp)
target_compile_definitions(HyphaClockDiagnostic PUBLIC
    NOMINMAX=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 KIRIN_HYPHA_PRESENTATION_CLOCK=1)
if(MSVC)
    target_compile_options(HyphaClockDiagnostic PRIVATE /utf-8)
endif()
target_link_libraries(HyphaClockDiagnostic PRIVATE juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
# This separately named target is diagnostic only; normal product/release/install targets do not
# include this standalone CMake project or its VST3. The known-delay fixture is opt-in below.
juce_add_plugin(KirinHyphaPdcValidationDelay
    PLUGIN_MANUFACTURER_CODE Kirn PLUGIN_CODE Khpd FORMATS VST3
    PRODUCT_NAME "Kirin Hypha PDC Validation Delay 4096"
    COMPANY_NAME Kirin BUNDLE_ID com.kirinmastering.hypha.pdc-validation-delay
    VST3_CATEGORIES Fx Tools IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(KirinHyphaPdcValidationDelay PRIVATE ../pdc_validation_delay/PluginProcessor.cpp)
if(MSVC)
    target_compile_options(KirinHyphaPdcValidationDelay PRIVATE /utf-8)
endif()
target_compile_definitions(KirinHyphaPdcValidationDelay PUBLIC
    NOMINMAX=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(KirinHyphaPdcValidationDelay PRIVATE juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
