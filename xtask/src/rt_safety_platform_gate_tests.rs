#[test]
fn certified_hosts_use_native_complete_identity_before_audio_callbacks() {
    let reader = include_str!("../../juce_shell/src/HostExecutableIdentity.cpp");
    let live = include_str!("../../juce_shell/src/HyphaLiveCompareClockPolicy.h");
    let chain = include_str!("../../juce_shell/src/HyphaChainClockPolicy.h");
    let format = include_str!("../../juce_shell/src/PluginProcessorFormat.cpp");
    let rt = include_str!("../../juce_shell/src/PluginProcessorLiveCompareRealtime.cpp");
    let cmake = include_str!("../../juce_shell/cmake/LiveCompare.cmake");
    let ci = include_str!("../../.github/workflows/ci.yml");
    let gate = include_str!("../../scripts/test_release_source.sh");
    assert!(reader.contains("juce::File::hostApplicationPath"));
    assert!(reader.contains("identity.version = app.getVersion()"));
    assert!(reader.contains("GetFileVersionInfoW") && reader.contains("FileVersion"));
    assert!(reader.contains("identity.version = fileVersionString (executable)"));
    assert!(reader.contains("identity.fileVersion = executable.getVersion()"));
    assert!(live.contains("wrapperType_AAX ? host.fileVersion : host.version"));
    for policy in [live, chain] {
        assert!(policy.contains("host_identity::current()"));
        assert!(!policy.contains("executable.getVersion()"));
    }
    assert!(format.contains("live_compare_clock_policy::current (wrapperType, sampleRate)"));
    assert!(!rt.contains("host_identity") && !rt.contains("clock_policy::current"));
    for source in [cmake, ci, gate] {
        assert!(
            source.contains("KirinHostIdentityTests") && source.contains("kirin_host_identity")
        );
    }
}

#[test]
fn product_runtime_contracts_are_registered_in_platform_gates() {
    let ci = include_str!("../../.github/workflows/ci.yml");
    let source_gate = include_str!("../../scripts/test_release_source.sh");
    let root_cmake = include_str!("../../juce_shell/CMakeLists.txt").to_owned()
        + include_str!("../../juce_shell/cmake/PairPreviewTests.cmake")
        + include_str!("../../juce_shell/cmake/ReferenceAudioStreamingTests.cmake");
    assert!(root_cmake.contains("include(cmake/ReferenceAudioStreamingTests.cmake)"));
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
            "KirinReferenceAudioStreamingTests",
            "kirin_reference_audio_streaming",
        ),
        (
            "KirinPairPreviewLifetimeTests",
            "kirin_pair_preview_lifetime",
        ),
    ] {
        assert!(root_cmake.contains(target) && root_cmake.contains(test_name));
        assert!(ci.contains(target) && ci.contains(test_name));
        assert!(source_gate.contains(target) && source_gate.contains(test_name));
    }
    assert!(ci.contains("-R '^(kirin_local_blind_.*|kirin_editor_surface_product)$'"));
    let live_compare = include_str!("../../juce_shell/cmake/LiveCompare.cmake");
    assert!(root_cmake.contains("include(cmake/LiveCompare.cmake)"));
    for (definition, target, test) in [
        (
            live_compare,
            "KirinLiveCompareTimingTests",
            "kirin_live_compare_timing",
        ),
        (
            cmake,
            "KirinLiveTimingProductTests",
            "kirin_live_timing_product",
        ),
    ] {
        assert!(definition.contains(target) && definition.contains(test));
        assert!(ci.contains(target) && source_gate.contains(target) && source_gate.contains(test));
    }
    assert!(live_compare.contains("KirinLiveCompareCorrespondenceTests"));
    assert!(live_compare.contains("kirin_live_compare_correspondence"));
    assert!(ci.contains("KirinLiveCompareCorrespondenceTests"));
    assert!(live_compare.contains("KirinLiveCompareSessionTests"));
    assert!(live_compare.contains("kirin_live_compare_session"));
    assert!(ci.contains("KirinLiveCompareSessionTests"));
    assert!(live_compare.contains("KirinLiveCompareMatchTests"));
    assert!(ci.contains("KirinLiveCompareMatchTests"));
    assert!(ci.contains(
        "-R '^(kirin_live_compare_(correspondence|timing|session|match|loop|loop_feasibility|completion|authority|reentry|gain_snapshot)|kirin_live_blind_session)$'"
    ));
    assert!(source_gate.contains("KirinLiveCompareMatchTests"));
    assert!(source_gate.contains("KirinLiveCompareSessionTests"));
    assert!(source_gate.contains("KirinLiveCompareCorrespondenceTests"));
    // PIN to Blind and the offset warning run the real processors through the live ring of each
    // platform: the macOS source gate and the Windows CI both execute them.
    let local_blind = include_str!("../../juce_shell/cmake/LocalBlind.cmake");
    assert!(local_blind.contains("KirinLiveComparePinProductTests"));
    assert!(local_blind.contains("kirin_live_compare_pin_product"));
    assert!(source_gate.contains("KirinLiveComparePinProductTests"));
    assert!(local_blind.contains("KirinLiveCompareOffsetProductTests"));
    assert!(local_blind.contains("kirin_live_compare_offset_product"));
    assert!(source_gate.contains("KirinLiveCompareOffsetProductTests"));
    assert!(local_blind.contains("KirinLiveCompareAaxGroupProductTests"));
    assert!(local_blind.contains("kirin_live_compare_aax_group_product"));
    assert!(source_gate.contains("KirinLiveCompareAaxGroupProductTests"));
    // INV-LC25: the chain timing runs the real processors and ring on both platforms too.
    assert!(local_blind.contains("KirinLiveChainTimingProductTests"));
    assert!(local_blind.contains("kirin_live_chain_timing_product"));
    assert!(source_gate.contains("KirinLiveChainTimingProductTests"));
    assert!(source_gate.contains("kirin_live_chain_timing_product"));
    assert!(ci.contains("KirinLiveChainTimingProductTests"));
    assert!(
        !local_blind.contains("if(NOT WIN32)\n        add_executable(KirinLiveCompare"),
        "the live compare product tests are built on Windows too"
    );
    assert!(
        ci.contains("--target KirinLiveComparePinProductTests KirinLiveCompareOffsetProductTests KirinLiveCompareAaxGroupProductTests")
    );
    assert!(
        ci.contains("-R '^kirin_live_.*(product|restore-active|restore-invalid|restore-preparing|restore-approval|held|finishing|out-of-range|reuse-held|restore-matched-listen)$'")
    );
    for target in [
        "KirinLiveCompareLoopTests",
        "KirinLiveCompareTimingTests",
        "KirinLiveTimingProductTests",
        "KirinLiveCompareLoopFeasibilityTests",
        "KirinLiveBlindProductTests",
        "KirinLiveRecoveryProductTests",
        "KirinLiveCompareLifecycleTests",
        "Kirin_live_blind_session_Tests",
        "Kirin_live_compare_completion_Tests",
        "Kirin_live_compare_authority_Tests",
        "KirinLiveCompare_reentry_Tests",
        "KirinLiveCompare_gain_snapshot_Tests",
        "KirinLiveReentryProductTests",
    ] {
        assert!(ci.contains(target) && source_gate.contains(target));
    }
    assert!(live_compare.contains("foreach(contract IN ITEMS reentry gain_snapshot)"));
    assert!(live_compare.contains("add_test(NAME kirin_live_compare_${contract}"));
    assert!(local_blind.contains("KirinLiveReentryProductTests"));
    assert!(local_blind.contains("add_test(NAME kirin_live_reentry_${scenario}_product"));
    assert_eq!(
        source_gate
            .lines()
            .filter(|line| line.starts_with("JUCE_TEST_REGEX="))
            .count(),
        1,
        "the native gate must use one explicit, auditable CTest inventory"
    );
    let selected: Vec<_> = source_gate
        .lines()
        .find_map(|line| line.strip_prefix("JUCE_TEST_REGEX='^("))
        .and_then(|line| line.strip_suffix(")$'"))
        .expect("the native gate has one anchored test selection")
        .split('|')
        .collect();
    let expected_count: usize = source_gate
        .lines()
        .find_map(|line| line.strip_prefix("JUCE_TEST_COUNT="))
        .expect("the native gate pins its expected CTest inventory")
        .parse()
        .expect("the expected CTest inventory is an integer");
    let unique: std::collections::BTreeSet<_> = selected.iter().copied().collect();
    assert_eq!(unique.len(), selected.len(), "duplicate CTest selection");
    assert_eq!(
        selected.len(),
        expected_count,
        "native gate selection and expected inventory must agree"
    );
    for test in [
        "kirin_native_storage_guard_setup",
        "kirin_native_storage_guard_cleanup",
        "kirin_native_storage_guard_contract",
        "kirin_live_publication_PRE_storage-only",
        "kirin_live_publication_POST_storage-only",
        "kirin_editor_surface_product",
        "kirin_pair_preview_lifetime",
        "kirin_reference_audio_streaming",
        "kirin_live_compare_correspondence",
        "kirin_live_compare_timing",
        "kirin_live_timing_product",
        "kirin_live_compare_session",
        "kirin_live_compare_match",
        "kirin_live_compare_pin_product",
        "kirin_live_compare_offset_product",
        "kirin_live_compare_aax_group_product",
        "kirin_live_compare_reentry",
        "kirin_live_compare_gain_snapshot",
        "kirin_live_reentry_stop_product",
        "kirin_live_reentry_seek_product",
        "kirin_live_reentry_clock_product",
        "kirin_live_reentry_restore_product",
        "kirin_live_reentry_end_product",
        "kirin_live_reentry_blind-stop_product",
        "kirin_live_reentry_compensation_product",
        "kirin_live_reentry_seek-odd_product",
        "kirin_live_reentry_pending-odd_product",
        "kirin_live_reentry_proof-odd_product",
        "kirin_live_reentry_pair-clear_product",
        "kirin_live_reentry_pair-set_product",
        "kirin_live_reentry_clock-blind_product",
        "kirin_live_reentry_preparing-clock_product",
        "kirin_live_reentry_preparing-stop_product",
        "kirin_live_reentry_reuse_product",
        "kirin_live_reentry_pending-end_product",
        "kirin_live_reentry_restore-blind_product",
        "kirin_live_reentry_failure-named_product",
        "kirin_live_reentry_preparing-dc_product",
        "kirin_live_reentry_preparing-content_product",
        "kirin_live_reentry_preparing-seek_product",
    ] {
        assert!(
            selected.contains(&test),
            "native gate does not execute {test}"
        );
    }
}

#[test]
fn every_native_test_requires_the_read_only_real_storage_fixture() {
    let cmake = include_str!("../../juce_shell/CMakeLists.txt");
    assert!(cmake
        .trim_end()
        .ends_with("include(cmake/NativeStorageGuard.cmake)"));
    let guard = include_str!("../../juce_shell/cmake/NativeStorageGuard.cmake");
    for contract in [
        "get_property(kirin_hypha_native_tests DIRECTORY PROPERTY TESTS)",
        "FIXTURES_SETUP kirin_real_storage",
        "FIXTURES_CLEANUP kirin_real_storage",
        "APPEND PROPERTY FIXTURES_REQUIRED kirin_real_storage",
        "KIRIN_HYPHA_TEST_STORAGE_ROOT=${kirin_path_root}",
        "native_storage_guard.test.mjs",
    ] {
        assert!(
            guard.contains(contract),
            "missing native storage guard: {contract}"
        );
    }
    let fixture = include_str!("../../juce_shell/tests/live_compare_publication_product_test.cpp");
    let isolated = fixture
        .find("Controller::placeUnderForTest (sandbox.directory())")
        .unwrap();
    let checked = fixture
        .find("requirePublicationStorage (sandbox.directory())")
        .unwrap();
    let processors = fixture
        .find("juce::ScopedJuceInitialiser_GUI initialise")
        .unwrap();
    assert!(isolated < checked && checked < processors);
    let modes = include_str!("../../juce_shell/cmake/LivePublicationTests.cmake");
    assert!(modes.contains("foreach(role IN ITEMS PRE POST)"));
    assert!(modes.contains("state-only shared-ring storage-only"));
    let storage = include_str!("../../juce_shell/cmake/StoragePathsTests.cmake");
    let release = include_str!("../../scripts/test_release_source.sh");
    assert!(storage.contains("foreach(scenario environment sandbox defaults relative unwritable)"));
    assert!(release.contains("KirinStoragePathsTests"));
    for scenario in [
        "environment",
        "sandbox",
        "defaults",
        "relative",
        "unwritable",
    ] {
        assert!(release.contains(&format!("kirin_storage_paths_{scenario}")));
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

#[test]
fn content_analysis_gate_covers_every_independent_phase() {
    let gate = include_str!("../../scripts/test_release_source.sh");
    let selected: std::collections::BTreeSet<_> = gate
        .lines()
        .find_map(|line| line.strip_prefix("JUCE_TEST_REGEX='^("))
        .and_then(|line| line.strip_suffix(")$'"))
        .unwrap()
        .split('|')
        .filter(|name| {
            [
                "kirin_content_analysis_",
                "kirin_au_analysis_",
                "kirin_reaper_analysis_",
            ]
            .iter()
            .any(|prefix| name.starts_with(prefix))
                && name.ends_with("_product")
        })
        .map(str::to_owned)
        .collect();
    let mut expected = std::collections::BTreeSet::new();
    for layout in ["mono", "stereo"] {
        for host in ["content", "au", "reaper"] {
            for scenario in ["missing", "omitted", "included", "zero"] {
                if host == "reaper" && scenario != "missing" {
                    continue;
                }
                for phase in ["linear", "loop", "recovery", "cycle_listen", "cycle_blind"] {
                    if phase.starts_with("cycle_") && (host != "au" || scenario == "included") {
                        continue;
                    }
                    expected.insert(format!(
                        "kirin_{host}_analysis_{layout}_{scenario}_{phase}_product"
                    ));
                    if host == "reaper" {
                        expected.insert(format!(
                            "kirin_{host}_analysis_{layout}_{scenario}_{phase}_jitter_product"
                        ));
                    }
                }
            }
        }
    }
    assert_eq!(
        selected, expected,
        "every host/layout/latency phase must run; no old aggregate entries"
    );
    assert!(gate.contains("KirinAnalysisProductWaitTests"));
    assert!(gate.contains("kirin_analysis_product_wait"));
    assert!(gate.contains("KirinAnalysisProductTimingTests"));
    assert!(gate.contains("kirin_analysis_product_timing"));
    let cmake = include_str!("../../juce_shell/cmake/AaxAnalysis.cmake");
    assert!(cmake.contains("include(${CMAKE_CURRENT_LIST_DIR}/ContentAnalysisTests.cmake)"));
    let phases = include_str!("../../juce_shell/cmake/ContentAnalysisTests.cmake");
    assert!(phases.contains(
        "COMMAND KirinContentAnalysisProductTests ${layout} ${scenario} ${host} ${phase} ${phase_timeout}"
    ));
    assert!(phases.contains("${phase_timeout} jitter"));
    assert!(phases.contains("PROPERTIES TIMEOUT ${phase_timeout}"));
}

#[test]
fn reaper_listen_tests_are_built_and_executed_on_both_platforms() {
    let gate = include_str!("../../scripts/test_release_source.sh");
    let ci = include_str!("../../.github/workflows/ci.yml");
    let targets = gate
        .split("JUCE_TEST_TARGETS=(")
        .nth(1)
        .unwrap()
        .split(')')
        .next()
        .unwrap();
    let selection = gate
        .lines()
        .find_map(|line| line.strip_prefix("JUCE_TEST_REGEX='^("))
        .unwrap()
        .trim_end_matches(")$'");
    let windows = ci
        .split("- name: Validate REAPER LISTEN contracts and product flow")
        .nth(1)
        .expect("Windows runs the same REAPER contracts")
        .split("- name:")
        .next()
        .unwrap();
    let windows_targets = windows
        .lines()
        .find(|line| line.contains("cmake --build"))
        .unwrap()
        .split("--target ")
        .nth(1)
        .unwrap();
    let windows_tests = windows
        .lines()
        .find(|line| line.contains("ctest --test-dir"))
        .unwrap()
        .split("-R '^(")
        .nth(1)
        .unwrap()
        .trim_end_matches(")$'");
    for (target, test) in [
        ("Kirin_reaper_clock_Tests", "kirin_reaper_clock"),
        (
            "Kirin_reaper_correspondence_Tests",
            "kirin_reaper_correspondence",
        ),
        ("Kirin_reaper_fault_Tests", "kirin_reaper_fault"),
        (
            "KirinReaperListenProductTests",
            "kirin_reaper_listen_mono_product",
        ),
        (
            "KirinReaperListenProductTests",
            "kirin_reaper_listen_stereo_product",
        ),
        ("KirinChainTraceTests", "kirin_chain_trace"),
        ("KirinListenTextLayoutTests", "kirin_listen_text_layout"),
        (
            "KirinHostContentClockPolicyTests",
            "kirin_host_content_clock_policy",
        ),
    ] {
        assert!(
            targets.split_whitespace().any(|name| name == target),
            "release build misses {target}"
        );
        assert!(
            selection.split('|').any(|name| name == test),
            "release execution misses {test}"
        );
        assert!(
            windows_targets
                .split_whitespace()
                .any(|name| name == target),
            "Windows build misses {target}"
        );
        assert!(
            windows_tests.split('|').any(|name| name == test),
            "Windows execution misses {test}"
        );
    }
    let macos = ci
        .split("  test:\n")
        .nth(1)
        .unwrap()
        .split("  auval-arm64:")
        .next()
        .unwrap();
    assert!(macos.contains("bash scripts/test_release_source.sh"));
    let lightweight = include_str!("../../scripts/test_lightweight_contract.sh");
    for script in [
        "analyze_chain_trace.test.mjs",
        "listen_text_inventory.test.mjs",
    ] {
        assert!(lightweight.contains(script));
    }
}

#[test]
fn release_inventory_checks_all_registered_tests_before_the_selective_build() {
    let gate = include_str!("../../scripts/test_release_source.sh");
    let check = gate
        .find("run node scripts/check_native_test_inventory.mjs")
        .unwrap();
    assert!(check < gate.find("run cmake --build").unwrap());
    for argument in [
        "--group \"$JUCE_TEST_COUNT\" \"$JUCE_TEST_REGEX\"",
        "--group \"$UPDATE_INTEGRATION_COUNT\" \"$UPDATE_INTEGRATION_REGEX\"",
        "--exclusions scripts/release_native_test_exclusions.json",
    ] {
        assert!(gate.contains(argument));
    }
    let lightweight = include_str!("../../scripts/test_lightweight_contract.sh");
    assert!(lightweight.contains("node --test scripts/check_native_test_inventory.test.mjs"));
}
