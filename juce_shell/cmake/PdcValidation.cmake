# A separately identified diagnostic effect with an exact, declared 4096-sample latency.
# AAX is available only with the external licensed SDK. No product gate is opened here.
# This target stays absent from normal configure/build/install/release targets.
option(KIRIN_HYPHA_BUILD_PDC_VALIDATION_DELAY
    "Build the non-shipping 4096-sample host PDC validation effect" OFF)
if(KIRIN_HYPHA_BUILD_PDC_VALIDATION_DELAY)
    set(_pdc_formats VST3)
    set(_pdc_aax_args)
    if(KIRIN_HYPHA_AAX_ENABLED)
        list(APPEND _pdc_formats AAX)
        list(APPEND _pdc_aax_args
            AAX_IDENTIFIER com.kirinmastering.hypha.pdc-validation-delay
            AAX_CATEGORY ePlugInCategory_None)
    endif()
    juce_add_plugin(KirinHyphaPdcValidationDelay
        PLUGIN_MANUFACTURER_CODE Kirn
        PLUGIN_CODE Khpd
        FORMATS ${_pdc_formats}
        ${_pdc_aax_args}
        PRODUCT_NAME "Kirin Hypha PDC Validation Delay 4096"
        COMPANY_NAME "Kirin"
        BUNDLE_ID "com.kirinmastering.hypha.pdc-validation-delay"
        VST3_CATEGORIES Fx Tools
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)
    if(KIRIN_HYPHA_AAX_ENABLED)
        target_compile_definitions(KirinHyphaPdcValidationDelay_AAX PRIVATE
            JucePlugin_AAXDisableAudioSuite=1)
    endif()
    target_sources(KirinHyphaPdcValidationDelay PRIVATE
        tests/pdc_validation_delay/PluginProcessor.cpp)
    target_compile_definitions(KirinHyphaPdcValidationDelay PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_compile_options(KirinHyphaPdcValidationDelay PRIVATE
        ${KIRIN_SOURCE_ENCODING_ARGS})
    target_link_libraries(KirinHyphaPdcValidationDelay PRIVATE
        juce::juce_audio_processors
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
endif()
