cmake_minimum_required(VERSION 3.22)

project(KirinHypha VERSION 0.1.0 LANGUAGES C CXX)

# Phase 2 後段: Intel native only. universal(lipo) is out of scope (Phase 2 後半/Phase 4).
set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Build architectures for macOS" FORCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 7.0.12 (GPLv3 option), pinned as submodule. Does not touch crates/ or xtask.
add_subdirectory(JUCE)

# B-070: PRE and POST are two plugin targets sharing one role-parameterized base
# (KirinHyphaProcessorBase). They link the same kirin_hypha_ffi staticlib and compile the
# same PluginProcessor.cpp / PluginEditor.cpp; only the per-target PluginMain{PRE,POST}.cpp
# (the createPluginFilter Role) and the AU/VST3 identifiers differ.
function(add_kirin_plugin TARGET CODE PRODUCT MAIN_SRC)
    juce_add_plugin(${TARGET}
        PLUGIN_MANUFACTURER_CODE Kirn          # newly assigned (>=1 uppercase)
        PLUGIN_CODE ${CODE}                    # Khpr (PRE) / Khpo (POST); exactly 1 uppercase
        FORMATS AU VST3
        PRODUCT_NAME ${PRODUCT}
        COMPANY_NAME "Kirin"
        AU_MAIN_TYPE kAudioUnitType_Effect
        VST3_CATEGORIES Fx Analyzer
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${TARGET} PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        ${MAIN_SRC})

    # kirin_hypha_ffi: C ABI header + Rust staticlib (B-052 boundary, unchanged).
    target_include_directories(${TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../crates/kirin_hypha_ffi/include)

    target_link_libraries(${TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../target/release/libkirin_hypha_ffi.a)

    target_compile_definitions(${TARGET} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)          # permitted: built under GPLv3

    target_link_libraries(${TARGET}
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endfunction()

add_kirin_plugin(KirinHyphaPRE  Khpr "Kirin Hypha PRE"  src/PluginMainPRE.cpp)
add_kirin_plugin(KirinHyphaPOST Khpo "Kirin Hypha POST" src/PluginMainPOST.cpp)
