//! Deterministic broadband SPACE decay facts.
//!
//! This module defines calculation only. It does not infer reverberation, depth, RT60, or a
//! quality judgement, and it does not select musical intervals. Callers must preserve the exact
//! interval, floor, and event identity used for PRE/POST comparison.

use serde::Serialize;

pub const SPACE_DECAY_CALCULATOR_DEFINITION_ID: &str = "hypha.space.broadband-fixed-window.v2";
pub const SPACE_DECAY_BIN_MILLISECONDS: u32 = 10;
pub const SPACE_EARLY_SPLIT_MILLISECONDS: u32 = 80;
pub const SPACE_EARLY_END_MILLISECONDS: u32 = 250;
pub const SPACE_DECAY_MAX_INTERVAL_MILLISECONDS: u32 = 6_000;
pub const SPACE_D20_OBSERVED_FALL_DB: f64 = 20.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SpaceD20Policy {
    pub minimum_points: usize,
    pub minimum_r_squared: f64,
    pub maximum_rise_db: f64,
}

impl SpaceD20Policy {
    pub fn validate(self) -> Result<Self, &'static str> {
        if self.minimum_points < 10
            || !self.minimum_r_squared.is_finite()
            || !(0.0..=1.0).contains(&self.minimum_r_squared)
            || !self.maximum_rise_db.is_finite()
            || !(0.0..=20.0).contains(&self.maximum_rise_db)
        {
            return Err("invalid_d20_policy");
        }
        Ok(self)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
pub enum SpaceD20Rejection {
    InvalidPolicy,
    FewerThanRequiredPoints,
    NonNegativeSlope,
    InsufficientObservedFall,
    ExcessiveRise,
    RegressionFit,
}

impl SpaceD20Rejection {
    pub const fn as_str(self) -> &'static str {
        match self {
            Self::InvalidPolicy => "invalid_d20_policy",
            Self::FewerThanRequiredPoints => "fewer_than_required_points",
            Self::NonNegativeSlope => "non_negative_slope",
            Self::InsufficientObservedFall => "insufficient_observed_fall",
            Self::ExcessiveRise => "excessive_rise",
            Self::RegressionFit => "regression_fit",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
pub struct SpaceDecayFit {
    pub points: usize,
    pub slope_db_per_second: f64,
    pub r_squared: f64,
    pub observed_fall_db: f64,
    pub largest_rise_db: f64,
    /// Twenty-decibel equivalent time from the fitted slope. This is present only after an
    /// actual endpoint fall of at least 20 dB; it is not ISO T20 or RT60.
    pub d20_seconds: Option<f64>,
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
pub struct SpaceDecayFacts {
    pub early_db: Option<f64>,
    pub early_reason: Option<&'static str>,
    pub fit: Option<SpaceDecayFit>,
    pub fit_reason: Option<&'static str>,
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize)]
pub struct LocalDecayEpisode {
    pub peak_window_start_sample: usize,
    pub trough_window_end_exclusive_sample: usize,
    pub points: usize,
    pub observed_fall_db: f64,
    pub slope_db_per_second: f64,
    pub r_squared: f64,
    pub end_reason: &'static str,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct LocalDecayThreshold {
    pub recovery_db: f64,
    pub episodes: Vec<LocalDecayEpisode>,
}

#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct LocalDecayProfile {
    pub interval_start_sample: usize,
    pub interval_end_exclusive_sample: usize,
    pub bin_milliseconds: u32,
    pub valid_bin_count: usize,
    pub below_floor_bin_count: usize,
    pub valid_span_count: usize,
    pub thresholds: Vec<LocalDecayThreshold>,
}

#[derive(Clone, Copy)]
struct EnvelopeBin {
    start_sample: usize,
    end_sample: usize,
    center_seconds: f64,
    db: f64,
}

pub fn space_decay_boundary(rate: u32, milliseconds: u32) -> Option<usize> {
    usize::try_from((u64::from(rate) * u64::from(milliseconds) + 500) / 1_000).ok()
}

pub fn space_decay_power(samples: &[f32], channels: usize) -> f64 {
    samples
        .iter()
        .map(|&value| f64::from(value).powi(2))
        .sum::<f64>()
        / channels as f64
}

pub fn validate_space_decay_input(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    floor_db: f64,
) -> Result<(), &'static str> {
    if !(8_000..=768_000).contains(&rate)
        || !matches!(channels, 1 | 2)
        || !pcm.len().is_multiple_of(channels)
        || pcm.iter().any(|sample| !sample.is_finite())
        || !floor_db.is_finite()
        || !(-300.0..=0.0).contains(&floor_db)
    {
        return Err("invalid_input");
    }
    Ok(())
}

pub fn space_early(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    floor: f64,
) -> Result<f64, &'static str> {
    let split =
        space_decay_boundary(rate, SPACE_EARLY_SPLIT_MILLISECONDS).ok_or("boundary_overflow")?;
    let end =
        space_decay_boundary(rate, SPACE_EARLY_END_MILLISECONDS).ok_or("boundary_overflow")?;
    if pcm.len() / channels < end {
        return Err("incomplete_window");
    }
    let first = space_decay_power(&pcm[..split * channels], channels);
    let last = space_decay_power(&pcm[split * channels..end * channels], channels);
    if first / split as f64 <= floor || last / (end - split) as f64 <= floor {
        return Err("window_at_or_below_supplied_floor");
    }
    Ok(10.0 * (first / last).log10())
}

pub fn fit_space_decay(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    start_ms: u32,
    end_ms: u32,
    floor: f64,
) -> Result<SpaceDecayFit, &'static str> {
    if start_ms >= end_ms
        || end_ms > SPACE_DECAY_MAX_INTERVAL_MILLISECONDS
        || !start_ms.is_multiple_of(SPACE_DECAY_BIN_MILLISECONDS)
        || !end_ms.is_multiple_of(SPACE_DECAY_BIN_MILLISECONDS)
    {
        return Err("fit_range_invalid");
    }
    if pcm.len() / channels < space_decay_boundary(rate, end_ms).ok_or("boundary_overflow")? {
        return Err("incomplete_window");
    }
    let mut points = Vec::new();
    for ms in (start_ms..end_ms).step_by(SPACE_DECAY_BIN_MILLISECONDS as usize) {
        let first = space_decay_boundary(rate, ms).ok_or("boundary_overflow")?;
        let end = space_decay_boundary(rate, ms + SPACE_DECAY_BIN_MILLISECONDS)
            .ok_or("boundary_overflow")?;
        let average = space_decay_power(&pcm[first * channels..end * channels], channels)
            / (end - first) as f64;
        if average <= floor {
            return Err("fit_at_or_below_supplied_floor");
        }
        points.push((
            (first as f64 + end as f64) * 0.5 / f64::from(rate),
            10.0 * average.log10(),
        ));
    }
    if points.len() < 10 {
        return Err("fewer_than_ten_points");
    }
    let count = points.len() as f64;
    let mean_x = points.iter().map(|point| point.0).sum::<f64>() / count;
    let mean_y = points.iter().map(|point| point.1).sum::<f64>() / count;
    let xx = points
        .iter()
        .map(|point| (point.0 - mean_x).powi(2))
        .sum::<f64>();
    let xy = points
        .iter()
        .map(|point| (point.0 - mean_x) * (point.1 - mean_y))
        .sum::<f64>();
    let yy = points
        .iter()
        .map(|point| (point.1 - mean_y).powi(2))
        .sum::<f64>();
    let slope = xy / xx;
    let observed_fall = points[0].1 - points[points.len() - 1].1;
    let largest_rise = points
        .windows(2)
        .map(|pair| pair[1].1 - pair[0].1)
        .fold(0.0, f64::max);
    Ok(SpaceDecayFit {
        points: points.len(),
        slope_db_per_second: slope,
        r_squared: if yy > 0.0 {
            (xy * xy / (xx * yy)).clamp(0.0, 1.0)
        } else {
            0.0
        },
        observed_fall_db: observed_fall,
        largest_rise_db: largest_rise,
        d20_seconds: (slope < 0.0 && observed_fall >= SPACE_D20_OBSERVED_FALL_DB)
            .then(|| SPACE_D20_OBSERVED_FALL_DB / -slope),
    })
}

/// Applies an explicit caller-owned acceptance policy to a calculated fit. No default policy is
/// provided: the evaluation version and coefficients must be pinned by the route that uses them.
pub fn qualify_space_d20(
    fit: &SpaceDecayFit,
    policy: SpaceD20Policy,
) -> Result<f64, SpaceD20Rejection> {
    let policy = policy
        .validate()
        .map_err(|_| SpaceD20Rejection::InvalidPolicy)?;
    if fit.points < policy.minimum_points {
        return Err(SpaceD20Rejection::FewerThanRequiredPoints);
    }
    if fit.slope_db_per_second >= 0.0 {
        return Err(SpaceD20Rejection::NonNegativeSlope);
    }
    if fit.observed_fall_db < SPACE_D20_OBSERVED_FALL_DB {
        return Err(SpaceD20Rejection::InsufficientObservedFall);
    }
    if fit.largest_rise_db > policy.maximum_rise_db {
        return Err(SpaceD20Rejection::ExcessiveRise);
    }
    if fit.r_squared < policy.minimum_r_squared {
        return Err(SpaceD20Rejection::RegressionFit);
    }
    fit.d20_seconds
        .ok_or(SpaceD20Rejection::InsufficientObservedFall)
}

pub fn analyze_space_decay(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    start_ms: u32,
    end_ms: u32,
    floor_db: f64,
) -> Result<SpaceDecayFacts, &'static str> {
    validate_space_decay_input(pcm, rate, channels, floor_db)?;
    let floor = 10.0_f64.powf(floor_db / 10.0);
    let early = space_early(pcm, rate, channels, floor);
    let fit = fit_space_decay(pcm, rate, channels, start_ms, end_ms, floor);
    Ok(SpaceDecayFacts {
        early_db: early.as_ref().ok().copied(),
        early_reason: early.err(),
        fit_reason: fit.as_ref().err().copied(),
        fit: fit.ok(),
    })
}

fn regression(points: &[EnvelopeBin]) -> (f64, f64) {
    let count = points.len() as f64;
    let mean_x = points.iter().map(|point| point.center_seconds).sum::<f64>() / count;
    let mean_y = points.iter().map(|point| point.db).sum::<f64>() / count;
    let xx = points
        .iter()
        .map(|point| (point.center_seconds - mean_x).powi(2))
        .sum::<f64>();
    let xy = points
        .iter()
        .map(|point| (point.center_seconds - mean_x) * (point.db - mean_y))
        .sum::<f64>();
    let yy = points
        .iter()
        .map(|point| (point.db - mean_y).powi(2))
        .sum::<f64>();
    (
        xy / xx,
        if yy > 0.0 {
            (xy * xy / (xx * yy)).clamp(0.0, 1.0)
        } else {
            0.0
        },
    )
}

fn push_episode(
    episodes: &mut Vec<LocalDecayEpisode>,
    span: &[EnvelopeBin],
    peak: usize,
    trough: usize,
    recovery_db: f64,
    end_reason: &'static str,
) {
    if trough <= peak || span[peak].db - span[trough].db < recovery_db {
        return;
    }
    let points = &span[peak..=trough];
    let (slope_db_per_second, r_squared) = regression(points);
    episodes.push(LocalDecayEpisode {
        peak_window_start_sample: span[peak].start_sample,
        trough_window_end_exclusive_sample: span[trough].end_sample,
        points: points.len(),
        observed_fall_db: span[peak].db - span[trough].db,
        slope_db_per_second,
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

pub fn local_decay_profile(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    start_ms: u32,
    end_ms: u32,
    floor_db: f64,
    recovery_thresholds_db: &[f64],
) -> Result<LocalDecayProfile, &'static str> {
    validate_space_decay_input(pcm, rate, channels, floor_db)?;
    if start_ms >= end_ms
        || end_ms > SPACE_DECAY_MAX_INTERVAL_MILLISECONDS
        || !start_ms.is_multiple_of(SPACE_DECAY_BIN_MILLISECONDS)
        || !end_ms.is_multiple_of(SPACE_DECAY_BIN_MILLISECONDS)
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
    let interval_start = space_decay_boundary(rate, start_ms).ok_or("boundary_overflow")?;
    let interval_end = space_decay_boundary(rate, end_ms).ok_or("boundary_overflow")?;
    if pcm.len() / channels < interval_end {
        return Err("incomplete_window");
    }
    let floor = 10.0_f64.powf(floor_db / 10.0);
    let mut bins = Vec::new();
    for ms in (start_ms..end_ms).step_by(SPACE_DECAY_BIN_MILLISECONDS as usize) {
        let first = space_decay_boundary(rate, ms).ok_or("boundary_overflow")?;
        let end = space_decay_boundary(rate, ms + SPACE_DECAY_BIN_MILLISECONDS)
            .ok_or("boundary_overflow")?;
        let average = space_decay_power(&pcm[first * channels..end * channels], channels)
            / (end - first) as f64;
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
        bin_milliseconds: SPACE_DECAY_BIN_MILLISECONDS,
        valid_bin_count: bins.iter().flatten().count(),
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

#[cfg(test)]
#[path = "space_decay_tests.rs"]
mod tests;
