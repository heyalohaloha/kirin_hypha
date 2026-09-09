//! Offline Reference Blind gain facts from sample-aligned A/B PCM.

use ebur128::{EbuR128, Mode};

const MINIMUM_SAMPLE_RATE: u32 = 8_000;
const MAXIMUM_SAMPLE_RATE: u32 = 768_000;
const ACTIVE_FLOOR_LUFS: f64 = -100.0;
const MAXIMUM_LEVEL_DB: f64 = 24.0;
pub const MINIMUM_PAIRED_BLOCKS: usize = 27;
pub const TRACK_EVENT_WINDOW_MS: u32 = 20;
pub const MINIMUM_PAIRED_EVENT_WINDOWS: usize = 3;
const TRACK_EVENT_RELATIVE_POWER_FLOOR: f64 = 1.0e-4;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ReferenceGainFacts {
    pub paired_block_count: u64,
    pub paired_loudness_delta_median_millilu: i64,
    pub a_cue_true_peak_millidbtp: i64,
    pub b_cue_true_peak_millidbtp: i64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TrackEventGainFacts {
    pub paired_window_count: u64,
    pub paired_energy_delta_millidb: i64,
    pub post_cue_true_peak_millidbtp: i64,
    pub pre_cue_true_peak_millidbtp: i64,
}

fn checked_level_milli(value: f64) -> Option<i64> {
    (value.is_finite() && value > ACTIVE_FLOOR_LUFS && value <= MAXIMUM_LEVEL_DB)
        .then(|| (value * 1_000.0).round() as i64)
}

fn block_loudness(samples: &[f32], sample_rate: u32, channels: usize) -> Option<f64> {
    let mut meter = EbuR128::new(channels as u32, sample_rate, Mode::M).ok()?;
    meter.add_frames_f32(samples).ok()?;
    meter
        .loudness_momentary()
        .ok()
        .filter(|value| value.is_finite() && *value > ACTIVE_FLOOR_LUFS)
}

fn cue_true_peak(samples: &[f32], sample_rate: u32, channels: usize) -> Option<i64> {
    let mut meter = EbuR128::new(channels as u32, sample_rate, Mode::TRUE_PEAK).ok()?;
    meter.add_frames_f32(samples).ok()?;
    let linear = (0..channels as u32)
        .filter_map(|channel| meter.true_peak(channel).ok())
        .filter(|value| value.is_finite())
        .fold(None::<f64>, |current, value| {
            Some(current.map_or(value, |previous| previous.max(value)))
        })?;
    (linear > 0.0)
        .then(|| 20.0 * linear.log10())
        .and_then(checked_level_milli)
}

fn checked_gain_delta_milli(value: f64) -> Option<i64> {
    (value.is_finite() && value.abs() <= MAXIMUM_LEVEL_DB).then(|| (value * 1_000.0).round() as i64)
}

fn window_power(samples: &[f32]) -> f64 {
    samples
        .iter()
        .map(|sample| f64::from(*sample) * f64::from(*sample))
        .sum::<f64>()
        / samples.len() as f64
}

/// Fixed gain facts for an exact four-second TRACK/STEM capture containing short or sparse events.
///
/// This is deliberately a separate policy from `analyze_reference_gain`: it uses non-overlapping
/// 20 ms energy windows and requires three paired active windows. It never pads or repeats a short
/// event, and it never treats capture absence as source silence.
pub fn analyze_track_event_gain(
    post: &[f32],
    pre: &[f32],
    sample_rate: u32,
    channels: usize,
) -> Result<TrackEventGainFacts, &'static str> {
    if !(MINIMUM_SAMPLE_RATE..=MAXIMUM_SAMPLE_RATE).contains(&sample_rate)
        || !matches!(channels, 1 | 2)
        || post.len() != pre.len()
        || post.is_empty()
        || !post.len().is_multiple_of(channels)
        || post.iter().chain(pre).any(|sample| !sample.is_finite())
    {
        return Err("track_event_gain_input_invalid");
    }

    let frame_count = post.len() / channels;
    let exact_frames = usize::try_from(sample_rate)
        .ok()
        .and_then(|rate| rate.checked_mul(4))
        .ok_or("track_event_gain_duration_invalid")?;
    if frame_count != exact_frames {
        return Err("track_event_gain_duration_invalid");
    }
    let window_count = 4_000usize / TRACK_EVENT_WINDOW_MS as usize;
    if window_count == 0 || frame_count < window_count {
        return Err("track_event_gain_window_invalid");
    }

    // Require activity on both exact sides. The -100 dBFS absolute floor rejects digital silence;
    // the per-side -40 dB relative floor prevents long noise beds from outvoting short events.
    const ACTIVE_POWER_FLOOR: f64 = 1.0e-10;
    let mut powers = Vec::with_capacity(window_count);
    let mut post_peak_power = 0.0f64;
    let mut pre_peak_power = 0.0f64;
    for window in 0..window_count {
        // Integer boundaries cover the exact range once even at unusual integral sample rates.
        let start = frame_count * window / window_count * channels;
        let end = frame_count * (window + 1) / window_count * channels;
        let post_power = window_power(&post[start..end]);
        let pre_power = window_power(&pre[start..end]);
        post_peak_power = post_peak_power.max(post_power);
        pre_peak_power = pre_peak_power.max(pre_power);
        powers.push((post_power, pre_power));
    }
    let post_floor = ACTIVE_POWER_FLOOR.max(post_peak_power * TRACK_EVENT_RELATIVE_POWER_FLOOR);
    let pre_floor = ACTIVE_POWER_FLOOR.max(pre_peak_power * TRACK_EVENT_RELATIVE_POWER_FLOOR);
    let mut deltas = Vec::with_capacity(window_count);
    for (post_power, pre_power) in powers {
        if post_power > post_floor && pre_power > pre_floor {
            deltas.push(
                checked_gain_delta_milli(10.0 * (post_power / pre_power).log10())
                    .ok_or("track_event_gain_delta_invalid")?,
            );
        }
    }
    if deltas.len() < MINIMUM_PAIRED_EVENT_WINDOWS {
        return Err("track_event_gain_evidence_insufficient");
    }
    deltas.sort_unstable();
    let median = if deltas.len().is_multiple_of(2) {
        let high = deltas[deltas.len() / 2];
        let low = deltas[deltas.len() / 2 - 1];
        (low + high) / 2
    } else {
        deltas[deltas.len() / 2]
    };

    Ok(TrackEventGainFacts {
        paired_window_count: deltas.len() as u64,
        paired_energy_delta_millidb: median,
        post_cue_true_peak_millidbtp: cue_true_peak(post, sample_rate, channels)
            .ok_or("track_event_gain_post_true_peak_unavailable")?,
        pre_cue_true_peak_millidbtp: cue_true_peak(pre, sample_rate, channels)
            .ok_or("track_event_gain_pre_true_peak_unavailable")?,
    })
}

pub fn analyze_reference_gain(
    a: &[f32],
    b: &[f32],
    sample_rate: u32,
    channels: usize,
) -> Result<ReferenceGainFacts, &'static str> {
    if !(MINIMUM_SAMPLE_RATE..=MAXIMUM_SAMPLE_RATE).contains(&sample_rate)
        || !matches!(channels, 1 | 2)
        || a.len() != b.len()
        || a.is_empty()
        || !a.len().is_multiple_of(channels)
        || a.iter().chain(b).any(|sample| !sample.is_finite())
    {
        return Err("reference_gain_input_invalid");
    }

    let frame_count = a.len() / channels;
    let block_frames = ((u64::from(sample_rate) * 4 + 5) / 10) as usize;
    let hop_frames = ((u64::from(sample_rate) + 5) / 10) as usize;
    if block_frames == 0 || hop_frames == 0 || frame_count < block_frames {
        return Err("reference_gain_duration_insufficient");
    }

    let mut best_run = Vec::<i64>::new();
    let mut current_run = Vec::<i64>::new();
    for start_frame in (0..=frame_count - block_frames).step_by(hop_frames) {
        let start = start_frame * channels;
        let end = (start_frame + block_frames) * channels;
        match (
            block_loudness(&a[start..end], sample_rate, channels),
            block_loudness(&b[start..end], sample_rate, channels),
        ) {
            (Some(a_loudness), Some(b_loudness)) => {
                let delta = checked_level_milli(a_loudness - b_loudness)
                    .ok_or("reference_gain_delta_invalid")?;
                current_run.push(delta);
            }
            _ => {
                if current_run.len() > best_run.len() {
                    best_run = std::mem::take(&mut current_run);
                } else {
                    current_run.clear();
                }
            }
        }
    }
    if current_run.len() > best_run.len() {
        best_run = current_run;
    }
    if best_run.len() < MINIMUM_PAIRED_BLOCKS {
        return Err("reference_gain_paired_blocks_insufficient");
    }
    best_run.sort_unstable();
    let median = if best_run.len().is_multiple_of(2) {
        let high = best_run[best_run.len() / 2];
        let low = best_run[best_run.len() / 2 - 1];
        (low + high) / 2
    } else {
        best_run[best_run.len() / 2]
    };

    Ok(ReferenceGainFacts {
        paired_block_count: best_run.len() as u64,
        paired_loudness_delta_median_millilu: median,
        a_cue_true_peak_millidbtp: cue_true_peak(a, sample_rate, channels)
            .ok_or("reference_gain_a_true_peak_unavailable")?,
        b_cue_true_peak_millidbtp: cue_true_peak(b, sample_rate, channels)
            .ok_or("reference_gain_b_true_peak_unavailable")?,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn stereo_tone(sample_rate: u32, seconds: usize, amplitude: f32) -> Vec<f32> {
        let frames = sample_rate as usize * seconds;
        let mut result = Vec::with_capacity(frames * 2);
        for frame in 0..frames {
            let sample = amplitude
                * (std::f32::consts::TAU * 997.0 * frame as f32 / sample_rate as f32).sin();
            result.extend_from_slice(&[sample, sample * 0.75]);
        }
        result
    }

    #[test]
    fn aligned_three_seconds_yield_the_required_twenty_seven_blocks() {
        let a = stereo_tone(48_000, 3, 0.2);
        let b = stereo_tone(48_000, 3, 0.1);
        let facts = analyze_reference_gain(&a, &b, 48_000, 2).unwrap();
        assert_eq!(facts.paired_block_count, 27);
        assert!((5_990..=6_050).contains(&facts.paired_loudness_delta_median_millilu));
        assert!((-14_100..=-13_700).contains(&facts.a_cue_true_peak_millidbtp));
        assert!((-20_100..=-19_700).contains(&facts.b_cue_true_peak_millidbtp));
    }

    #[test]
    fn a_gap_breaks_continuity_instead_of_adding_disjoint_blocks() {
        let a = stereo_tone(48_000, 4, 0.2);
        let mut b = stereo_tone(48_000, 4, 0.1);
        b[48_000 * 2..48_000 * 4].fill(0.0);
        assert_eq!(
            analyze_reference_gain(&a, &b, 48_000, 2),
            Err("reference_gain_paired_blocks_insufficient")
        );
    }

    #[test]
    fn malformed_or_nonfinite_pcm_fails_closed() {
        let a = stereo_tone(48_000, 3, 0.2);
        let mut b = stereo_tone(48_000, 3, 0.1);
        b[100] = f32::NAN;
        assert_eq!(
            analyze_reference_gain(&a, &b, 48_000, 2),
            Err("reference_gain_input_invalid")
        );
        assert_eq!(
            analyze_reference_gain(&a, &b[..b.len() - 2], 48_000, 2),
            Err("reference_gain_input_invalid")
        );
    }

    #[test]
    fn exact_track_event_policy_matches_short_and_sparse_gain() {
        let mut post = vec![0.0; 48_000 * 4];
        let tone = stereo_tone(48_000, 1, 0.2);
        for (target, source) in post.iter_mut().take(48_000).zip(tone.iter().step_by(2)) {
            *target = *source;
        }
        let pre: Vec<f32> = post.iter().map(|sample| sample * 0.5).collect();
        let facts = analyze_track_event_gain(&post, &pre, 48_000, 1).unwrap();
        assert_eq!(facts.paired_window_count, 50);
        assert!((6_019..=6_023).contains(&facts.paired_energy_delta_millidb));

        let mut sparse_post = vec![0.0; 48_000 * 4];
        for event in [2_000usize, 51_000, 100_000, 149_000] {
            sparse_post[event..event + 1_440].copy_from_slice(&post[..1_440]);
        }
        let sparse_pre: Vec<f32> = sparse_post.iter().map(|sample| sample * 2.0).collect();
        let sparse = analyze_track_event_gain(&sparse_post, &sparse_pre, 48_000, 1).unwrap();
        assert!(sparse.paired_window_count >= MINIMUM_PAIRED_EVENT_WINDOWS as u64);
        assert!((-6_023..=-6_019).contains(&sparse.paired_energy_delta_millidb));

        let stereo_post = stereo_tone(44_100, 4, 0.2);
        let stereo_pre: Vec<f32> = stereo_post.iter().map(|sample| sample * 0.5).collect();
        let stereo = analyze_track_event_gain(&stereo_post, &stereo_pre, 44_100, 2).unwrap();
        assert_eq!(stereo.paired_window_count, 200);
        assert!((6_019..=6_023).contains(&stereo.paired_energy_delta_millidb));

        let mut noisy_post = post.clone();
        let mut noisy_pre = pre.clone();
        for sample in noisy_post.iter_mut().skip(48_000) {
            *sample = 0.0001;
        }
        for sample in noisy_pre.iter_mut().skip(48_000) {
            *sample = 0.0002;
        }
        let noisy = analyze_track_event_gain(&noisy_post, &noisy_pre, 48_000, 1).unwrap();
        assert_eq!(noisy.paired_window_count, 50);
        assert!((6_019..=6_023).contains(&noisy.paired_energy_delta_millidb));
    }

    #[test]
    fn track_event_policy_rejects_wrong_duration_silence_and_one_sided_evidence() {
        let silence = vec![0.0; 48_000 * 4];
        assert_eq!(
            analyze_track_event_gain(&silence, &silence, 48_000, 1),
            Err("track_event_gain_evidence_insufficient")
        );
        let one_second = vec![0.1; 48_000];
        assert_eq!(
            analyze_track_event_gain(&one_second, &one_second, 48_000, 1),
            Err("track_event_gain_duration_invalid")
        );
        let post = vec![0.1; 48_000 * 4];
        assert_eq!(
            analyze_track_event_gain(&post, &silence, 48_000, 1),
            Err("track_event_gain_evidence_insufficient")
        );
        let mut non_finite = post;
        non_finite[0] = f32::NAN;
        assert_eq!(
            analyze_track_event_gain(&non_finite, &silence, 48_000, 1),
            Err("track_event_gain_input_invalid")
        );
    }
}
