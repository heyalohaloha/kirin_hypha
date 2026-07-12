//! Producer-owned PRE/POST content clock.
//!
//! A DAW host position identifies a callback, not necessarily the source-audio interval inside
//! that callback after plug-in delay compensation. This module derives one constant PRE→POST
//! slot mapping from the complete measured neighborhood and then anchors the selected POST range
//! to the dropped Broadcast-Wave origin when one exists.

use std::collections::BTreeMap;

use crate::plugin_data::{Frame, PluginDataFile};
use crate::record_expected::ExpectedWavMetadata;

const FRAME_INTERVAL_MS: u64 = 100;
const MAX_CONTENT_LAG_SLOTS: usize = 100;

pub(crate) struct ContentClockPlan {
    pub pre_frames: Vec<Frame>,
    pub post_frames: Vec<Frame>,
    pub canonical_slots: Vec<i64>,
    pub pre_to_post_offset_samples: i64,
    pub alignment_score: Option<f64>,
}

pub(crate) fn build_content_clock_plan(
    pre: &PluginDataFile,
    post: &PluginDataFile,
    expected: &ExpectedWavMetadata,
    expected_len: usize,
    slot_samples: i64,
) -> Option<ContentClockPlan> {
    let pre_source = trace_source(pre);
    let post_source = trace_source(post);
    let (offset_slots, alignment_score) =
        estimate_pre_to_post_offset(&pre_source, &post_source, slot_samples);
    let offset_samples = offset_slots.saturating_mul(slot_samples);
    let selected_pre = select_aligned_window(
        &pre_source,
        &post_source,
        expected,
        expected_len,
        slot_samples,
        offset_samples,
    )?;
    let selected_post: Vec<i64> = selected_pre
        .iter()
        .map(|position| position.saturating_add(offset_samples))
        .collect();
    let pre_frames = selected_frames(&pre_source, &selected_pre)?;
    let post_frames = selected_frames(&post_source, &selected_post)?;
    Some(ContentClockPlan {
        pre_frames,
        post_frames,
        canonical_slots: selected_post,
        pre_to_post_offset_samples: offset_samples,
        alignment_score,
    })
}

fn trace_source(data: &PluginDataFile) -> BTreeMap<i64, Frame> {
    let context_is_usable = !data.trace_context_frames.is_empty()
        && data.trace_context_frames.len() == data.trace_context_slot_positions.len();
    let (positions, frames) = if context_is_usable {
        (
            &data.trace_context_slot_positions,
            &data.trace_context_frames,
        )
    } else {
        (&data.trace_slot_positions, &data.frames)
    };
    positions
        .iter()
        .copied()
        .zip(frames.iter().cloned())
        .collect()
}

fn selected_frames(source: &BTreeMap<i64, Frame>, positions: &[i64]) -> Option<Vec<Frame>> {
    positions
        .iter()
        .enumerate()
        .map(|(index, position)| {
            source.get(position).cloned().map(|mut frame| {
                frame.t_ms = (index as u64 + 1) * FRAME_INTERVAL_MS;
                frame
            })
        })
        .collect()
}

fn select_aligned_window(
    pre: &BTreeMap<i64, Frame>,
    post: &BTreeMap<i64, Frame>,
    expected: &ExpectedWavMetadata,
    expected_len: usize,
    slot_samples: i64,
    offset_samples: i64,
) -> Option<Vec<i64>> {
    let window_is_complete = |pre_positions: &[i64]| {
        pre_positions.len() == expected_len
            && pre_positions
                .windows(2)
                .all(|pair| pair[1].saturating_sub(pair[0]) == slot_samples)
            && pre_positions
                .iter()
                .all(|pre_position| post.contains_key(&pre_position.saturating_add(offset_samples)))
    };

    if let Some(wav_start) = expected.wav_time_reference_samples {
        let post_first = i64::try_from(wav_start).ok()?.saturating_add(slot_samples);
        let pre_first = post_first.saturating_sub(offset_samples);
        let anchored: Vec<i64> = (0..expected_len)
            .map(|index| pre_first.saturating_add(index as i64 * slot_samples))
            .collect();
        if anchored.iter().all(|position| pre.contains_key(position))
            && window_is_complete(&anchored)
        {
            return Some(anchored);
        }
    }

    let paired_pre: Vec<i64> = pre
        .keys()
        .copied()
        .filter(|position| post.contains_key(&position.saturating_add(offset_samples)))
        .collect();
    paired_pre
        .windows(expected_len)
        .find(|window| window_is_complete(window))
        .map(<[i64]>::to_vec)
}

fn estimate_pre_to_post_offset(
    pre: &BTreeMap<i64, Frame>,
    post: &BTreeMap<i64, Frame>,
    slot_samples: i64,
) -> (i64, Option<f64>) {
    if pre.len() < 3 || post.len() < 3 || slot_samples <= 0 {
        return (0, None);
    }
    let max_lag = ((pre.len().min(post.len()) / 3).min(MAX_CONTENT_LAG_SLOTS)) as i64;
    let mut best: Option<(i64, f64, usize)> = None;
    for lag in -max_lag..=max_lag {
        let offset = lag.saturating_mul(slot_samples);
        let pairs: Vec<(&Frame, &Frame)> = pre
            .iter()
            .filter_map(|(position, pre_frame)| {
                post.get(&position.saturating_add(offset))
                    .map(|post_frame| (pre_frame, post_frame))
            })
            .collect();
        if pairs.len() < 3 {
            continue;
        }
        let lufs = pearson_metric(&pairs, |frame| frame.lufs_m);
        let peak = pearson_metric(&pairs, |frame| frame.true_peak);
        let score = match (lufs, peak) {
            (Some(a), Some(b)) => (a + b) * 0.5,
            (Some(value), None) | (None, Some(value)) => value,
            (None, None) => continue,
        };
        let replace = best.is_none_or(|(best_lag, best_score, best_count)| {
            score > best_score + 1e-9
                || ((score - best_score).abs() <= 1e-9
                    && (pairs.len() > best_count
                        || (pairs.len() == best_count && lag.abs() < best_lag.abs())))
        });
        if replace {
            best = Some((lag, score, pairs.len()));
        }
    }
    best.map_or((0, None), |(lag, score, _)| (lag, Some(score)))
}

fn pearson_metric(pairs: &[(&Frame, &Frame)], value: impl Fn(&Frame) -> f64) -> Option<f64> {
    let n = pairs.len() as f64;
    let mean_left = pairs.iter().map(|(left, _)| value(left)).sum::<f64>() / n;
    let mean_right = pairs.iter().map(|(_, right)| value(right)).sum::<f64>() / n;
    let mut covariance = 0.0;
    let mut variance_left = 0.0;
    let mut variance_right = 0.0;
    for (left, right) in pairs {
        let a = value(left) - mean_left;
        let b = value(right) - mean_right;
        covariance += a * b;
        variance_left += a * a;
        variance_right += b * b;
    }
    let denominator = (variance_left * variance_right).sqrt();
    (denominator > 1e-12).then_some(covariance / denominator)
}
