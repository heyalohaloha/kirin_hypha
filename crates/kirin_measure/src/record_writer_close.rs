//! The IO Thread closes one Record writer: it waits for the Measure Thread's post-drain seal,
//! then bakes the final summary, integrity and take into the record.
use super::*;

#[allow(clippy::too_many_arguments)]
pub(super) fn close_recording_context(
    record_sm: &Arc<RecordStateMachine>,
    measure_result: &Arc<Mutex<MeasureResult>>,
    session_summary: Option<&Arc<Mutex<Option<SessionSummary>>>>,
    overflow: &Arc<std::sync::atomic::AtomicU64>,
    oversized_drop: &Arc<std::sync::atomic::AtomicU64>,
    record_trace_queue: Option<&RecordTraceQueue>,
    record_take_tracker: Option<&RecordTakeTracker>,
    record_mark_queue: Option<&RecordMarkQueue>,
    mut ctx: RecordingCtx,
) -> Result<(), String> {
    // ── B-132 (G-115-382) P1/共通B: drain-completion barrier ──────────────
    // Measure Thread が Record→Watch エッジで ring 残量を tight-drain → 最終 finalize →
    // session_summary 書込を完了し seal を前進させるのを **bounded** に待ってから取り出す。
    // これで「post-drain の確定スナップショット」のみ焼く（live meter は元々別スロット）。
    let sealed = wait_for_seal(
        record_sm,
        ctx.seal_at_start,
        record_trace_queue,
        SEAL_WAIT_NO_PROGRESS_TIMEOUT,
        SEAL_WAIT_ABSOLUTE_TIMEOUT,
    );
    // B-043: Record→Watch 遷移時に Measure Thread の最新セッション集計を取り出して注入。
    let summary = session_summary.and_then(take_session_summary);
    let generation = ctx.record_generation;
    // B-076: この Record 中に ring 満杯で落ちたサンプル数 = 現在の累積 - 開始時 snapshot。
    let push_dropped = overflow
        .load(std::sync::atomic::Ordering::Relaxed)
        .saturating_sub(ctx.overflow_start);
    // B-125: oversized block で落ちたサンプル数（別カウンタの per-Record 差分）。
    let oversized_dropped = oversized_drop
        .load(std::sync::atomic::Ordering::Relaxed)
        .saturating_sub(ctx.oversized_drop_start);
    // 別計数のまま set_integrity に渡し、JSON へは合算を焼く（ZSA / B-125 裁定）。
    ctx.writer.set_integrity(push_dropped, oversized_dropped);
    // B-132 共通B: seal が timeout（measure 死 / shutdown / stall で finalize 不能）なら
    // 「不完全を不完全と記録」する（set_integrity の後に OR で立てる / silent truncation 排除）。
    if !sealed {
        log::warn!(
            "[writer] drain seal not observed (no progress for {:?}, or legacy absolute {:?} \
             exceeded) — marking integrity_degraded (incomplete tail)",
            SEAL_WAIT_NO_PROGRESS_TIMEOUT,
            SEAL_WAIT_ABSOLUTE_TIMEOUT
        );
        ctx.writer.mark_integrity_degraded();
        ctx.writer.add_integrity_reason("drain_seal_timeout");
    }
    if let Some(queue) = record_trace_queue {
        let _ = drain_trace_queue_into_writer(&mut ctx, queue);
    } else {
        append_current_measure_snapshot(&mut ctx, measure_result)?;
    }
    // BWF metadata must be attached before epoch selection. The subsequent close path may refresh
    // it again for persistence, but filtering cannot defer this read until after the tracker has
    // already been consumed.
    refresh_pair_record_metadata_if_missing(&mut ctx);
    apply_record_take_snapshot(&mut ctx, record_take_tracker);
    if let Some(queue) = record_mark_queue {
        writer_close_with_summary_and_marks(ctx, summary, queue);
    } else {
        writer_close_with_summary(ctx, summary);
    }
    // The record of this generation is closed, so its display never stays FINALIZING past the
    // close. A drain that sealed without closing this generation's display (its worker finalized
    // another generation) shows the result; a close without the seal shows the final measurement
    // unavailable, as the record names its incomplete tail. A display the drain already closed,
    // or a newer Keep's, is left as it is.
    if sealed {
        record_sm.finalize_record_display(generation, summary);
    } else {
        record_sm.mark_record_display_unavailable(generation);
    }
    Ok(())
}

#[cfg(test)]
#[path = "record_writer_close_tests.rs"]
mod tests;
