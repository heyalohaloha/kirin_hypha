#[test]
fn product_runtime_contracts_are_registered_in_platform_gates() {
    let ci = include_str!("../../.github/workflows/ci.yml");
    let source_gate = include_str!("../../scripts/test_release_source.sh");
    let root_cmake = include_str!("../../juce_shell/CMakeLists.txt");
    let cmake = include_str!("../../juce_shell/cmake/LocalBlind.cmake");
    let portable = include_str!("../../juce_shell/cmake/LocalBlindPortable.cmake");
    assert!(cmake.contains("include(${CMAKE_CURRENT_LIST_DIR}/LocalBlindPortable.cmake)"));
    assert!(cmake.contains("kirin_add_local_blind_portable_contracts"));
    for target in [
        "KirinLocalBlindCaptureTests",
        "KirinLocalBlindCaptureServiceTests",
        "KirinLocalBlindTrialTests",
        "KirinLocalBlindPreparationTests",
        "KirinLocalBlindHostContextTests",
    ] {
        assert!(ci.contains(target));
        assert!(source_gate.contains(target));
        assert!(cmake.contains(target) || portable.contains(target));
    }
    for (target, test_name) in [
        (
            "KirinCaptureWorkAttachmentTests",
            "kirin_capture_work_attachment",
        ),
        (
            "KirinReferenceAuditionRuntimeTests",
            "kirin_reference_audition_runtime",
        ),
        (
            "KirinReferenceAudioPagesTests",
            "kirin_reference_audio_pages",
        ),
    ] {
        assert!(root_cmake.contains(target) && root_cmake.contains(test_name));
        assert!(ci.contains(target) && ci.contains(test_name));
        assert!(source_gate.contains(target) && source_gate.contains(test_name));
    }
    assert!(ci.contains("-R '^kirin_local_blind_'"));
}
