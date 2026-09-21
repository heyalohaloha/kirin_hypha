#[test]
fn product_runtime_contracts_are_registered_in_platform_gates() {
    let ci = include_str!("../../.github/workflows/ci.yml");
    let source_gate = include_str!("../../scripts/test_release_source.sh");
    let root_cmake = include_str!("../../juce_shell/CMakeLists.txt").to_owned()
        + include_str!("../../juce_shell/cmake/PairPreviewTests.cmake");
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
        "KirinLocalBlindProductTests",
        "KirinEditorSurfaceProductTests",
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
        (
            "KirinPairPreviewLifetimeTests",
            "kirin_pair_preview_lifetime",
        ),
        (
            "KirinReferenceAuditionRuntimeTests",
            "kirin_reference_capture_memory",
        ),
    ] {
        assert!(root_cmake.contains(target) && root_cmake.contains(test_name));
        assert!(ci.contains(target) && ci.contains(test_name));
        assert!(source_gate.contains(target) && source_gate.contains(test_name));
    }
    assert!(ci.contains("-R '^(kirin_local_blind_.*|kirin_editor_surface_product)$'"));
    let selected: Vec<_> = source_gate
        .lines()
        .find_map(|line| line.strip_prefix("JUCE_TEST_REGEX='^("))
        .and_then(|line| line.strip_suffix(")$'"))
        .expect("the native gate has one anchored test selection")
        .split('|')
        .collect();
    for test in [
        "kirin_editor_surface_product",
        "kirin_pair_preview_lifetime",
        "kirin_reference_capture_memory",
    ] {
        assert!(
            selected.contains(&test),
            "native gate does not execute {test}"
        );
    }
}

#[test]
fn visible_guide_action_accepts_pending_connection_before_opening_received_guide_details() {
    let action = include_str!("../../juce_shell/src/PluginEditorMenu.cpp");
    let connect = action
        .find("processorRef.acceptPreDisplayConnection()")
        .expect("the visible guide action accepts a pending connection");
    let details = action.find("if (! guide.guideAvailable").unwrap();
    assert!(action.contains("processorRef.pendingPreDisplayConnection().validAt"));
    assert!(connect < details);
}
