//! Δ の mode コードと殻の表示規約。
//!
//! B-976 で `kirin_hypha_delta_ffi.h` を分けたときに `juce_lifecycle_wiring.rs` から
//! 切り出した（行数規律 / B-977）。

#[path = "support/juce_lifecycle_sources.rs"]
mod sources;
use sources::read_repo;

#[test]
fn paired_pre_off_is_absolute_while_inactive_and_stale_preserve_delta_layout() {
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
    ] {
        assert!(
            ffi_header.contains(required),
            "ABI contract missing {required}"
        );
    }

    let editor = read_repo("juce_shell/src/PluginEditor.cpp");
    let display_contract = read_repo("juce_shell/src/HyphaDisplayContract.h");
    assert!(display_contract.contains("mode == KIRIN_DELTA_MODE_BYPASSED"));
    assert!(display_contract.contains("mode == KIRIN_DELTA_MODE_PRE_INACTIVE"));
    assert!(display_contract.contains("mode == KIRIN_DELTA_MODE_ACTIVE"));
    assert!(display_contract.contains("pairedPreIsExplicitlyBypassed"));
    assert!(editor.contains("display::preUnavailableForDelta (rawD.mode)"));
    assert!(editor.contains("display::recordPairContext ("));
    assert!(editor.contains("cachedRecordDisplay.pair_matches_current != 0"));
    assert!(editor.contains("display::recordMetricMode (recordPairSelected, haveD, d.mode)"));
    assert!(
        editor.contains("display::watchMetricMode (pairSelected, effectiveHaveD, effectiveMode)")
    );
    assert!(!editor.contains("rawD.mode == 0"));
    assert!(editor.contains("Kind::Abs6"));
    assert!(editor.contains("const bool unavailable = ! haveHeldD;"));
    assert!(editor
        .contains("else // no selected pair, or paired PRE explicitly bypassed -> POST absolute"));
    assert!(display_contract.contains("mode == KIRIN_DELTA_MODE_BYPASSED"));
    assert!(editor.contains("Paired PRE is off. Showing POST absolute values."));
    assert!(editor.contains("COL_SPECTRUM_POST"));
    let producer = read_repo("crates/kirin_measure/src/io_thread_post_tick.rs")
        + &read_repo("crates/kirin_measure/src/io_thread_post_delta.rs");
    assert!(producer.contains("mode: DeltaMode::PreInactive"));
    assert!(producer.contains("Some(SignalState::Inactive) => DeltaMode::PreInactive"));
    assert!(producer.contains("POST absolute until it resumes"));
    assert!(!producer.contains("idle はラッチ維持で Stale"));
}
