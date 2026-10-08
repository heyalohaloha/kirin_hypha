//! 比較状態の ABI と出荷 Observatory への配線。
//!
//! B-976 で `kirin_hypha_delta_ffi.h` を分けたときに `juce_lifecycle_wiring.rs` から
//! 切り出した（行数規律 / B-977）。

#[path = "support/juce_lifecycle_sources.rs"]
mod sources;
use sources::read_repo;

#[test]
fn comparison_state_drives_the_shipping_observatory_without_a_hidden_delta_path() {
    // B-976: Δ の mode コードは `kirin_hypha_delta_ffi.h` へ分けた（行数規律）。
    // 主 header が include するので、殻から見えるコードの集合は変わらない。
    let ffi_header = read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_ffi.h")
        + &read_repo("crates/kirin_hypha_ffi/include/kirin_hypha_delta_ffi.h");
    assert!(
        ffi_header.contains("#include \"kirin_hypha_delta_ffi.h\""),
        "主 header が delta header を include していない"
    );
    for required in [
        "KIRIN_DELTA_MODE_ACTIVE 0u",
        "KIRIN_DELTA_MODE_BYPASSED 3u",
        "KIRIN_DELTA_MODE_PRE_INACTIVE 4u",
        // B-976: 比較が成立しない 2 状態。
        "KIRIN_DELTA_MODE_LAYOUT_MISMATCH 5u",
        "KIRIN_DELTA_MODE_LAYOUT_UNKNOWN 6u",
        "KIRIN_PAIR_STATUS_PAIRED 2u",
        "KIRIN_SIGNAL_STATE_ACTIVE 1u",
        "KIRIN_COMPARISON_STATE_REJECTED 0u",
        "KIRIN_COMPARISON_STATE_ACTIVE 2u",
        "KIRIN_COMPARISON_STATE_HOLDING 3u",
        "KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH 6u",
        "KIRIN_COMPARISON_REASON_LAYOUT_UNKNOWN 7u",
    ] {
        assert!(
            ffi_header.contains(required),
            "ABI contract missing {required}"
        );
    }

    let editor = read_repo("juce_shell/src/PluginEditor.cpp")
        + &read_repo("juce_shell/src/PluginEditorMeter.cpp")
        + &read_repo("juce_shell/src/PluginEditorObservatory.cpp");
    let target_action = editor
        .split("observatoryView.onTargetChange =")
        .nth(1)
        .expect("shipping target action")
        .split("observatoryView.onTimeRangeChange =")
        .next()
        .unwrap();
    assert!(target_action.contains("observatoryView.setTarget (target)"));
    assert!(target_action.contains("nextTimeSnapshotMs = 0.0"));
    assert!(
        !target_action.contains("retireTimePresentation"),
        "main-target action must preserve independently observed PSR"
    );
    let meter = read_repo("juce_shell/src/PluginEditorMeter.cpp");
    assert!(meter.contains("void KirinHyphaEditor::refreshWatchSnapshot()"));
    assert!(meter.contains("processorRef.pollWatchDisplay (watch)"));
    assert!(meter.contains("uint8_t KirinHyphaEditor::refreshRecordPhase()"));
    assert!(meter.contains("processorRef.pollRecordDisplay (observed)"));
    assert!(meter.contains("observatoryView.setKeepActive (keepActive)"));
    assert!(meter.contains("processorRef.drainKeepActionNotice()"));
    for retired in [
        "pollDelta",
        "DisplaySmoother",
        "MetricCell",
        "fillDelta",
        "fillAbs",
        "configureForKind",
        "PostControls",
    ] {
        assert!(
            !editor.contains(retired),
            "retired hidden path remains: {retired}"
        );
    }
    let producer = read_repo("crates/kirin_measure/src/io_thread_post_tick.rs")
        + &read_repo("crates/kirin_measure/src/io_thread_post_delta.rs");
    assert!(producer.contains("mode: DeltaMode::PreInactive"));
    assert!(producer.contains("Some(SignalState::Inactive) => DeltaMode::PreInactive"));
    assert!(producer.contains("POST absolute until it resumes"));
    assert!(!producer.contains("idle はラッチ維持で Stale"));

    let observatory = read_repo("juce_shell/src/PluginEditorObservatory.cpp");
    let presentation = read_repo("juce_shell/src/HyphaComparisonPresentation.h");
    assert!(observatory.contains("observatoryView.setObservatoryFrame (frame, frameAvailable)"));
    assert!(observatory.contains("frame.comparison_state"));
    assert!(observatory.contains("frame.comparison_reason"));
    assert!(observatory.contains("frame.comparison_generation > comparisonActionAfterGeneration"));
    assert!(observatory.contains("notifiesExplicitAction ("));
    assert!(presentation.contains("KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH"));
    assert!(presentation.contains("MATCH PRE / POST BUS"));

    let time = read_repo("juce_shell/src/HyphaObservatoryViewFooter.cpp");
    let spectrum = read_repo("juce_shell/src/HyphaSpectrumComponent.cpp")
        + &read_repo("juce_shell/src/HyphaSpectrumChromePainter.cpp")
        + &read_repo("juce_shell/src/PluginEditorObservatory.cpp");
    assert!(time.contains("time_snapshot::paint"));
    let time_packet = read_repo("juce_shell/src/HyphaTimeSnapshotPainter.cpp")
        + &read_repo("juce_shell/src/HyphaTimeSnapshotPresentation.cpp");
    assert!(time_packet.contains("currentReason (psr)"));
    assert!(time_packet.contains("component.facts.reason"));
    assert!(time_packet.contains("KIRIN_TIME_CURRENT_WAITING"));
    let time_poll = read_repo("juce_shell/src/PluginEditorTimeSnapshots.cpp");
    assert!(
        time_poll.contains("presentationContext().density != hypha::observatory::Density::compact")
    );
    assert!(
        !time_poll.contains("experienceFamily()"),
        "125% shares the compact family but must acquire PSR"
    );
    assert!(spectrum.contains("spectrumView.setComparisonStatus"));
    assert!(spectrum.contains("state.comparisonStatus"));
}
