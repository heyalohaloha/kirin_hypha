//! Offline Reference Blind gain facts from sample-aligned A/B PCM.

use ebur128::{EbuR128, Mode};

const MINIMUM_SAMPLE_RATE: u32 = 8_000;
const MAXIMUM_SAMPLE_RATE: u32 = 768_000;
const ACTIVE_FLOOR_LUFS: f64 = -100.0;
const MAXIMUM_LEVEL_DB: f64 = 24.0;
pub const MINIMUM_PAIRED_BLOCKS: usize = 27;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ReferenceGainFacts {
    pub paired_block_count: u64,
    pub paired_loudness_delta_median_millilu: i64,
    pub a_cue_true_peak_millidbtp: i64,
    pub b_cue_true_peak_millidbtp: i64,
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
}
