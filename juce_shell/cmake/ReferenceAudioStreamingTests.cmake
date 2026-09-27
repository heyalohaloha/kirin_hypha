juce_add_console_app(KirinReferenceAudioStreamingTests
    PRODUCT_NAME "Kirin Reference Audio Streaming Tests")
target_sources(KirinReferenceAudioStreamingTests PRIVATE
    tests/reference_audio_streaming_test.cpp
    src/reference_audition/ReferenceAudioPages.cpp
    src/reference_audition/ReferenceVisualAudio.cpp
    src/reference_audition/ReferenceAudioPageRender.cpp
    src/reference_audition/ReferenceAuditionProtocol.cpp)
target_compile_definitions(KirinReferenceAudioStreamingTests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_compile_options(KirinReferenceAudioStreamingTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
target_link_libraries(KirinReferenceAudioStreamingTests PRIVATE
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
add_test(NAME kirin_reference_audio_streaming COMMAND KirinReferenceAudioStreamingTests)
set_tests_properties(kirin_reference_audio_streaming PROPERTIES TIMEOUT 30)
