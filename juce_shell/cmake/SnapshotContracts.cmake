# The sized handshake is tested against the actual archive on each native platform.
add_executable(KirinSnapshotAbiContractTests
    tests/SnapshotAbiContractTest.cpp tests/TimeSnapshotProbe.cpp)
target_include_directories(KirinSnapshotAbiContractTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../crates/kirin_hypha_ffi/include")
target_link_libraries(KirinSnapshotAbiContractTests PRIVATE
    "${KIRIN_FFI_LIB}" ${KIRIN_RUST_NATIVE_LIBS})
if(APPLE)
    target_link_libraries(KirinSnapshotAbiContractTests PRIVATE
        "-framework Foundation" "-framework Security")
endif()
if(TARGET KirinHyphaRustFFI)
    add_dependencies(KirinSnapshotAbiContractTests KirinHyphaRustFFI)
endif()
add_test(NAME kirin_snapshot_abi_contract COMMAND KirinSnapshotAbiContractTests)
