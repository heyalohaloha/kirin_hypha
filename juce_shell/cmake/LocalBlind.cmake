# Standalone CPU/ownership tests, also included by the existing UI-test CI configuration.
option(KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS "Build local PRE/POST Blind runtime contracts" OFF)
if(KIRIN_HYPHA_BUILD_LOCAL_BLIND_TESTS OR KIRIN_HYPHA_BUILD_UI_RENDER_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    include(${CMAKE_CURRENT_LIST_DIR}/LocalBlindPortable.cmake)
    kirin_add_local_blind_portable_contracts("${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(KirinLocalBlindPreparationTests
        tests/local_blind_preparation_test.cpp src/local_blind/LocalBlindPreparation.cpp
        src/local_blind/LocalBlindTrial.cpp)
    target_compile_features(KirinLocalBlindPreparationTests PRIVATE cxx_std_17)
    target_compile_options(KirinLocalBlindPreparationTests PRIVATE ${KIRIN_SOURCE_ENCODING_ARGS})
    target_link_libraries(KirinLocalBlindPreparationTests PRIVATE Threads::Threads)
    target_include_directories(KirinLocalBlindPreparationTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../crates/kirin_hypha_ffi/include)
    target_link_libraries(KirinLocalBlindPreparationTests PRIVATE ${KIRIN_FFI_LIB} ${KIRIN_RUST_NATIVE_LIBS})
    if(TARGET KirinHyphaRustFFI)
        add_dependencies(KirinLocalBlindPreparationTests KirinHyphaRustFFI)
    endif()
    if(APPLE)
        target_link_libraries(KirinLocalBlindPreparationTests PRIVATE "-framework Security" "-framework CoreFoundation")
    endif()
    add_test(NAME kirin_local_blind_preparation COMMAND KirinLocalBlindPreparationTests
        "${CMAKE_CURRENT_SOURCE_DIR}/../test_signals/S-1_1kHz_sine_m6dBFS_10s.wav")
    set_tests_properties(kirin_local_blind_preparation
        PROPERTIES TIMEOUT 120)
endif()
