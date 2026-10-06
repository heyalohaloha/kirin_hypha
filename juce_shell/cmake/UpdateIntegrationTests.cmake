if(KIRIN_HYPHA_BUILD_UPDATE_TESTS AND KIRIN_HYPHA_BUILD_REFERENCE_AUDITION_TESTS)
    add_executable(KirinUpdateRuntimeIntegrationTests tests/update_runtime_integration_test.cpp
        src/update/UpdateChecker.cpp
        src/update/UpdateManifest.cpp src/update/UpdateStore.cpp
        src/reference_audition/ReferenceRuntimeV2Repository.cpp
        src/reference_audition/ReferenceRuntimeRepositoryParsing.cpp
        src/reference_audition/ReferenceRuntimeV2PresetParsing.cpp
        src/reference_audition/ReferenceLibraryRepository.cpp
        src/reference_audition/ReferenceLibrarySets.cpp
        src/reference_audition/ReferenceLibrarySongs.cpp
        src/reference_audition/ReferenceSourceRanges.cpp
        src/reference_audition/ReferenceRuntimeV2Source.cpp)
    target_compile_features(KirinUpdateRuntimeIntegrationTests PRIVATE cxx_std_17)
    target_compile_definitions(KirinUpdateRuntimeIntegrationTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 "HYPHA_UPDATE_LOADED_VERSION=\"${PROJECT_VERSION}\"")
    target_include_directories(KirinUpdateRuntimeIntegrationTests PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/generated src ../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinUpdateRuntimeIntegrationTests PRIVATE ${KIRIN_FFI_LIB}
        juce::juce_core juce::juce_cryptography juce::juce_audio_formats ${KIRIN_RUST_NATIVE_LIBS})
    add_dependencies(KirinUpdateRuntimeIntegrationTests HyphaBuildIdentity)
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(KirinUpdateRuntimeIntegrationTests KirinHyphaRustFFI)
    endif()
    hypha_update_transport_sources(KirinUpdateRuntimeIntegrationTests)
    hypha_update_build_trust(KirinUpdateRuntimeIntegrationTests)
    foreach(scenario off success failure cancel lifetime)
        add_test(NAME kirin_update_runtime_${scenario} COMMAND KirinUpdateRuntimeIntegrationTests ${scenario})
        set_tests_properties(kirin_update_runtime_${scenario} PROPERTIES TIMEOUT 60)
    endforeach()
endif()
