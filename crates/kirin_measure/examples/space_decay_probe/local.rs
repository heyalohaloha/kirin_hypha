//! Multi-threshold local peak-to-trough diagnostics for a human-selected SPACE interval.
//! This module is research-only and does not define acceptance or a product display value.

use serde::Serialize;

use super::analysis::{boundary, power, validate_input};

#[derive(Debug, Serialize)]
pub(crate) struct LocalDecayEpisode {
    pub(crate) peak_window_start_sample: usize,
    pub(crate) trough_window_end_exclusive_sample: usize,
    pub(crate) points: usize,
    pub(crate) observed_fall_db: f64,
    pub(crate) slope_db_per_second: f64,
    pub(crate) r_squared: f64,
    pub(crate) end_reason: &'static str,
}

#[derive(Debug, Serialize)]
pub(crate) struct LocalDecayThreshold {
    pub(crate) recovery_db: f64,
    pub(crate) episodes: Vec<LocalDecayEpisode>,
}

#[derive(Debug, Serialize)]
pub(crate) struct LocalDecayProfile {
    pub(crate) interval_start_sample: usize,
    pub(crate) interval_end_exclusive_sample: usize,
    pub(crate) bin_milliseconds: u32,
    pub(crate) valid_bin_count: usize,
    pub(crate) below_floor_bin_count: usize,
    pub(crate) valid_span_count: usize,
    pub(crate) thresholds: Vec<LocalDecayThreshold>,
}

#[derive(Clone, Copy)]
struct EnvelopeBin {
    start_sample: usize,
    end_sample: usize,
    center_seconds: f64,
    db: f64,
}

fn regression(points: &[EnvelopeBin]) -> (f64, f64) {
    let count = points.len() as f64;
    let mx = points.iter().map(|point| point.center_seconds).sum::<f64>() / count;
    let my = points.iter().map(|point| point.db).sum::<f64>() / count;
    let xx = points
        .iter()
        .map(|point| (point.center_seconds - mx).powi(2))
        .sum::<f64>();
    let xy = points
        .iter()
        .map(|point| (point.center_seconds - mx) * (point.db - my))
        .sum::<f64>();
    let yy = points
        .iter()
        .map(|point| (point.db - my).powi(2))
        .sum::<f64>();
    let slope = xy / xx;
    let r_squared = if yy > 0.0 {
        (xy * xy / (xx * yy)).clamp(0.0, 1.0)
    } else {
        0.0
    };
    (slope, r_squared)
}

fn push_episode(
    episodes: &mut Vec<LocalDecayEpisode>,
    span: &[EnvelopeBin],
    peak: usize,
    trough: usize,
    recovery_db: f64,
    end_reason: &'static str,
) {
    if trough <= peak {
        return;
    }
    let fall = span[peak].db - span[trough].db;
    if fall < recovery_db {
        return;
    }
    let points = &span[peak..=trough];
    let (slope, r_squared) = regression(points);
    episodes.push(LocalDecayEpisode {
        peak_window_start_sample: span[peak].start_sample,
        trough_window_end_exclusive_sample: span[trough].end_sample,
        points: points.len(),
        observed_fall_db: fall,
        slope_db_per_second: slope,
        r_squared,
        end_reason,
    });
}

fn local_episodes(bins: &[Option<EnvelopeBin>], recovery_db: f64) -> Vec<LocalDecayEpisode> {
    let mut episodes = Vec::new();
    let mut offset = 0;
    while offset < bins.len() {
        while offset < bins.len() && bins[offset].is_none() {
            offset += 1;
        }
        let start = offset;
        while offset < bins.len() && bins[offset].is_some() {
            offset += 1;
        }
        if offset - start < 2 {
            continue;
        }
        let span = bins[start..offset]
            .iter()
            .map(|bin| bin.expect("validated contiguous span"))
            .collect::<Vec<_>>();
        let mut peak = 0;
        let mut trough = 0;
        for index in 1..span.len() {
            if span[index].db < span[trough].db {
                trough = index;
            } else if trough > peak && span[index].db - span[trough].db >= recovery_db {
                push_episode(&mut episodes, &span, peak, trough, recovery_db, "recovery");
                peak = index;
                trough = index;
            } else if span[index].db > span[peak].db {
                peak = index;
                trough = index;
            }
        }
        push_episode(
            &mut episodes,
            &span,
            peak,
            trough,
            recovery_db,
            if offset < bins.len() {
                "floor_boundary"
            } else {
                "interval_end"
            },
        );
    }
    episodes
}

pub(crate) fn local_decay_profile(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    start_ms: u32,
    end_ms: u32,
    floor_db: f64,
    recovery_thresholds_db: &[f64],
) -> Result<LocalDecayProfile, &'static str> {
    validate_input(pcm, rate, channels, floor_db)?;
    if start_ms >= end_ms
        || end_ms > 6_000
        || !start_ms.is_multiple_of(10)
        || !end_ms.is_multiple_of(10)
        || recovery_thresholds_db.is_empty()
        || recovery_thresholds_db
            .iter()
            .any(|value| !value.is_finite() || *value <= 0.0 || *value > 60.0)
        || recovery_thresholds_db
            .windows(2)
            .any(|pair| pair[0] >= pair[1])
    {
        return Err("local_profile_parameters_invalid");
    }
    let interval_start = boundary(rate, start_ms).ok_or("boundary_overflow")?;
    let interval_end = boundary(rate, end_ms).ok_or("boundary_overflow")?;
    if pcm.len() / channels < interval_end {
        return Err("incomplete_window");
    }
    let floor = 10.0_f64.powf(floor_db / 10.0);
    let mut bins = Vec::new();
    for ms in (start_ms..end_ms).step_by(10) {
        let first = boundary(rate, ms).ok_or("boundary_overflow")?;
        let end = boundary(rate, ms + 10).ok_or("boundary_overflow")?;
        let average =
            power(&pcm[first * channels..end * channels], channels) / (end - first) as f64;
        bins.push((average > floor).then(|| EnvelopeBin {
            start_sample: first,
            end_sample: end,
            center_seconds: (first as f64 + end as f64) * 0.5 / f64::from(rate),
            db: 10.0 * average.log10(),
        }));
    }
    let valid_span_count = bins
        .iter()
        .enumerate()
        .filter(|(index, bin)| bin.is_some() && (*index == 0 || bins[*index - 1].is_none()))
        .count();
    Ok(LocalDecayProfile {
        interval_start_sample: interval_start,
        interval_end_exclusive_sample: interval_end,
        bin_milliseconds: 10,
        valid_bin_count: bins.iter().filter(|bin| bin.is_some()).count(),
        below_floor_bin_count: bins.iter().filter(|bin| bin.is_none()).count(),
        valid_span_count,
        thresholds: recovery_thresholds_db
            .iter()
            .map(|&recovery_db| LocalDecayThreshold {
                recovery_db,
                episodes: local_episodes(&bins, recovery_db),
            })
            .collect(),
    })
}
