# Product fixtures must compile the processor with the test clock too. An executable-only
# definition would leave the shipping static library on steady_clock (and violate the ODR).
set(_clock_fixtures
    KirinContentAnalysisProductTests KirinAaxAnalysisProductTests KirinReaperListenProductTests
    KirinLiveTimingProductTests KirinLiveBlindProductTests KirinLocalBlindProductTests
    KirinLiveComparePinProductTests KirinLiveCompareOffsetProductTests
    KirinLiveCompareAaxGroupProductTests KirinLiveCompareLifecycleTests
    KirinLiveReentryProductTests KirinLiveRecoveryProductTests)
set(_have_clock_fixture FALSE)
foreach(fixture IN LISTS _clock_fixtures)
    if(TARGET ${fixture})
        set(_have_clock_fixture TRUE)
    endif()
endforeach()
if(NOT _have_clock_fixture)
    return()
endif()

find_program(KIRIN_TEST_CARGO cargo REQUIRED)
set(_test_rust_dir "${CMAKE_CURRENT_BINARY_DIR}/test-rust")
if(WIN32)
    set(_test_ffi "${_test_rust_dir}/release/kirin_hypha_ffi.lib")
else()
    set(_test_ffi "${_test_rust_dir}/release/libkirin_hypha_ffi.a")
endif()
add_custom_target(KirinNativeTestFFI
    COMMAND ${KIRIN_TEST_CARGO} build --release --locked -j4 -p kirin_hypha_ffi
        --features native-test-support --target-dir "${_test_rust_dir}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    BYPRODUCTS "${_test_ffi}" VERBATIM)

add_library(KirinNativeTestProcessor STATIC)
foreach(property SOURCES INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS COMPILE_FEATURES)
    get_target_property(value KirinHyphaPOST ${property})
    if(value)
        set_property(TARGET KirinNativeTestProcessor PROPERTY ${property} "${value}")
    endif()
endforeach()
get_target_property(_test_links KirinHyphaPOST LINK_LIBRARIES)
list(REMOVE_ITEM _test_links "${KIRIN_FFI_LIB}")
target_link_libraries(KirinNativeTestProcessor PRIVATE ${_test_links} "${_test_ffi}")
target_compile_definitions(KirinNativeTestProcessor PUBLIC KIRIN_HYPHA_TEST_WALL_CLOCK=1)
target_compile_options(KirinNativeTestProcessor PUBLIC ${KIRIN_FORCE_INCLUDE_ARGS} ${KIRIN_SOURCE_ENCODING_ARGS})
add_dependencies(KirinNativeTestProcessor HyphaBuildIdentity KirinNativeTestFFI KirinHyphaPOST)

foreach(fixture IN LISTS _clock_fixtures)
    if(TARGET ${fixture})
        get_target_property(links ${fixture} LINK_LIBRARIES)
        list(TRANSFORM links REPLACE "^KirinHyphaPOST$" "KirinNativeTestProcessor")
        set_property(TARGET ${fixture} PROPERTY LINK_LIBRARIES "${links}")
    endif()
endforeach()

add_executable(KirinProductWallClockTests tests/product_wall_clock_test.cpp)
target_compile_definitions(KirinProductWallClockTests PRIVATE KIRIN_HYPHA_TEST_WALL_CLOCK=1)
add_test(NAME kirin_product_wall_clock COMMAND KirinProductWallClockTests)
add_executable(KirinShippingWallClockTests tests/shipping_wall_clock_test.cpp)
add_test(NAME kirin_shipping_wall_clock COMMAND KirinShippingWallClockTests)
set_tests_properties(kirin_product_wall_clock kirin_shipping_wall_clock PROPERTIES TIMEOUT 30)

add_executable(KirinProcessorWallClockTests tests/processor_wall_clock_test.cpp)
target_compile_definitions(KirinProcessorWallClockTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,COMPILE_DEFINITIONS>")
target_include_directories(KirinProcessorWallClockTests PRIVATE "$<TARGET_PROPERTY:KirinHyphaPOST,INCLUDE_DIRECTORIES>")
target_link_libraries(KirinProcessorWallClockTests PRIVATE KirinNativeTestProcessor juce::juce_recommended_warning_flags)
add_test(NAME kirin_processor_wall_clock COMMAND KirinProcessorWallClockTests)
set_tests_properties(kirin_processor_wall_clock PROPERTIES TIMEOUT 60)
