//! Producer-owned PRE/POST WAV-start clock.
//!
//! Metric values are deliberately absent from the clock decision. A Broadcast-Wave time
//! reference is the strongest start anchor. When the WAV has no `bext`, the producer uses the
//! exact contiguous DAW/render range observed for this Record generation. Metric shapes and the
//! relative position of a frame inside the broader Keep history never participate in the decision.

use std::collections::BTreeMap;

use crate::plugin_data::{Frame, PluginDataFile};
use crate::record_expected::ExpectedWavMetadata;

const FRAME_INTERVAL_MS: u64 = 100;
pub(crate) const TRACE_COMPARISON_RAW_HOST_EXACT: &str = "raw_host_clock_exact";

pub(crate) struct WavStartClockPlan {
    pub pre_frames: Vec<Frame>,
    pub post_frames: Vec<Frame>,
    /// Producer/DAW positions used only to select the exact source frames.
    pub pre_producer_slots: Vec<i64>,
    pub post_producer_slots: Vec<i64>,
    /// Public TRACE positions on the dropped WAV axis. A role-specific presentation-latency phase
    /// may move the first complete 100 ms window inside `[slot_samples, 2 * slot_samples)`.
    pub pre_wav_slots: Vec<i64>,
    pub post_wav_slots: Vec<i64>,
    /// Untouched raw-host endpoints retained only as an independent PRE/POST comparison proof.
    /// These are not positions on the dropped-WAV axis.
    pub pre_comparison_slots: Vec<i64>,
    pub post_comparison_slots: Vec<i64>,
    pub comparison_resolution: Option<&'static str>,
    pub pre_origin_position_samples: Option<i64>,
    pub post_origin_position_samples: Option<i64>,
    pub start_basis: &'static str,
    pub pre_clock_model: &'static str,
    pub post_clock_model: &'static str,
    pub exact: bool,
}

pub(crate) fn build_wav_start_clock_plan(
    pre: &PluginDataFile,
    post: &PluginDataFile,
    expected: &ExpectedWavMetadata,
    expected_len: usize,
    slot_samples: i64,
) -> Option<WavStartClockPlan> {
    if expected_len == 0 || slot_samples <= 0 {
        return None;
    }
    let shared_session = pre
        .record_session_id
        .as_deref()
        .zip(post.record_session_id.as_deref())
        .is_some_and(|(pre_session, post_session)| {
            !pre_session.trim().is_empty() && pre_session == post_session
        });
    if !shared_session {
        return None;
    }
    if pre.sample_rate == 0
        || pre.sample_rate != post.sample_rate
        || pre.sample_rate != expected.expected_sample_rate
    {
        return None;
    }
    // Resolve each plug-in node independently. DAWs may report PRE and POST project positions with
    // different PDC semantics, and a latency change creates multiple immutable capture epochs.
    // The observation journal retains all of those factual segments for this late decision.
    let pre_observed = crate::trace_clock_resolution::resolve_exact_side(
        pre,
        expected,
        expected_len,
        slot_samples,
    );
    let post_observed = crate::trace_clock_resolution::resolve_exact_side(
        post,
        expected,
        expected_len,
        slot_samples,
    );
    if let (Some(pre_side), Some(post_side)) = (pre_observed, post_observed) {
        let start_basis = if expected.wav_time_reference_samples.is_some() {
            crate::trace_alignment::TRACE_ALIGNMENT_START_BWF
        } else {
            crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE
        };
        return Some(WavStartClockPlan {
            pre_frames: pre_side.frames,
            post_frames: post_side.frames,
            pre_producer_slots: pre_side.producer_slots,
            post_producer_slots: post_side.producer_slots,
            pre_wav_slots: pre_side.wav_slots,
            post_wav_slots: post_side.wav_slots,
            pre_comparison_slots: Vec::new(),
            post_comparison_slots: Vec::new(),
            comparison_resolution: None,
            pre_origin_position_samples: Some(pre_side.origin_position_samples),
            post_origin_position_samples: Some(post_side.origin_position_samples),
            start_basis,
            pre_clock_model: pre_side.model,
            post_clock_model: post_side.model,
            exact: true,
        });
    }

    // Legacy/current direct slots remain an exact compatibility path when no observation journal
    // exists. They are never repaired from the old Watch-context fields.
    let pre_source = direct_trace_source(pre);
    let post_source = direct_trace_source(post);
    let render_range = shared_producer_render_range(pre, post, expected);
    let direct_clock_is_eligible =
        expected.wav_time_reference_samples.is_some() || has_compatible_host_clocks(pre, post);
    if direct_clock_is_eligible {
        if let (Some(pre_source), Some(post_source)) = (pre_source, post_source) {
            if let Some((selected_positions, start_basis)) = select_wav_window(
                &pre_source,
                &post_source,
                expected,
                expected_len,
                slot_samples,
                render_range,
            ) {
                let producer_origin = selected_positions
                    .first()
                    .and_then(|first| first.checked_sub(slot_samples));
                return Some(WavStartClockPlan {
                    pre_frames: selected_frames(&pre_source, &selected_positions)?,
                    post_frames: selected_frames(&post_source, &selected_positions)?,
                    pre_producer_slots: selected_positions.clone(),
                    post_producer_slots: selected_positions,
                    pre_wav_slots: wav_slots(expected_len, slot_samples)?,
                    post_wav_slots: wav_slots(expected_len, slot_samples)?,
                    pre_comparison_slots: Vec::new(),
                    post_comparison_slots: Vec::new(),
                    comparison_resolution: None,
                    pre_origin_position_samples: producer_origin,
                    post_origin_position_samples: producer_origin,
                    start_basis,
                    pre_clock_model: "direct_producer_slots",
                    post_clock_model: "direct_producer_slots",
                    exact: true,
                });
            }
            if let (Some(pre_side), Some(post_side)) = (
                select_sparse_wav_side(
                    &pre_source,
                    expected,
                    expected_len,
                    slot_samples,
                    render_range,
                ),
                select_sparse_wav_side(
                    &post_source,
                    expected,
                    expected_len,
                    slot_samples,
                    render_range,
                ),
            ) {
                return Some(WavStartClockPlan {
                    pre_frames: pre_side.frames,
                    post_frames: post_side.frames,
                    pre_producer_slots: pre_side.producer_slots,
                    post_producer_slots: post_side.producer_slots,
                    pre_wav_slots: pre_side.wav_slots,
                    post_wav_slots: post_side.wav_slots,
                    pre_comparison_slots: Vec::new(),
                    post_comparison_slots: Vec::new(),
                    comparison_resolution: None,
                    pre_origin_position_samples: Some(pre_side.origin),
                    post_origin_position_samples: Some(post_side.origin),
                    start_basis: pre_side.start_basis,
                    pre_clock_model: "direct_producer_slots",
                    post_clock_model: "direct_producer_slots",
                    exact: true,
                });
            }
        }
    }

    // Absolute placement on a non-BWF file may remain unresolved even when both plug-ins observed
    // the exact same raw host clock. Preserve that producer fact separately: it qualifies only
    // PRE/POST subtraction on an elapsed axis and never promotes the dropped-WAV axis to exact.
    if let Some((pre_side, post_side)) =
        shared_raw_host_comparison(pre, post, expected_len, slot_samples)
    {
        return Some(WavStartClockPlan {
            pre_frames: pre_side.frames,
            post_frames: post_side.frames,
            pre_producer_slots: pre_side.raw_slots.clone(),
            post_producer_slots: post_side.raw_slots.clone(),
            pre_wav_slots: pre_side.elapsed_slots,
            post_wav_slots: post_side.elapsed_slots,
            pre_comparison_slots: pre_side.raw_slots,
            post_comparison_slots: post_side.raw_slots,
            comparison_resolution: Some(TRACE_COMPARISON_RAW_HOST_EXACT),
            pre_origin_position_samples: None,
            post_origin_position_samples: None,
            start_basis: crate::trace_alignment::TRACE_ALIGNMENT_START_ORDER_FALLBACK,
            pre_clock_model: "chronological_fallback",
            post_clock_model: "chronological_fallback",
            exact: false,
        });
    }

    // Timing quality must never erase measured tracks. This last path preserves only real measured
    // frames, aligns them by their Record order, and labels the result non-canonical so consumers
    // can display it while keeping exactness as separate quality metadata.
    let mut pre_frames =
        crate::trace_clock_resolution::chronological_fallback_frames(pre, expected_len);
    let mut post_frames =
        crate::trace_clock_resolution::chronological_fallback_frames(post, expected_len);
    pre_frames.truncate(expected_len);
    post_frames.truncate(expected_len);
    let pre_fallback_slots = wav_slots_for_frames(&pre_frames, expected_len, slot_samples)?;
    let post_fallback_slots = wav_slots_for_frames(&post_frames, expected_len, slot_samples)?;
    Some(WavStartClockPlan {
        pre_frames,
        post_frames,
        pre_producer_slots: pre_fallback_slots.clone(),
        post_producer_slots: post_fallback_slots.clone(),
        pre_wav_slots: pre_fallback_slots,
        post_wav_slots: post_fallback_slots,
        pre_comparison_slots: Vec::new(),
        post_comparison_slots: Vec::new(),
        comparison_resolution: None,
        pre_origin_position_samples: None,
        post_origin_position_samples: None,
        start_basis: crate::trace_alignment::TRACE_ALIGNMENT_START_ORDER_FALLBACK,
        pre_clock_model: "chronological_fallback",
        post_clock_model: "chronological_fallback",
        exact: false,
    })
}

struct RawHostComparisonSide {
    frames: Vec<Frame>,
    raw_slots: Vec<i64>,
    elapsed_slots: Vec<i64>,
}

/// Select factual measured frames on one untouched host clock. A capture epoch is never mixed
/// with another epoch: a DAW position jump therefore disables this proof instead of being hidden.
fn raw_host_comparison_side(
    data: &PluginDataFile,
    shared_origin: i64,
    expected_len: usize,
    slot_samples: i64,
) -> Option<RawHostComparisonSide> {
    let raw_range = data.raw_host_clock_range?;
    if raw_range.start_position_samples != shared_origin
        || raw_range.end_position_samples <= shared_origin
    {
        return None;
    }
    // The producer-private slots are bound one-to-one with `data.frames` at Record close. A late
    // Drop can shorten that take, so select frame/slot pairs together before validating the WAV
    // window. Comparing the untrimmed slot count with the already-trimmed frame count would turn a
    // normal DAW lead/tail into a false comparison failure.
    if let Some(side) = baked_raw_host_comparison_side(data, raw_range, expected_len, slot_samples)
    {
        return Some(side);
    }
    let factual_frames =
        crate::trace_clock_resolution::chronological_fallback_frames(data, expected_len);
    if factual_frames.len() < 2 {
        return None;
    }
    let elapsed_slots = wav_slots_for_frames(&factual_frames, expected_len, slot_samples)?;
    let factual_times = factual_frames
        .iter()
        .map(|frame| frame.t_ms)
        .collect::<Vec<_>>();
    let factual_by_time = factual_frames
        .iter()
        .map(|frame| (frame.t_ms, frame))
        .collect::<std::collections::BTreeMap<_, _>>();
    let mut observations_by_epoch = std::collections::BTreeMap::<u64, Vec<(u64, i64)>>::new();
    for observation in &data.trace_clock_observations {
        let (Some(position), Some(capture_epoch)) = (
            observation.raw_host_position_samples,
            observation.capture_epoch,
        ) else {
            continue;
        };
        let Some(factual_frame) = factual_by_time.get(&observation.frame.t_ms) else {
            continue;
        };
        if position < raw_range.start_position_samples
            || position > raw_range.end_position_samples
            || !same_frame_measurement(factual_frame, &observation.frame)
            || !matches!(
                observation.clock_source.as_deref(),
                Some("project_timeline" | "audio_render_timeline")
            )
        {
            continue;
        }
        observations_by_epoch
            .entry(capture_epoch)
            .or_default()
            .push((observation.frame.t_ms, position));
    }

    // The observation journal intentionally spans DAW position jumps. Select only a complete
    // epoch whose raw-clock timestamps match every producer-selected public frame. A partial
    // pre/post-bounce epoch cannot invalidate that proof; two different complete mappings remain
    // ambiguous and therefore produce no comparison qualification.
    let mut candidate = None::<RawHostComparisonSide>;
    for mut selected in observations_by_epoch.into_values() {
        selected.sort_by_key(|(t_ms, _)| *t_ms);
        if selected.len() != factual_frames.len()
            || !selected
                .windows(2)
                .all(|pair| pair[0].0 < pair[1].0 && pair[0].1 < pair[1].1)
            || selected
                .iter()
                .map(|(t_ms, _)| *t_ms)
                .ne(factual_times.iter().copied())
        {
            continue;
        }
        let side = RawHostComparisonSide {
            frames: factual_frames.clone(),
            raw_slots: selected.iter().map(|(_, raw)| *raw).collect(),
            elapsed_slots: elapsed_slots.clone(),
        };
        if let Some(existing) = &candidate {
            if existing.raw_slots != side.raw_slots || existing.elapsed_slots != side.elapsed_slots
            {
                return None;
            }
        } else {
            candidate = Some(side);
        }
    }
    candidate
}

fn baked_raw_host_comparison_side(
    data: &PluginDataFile,
    raw_range: crate::plugin_data::HostClockRange,
    expected_len: usize,
    slot_samples: i64,
) -> Option<RawHostComparisonSide> {
    if data.trace_raw_host_slot_positions.len() != data.frames.len() {
        return None;
    }
    if !data
        .trace_raw_host_slot_positions
        .windows(2)
        .all(|pair| pair[0] < pair[1])
        || !data.trace_raw_host_slot_positions.iter().all(|position| {
            *position >= raw_range.start_position_samples
                && *position <= raw_range.end_position_samples
        })
    {
        return None;
    }
    let max_ms = u64::try_from(expected_len)
        .ok()?
        .checked_mul(FRAME_INTERVAL_MS)?;
    let mut by_time = std::collections::BTreeMap::<u64, (Frame, i64)>::new();
    for (frame, raw_position) in data
        .frames
        .iter()
        .cloned()
        .zip(data.trace_raw_host_slot_positions.iter().copied())
    {
        if frame.t_ms == 0 || frame.t_ms > max_ms || frame.t_ms % FRAME_INTERVAL_MS != 0 {
            continue;
        }
        if by_time.insert(frame.t_ms, (frame, raw_position)).is_some() {
            return None;
        }
    }
    if by_time.len() < 2 {
        return None;
    }
    let frames = by_time
        .values()
        .map(|(frame, _)| frame.clone())
        .collect::<Vec<_>>();
    let raw_slots = by_time
        .values()
        .map(|(_, raw_position)| *raw_position)
        .collect::<Vec<_>>();
    if !raw_slots.windows(2).all(|pair| pair[0] < pair[1]) {
        return None;
    }
    Some(RawHostComparisonSide {
        elapsed_slots: wav_slots_for_frames(&frames, expected_len, slot_samples)?,
        frames,
        raw_slots,
    })
}

fn same_frame_measurement(left: &Frame, right: &Frame) -> bool {
    left.t_ms == right.t_ms
        && left.n_prime == right.n_prime
        && left.sharpness == right.sharpness
        && left.lufs_m == right.lufs_m
        && left.true_peak == right.true_peak
        && left.crest == right.crest
        && left.psr == right.psr
}

fn shared_raw_host_comparison(
    pre: &PluginDataFile,
    post: &PluginDataFile,
    expected_len: usize,
    slot_samples: i64,
) -> Option<(RawHostComparisonSide, RawHostComparisonSide)> {
    let pre_range = pre.raw_host_clock_range?;
    let post_range = post.raw_host_clock_range?;
    if pre_range.start_position_samples != post_range.start_position_samples {
        return None;
    }
    let origin = pre_range.start_position_samples;
    let pre_side = raw_host_comparison_side(pre, origin, expected_len, slot_samples)?;
    let post_side = raw_host_comparison_side(post, origin, expected_len, slot_samples)?;
    let post_by_time = post_side
        .frames
        .iter()
        .zip(&post_side.raw_slots)
        .map(|(frame, raw_position)| (frame.t_ms, *raw_position))
        .collect::<std::collections::BTreeMap<_, _>>();
    let post_time_by_raw = post_side
        .frames
        .iter()
        .zip(&post_side.raw_slots)
        .map(|(frame, raw_position)| (*raw_position, frame.t_ms))
        .collect::<std::collections::BTreeMap<_, _>>();
    let mut common_count = 0_usize;
    for (frame, pre_raw_position) in pre_side.frames.iter().zip(&pre_side.raw_slots) {
        if let Some(post_raw_position) = post_by_time.get(&frame.t_ms) {
            if pre_raw_position != post_raw_position {
                return None;
            }
            common_count = common_count.saturating_add(1);
        }
        if post_time_by_raw
            .get(pre_raw_position)
            .is_some_and(|post_time| *post_time != frame.t_ms)
        {
            return None;
        }
    }
    (common_count >= 2).then_some((pre_side, post_side))
}

fn wav_slots_for_frames(
    frames: &[Frame],
    expected_len: usize,
    slot_samples: i64,
) -> Option<Vec<i64>> {
    let max_ms = u64::try_from(expected_len)
        .ok()?
        .checked_mul(FRAME_INTERVAL_MS)?;
    frames
        .iter()
        .map(|frame| {
            if frame.t_ms == 0 || frame.t_ms > max_ms || frame.t_ms % FRAME_INTERVAL_MS != 0 {
                return None;
            }
            i64::try_from(frame.t_ms / FRAME_INTERVAL_MS)
                .ok()?
                .checked_mul(slot_samples)
        })
        .collect()
}

fn wav_slots(expected_len: usize, slot_samples: i64) -> Option<Vec<i64>> {
    (1..=expected_len)
        .map(|index| i64::try_from(index).ok()?.checked_mul(slot_samples))
        .collect()
}

pub(crate) fn has_compatible_host_clocks(pre: &PluginDataFile, post: &PluginDataFile) -> bool {
    let (Some(pre_clock), Some(post_clock)) = (pre.trace_clock.as_ref(), post.trace_clock.as_ref())
    else {
        return false;
    };
    let sources_are_known = |sources: &[String]| {
        !sources.is_empty()
            && sources.iter().all(|source| {
                matches!(
                    source.as_str(),
                    "project_timeline" | "audio_render_timeline"
                )
            })
    };
    pre_clock.basis == crate::trace_alignment::TRACE_CLOCK_BASIS
        && post_clock.basis == crate::trace_alignment::TRACE_CLOCK_BASIS
        && pre_clock.sample_rate > 0
        && pre_clock.sample_rate == post_clock.sample_rate
        && sources_are_known(&pre_clock.sources)
        && sources_are_known(&post_clock.sources)
}

fn direct_trace_source(data: &PluginDataFile) -> Option<BTreeMap<i64, Frame>> {
    if data.frames.is_empty()
        || data.frames.len() != data.trace_slot_positions.len()
        || !data
            .trace_slot_positions
            .windows(2)
            .all(|pair| pair[0] < pair[1])
    {
        return None;
    }
    Some(
        data.trace_slot_positions
            .iter()
            .copied()
            .zip(data.frames.iter().cloned())
            .collect(),
    )
}

struct SparseDirectSide {
    frames: Vec<Frame>,
    producer_slots: Vec<i64>,
    wav_slots: Vec<i64>,
    origin: i64,
    start_basis: &'static str,
}

fn select_sparse_wav_side(
    source: &BTreeMap<i64, Frame>,
    expected: &ExpectedWavMetadata,
    expected_len: usize,
    slot_samples: i64,
    render_range: Option<(i64, i64)>,
) -> Option<SparseDirectSide> {
    let (origin, start_basis) = if let Some(start) = expected.wav_time_reference_samples {
        (
            i64::try_from(start).ok()?,
            crate::trace_alignment::TRACE_ALIGNMENT_START_BWF,
        )
    } else {
        (
            render_range?.0,
            crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE,
        )
    };
    let mut frames = Vec::new();
    let mut producer_slots = Vec::new();
    let mut wav_slots = Vec::new();
    for (position, source_frame) in source {
        let relative = position.checked_sub(origin)?;
        if relative <= 0 || relative % slot_samples != 0 {
            continue;
        }
        let slot_index = usize::try_from(relative / slot_samples).ok()?;
        if slot_index == 0 || slot_index > expected_len {
            continue;
        }
        let mut frame = source_frame.clone();
        frame.t_ms = u64::try_from(slot_index)
            .ok()?
            .checked_mul(FRAME_INTERVAL_MS)?;
        frames.push(frame);
        producer_slots.push(*position);
        wav_slots.push(relative);
    }
    (!frames.is_empty()).then_some(SparseDirectSide {
        frames,
        producer_slots,
        wav_slots,
        origin,
        start_basis,
    })
}

fn selected_frames(source: &BTreeMap<i64, Frame>, positions: &[i64]) -> Option<Vec<Frame>> {
    positions
        .iter()
        .enumerate()
        .map(|(index, position)| {
            let t_ms = u64::try_from(index)
                .ok()?
                .checked_add(1)?
                .checked_mul(FRAME_INTERVAL_MS)?;
            let mut frame = source.get(position)?.clone();
            frame.t_ms = t_ms;
            Some(frame)
        })
        .collect()
}

fn select_wav_window(
    pre: &BTreeMap<i64, Frame>,
    post: &BTreeMap<i64, Frame>,
    expected: &ExpectedWavMetadata,
    expected_len: usize,
    slot_samples: i64,
    render_range: Option<(i64, i64)>,
) -> Option<(Vec<i64>, &'static str)> {
    let window_is_complete = |positions: &[i64]| {
        positions.len() == expected_len
            && positions
                .windows(2)
                .all(|pair| pair[1].saturating_sub(pair[0]) == slot_samples)
            && positions
                .iter()
                .all(|position| pre.contains_key(position) && post.contains_key(position))
    };

    let (wav_start, start_basis) = if let Some(wav_start) = expected.wav_time_reference_samples {
        (
            i64::try_from(wav_start).ok()?,
            crate::trace_alignment::TRACE_ALIGNMENT_START_BWF,
        )
    } else {
        let (start, end) = render_range?;
        let duration = i64::try_from(expected.expected_duration_samples).ok()?;
        if end.checked_sub(start) != Some(duration) {
            return None;
        }
        (
            start,
            crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE,
        )
    };
    let first = wav_start.checked_add(slot_samples)?;
    let anchored: Vec<i64> = (0..expected_len)
        .map(|index| {
            i64::try_from(index)
                .ok()?
                .checked_mul(slot_samples)?
                .checked_add(first)
        })
        .collect::<Option<Vec<_>>>()?;
    if window_is_complete(&anchored) {
        return Some((anchored, start_basis));
    }
    None
}

fn shared_producer_render_range(
    pre: &PluginDataFile,
    post: &PluginDataFile,
    expected: &ExpectedWavMetadata,
) -> Option<(i64, i64)> {
    let pre_take = pre.bounce_take.as_ref()?;
    let post_take = post.bounce_take.as_ref()?;
    let pre_range = (
        pre_take.host_start_position_samples?,
        pre_take.host_end_position_samples?,
    );
    let post_range = (
        post_take.host_start_position_samples?,
        post_take.host_end_position_samples?,
    );
    let duration = i64::try_from(expected.expected_duration_samples).ok()?;
    (pre_range == post_range
        && pre_take.sample_rate == expected.expected_sample_rate
        && post_take.sample_rate == expected.expected_sample_rate
        && pre_take.duration_samples == expected.expected_duration_samples
        && post_take.duration_samples == expected.expected_duration_samples
        && pre_range.1.checked_sub(pre_range.0) == Some(duration))
    .then_some(pre_range)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{BounceTake, HostClockRange, Role, TraceClock, TraceClockObservation};

    fn frame(value: f64) -> Frame {
        Frame {
            t_ms: 0,
            n_prime: None,
            sharpness: None,
            lufs_m: value,
            true_peak: value * -3.0,
            crest: value.abs(),
            psr: None,
        }
    }

    fn source(positions: &[i64], values: &[f64]) -> BTreeMap<i64, Frame> {
        positions
            .iter()
            .copied()
            .zip(values.iter().copied().map(frame))
            .collect()
    }

    fn expected(wav_start: Option<u64>) -> ExpectedWavMetadata {
        ExpectedWavMetadata {
            expected_duration_samples: 14_400,
            expected_sample_rate: 48_000,
            wav_time_reference_samples: wav_start,
            wav_path: "/tmp/wav-start-clock.wav".to_string(),
            bounce_id: "bounce-wav-start-clock".to_string(),
            created_at_ms: 1,
            wav_file_size: Some(1),
            wav_mtime_ms: 1,
            wav_hash: Some("hash-wav-start-clock".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    fn expected_96k_15s(wav_start: u64) -> ExpectedWavMetadata {
        ExpectedWavMetadata {
            expected_duration_samples: 1_440_000,
            expected_sample_rate: 96_000,
            wav_time_reference_samples: Some(wav_start),
            wav_path: "/tmp/wav-start-clock-96k.wav".to_string(),
            bounce_id: "bounce-wav-start-clock-96k".to_string(),
            created_at_ms: 1,
            wav_file_size: Some(1),
            wav_mtime_ms: 1,
            wav_hash: Some("hash-wav-start-clock-96k".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        }
    }

    fn plugin_data(role: Role, session: &str, source_name: &str) -> PluginDataFile {
        let mut data = PluginDataFile::new(
            "installation".to_string(),
            "project".to_string(),
            format!("{role:?}"),
            role,
            None,
            48_000,
            None,
            None,
            None,
            None,
        );
        data.record_session_id = Some(session.to_string());
        data.trace_clock = Some(TraceClock {
            basis: crate::trace_alignment::TRACE_CLOCK_BASIS.to_string(),
            origin_position_samples: 96_000,
            end_position_samples: 115_200,
            sample_rate: 48_000,
            sources: vec![source_name.to_string()],
        });
        data.trace_slot_positions = vec![100_800, 105_600, 110_400];
        data.frames = vec![frame(-30.0), frame(-20.0), frame(-10.0)];
        data.bounce_take = Some(BounceTake {
            source: "render_clock_native".to_string(),
            time_axis: "native_samples".to_string(),
            alignment_status: "sample_count_ready".to_string(),
            sample_rate: 48_000,
            wav_start_sample: 0,
            wav_end_sample: 14_400,
            duration_samples: 14_400,
            duration_frames_48k: 14_400,
            start_t_ms: 0,
            end_t_ms: 300,
            trace_sample_count: 3,
            frame_count: 3,
            host_start_position_samples: Some(96_000),
            host_end_position_samples: Some(110_400),
        });
        data
    }

    fn raw_host_comparison_data(role: Role, origin: i64, positions: &[i64]) -> PluginDataFile {
        let mut data = plugin_data(role, "session", "audio_render_timeline");
        data.trace_slot_positions.clear();
        data.frames.clear();
        data.raw_host_clock_range = Some(HostClockRange {
            start_position_samples: origin,
            end_position_samples: origin + 19_200,
        });
        data.trace_clock_observations = positions
            .iter()
            .enumerate()
            .map(|(index, position)| TraceClockObservation {
                frame: Frame {
                    t_ms: u64::try_from(*position - origin).unwrap() * 1_000 / 48_000,
                    ..frame(-30.0 + index as f64)
                },
                producer_position_samples: None,
                raw_host_position_samples: Some(*position),
                capture_epoch: Some(7),
                clock_source: Some("audio_render_timeline".to_string()),
                presentation_latency_source: Some("audio_unit_v2".to_string()),
                input_presentation_latency_samples: Some(0),
                output_presentation_latency_samples: Some(512),
            })
            .collect();
        let take = data.bounce_take.as_mut().unwrap();
        take.duration_samples = 19_200;
        take.host_start_position_samples = Some(origin - 512);
        take.host_end_position_samples = Some(origin + 18_688);
        data
    }

    #[test]
    fn bwf_start_selects_identical_samples_even_when_shapes_disagree() {
        let positions = [96_000, 100_800, 105_600, 110_400, 115_200];
        let pre = source(&positions, &[-30.0, -8.0, -25.0, -12.0, -20.0]);
        let post = source(&positions, &[4.0, -40.0, 2.0, -35.0, 1.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(96_000)), 3, 4_800, None);

        assert_eq!(
            selected,
            Some((
                vec![100_800, 105_600, 110_400],
                crate::trace_alignment::TRACE_ALIGNMENT_START_BWF,
            ))
        );
    }

    #[test]
    fn bwf_start_never_falls_forward_to_a_different_complete_window() {
        let pre_positions = [100_800, 105_600, 110_400, 115_200];
        let post_positions = [105_600, 110_400, 115_200, 120_000];
        let pre = source(&pre_positions, &[-30.0, -8.0, -25.0, -12.0]);
        let post = source(&post_positions, &[-7.0, -6.0, -5.0, -4.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(96_000)), 3, 4_800, None);

        assert_eq!(selected, None);
    }

    #[test]
    fn out_of_range_bwf_start_cannot_saturate_into_a_false_window() {
        let positions = [i64::MAX - 9_600, i64::MAX - 4_800, i64::MAX];
        let pre = source(&positions, &[-3.0, -2.0, -1.0]);
        let post = source(&positions, &[-30.0, -20.0, -10.0]);

        let selected = select_wav_window(&pre, &post, &expected(Some(u64::MAX)), 3, 4_800, None);

        assert_eq!(selected, None);
    }

    #[test]
    fn missing_bext_uses_the_exact_render_start_not_a_dense_tail() {
        let pre_positions = [91_200, 96_000, 100_800, 105_600, 110_400];
        let post_positions = [96_000, 100_800, 105_600, 110_400, 115_200];
        let pre = source(&pre_positions, &[-1.0, -2.0, -3.0, -4.0, -5.0]);
        let post = source(&post_positions, &[-50.0, 10.0, -40.0, 20.0, -30.0]);

        let selected = select_wav_window(
            &pre,
            &post,
            &expected(None),
            3,
            4_800,
            Some((91_200, 105_600)),
        );

        assert_eq!(
            selected,
            Some((
                vec![96_000, 100_800, 105_600],
                crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE,
            ))
        );
    }

    #[test]
    fn missing_bext_never_promotes_dense_frames_without_a_producer_range() {
        let positions = [96_000, 100_800, 105_600, 110_400];
        let pre = source(&positions, &[-1.0, -2.0, -3.0, -4.0]);
        let post = source(&positions, &[-10.0, -20.0, -30.0, -40.0]);

        assert_eq!(
            select_wav_window(&pre, &post, &expected(None), 3, 4_800, None),
            None
        );
    }

    #[test]
    fn bwf_plan_requires_one_record_session_and_exact_direct_slots_not_a_host_clock() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");

        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_some()
        );

        post.record_session_id = Some("other-session".to_string());
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_none()
        );

        post.record_session_id = Some("session".to_string());
        pre.trace_clock = None;
        post.trace_clock = None;
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_some()
        );

        post.trace_slot_positions.pop();
        let fallback = build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800)
            .expect("measured pair remains reachable");
        assert!(!fallback.exact);
        assert_eq!(fallback.post_clock_model, "chronological_fallback");
    }

    #[test]
    fn bwf_plan_selects_exact_slots_from_same_record_epoch_lead_and_tail() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        let positions = vec![96_000, 100_800, 105_600, 110_400, 115_200];
        pre.trace_slot_positions = positions.clone();
        post.trace_slot_positions = positions;
        pre.frames = vec![
            frame(-90.0),
            frame(-30.0),
            frame(-20.0),
            frame(-10.0),
            frame(-80.0),
        ];
        post.frames = vec![
            frame(90.0),
            frame(10.0),
            frame(20.0),
            frame(30.0),
            frame(80.0),
        ];

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800)
            .expect("direct Record lead/tail may surround the exact WAV window");

        assert_eq!(plan.pre_producer_slots, vec![100_800, 105_600, 110_400]);
        assert_eq!(plan.post_producer_slots, vec![100_800, 105_600, 110_400]);
        assert_eq!(
            plan.pre_frames
                .iter()
                .map(|frame| frame.lufs_m)
                .collect::<Vec<_>>(),
            vec![-30.0, -20.0, -10.0]
        );
        assert_eq!(
            plan.post_frames
                .iter()
                .map(|frame| frame.lufs_m)
                .collect::<Vec<_>>(),
            vec![10.0, 20.0, 30.0]
        );
    }

    #[test]
    fn render_range_without_compatible_clocks_is_visible_but_not_exact() {
        let pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        assert!(build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800).is_some());

        post.trace_clock = None;
        let fallback = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("measured render remains visible");
        assert!(!fallback.exact);

        post.trace_clock = plugin_data(Role::Post, "session", "unknown_clock").trace_clock;
        let fallback = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("unknown clock is a quality issue, not visibility loss");
        assert!(!fallback.exact);
    }

    #[test]
    fn unresolved_wav_start_preserves_exact_raw_host_comparison_with_sparse_gaps() {
        let origin = 15_594_720;
        let pre = raw_host_comparison_data(
            Role::Pre,
            origin,
            &[origin + 4_800, origin + 9_600, origin + 14_400],
        );
        let post = raw_host_comparison_data(Role::Post, origin, &[origin + 4_800, origin + 14_400]);

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("same raw host clock remains comparable");

        assert!(!plan.exact, "the non-BWF file start remains unresolved");
        assert_eq!(
            plan.comparison_resolution,
            Some(TRACE_COMPARISON_RAW_HOST_EXACT)
        );
        assert_eq!(
            plan.pre_comparison_slots,
            vec![origin + 4_800, origin + 9_600, origin + 14_400]
        );
        assert_eq!(
            plan.post_comparison_slots,
            vec![origin + 4_800, origin + 14_400]
        );
        assert_eq!(plan.pre_wav_slots, vec![4_800, 9_600, 14_400]);
        assert_eq!(plan.post_wav_slots, vec![4_800, 14_400]);
        assert_eq!(
            plan.post_frames
                .iter()
                .map(|frame| frame.t_ms)
                .collect::<Vec<_>>(),
            vec![100, 300]
        );
    }

    #[test]
    fn raw_host_comparison_ignores_partial_epochs_around_the_selected_offline_render() {
        let origin = 15_594_720;
        let mut pre = raw_host_comparison_data(
            Role::Pre,
            origin,
            &[origin + 4_800, origin + 9_600, origin + 14_400],
        );
        let mut post = raw_host_comparison_data(
            Role::Post,
            origin,
            &[origin + 4_800, origin + 9_600, origin + 14_400],
        );
        pre.frames = pre
            .trace_clock_observations
            .iter()
            .map(|observation| observation.frame.clone())
            .collect();
        post.frames = post
            .trace_clock_observations
            .iter()
            .map(|observation| observation.frame.clone())
            .collect();
        for data in [&mut pre, &mut post] {
            let mut pre_bounce_observation = data.trace_clock_observations[0].clone();
            pre_bounce_observation.capture_epoch = Some(6);
            pre_bounce_observation.frame.lufs_m = -90.0;
            data.trace_clock_observations
                .insert(0, pre_bounce_observation);
        }

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("the unique complete render epoch remains provable");

        assert_eq!(
            plan.comparison_resolution,
            Some(TRACE_COMPARISON_RAW_HOST_EXACT)
        );
        assert_eq!(plan.pre_frames[0].lufs_m, -30.0);
        assert_eq!(plan.post_frames[0].lufs_m, -30.0);
        assert_eq!(
            plan.pre_comparison_slots,
            vec![origin + 4_800, origin + 9_600, origin + 14_400]
        );
        assert_eq!(plan.pre_comparison_slots, plan.post_comparison_slots);
    }

    #[test]
    fn logic_late_drop_windows_259_raw_pairs_to_the_245_aiff_frames() {
        let origin = 16_122_720;
        let raw_first = 16_128_512;
        let captured_raw_slots = (0_i64..259)
            .map(|index| raw_first + index * 9_600)
            .collect::<Vec<_>>();
        let mut pre = plugin_data(Role::Pre, "session", "audio_render_timeline");
        let mut post = plugin_data(Role::Post, "session", "audio_render_timeline");
        for (role_index, data) in [&mut pre, &mut post].into_iter().enumerate() {
            data.sample_rate = 96_000;
            data.trace_clock = None;
            data.trace_clock_observations.clear();
            data.trace_slot_positions.clear();
            data.raw_host_clock_range = Some(HostClockRange {
                start_position_samples: origin,
                end_position_samples: 18_614_112,
            });
            data.frames = (1_u64..=259)
                .map(|index| Frame {
                    t_ms: index * 100,
                    ..frame(-30.0 + role_index as f64 + index as f64)
                })
                .collect();
            data.trace_raw_host_slot_positions = captured_raw_slots.clone();
        }
        let expected = ExpectedWavMetadata {
            expected_duration_samples: 2_352_000,
            expected_sample_rate: 96_000,
            wav_time_reference_samples: None,
            wav_path: "/tmp/logic-late-drop.aif".to_string(),
            bounce_id: "logic-late-drop".to_string(),
            created_at_ms: 1,
            wav_file_size: Some(14_131_452),
            wav_mtime_ms: 1,
            wav_hash: Some("logic-late-drop-hash".to_string()),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        };

        let plan = build_wav_start_clock_plan(&pre, &post, &expected, 245, 9_600)
            .expect("late Drop must window the producer-bound frame/slot pairs together");

        assert!(!plan.exact);
        assert_eq!(
            plan.comparison_resolution,
            Some(TRACE_COMPARISON_RAW_HOST_EXACT)
        );
        assert_eq!(plan.pre_frames.len(), 245);
        assert_eq!(plan.post_frames.len(), 245);
        assert_eq!(plan.pre_comparison_slots.len(), 245);
        assert_eq!(plan.pre_comparison_slots, plan.post_comparison_slots);
        assert_eq!(plan.pre_comparison_slots.first(), Some(&raw_first));
        assert_eq!(plan.pre_comparison_slots.last(), Some(&18_470_912));
        assert_eq!(plan.pre_wav_slots.first(), Some(&9_600));
        assert_eq!(plan.pre_wav_slots.last(), Some(&2_352_000));

        // Even a close-time tail outside the selected WAV window remains part of the producer
        // evidence. Corrupting it must disable exact comparison instead of being hidden by trim.
        let previous_tail = pre.trace_raw_host_slot_positions[257];
        pre.trace_raw_host_slot_positions[258] = previous_tail;
        let fallback = build_wav_start_clock_plan(&pre, &post, &expected, 245, 9_600)
            .expect("measured frames remain visible when producer evidence is invalid");
        assert_eq!(fallback.comparison_resolution, None);
        assert!(fallback.pre_comparison_slots.is_empty());
        assert!(fallback.post_comparison_slots.is_empty());
    }

    #[test]
    fn raw_host_observation_phase_is_independent_from_elapsed_frame_grid() {
        let origin = 15_594_720;
        let raw_slots = [origin + 512, origin + 5_312, origin + 10_112];
        let mut pre = raw_host_comparison_data(Role::Pre, origin, &[origin + 4_800]);
        let mut post = raw_host_comparison_data(Role::Post, origin, &[origin + 4_800]);
        for data in [&mut pre, &mut post] {
            data.trace_clock_observations = raw_slots
                .iter()
                .enumerate()
                .map(|(index, raw_position)| TraceClockObservation {
                    frame: Frame {
                        t_ms: (index as u64 + 1) * 100,
                        ..frame(-30.0 + index as f64)
                    },
                    producer_position_samples: None,
                    raw_host_position_samples: Some(*raw_position),
                    capture_epoch: Some(7),
                    clock_source: Some("audio_render_timeline".to_string()),
                    presentation_latency_source: Some("audio_unit_v2".to_string()),
                    input_presentation_latency_samples: Some(0),
                    output_presentation_latency_samples: Some(512),
                })
                .collect();
            data.frames = data
                .trace_clock_observations
                .iter()
                .map(|observation| observation.frame.clone())
                .collect();
        }

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("raw host phase must not redefine elapsed 100 ms frame identity");

        assert_eq!(
            plan.comparison_resolution,
            Some(TRACE_COMPARISON_RAW_HOST_EXACT)
        );
        assert_eq!(plan.pre_comparison_slots, raw_slots);
        assert_eq!(plan.pre_comparison_slots, plan.post_comparison_slots);
        assert_eq!(plan.pre_wav_slots, vec![4_800, 9_600, 14_400]);
    }

    #[test]
    fn raw_host_comparison_rejects_a_mismatch_at_any_shared_elapsed_frame() {
        let origin = 15_594_720;
        let pre_slots = vec![origin + 512, origin + 5_312, origin + 10_112];
        let mut post_slots = pre_slots.clone();
        post_slots[2] += 1;
        let mut pre = raw_host_comparison_data(Role::Pre, origin, &[origin + 4_800]);
        let mut post = raw_host_comparison_data(Role::Post, origin, &[origin + 4_800]);
        for (data, raw_slots) in [(&mut pre, pre_slots), (&mut post, post_slots)] {
            data.frames = (1_u64..=3)
                .map(|index| Frame {
                    t_ms: index * 100,
                    ..frame(-30.0 + index as f64)
                })
                .collect();
            data.trace_raw_host_slot_positions = raw_slots;
        }

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("measured frames remain visible when exact subtraction is unavailable");

        assert_eq!(plan.comparison_resolution, None);
        assert!(plan.pre_comparison_slots.is_empty());
        assert!(plan.post_comparison_slots.is_empty());
    }

    #[test]
    fn different_raw_host_origins_never_qualify_pre_post_comparison() {
        let pre_origin = 15_594_720;
        let post_origin = pre_origin + 512;
        let pre = raw_host_comparison_data(
            Role::Pre,
            pre_origin,
            &[pre_origin + 4_800, pre_origin + 9_600, pre_origin + 14_400],
        );
        let post = raw_host_comparison_data(
            Role::Post,
            post_origin,
            &[
                post_origin + 4_800,
                post_origin + 9_600,
                post_origin + 14_400,
            ],
        );

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("measured frames remain visible");

        assert!(!plan.exact);
        assert_eq!(plan.comparison_resolution, None);
        assert!(plan.pre_comparison_slots.is_empty());
        assert!(plan.post_comparison_slots.is_empty());
    }

    #[test]
    fn mixed_au_vst3_clock_sources_are_known_and_share_the_presentation_axis() {
        let pre = plugin_data(Role::Pre, "session", "audio_render_timeline");
        let post = plugin_data(Role::Post, "session", "project_timeline");

        assert!(has_compatible_host_clocks(&pre, &post));
        assert!(
            build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800).is_some()
        );
    }

    #[test]
    fn pre_and_post_resolve_different_host_pdc_semantics_on_one_wav_axis() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "audio_render_timeline");
        let targets = [100_800_i64, 105_600, 110_400];
        pre.trace_clock_observations = targets
            .iter()
            .enumerate()
            .map(|(index, target)| TraceClockObservation {
                frame: Frame {
                    t_ms: (index as u64 + 1) * 100,
                    ..frame(-30.0 + index as f64)
                },
                producer_position_samples: None,
                raw_host_position_samples: Some(*target),
                capture_epoch: Some(10),
                clock_source: Some("project_timeline".to_string()),
                presentation_latency_source: Some("vst3".to_string()),
                input_presentation_latency_samples: Some(0),
                output_presentation_latency_samples: Some(256),
            })
            .collect();
        post.trace_clock_observations = targets
            .iter()
            .enumerate()
            .map(|(index, target)| TraceClockObservation {
                frame: Frame {
                    t_ms: (index as u64 + 1) * 100,
                    ..frame(-20.0 + index as f64)
                },
                producer_position_samples: None,
                raw_host_position_samples: target.checked_sub(512),
                capture_epoch: Some(20),
                clock_source: Some("audio_render_timeline".to_string()),
                presentation_latency_source: Some("audio_unit_v2".to_string()),
                input_presentation_latency_samples: Some(0),
                output_presentation_latency_samples: Some(512),
            })
            .collect();

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800)
            .expect("independent host models");

        assert!(plan.exact);
        assert_eq!(plan.pre_clock_model, "raw_host_position");
        assert_eq!(plan.post_clock_model, "raw_plus_output_latency");
        assert_eq!(plan.pre_wav_slots, vec![4_800, 9_600, 14_400]);
        assert_eq!(plan.post_wav_slots, vec![4_800, 9_600, 14_400]);
    }

    #[test]
    fn one_missing_post_window_keeps_its_exact_wav_gap() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        let targets = [100_800_i64, 105_600, 110_400];
        pre.trace_clock_observations = targets
            .iter()
            .enumerate()
            .map(|(index, target)| TraceClockObservation {
                frame: Frame {
                    t_ms: (index as u64 + 1) * 100,
                    ..frame(-30.0 + index as f64)
                },
                producer_position_samples: Some(*target),
                raw_host_position_samples: Some(*target),
                capture_epoch: Some(10),
                clock_source: Some("project_timeline".to_string()),
                presentation_latency_source: Some("vst3".to_string()),
                input_presentation_latency_samples: Some(0),
                output_presentation_latency_samples: Some(0),
            })
            .collect();
        post.trace_clock_observations = pre
            .trace_clock_observations
            .iter()
            .enumerate()
            .filter(|(index, _)| *index != 1)
            .map(|(_, observation)| observation.clone())
            .collect();

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800)
            .expect("anchored sparse pair");

        assert!(plan.exact, "the surviving slots remain sample-anchored");
        assert_eq!(plan.pre_wav_slots, vec![4_800, 9_600, 14_400]);
        assert_eq!(plan.post_wav_slots, vec![4_800, 14_400]);
        assert_eq!(
            plan.post_frames
                .iter()
                .map(|frame| frame.t_ms)
                .collect::<Vec<_>>(),
            vec![100, 300]
        );
    }

    #[test]
    fn missing_bext_dynamic_latency_resolves_from_the_producer_presentation_range() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "audio_render_timeline");
        let targets = [100_800_i64, 105_600, 110_400];
        for (side, base) in [(&mut pre, -30.0), (&mut post, -20.0)] {
            side.trace_slot_positions.clear();
            side.trace_clock_observations = targets
                .iter()
                .enumerate()
                .map(|(index, target)| {
                    let output = if index == 0 { 256 } else { 512 };
                    TraceClockObservation {
                        frame: Frame {
                            t_ms: (index as u64 + 1) * 100,
                            ..frame(base + index as f64)
                        },
                        producer_position_samples: Some(*target),
                        raw_host_position_samples: target.checked_add(i64::from(output)),
                        capture_epoch: Some(index as u64 + 10),
                        clock_source: Some("project_timeline".to_string()),
                        presentation_latency_source: Some("vst3".to_string()),
                        input_presentation_latency_samples: Some(0),
                        output_presentation_latency_samples: Some(output),
                    }
                })
                .collect();
        }

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("non-BWF presentation clock with dynamic latency");

        assert!(plan.exact);
        assert_eq!(
            plan.start_basis,
            crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE
        );
        assert_eq!(plan.pre_clock_model, "producer_plus_output_latency");
        assert_eq!(plan.post_clock_model, "producer_plus_output_latency");
        assert_eq!(plan.pre_wav_slots, vec![5_056, 10_112]);
        assert_eq!(plan.post_wav_slots, vec![5_056, 10_112]);
    }

    #[test]
    fn missing_bext_plan_uses_producer_render_range_with_different_metric_shapes() {
        let pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        post.frames = vec![frame(40.0), frame(-60.0), frame(30.0)];

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(None), 3, 4_800)
            .expect("producer render-range plan");
        assert_eq!(
            plan.start_basis,
            crate::trace_alignment::TRACE_ALIGNMENT_START_RENDER_RANGE
        );
        assert_ne!(plan.pre_frames[0].lufs_m, plan.post_frames[0].lufs_m);
    }

    #[test]
    fn context_cannot_repair_missing_direct_bwf_slots() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        let positions = [95_940, 96_000, 100_800, 105_600, 110_400];
        pre.trace_context_slot_positions = positions.to_vec();
        pre.trace_context_frames = vec![
            frame(-31.0),
            frame(-30.0),
            frame(-20.0),
            frame(-10.0),
            frame(-5.0),
        ];
        post.trace_context_slot_positions = positions.to_vec();
        post.trace_context_frames = vec![
            frame(20.0),
            frame(10.0),
            frame(4.0),
            frame(-40.0),
            frame(2.0),
        ];
        post.frames.clear();
        post.trace_slot_positions.clear();
        pre.trace_clock = None;
        post.trace_clock = None;

        let fallback = build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800)
            .expect("the measured pair identity remains reachable");
        assert!(!fallback.exact);
        assert_eq!(fallback.pre_frames.len(), 3);
        assert!(fallback.post_frames.is_empty());
        assert_eq!(fallback.pre_frames[0].lufs_m, -30.0);
        assert_ne!(fallback.pre_frames[0].lufs_m, -31.0);
    }

    #[test]
    fn context_cannot_replace_a_missing_anchored_direct_slot_and_gap_stays_sparse() {
        let mut pre = plugin_data(Role::Pre, "session", "project_timeline");
        let mut post = plugin_data(Role::Post, "session", "project_timeline");
        let exact_context = vec![100_800, 105_600, 110_400];
        pre.trace_context_slot_positions = exact_context.clone();
        pre.trace_context_frames = vec![frame(-20.0), frame(-10.0), frame(-5.0)];
        post.trace_context_slot_positions = exact_context;
        post.trace_context_frames = vec![frame(4.0), frame(-40.0), frame(2.0)];
        post.trace_slot_positions = vec![100_800, 110_400, 115_200];
        pre.trace_clock = None;
        post.trace_clock = None;

        let plan = build_wav_start_clock_plan(&pre, &post, &expected(Some(96_000)), 3, 4_800)
            .expect("current measured frames remain reachable");
        assert!(plan.exact);
        assert_eq!(plan.post_wav_slots, vec![4_800, 14_400]);
        assert_eq!(plan.post_frames[0].lufs_m, -30.0);
        assert_ne!(plan.post_frames[0].lufs_m, 4.0);
    }

    #[test]
    fn studio_one_96k_bwf_uses_exact_150_direct_slots_and_ignores_context() {
        const WAV_START: i64 = 6_480_000;
        const SLOT_SAMPLES: i64 = 9_600;
        let direct_positions = (6_470_400_i64..=7_920_000)
            .step_by(SLOT_SAMPLES as usize)
            .collect::<Vec<_>>();
        assert_eq!(
            direct_positions.len(),
            152,
            "failed generation's full producer grid"
        );
        let mut context_positions = vec![6_475_940, WAV_START];
        context_positions.extend((1_i64..=151).map(|index| WAV_START + index * SLOT_SAMPLES));
        let context_frames: Vec<Frame> = context_positions
            .iter()
            .enumerate()
            .map(|(index, _)| frame(1_000.0 + index as f64))
            .collect();
        let mut pre = plugin_data(Role::Pre, "session-96k", "project_timeline");
        let mut post = plugin_data(Role::Post, "session-96k", "project_timeline");
        for (side, base_value) in [(&mut pre, -30.0), (&mut post, 20.0)] {
            side.sample_rate = 96_000;
            side.source_format = 96_000;
            side.trace_slot_positions = direct_positions.clone();
            side.frames = (0..direct_positions.len())
                .map(|index| frame(base_value + index as f64 * 0.01))
                .collect();
            side.trace_context_slot_positions = context_positions.clone();
            side.trace_context_frames = context_frames.clone();
            side.trace_clock = None;
        }

        let plan = build_wav_start_clock_plan(
            &pre,
            &post,
            &expected_96k_15s(WAV_START as u64),
            150,
            SLOT_SAMPLES,
        )
        .expect("exact 96 kHz BWF window");

        assert_eq!(plan.pre_producer_slots.len(), 150);
        assert_eq!(plan.pre_producer_slots.first(), Some(&6_489_600));
        assert_eq!(plan.pre_producer_slots.last(), Some(&7_920_000));
        assert_eq!(plan.pre_wav_slots.first(), Some(&9_600));
        assert_eq!(plan.pre_wav_slots.last(), Some(&1_440_000));
        assert_eq!(plan.post_wav_slots, plan.pre_wav_slots);
        assert_eq!(plan.pre_frames.len(), 150);
        assert_eq!(plan.post_frames.len(), 150);
        assert_eq!(plan.pre_frames[0].lufs_m, -29.98);
        assert_eq!(plan.post_frames[0].lufs_m, 20.02);
    }
}
