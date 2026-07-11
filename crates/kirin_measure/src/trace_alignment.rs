//! PRE/POST Record TRACE の内容位置 co-registration。
//!
//! `started_at_ms` と native sample count は「同じ Record transaction / 同じ長さ」を
//! 証明するが、Studio One の offline export が plugin-chain latency 用に流す補償
//! pre-roll を WAV 本編から除外したかどうかまでは証明しない。B-356 は pair close
//! 時に LUFS-M と True Peak の形状を独立に照合し、両方が同じ一意 lag を支持した
//! 場合だけ、遅れている側を保持済み render context から canonical WAV axis へ
//! 再構成する。通常 shelf に出る `frames[]` は確定済みの完成品であり、consumer に
//! 時刻補正や利用者の再操作を要求しない。

use serde::{Deserialize, Serialize};

use crate::plugin_data::{Frame, PluginDataFile, Role};

pub const TRACE_CONTENT_ALIGNMENT_METHOD: &str = "lufs_tp_correlation_v1";
pub const TRACE_CONTENT_ALIGNMENT_ALIGNED: &str = "aligned";
pub const TRACE_CONTENT_ALIGNMENT_OFFSET: &str = "offset_detected";
pub const TRACE_CONTENT_ALIGNMENT_CANONICALIZED: &str = "canonicalized";
pub const TRACE_CONTENT_ALIGNMENT_UNVERIFIABLE: &str = "unverifiable";

const FRAME_INTERVAL_MS: i64 = 100;
const MIN_COMPARABLE_FRAMES: usize = 30;
const MIN_CORRELATION: f64 = 0.80;
const MIN_ZERO_IMPROVEMENT: f64 = 0.08;
const MIN_RUNNER_UP_MARGIN: f64 = 0.05;
const SILENCE_FLOOR: f64 = -99.0;

/// Pair-wide content position verdict. The same value is written to PRE and POST.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TraceContentAlignment {
    pub status: String,
    /// Published frames are already canonical, so this is always zero on normal shelf output.
    pub post_offset_ms: i64,
    /// Offset discovered on the captured superset and applied inside Hypha before publish.
    pub source_post_offset_ms: i64,
    pub confidence: f64,
    pub compared_frame_count: u64,
    pub method: String,
}

#[derive(Debug, Clone, Copy)]
struct MetricEstimate {
    best_lag: i32,
    best_correlation: f64,
    zero_correlation: f64,
    zero_compared_frame_count: usize,
    runner_up_correlation: f64,
    compared_frame_count: usize,
}

pub(crate) fn canonicalize_pair_trace_content_alignment(
    left: &mut PluginDataFile,
    right: &mut PluginDataFile,
) -> TraceContentAlignment {
    let (pre, post) = match (left.role, right.role) {
        (Role::Pre, Role::Post) => (left, right),
        (Role::Post, Role::Pre) => (right, left),
        _ => return unverifiable(0, 0.0),
    };
    let pre_source = if pre.trace_context_frames.is_empty() {
        &pre.frames
    } else {
        &pre.trace_context_frames
    };
    let post_source = if post.trace_context_frames.is_empty() {
        &post.frames
    } else {
        &post.trace_context_frames
    };
    let mut alignment = estimate_frames(pre_source, post_source);
    if alignment.status == TRACE_CONTENT_ALIGNMENT_OFFSET {
        let source_offset = alignment.post_offset_ms;
        // Relative offset が負なら POST、正なら PRE が遅れている。同じ内容軸を保つため、
        // 遅れている側だけを前へ詰める。完成尺より後ろの context を保持しているので、
        // どちら向きでも末尾を欠かさず canonical 100 ms grid を再構成できる。
        let (pre_offset, post_offset) = if source_offset < 0 {
            (0, source_offset)
        } else {
            (-source_offset, 0)
        };
        let canonicalized =
            canonicalize_frames(pre, pre_offset) && canonicalize_frames(post, post_offset);
        if canonicalized {
            alignment.status = TRACE_CONTENT_ALIGNMENT_CANONICALIZED.to_string();
            alignment.source_post_offset_ms = source_offset;
            alignment.post_offset_ms = 0;
        }
    } else if alignment.status == TRACE_CONTENT_ALIGNMENT_ALIGNED {
        let _ = canonicalize_frames(pre, 0);
        let _ = canonicalize_frames(post, 0);
    }
    pre.trace_context_frames.clear();
    post.trace_context_frames.clear();
    pre.trace_context_psb_snapshots.clear();
    post.trace_context_psb_snapshots.clear();
    alignment
}

fn canonicalize_frames(data: &mut PluginDataFile, offset_ms: i64) -> bool {
    let end_t_ms = data
        .bounce_take
        .as_ref()
        .map(|take| take.end_t_ms)
        .or_else(|| data.frames.last().map(|frame| frame.t_ms))
        .unwrap_or(0);
    if end_t_ms == 0 {
        return false;
    }
    let source = if data.trace_context_frames.is_empty() {
        &data.frames
    } else {
        &data.trace_context_frames
    };
    let Some(canonical) = canonical_frames_from_source(source, offset_ms, end_t_ms) else {
        return false;
    };
    data.frames = canonical;
    if let Some(take) = data.bounce_take.as_mut() {
        take.frame_count = data.frames.len() as u64;
    }
    let explicit_silence_frame_count = data
        .frames
        .iter()
        .filter(|frame| {
            frame.lufs_m <= -99.9 && frame.true_peak <= -99.9 && frame.crest.abs() <= 0.1
        })
        .count() as u64;
    if let Some(diagnostics) = data.trace_diagnostics.as_mut() {
        diagnostics.expected_frame_count = data.frames.len() as u64;
        diagnostics.measured_frame_count = data.frames.len() as u64;
        diagnostics.missing_slots = 0;
        diagnostics.explicit_silence_frame_count = explicit_silence_frame_count;
    }
    if !data.trace_context_psb_snapshots.is_empty() {
        let mut snapshots = Vec::new();
        for snapshot in &data.trace_context_psb_snapshots {
            let shifted = snapshot.t_ms as i64 + offset_ms;
            if shifted < 0 || shifted as u64 > end_t_ms {
                continue;
            }
            let mut canonical = snapshot.clone();
            canonical.t_ms = shifted as u64;
            snapshots.push(canonical);
        }
        snapshots.sort_by_key(|snapshot| snapshot.t_ms);
        snapshots.dedup_by_key(|snapshot| snapshot.t_ms);
        data.psb_snapshots = Some(snapshots);
    }
    true
}

fn canonical_frames_from_source(
    source: &[Frame],
    offset_ms: i64,
    end_t_ms: u64,
) -> Option<Vec<Frame>> {
    let mut by_time = std::collections::BTreeMap::new();
    for frame in source {
        let shifted = frame.t_ms as i64 + offset_ms;
        if shifted >= FRAME_INTERVAL_MS && shifted as u64 <= end_t_ms {
            let mut canonical = frame.clone();
            canonical.t_ms = shifted as u64;
            by_time.insert(canonical.t_ms, canonical);
        }
    }
    let expected = (end_t_ms / FRAME_INTERVAL_MS as u64) as usize;
    let mut frames = Vec::with_capacity(expected);
    for index in 1..=expected {
        frames.push(by_time.remove(&(index as u64 * FRAME_INTERVAL_MS as u64))?);
    }
    Some(frames)
}

fn estimate_frames(pre: &[Frame], post: &[Frame]) -> TraceContentAlignment {
    let lufs = estimate_metric(pre, post, |frame| frame.lufs_m);
    let peak = estimate_metric(pre, post, |frame| frame.true_peak);
    let (Some(lufs), Some(peak)) = (lufs, peak) else {
        return unverifiable(0, 0.0);
    };
    let compared = lufs.compared_frame_count.min(peak.compared_frame_count);
    let confidence = lufs
        .best_correlation
        .min(peak.best_correlation)
        .clamp(0.0, 1.0);

    let same_nonzero_lag = lufs.best_lag != 0 && lufs.best_lag == peak.best_lag;
    let independently_strong =
        lufs.best_correlation >= MIN_CORRELATION && peak.best_correlation >= MIN_CORRELATION;
    let improves_over_unshifted = lufs.best_correlation - lufs.zero_correlation
        >= MIN_ZERO_IMPROVEMENT
        && peak.best_correlation - peak.zero_correlation >= MIN_ZERO_IMPROVEMENT;
    let unique_peak = lufs.best_correlation - lufs.runner_up_correlation >= MIN_RUNNER_UP_MARGIN
        && peak.best_correlation - peak.runner_up_correlation >= MIN_RUNNER_UP_MARGIN;
    if same_nonzero_lag && independently_strong && improves_over_unshifted && unique_peak {
        return verdict(
            TRACE_CONTENT_ALIGNMENT_OFFSET,
            -(lufs.best_lag as i64) * FRAME_INTERVAL_MS,
            confidence,
            compared,
        );
    }

    // 周期素材では遠い別周期が 0 lag を僅かに上回ることがある。非 0 lag が上記の
    // 独立・改善・一意性を満たさず、0 lag 自体が両 metric で強ければ、開始境界を
    // 保持する 0 lag を正本とする。これにより通常の Drum を誤って未確定にしない。
    if lufs.zero_correlation >= MIN_CORRELATION && peak.zero_correlation >= MIN_CORRELATION {
        return verdict(
            TRACE_CONTENT_ALIGNMENT_ALIGNED,
            0,
            lufs.zero_correlation
                .min(peak.zero_correlation)
                .clamp(0.0, 1.0),
            lufs.zero_compared_frame_count
                .min(peak.zero_compared_frame_count),
        );
    }

    unverifiable(compared, confidence)
}

fn estimate_metric(
    pre: &[Frame],
    post: &[Frame],
    value: impl Fn(&Frame) -> f64,
) -> Option<MetricEstimate> {
    let mut candidates = Vec::new();
    let max_lag = pre
        .len()
        .min(post.len())
        .saturating_sub(MIN_COMPARABLE_FRAMES) as i32;
    for lag in -max_lag..=max_lag {
        if let Some((correlation, count)) = pearson_at_lag(pre, post, lag, &value) {
            candidates.push((lag, correlation, count));
        }
    }
    let (zero_correlation, zero_compared_frame_count) = candidates
        .iter()
        .find(|(lag, _, _)| *lag == 0)
        .map(|(_, correlation, count)| (*correlation, *count))?;
    candidates.sort_by(|a, b| {
        b.1.total_cmp(&a.1)
            .then_with(|| a.0.abs().cmp(&b.0.abs()))
            .then_with(|| a.0.cmp(&b.0))
    });
    let best = *candidates.first()?;
    let runner_up = candidates.get(1).map_or(-1.0, |candidate| candidate.1);
    Some(MetricEstimate {
        best_lag: best.0,
        best_correlation: best.1,
        zero_correlation,
        zero_compared_frame_count,
        runner_up_correlation: runner_up,
        compared_frame_count: best.2,
    })
}

fn pearson_at_lag(
    pre: &[Frame],
    post: &[Frame],
    lag: i32,
    value: &impl Fn(&Frame) -> f64,
) -> Option<(f64, usize)> {
    let mut pairs = Vec::new();
    for (pre_index, pre_frame) in pre.iter().enumerate() {
        let post_index = pre_index as i64 + lag as i64;
        if post_index < 0 || post_index >= post.len() as i64 {
            continue;
        }
        let pre_value = value(pre_frame);
        let post_value = value(&post[post_index as usize]);
        if !pre_value.is_finite()
            || !post_value.is_finite()
            || pre_value <= SILENCE_FLOOR
            || post_value <= SILENCE_FLOOR
        {
            continue;
        }
        pairs.push((pre_value, post_value));
    }
    if pairs.len() < MIN_COMPARABLE_FRAMES {
        return None;
    }
    let count = pairs.len();
    let mean_pre = pairs.iter().map(|pair| pair.0).sum::<f64>() / count as f64;
    let mean_post = pairs.iter().map(|pair| pair.1).sum::<f64>() / count as f64;
    let mut covariance = 0.0;
    let mut variance_pre = 0.0;
    let mut variance_post = 0.0;
    for (pre_value, post_value) in pairs {
        let pre_delta = pre_value - mean_pre;
        let post_delta = post_value - mean_post;
        covariance += pre_delta * post_delta;
        variance_pre += pre_delta * pre_delta;
        variance_post += post_delta * post_delta;
    }
    let denominator = (variance_pre * variance_post).sqrt();
    if denominator <= f64::EPSILON {
        return None;
    }
    Some(((covariance / denominator).clamp(-1.0, 1.0), count))
}

fn verdict(
    status: &str,
    post_offset_ms: i64,
    confidence: f64,
    compared_frame_count: usize,
) -> TraceContentAlignment {
    TraceContentAlignment {
        status: status.to_string(),
        post_offset_ms,
        source_post_offset_ms: 0,
        confidence,
        compared_frame_count: compared_frame_count as u64,
        method: TRACE_CONTENT_ALIGNMENT_METHOD.to_string(),
    }
}

fn unverifiable(compared_frame_count: usize, confidence: f64) -> TraceContentAlignment {
    verdict(
        TRACE_CONTENT_ALIGNMENT_UNVERIFIABLE,
        0,
        confidence,
        compared_frame_count,
    )
}

#[cfg(test)]
mod canonicalization_tests;

#[cfg(test)]
mod tests {
    use super::{
        estimate_frames, TRACE_CONTENT_ALIGNMENT_ALIGNED, TRACE_CONTENT_ALIGNMENT_OFFSET,
        TRACE_CONTENT_ALIGNMENT_UNVERIFIABLE,
    };
    use crate::plugin_data::Frame;

    fn frame(index: usize, lufs: f64, peak: f64) -> Frame {
        Frame {
            t_ms: (index as u64 + 1) * 100,
            n_prime: Some([0.0; 20]),
            sharpness: Some(0.0),
            lufs_m: lufs,
            true_peak: peak,
            crest: 10.0,
            psr: None,
        }
    }

    fn source(index: usize) -> (f64, f64) {
        let x = index as f64;
        let lufs_jitter = ((index * 37) % 29) as f64 - 14.0;
        let peak_jitter = ((index * 53) % 31) as f64 - 15.0;
        let lufs = -22.0 + (x * 0.37).sin() * 4.0 + lufs_jitter * 0.45;
        let peak = -8.0 + (x * 0.53).sin() * 3.0 + peak_jitter * 0.30;
        (lufs, peak)
    }

    #[test]
    fn aligned_pair_keeps_zero_offset() {
        let pre: Vec<_> = (0..120)
            .map(|index| {
                let (lufs, peak) = source(index);
                frame(index, lufs, peak)
            })
            .collect();
        let post: Vec<_> = (0_usize..120)
            .map(|index| {
                let (lufs, peak) = source(index);
                frame(index, lufs + 4.0, peak - 2.0)
            })
            .collect();
        let alignment = estimate_frames(&pre, &post);
        assert_eq!(alignment.status, TRACE_CONTENT_ALIGNMENT_ALIGNED);
        assert_eq!(alignment.post_offset_ms, 0);
        assert!(alignment.confidence > 0.99);
    }

    #[test]
    fn periodic_pair_keeps_capture_boundary_when_another_cycle_is_not_unique() {
        let pre: Vec<_> = (0..120)
            .map(|index| {
                let cycle = index % 20;
                let (lufs, peak) = source(cycle);
                frame(index, lufs, peak)
            })
            .collect();
        let post: Vec<_> = (0..120)
            .map(|index| {
                let cycle = index % 20;
                let (lufs, peak) = source(cycle);
                let edge_noise = if index < 20 {
                    ((index * 7) % 9) as f64 * 0.12
                } else {
                    0.0
                };
                frame(index, lufs + edge_noise, peak - edge_noise)
            })
            .collect();

        let alignment = estimate_frames(&pre, &post);

        assert_eq!(alignment.status, TRACE_CONTENT_ALIGNMENT_ALIGNED);
        assert_eq!(alignment.post_offset_ms, 0);
        assert!(alignment.confidence >= 0.8);
    }

    #[test]
    fn one_slot_compensation_preroll_is_detected() {
        let pre: Vec<_> = (0..120)
            .map(|index| {
                let (lufs, peak) = source(index);
                frame(index, lufs, peak)
            })
            .collect();
        let mut post = vec![frame(0, -100.0, -100.0)];
        post.extend((0..119).map(|index| {
            let (lufs, peak) = source(index);
            frame(index + 1, lufs + 4.0, peak - 2.0)
        }));
        let alignment = estimate_frames(&pre, &post);
        assert_eq!(alignment.status, TRACE_CONTENT_ALIGNMENT_OFFSET);
        assert_eq!(alignment.post_offset_ms, -100);
        assert!(alignment.confidence > 0.99);
    }

    #[test]
    fn short_or_constant_pair_is_not_guessed() {
        let short: Vec<_> = (0..20).map(|index| frame(index, -20.0, -3.0)).collect();
        let alignment = estimate_frames(&short, &short);
        assert_eq!(alignment.status, TRACE_CONTENT_ALIGNMENT_UNVERIFIABLE);
        assert_eq!(alignment.post_offset_ms, 0);
    }

    #[test]
    fn metrics_disagreeing_on_lag_are_not_shifted() {
        let pre: Vec<_> = (0..120)
            .map(|index| {
                let (lufs, peak) = source(index);
                frame(index, lufs, peak)
            })
            .collect();
        let post: Vec<_> = (0_usize..120)
            .map(|index| {
                let lufs_source = index.saturating_sub(1);
                let (lufs, _) = source(lufs_source);
                let (_, peak) = source(index);
                frame(index, lufs, peak)
            })
            .collect();
        let alignment = estimate_frames(&pre, &post);
        assert_eq!(alignment.status, TRACE_CONTENT_ALIGNMENT_UNVERIFIABLE);
        assert_eq!(alignment.post_offset_ms, 0);
    }
}
