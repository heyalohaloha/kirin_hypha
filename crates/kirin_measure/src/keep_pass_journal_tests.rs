use super::*;

const BLOCK: u64 = 512;

fn chunk(generation: u64, raw: i64, aux: Option<i64>, in_trace: bool) -> KeepChunk {
    KeepChunk {
        generation,
        raw_host_start: Some(raw),
        auxiliary: AuxiliaryClockSamples {
            source: AuxiliaryClockSource::Vst3Continuous,
            samples: aux,
        },
        frames: BLOCK,
        in_trace,
        in_summary: true,
        after_offline_take: false,
    }
}

fn reasons(passes: &[KeepPass]) -> Vec<Option<&str>> {
    passes
        .iter()
        .map(|pass| pass.trace_excluded_reason.as_deref())
        .collect()
}

#[test]
fn a_loop_wrap_is_a_later_pass_and_every_frame_is_accounted() {
    let tracker = RecordTakeTracker::new();
    // The TRACE pass ends at the loop end; the wrap returns while the host clock runs on.
    let (mut raw, mut aux) = (94_976_i64, 1_000_000_i64);
    for _ in 0..2 {
        tracker.note_keep_chunk(chunk(3, raw, Some(aux), true));
        raw += BLOCK as i64;
        aux += BLOCK as i64;
    }
    assert!(!tracker.keep_trace_first_pass_only(3));
    tracker.note_keep_chunk(chunk(3, 0, Some(aux), false));
    tracker.note_keep_chunk(chunk(3, 512, Some(aux + 512), false));
    assert!(tracker.keep_trace_first_pass_only(3));
    let passes = tracker.keep_passes(3);
    assert_eq!(reasons(&passes), [None, Some(KEEP_PASS_LOOP_WRAP)]);
    assert_eq!(
        (
            passes[0].host_start_position_samples,
            passes[0].host_end_position_samples
        ),
        (Some(94_976), Some(96_000))
    );
    assert_eq!(
        (
            passes[1].host_start_position_samples,
            passes[1].host_end_position_samples
        ),
        (Some(0), Some(1_024))
    );
    let frames: u64 = passes.iter().map(|pass| pass.frames).sum();
    assert_eq!(frames, 4 * BLOCK, "the passes explain every accepted frame");
    assert!(passes[0].in_trace && !passes[1].in_trace);
    assert_eq!(passes[1].summary_frames, 2 * BLOCK);
}

#[test]
fn a_restart_or_a_skip_is_a_discontinuity_distinct_from_a_loop_wrap() {
    let tracker = RecordTakeTracker::new();
    tracker.note_keep_chunk(chunk(5, 10_000, Some(50_000), true));
    // Stop and play again: the host clock moved on without audio in between.
    tracker.note_keep_chunk(chunk(5, 10_512, Some(90_000), false));
    // A skip ahead while the host clock runs on.
    tracker.note_keep_chunk(chunk(5, 40_000, Some(90_512), false));
    // A host without a continuous clock proves no continuity.
    let mut unproven = chunk(5, 0, None, false);
    unproven.auxiliary.source = AuxiliaryClockSource::Unknown;
    tracker.note_keep_chunk(unproven);
    assert_eq!(
        reasons(&tracker.keep_passes(5)),
        [
            None,
            Some(KEEP_PASS_TRANSPORT_RESTART),
            Some(KEEP_PASS_POSITION_JUMP),
            Some(KEEP_PASS_TRANSPORT_RESTART),
        ]
    );
}

#[test]
fn a_pass_after_an_offline_take_is_named_and_reaches_no_summary() {
    let tracker = RecordTakeTracker::new();
    tracker.note_keep_chunk(chunk(2, 1_200, Some(7_000), true));
    let mut jump = chunk(2, 200, Some(7_512), false);
    jump.after_offline_take = true;
    jump.in_summary = false;
    tracker.note_keep_chunk(jump);
    let passes = tracker.keep_passes(2);
    assert_eq!(reasons(&passes), [None, Some(KEEP_PASS_AFTER_OFFLINE_TAKE)]);
    assert_eq!(passes[1].summary_frames, 0);
}

#[test]
fn contiguous_chunks_stay_one_pass_and_a_trace_only_keep_lists_nothing() {
    let tracker = RecordTakeTracker::new();
    let mut quiet = chunk(9, 512, Some(10_512), true);
    quiet.in_summary = false;
    tracker.note_keep_chunk(chunk(9, 0, Some(10_000), true));
    tracker.note_keep_chunk(quiet);
    tracker.note_keep_chunk(chunk(9, 1_024, Some(11_024), true));
    assert!(
        tracker.keep_passes(9).is_empty(),
        "one continuous TRACE pass adds no list"
    );
    assert!(!tracker.keep_trace_first_pass_only(9));
    // A later pass shows the single TRACE pass with its own summary account.
    tracker.note_keep_chunk(chunk(9, 0, Some(11_536), false));
    let passes = tracker.keep_passes(9);
    assert_eq!(passes.len(), 2);
    assert_eq!(
        (passes[0].frames, passes[0].summary_frames),
        (3 * BLOCK, 2 * BLOCK)
    );
    // The next Keep generation starts a fresh journal; an old generation reads nothing.
    tracker.note_keep_chunk(chunk(10, 0, Some(90_000), true));
    assert!(tracker.keep_passes(9).is_empty());
    assert!(!tracker.keep_trace_first_pass_only(10));
}

#[test]
fn the_record_field_is_additive_and_names_only_facts() {
    let pass = KeepPass {
        host_start_position_samples: Some(0),
        host_end_position_samples: None,
        frames: 512,
        summary_frames: 512,
        in_trace: false,
        trace_excluded_reason: Some(KEEP_PASS_LOOP_WRAP.into()),
    };
    let json = serde_json::to_value(&pass).unwrap();
    assert_eq!(
        json,
        serde_json::json!({
            "host_start_position_samples": 0,
            "frames": 512,
            "summary_frames": 512,
            "in_trace": false,
            "trace_excluded_reason": "loop_wrap",
        })
    );
    let back: KeepPass = serde_json::from_value(json).unwrap();
    assert_eq!(back, pass);
}

#[test]
fn a_host_without_positions_keeps_one_pass_and_the_list_stays_bounded() {
    let tracker = RecordTakeTracker::new();
    let mut unpositioned = chunk(4, 0, Some(1_000), true);
    unpositioned.raw_host_start = None;
    for block in 0..100 {
        unpositioned.auxiliary.samples = Some(1_000 + block * BLOCK as i64);
        tracker.note_keep_chunk(unpositioned);
    }
    // One later pass makes the list visible: the TRACE pass is still one entry.
    tracker.note_keep_chunk(chunk(4, 0, Some(1), false));
    let passes = tracker.keep_passes(4);
    assert_eq!(passes.len(), 2);
    assert_eq!(passes[0].frames, 100 * BLOCK);
    // Thousands of short passes end in one aggregate entry that still holds every frame.
    let wraps = KEEP_PASS_LIMIT as i64 + 50;
    for pass in 0..wraps {
        tracker.note_keep_chunk(chunk(4, 0, Some(10_000 + pass * BLOCK as i64), false));
    }
    let passes = tracker.keep_passes(4);
    assert_eq!(passes.len(), KEEP_PASS_LIMIT);
    let last = passes.last().unwrap();
    assert_eq!(
        last.trace_excluded_reason.as_deref(),
        Some(KEEP_PASS_MORE_PASSES)
    );
    assert_eq!(last.host_start_position_samples, None);
    assert_eq!(last.host_end_position_samples, None);
    let frames: u64 = passes.iter().map(|pass| pass.frames).sum();
    assert_eq!(frames, (100 + 1 + wraps as u64) * BLOCK);
}

#[test]
fn an_auxiliary_cut_at_every_callback_keeps_one_trace_pass() {
    // Some hosts cut their auxiliary clock at every callback while the position runs on. The take
    // continues through those cuts, so its TRACE stays one pass.
    let tracker = RecordTakeTracker::new();
    for block in 0..100_i64 {
        let raw = block * BLOCK as i64;
        tracker.note_keep_chunk(chunk(6, raw, Some(raw + (block % 2) * BLOCK as i64), true));
    }
    tracker.note_keep_chunk(chunk(6, 0, Some(1), false));
    let passes = tracker.keep_passes(6);
    assert_eq!(passes.len(), 2, "{passes:?}");
    assert_eq!(passes[0].frames, 100 * BLOCK);
    assert_eq!(
        passes[0].host_end_position_samples,
        Some(100 * BLOCK as i64)
    );
}
