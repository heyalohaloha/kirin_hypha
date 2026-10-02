cmake_minimum_required(VERSION 3.22)
project(HyphaClockDiagnostic VERSION 0.0.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT DEFINED HYPHA_DIAGNOSTIC_JUCE_ROOT)
    message(FATAL_ERROR "Explicit verified patched JUCE source is required")
endif()
add_subdirectory("${HYPHA_DIAGNOSTIC_JUCE_ROOT}" JUCE)
set(HYPHA_DIAGNOSTIC_AAX_SDK "" CACHE PATH "External licensed AAX SDK; diagnostics only")
option(HYPHA_DIAGNOSTIC_AAX_LICENSE_CONFIRMED "Confirm external SDK license for diagnostic build" OFF)
if(HYPHA_DIAGNOSTIC_AAX_SDK)
    if(NOT HYPHA_DIAGNOSTIC_AAX_LICENSE_CONFIRMED
       OR NOT EXISTS "${HYPHA_DIAGNOSTIC_AAX_SDK}/Interfaces/AAX_ITransport.h")
        message(FATAL_ERROR "An explicit licensed external AAX SDK is required")
    endif()
    file(READ "${HYPHA_DIAGNOSTIC_JUCE_ROOT}/modules/juce_audio_basics/audio_play_head/juce_AudioPlayHead.h" _clock_header)
    if(NOT _clock_header MATCHES "getKirinDiagnosticTodSamples")
        message(FATAL_ERROR "Apply aax-tod-diagnostic.patch to an ISOLATED diagnostic JUCE copy first")
    endif()
    if(NOT _clock_header MATCHES "getKirinDiagnosticAddClockSamples")
        message(FATAL_ERROR "Apply aax-addclock-diagnostic.patch to the ISOLATED diagnostic JUCE copy too")
    endif()
    file(READ "${HYPHA_DIAGNOSTIC_JUCE_ROOT}/modules/juce_audio_plugin_client/juce_audio_plugin_client_AAX.cpp" _aax_wrapper)
    if(NOT _aax_wrapper MATCHES "desc.AddClock.*diagnosticAddClock")
        message(FATAL_ERROR "The diagnostic AAX algorithm context must actually subscribe AddClock")
    endif()
    juce_set_aax_sdk_path("${HYPHA_DIAGNOSTIC_AAX_SDK}")
endif()
add_executable(HyphaClockTraceTest clock_trace_test.cpp)
enable_testing()
add_test(NAME HyphaClockTraceTest COMMAND HyphaClockTraceTest)
add_executable(HyphaIdentitySignalTest identity_signal_test.cpp)
add_test(NAME HyphaIdentitySignalTest COMMAND HyphaIdentitySignalTest)
set(CLOCK_DIAGNOSTIC_FORMATS VST3)
if(APPLE)
    list(APPEND CLOCK_DIAGNOSTIC_FORMATS AU)
endif()
if(HYPHA_DIAGNOSTIC_AAX_SDK)
    list(APPEND CLOCK_DIAGNOSTIC_FORMATS AAX)
endif()
juce_add_plugin(HyphaClockDiagnostic
    PLUGIN_MANUFACTURER_CODE Kirn PLUGIN_CODE Khck FORMATS ${CLOCK_DIAGNOSTIC_FORMATS}
    PRODUCT_NAME "Kirin Clock Diagnostic - Not Product"
    COMPANY_NAME Kirin BUNDLE_ID com.kirinmastering.clock-diagnostic
    AAX_IDENTIFIER com.kirinmastering.clock-diagnostic AAX_CATEGORY ePlugInCategory_None
    VST3_CATEGORIES Fx Tools IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(HyphaClockDiagnostic PRIVATE
    PluginProcessor.cpp
    ../../src/HyphaTypography.cpp)
target_compile_definitions(HyphaClockDiagnostic PUBLIC
    NOMINMAX=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 KIRIN_HYPHA_PRESENTATION_CLOCK=1)
if(MSVC)
    target_compile_options(HyphaClockDiagnostic PRIVATE /utf-8)
endif()
target_link_libraries(HyphaClockDiagnostic PRIVATE juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
# Explicitly separate identity and UUID. This one REPLACES input with a quiet diagnostic
# signal only after arming. Never substitute it for the read-only Clock Diagnostic/product.
juce_add_plugin(HyphaLoopIdentitySource
    PLUGIN_MANUFACTURER_CODE Kirn PLUGIN_CODE Khis FORMATS ${CLOCK_DIAGNOSTIC_FORMATS}
    PRODUCT_NAME "Kirin Loop Identity Source - Not Product"
    COMPANY_NAME Kirin BUNDLE_ID com.kirinmastering.loop-identity-source
    AAX_IDENTIFIER com.kirinmastering.loop-identity-source AAX_CATEGORY ePlugInCategory_None
    VST3_CATEGORIES Fx Tools IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(HyphaLoopIdentitySource PRIVATE PluginProcessor.cpp ../../src/HyphaTypography.cpp)
target_compile_definitions(HyphaLoopIdentitySource PUBLIC
    NOMINMAX=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 KIRIN_HYPHA_PRESENTATION_CLOCK=1 KIRIN_CLOCK_IDENTITY_SOURCE=1)
if(MSVC)
    target_compile_options(HyphaLoopIdentitySource PRIVATE /utf-8)
endif()
target_link_libraries(HyphaLoopIdentitySource PRIVATE juce::juce_audio_processors
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
# This separately named target is diagnostic only; normal product/release/install targets do not
# include this standalone CMake project or its VST3. The known-delay fixture is opt-in below.
juce_add_plugin(KirinHyphaPdcValidationDelay
    PLUGIN_MANUFACTURER_CODE Kirn PLUGIN_CODE Khpd FORMATS ${CLOCK_DIAGNOSTIC_FORMATS}
    PRODUCT_NAME "Kirin Hypha PDC Validation Delay 4096"
    COMPANY_NAME Kirin BUNDLE_ID com.kirinmastering.hypha.pdc-validation-delay
    AAX_IDENTIFIER com.kirinmastering.hypha.pdc-validation-delay AAX_CATEGORY ePlugInCategory_None
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
if(HYPHA_DIAGNOSTIC_AAX_SDK)
    foreach(_target HyphaClockDiagnostic HyphaLoopIdentitySource KirinHyphaPdcValidationDelay)
        target_compile_definitions(${_target} PUBLIC KIRIN_CLOCK_DIAGNOSTIC_TOD=1 KIRIN_CLOCK_DIAGNOSTIC_ADD_CLOCK=1)
        target_compile_definitions(${_target}_AAX PRIVATE JucePlugin_AAXDisableAudioSuite=1)
    endforeach()
endif()
