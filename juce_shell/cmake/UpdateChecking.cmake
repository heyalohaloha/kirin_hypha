include(${CMAKE_CURRENT_LIST_DIR}/UpdateTrust.cmake)

function(hypha_update_transport_sources target)
    target_sources(${target} PRIVATE src/update/UpdateTransport.cpp)
    if(WIN32)
        target_sources(${target} PRIVATE src/update/UpdateTransportWindows.cpp)
        target_link_libraries(${target} PRIVATE winhttp)
    elseif(APPLE)
        target_sources(${target} PRIVATE src/update/UpdateTransportMac.mm src/update/UpdateHttpResponse.cpp)
        target_link_libraries(${target} PRIVATE "-framework Network" "-framework Security")
    endif()
endfunction()

function(hypha_update_build_trust target)
    target_sources(${target} PRIVATE src/update/UpdateBuildTrust.cpp)
    target_compile_definitions(${target} PRIVATE
        "HYPHA_UPDATE_PUBLIC_KEY=\"${KIRIN_HYPHA_UPDATE_PUBLIC_KEY}\""
        "HYPHA_UPDATE_KEY_SHA256=\"${KIRIN_HYPHA_UPDATE_KEY_SHA256}\""
        "HYPHA_UPDATE_KEY_MARKER=\"${KIRIN_HYPHA_UPDATE_KEY_MARKER}\"")
endfunction()

function(hypha_add_update_checking target)
    target_sources(${target} PRIVATE src/PluginEditorUpdate.cpp
        src/update/UpdateChecker.cpp
        src/update/UpdateManifest.cpp src/update/UpdateStore.cpp)
    hypha_update_transport_sources(${target})
    hypha_update_build_trust(${target})
endfunction()

function(hypha_au_resource_usage target)
    # Existing file access is preserved. Only a deliberately provisioned update build
    # declares network access; no signing gate may silently accept a different reason.
    set(_update_plist_commands "")
    if(NOT KIRIN_HYPHA_UPDATE_PUBLIC_KEY STREQUAL "")
        set(_update_plist_commands "; /usr/libexec/PlistBuddy -c 'Add :AudioComponents:0:resourceUsage:network.client bool true' -c 'Add :KirinHyphaUpdateProtocol integer 1' \"$1\"")
    endif()
    add_custom_command(TARGET ${target}_AU POST_BUILD
        COMMAND /bin/sh -c
            "set -e; /usr/libexec/PlistBuddy -c 'Delete :AudioComponents:0:resourceUsage' \"$1\" >/dev/null 2>&1 || true; /usr/libexec/PlistBuddy -c 'Delete :KirinHyphaUpdateProtocol' \"$1\" >/dev/null 2>&1 || true; /usr/libexec/PlistBuddy -c 'Add :AudioComponents:0:resourceUsage dict' -c 'Add :AudioComponents:0:resourceUsage:temporary-exception.files.all.read-write bool true' \"$1\"${_update_plist_commands}"
            _ "$<TARGET_FILE_DIR:${target}_AU>/../Info.plist"
        VERBATIM COMMENT "AU: preserve file access; network only for pinned opt-in updater")
    foreach(format AU VST3 AAX)
        if(TARGET ${target}_${format})
            add_custom_command(TARGET ${target}_${format} POST_BUILD
                COMMAND /bin/sh -c
                    "set -e; /usr/libexec/PlistBuddy -c 'Delete :KirinHyphaUpdateKeySha256' \"$1\" >/dev/null 2>&1 || true; /usr/libexec/PlistBuddy -c 'Add :KirinHyphaUpdateKeySha256 string ${KIRIN_HYPHA_UPDATE_KEY_SHA256}' \"$1\""
                    _ "$<TARGET_FILE_DIR:${target}_${format}>/../Info.plist"
                VERBATIM COMMENT "${format}: bind update verification key to actual payload")
        endif()
    endforeach()
endfunction()

option(KIRIN_HYPHA_BUILD_UPDATE_TESTS "Build offline update checker/manifest/storage tests" OFF)
if(KIRIN_HYPHA_BUILD_UPDATE_TESTS)
    enable_testing()
    foreach(suite manifest store checker)
        add_executable(KirinUpdate_${suite}_Tests tests/update_${suite}_test.cpp
            src/update/UpdateManifest.cpp src/update/UpdateStore.cpp)
        target_compile_features(KirinUpdate_${suite}_Tests PRIVATE cxx_std_17)
        target_compile_definitions(KirinUpdate_${suite}_Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(KirinUpdate_${suite}_Tests PRIVATE juce::juce_core juce::juce_cryptography)
        add_test(NAME kirin_update_${suite} COMMAND KirinUpdate_${suite}_Tests)
    endforeach()
    target_sources(KirinUpdate_checker_Tests PRIVATE src/update/UpdateChecker.cpp)
    hypha_update_transport_sources(KirinUpdate_checker_Tests)
    hypha_update_build_trust(KirinUpdate_checker_Tests)
    target_include_directories(KirinUpdate_checker_Tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated src)
    target_compile_definitions(KirinUpdate_checker_Tests PRIVATE "HYPHA_UPDATE_LOADED_VERSION=\"${PROJECT_VERSION}\"")
    add_dependencies(KirinUpdate_checker_Tests HyphaBuildIdentity)
    add_executable(KirinUpdate_http_response_Tests tests/update_http_response_test.cpp src/update/UpdateHttpResponse.cpp)
    target_compile_features(KirinUpdate_http_response_Tests PRIVATE cxx_std_17)
    target_compile_definitions(KirinUpdate_http_response_Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(KirinUpdate_http_response_Tests PRIVATE juce::juce_core)
    add_test(NAME kirin_update_http_response COMMAND KirinUpdate_http_response_Tests)
    if(APPLE)
        add_executable(KirinUpdate_transport_mac_Tests tests/update_transport_mac_test.mm)
        hypha_update_transport_sources(KirinUpdate_transport_mac_Tests)
        target_compile_features(KirinUpdate_transport_mac_Tests PRIVATE cxx_std_17)
        target_compile_definitions(KirinUpdate_transport_mac_Tests PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 HYPHA_UPDATE_TRANSPORT_TESTING=1)
        target_link_libraries(KirinUpdate_transport_mac_Tests PRIVATE juce::juce_core)
        add_test(NAME kirin_update_transport_mac COMMAND KirinUpdate_transport_mac_Tests)
        set_tests_properties(kirin_update_transport_mac PROPERTIES TIMEOUT 30)
    endif()
endif()
