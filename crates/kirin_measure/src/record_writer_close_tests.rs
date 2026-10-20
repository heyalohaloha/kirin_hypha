//! A closed Keep never stays FINALIZING. The writer closes one Record generation; when the
//! Measure Thread's drain did not close that generation's display (a worker that finalized
//! another generation, or none that sealed), the close settles it: a sealed drain shows the
//! result, an unsealed one shows that the final measurement is unavailable. A newer Keep's display
//! is never touched by an older close.
use super::super::tests::{full_measure_result, isolated_base, make_ctx_with_sample_rate};
use super::*;
use crate::plugin_data::Role;
use crate::record_mark::new_record_mark_queue;
use crate::License;
use crate::{RecordDisplaySnapshot, RecordDisplayStatus};
use std::sync::atomic::AtomicU64;

/// A Keep the Measure Thread entered: its display is live with measured values.
fn keep(sm: &RecordStateMachine) -> u64 {
    sm.try_enter_record(License::Os).unwrap();
    let generation = sm.generation();
    sm.mark_record_display_measure_started(generation);
    generation
}

fn summary() -> SessionSummary {
    SessionSummary {
        lufs_i: Some(-18.4),
        max_true_peak: Some(-15.9),
        ..SessionSummary::default()
    }
}

/// The IO Thread closes the writer of `generation` after its Stop.
fn close(sm: &Arc<RecordStateMachine>, generation: u64, seal_at_start: u64) {
    let base = isolated_base();
    let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), 48_000);
    ctx.writer
        .set_record_session_id(Some(format!("keep-close-display-{generation}")));
    ctx.record_generation = generation;
    ctx.seal_at_start = seal_at_start;
    let measure = Arc::new(Mutex::new(full_measure_result()));
    let session_summary = Arc::new(Mutex::new(Some(summary())));
    let no_drop = Arc::new(AtomicU64::new(0));
    close_recording_context(
        sm,
        &measure,
        Some(&session_summary),
        &no_drop,
        &no_drop,
        Some(&new_record_trace_queue()),
        None,
        Some(&new_record_mark_queue()),
        ctx,
    )
    .unwrap();
}

fn display(sm: &RecordStateMachine) -> RecordDisplaySnapshot {
    sm.try_record_display_snapshot().expect("display snapshot")
}

#[test]
fn a_sealed_drain_that_finalized_another_generation_still_shows_the_keep_result() {
    let sm = Arc::new(RecordStateMachine::new());
    let generation = keep(&sm);
    let seal_at_start = sm.seal();
    sm.exit_record();
    // The drain completes and seals, but its worker finalizes a generation it remembered from
    // elsewhere: this Keep's own display is left FINALIZING by the Measure Thread.
    sm.finalize_record_display(generation + 1, Some(summary()));
    sm.bump_seal();
    close(&sm, generation, seal_at_start);

    let shown = display(&sm);
    assert_eq!(
        (shown.generation, shown.status),
        (generation, RecordDisplayStatus::Finalized),
        "a closed Keep shows its result, never RECORD FINALIZING"
    );
    assert_eq!(shown.summary.and_then(|s| s.lufs_i), Some(-18.4));
}

#[test]
fn a_close_without_a_seal_shows_the_final_measurement_unavailable() {
    let sm = Arc::new(RecordStateMachine::new());
    let generation = keep(&sm);
    let seal_at_start = sm.seal();
    sm.exit_record();
    // No worker drains this Keep: the writer stops waiting once the drain makes no progress.
    close(&sm, generation, seal_at_start);

    let shown = display(&sm);
    assert_eq!(
        (shown.generation, shown.status),
        (generation, RecordDisplayStatus::Unavailable),
        "an unsealed close is never presented as a final result, nor left FINALIZING"
    );
    assert!(shown.summary.is_none());
}

#[test]
fn an_older_close_never_touches_a_newer_keep() {
    let sm = Arc::new(RecordStateMachine::new());
    let older = keep(&sm);
    let seal_at_start = sm.seal();
    sm.exit_record();
    sm.bump_seal();
    let newer = keep(&sm);
    close(&sm, older, seal_at_start);

    let shown = display(&sm);
    assert_eq!(
        (shown.generation, shown.status),
        (newer, RecordDisplayStatus::Live)
    );
    assert!(shown.summary.is_none());
}

#[test]
fn a_close_after_the_drain_finalized_its_keep_keeps_that_result() {
    let sm = Arc::new(RecordStateMachine::new());
    let generation = keep(&sm);
    let seal_at_start = sm.seal();
    sm.exit_record();
    let drained = SessionSummary {
        lufs_i: Some(-20.0),
        ..SessionSummary::default()
    };
    sm.finalize_record_display(generation, Some(drained));
    sm.bump_seal();
    close(&sm, generation, seal_at_start);

    let shown = display(&sm);
    assert_eq!(shown.status, RecordDisplayStatus::Finalized);
    assert_eq!(
        shown.summary.and_then(|s| s.lufs_i),
        Some(-20.0),
        "the drain's own result stays"
    );
}
