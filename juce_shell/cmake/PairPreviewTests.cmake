# Development-only load/unload probe; no plugin bundle is installed or packaged.
add_library(KirinPairPreviewModule MODULE tests/pair_preview_module.cpp src/HyphaPairPreview.cpp)
target_include_directories(KirinPairPreviewModule PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../crates/kirin_hypha_ffi/include")
target_compile_options(KirinPairPreviewModule PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
target_link_libraries(KirinPairPreviewModule PRIVATE ${KIRIN_FFI_LIB} ${KIRIN_RUST_NATIVE_LIBS})
if(TARGET KirinHyphaRustFFI)
    add_dependencies(KirinPairPreviewModule KirinHyphaRustFFI)
endif()
if(APPLE)
    target_link_libraries(KirinPairPreviewModule PRIVATE "-framework Security" "-framework CoreFoundation")
endif()
juce_add_console_app(KirinPairPreviewLifetimeTests PRODUCT_NAME "Kirin Pair Preview Lifetime Tests")
target_sources(KirinPairPreviewLifetimeTests PRIVATE tests/pair_preview_lifetime_test.cpp)
target_compile_definitions(KirinPairPreviewLifetimeTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_compile_options(KirinPairPreviewLifetimeTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
target_link_libraries(KirinPairPreviewLifetimeTests PRIVATE juce::juce_core ${CMAKE_DL_LIBS})
add_dependencies(KirinPairPreviewLifetimeTests KirinPairPreviewModule)
add_test(NAME kirin_pair_preview_lifetime COMMAND KirinPairPreviewLifetimeTests
    "$<TARGET_FILE:KirinPairPreviewModule>" "$<TARGET_FILE:KirinHyphaPOST_VST3>" "$<TARGET_FILE:KirinReceiptPinProbe>")
set_tests_properties(kirin_pair_preview_lifetime PROPERTIES TIMEOUT 120)

# Exercise notification retirement through the actual development VST3's loader boundary.
# Probe exports are confined to test-enabled wrappers, never the shared production sources.
set(receipt_probe "${CMAKE_CURRENT_SOURCE_DIR}/tests/ReferenceReceiptPluginProbe.h")
set_source_files_properties("${receipt_probe}" PROPERTIES LANGUAGE CXX HEADER_FILE_ONLY FALSE)
add_library(KirinReceiptPinProbe MODULE "${receipt_probe}")
target_compile_features(KirinReceiptPinProbe PRIVATE cxx_std_17)
target_compile_definitions(KirinReceiptPinProbe PRIVATE KIRIN_RECEIPT_PIN_PROBE_ONLY=1)
target_link_libraries(KirinReceiptPinProbe PRIVATE ${CMAKE_DL_LIBS})
target_sources(KirinHyphaPOST_VST3 PRIVATE "${receipt_probe}")
target_include_directories(KirinHyphaPOST_VST3 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../crates/kirin_hypha_ffi/include")
target_compile_definitions(KirinHyphaPOST_VST3 PRIVATE
    KIRIN_REFERENCE_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_dependencies(KirinPairPreviewLifetimeTests KirinReceiptPinProbe KirinHyphaPOST_VST3)
