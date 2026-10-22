# Measure actual decoded mono PCM in each UI contract that calls the shared check.
include_guard(GLOBAL)
function(kirin_reference_visual_measurement_test target)
    target_sources(${target} PRIVATE
        tests/ReferenceMonoVisualTest.cpp
        src/reference_audition/ReferenceVisualObservation.cpp
        src/reference_audition/ReferenceVisualAudio.cpp
        src/reference_audition/ReferenceRuntimeV2Source.cpp
        src/reference_audition/ReferenceKirinSpectrum.cpp
        src/reference_audition/ReferenceKirinFingerprint.cpp
        src/reference_audition/ReferenceDynamicsTicks.cpp)
    target_link_libraries(${target} PRIVATE
        juce::juce_audio_formats "${KIRIN_FFI_LIB}" ${KIRIN_RUST_NATIVE_LIBS})
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(${target} KirinHyphaRustFFI)
    endif()
endfunction()
