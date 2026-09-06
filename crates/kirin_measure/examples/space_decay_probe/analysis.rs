//! Shared fixed-window research calculator; not a qualified product definition.
use serde::Serialize;

#[derive(Debug, Serialize)]
pub(crate) struct Fit {
    pub(crate) points: usize,
    pub(crate) slope_db_per_second: f64,
    pub(crate) r_squared: f64,
    pub(crate) observed_fall_db: f64,
    pub(crate) largest_rise_db: f64,
    pub(crate) d20_seconds: Option<f64>,
}

#[derive(Debug, Serialize)]
pub(crate) struct Facts {
    pub(crate) early_db: Option<f64>,
    pub(crate) early_reason: Option<&'static str>,
    pub(crate) fit: Option<Fit>,
    pub(crate) fit_reason: Option<&'static str>,
}

pub(crate) fn boundary(rate: u32, milliseconds: u32) -> Option<usize> {
    usize::try_from((u64::from(rate) * u64::from(milliseconds) + 500) / 1_000).ok()
}

pub(crate) fn power(samples: &[f32], channels: usize) -> f64 {
    samples
        .iter()
        .map(|&value| f64::from(value).powi(2))
        .sum::<f64>()
        / channels as f64
}

pub(crate) fn early(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    floor: f64,
) -> Result<f64, &'static str> {
    let split = boundary(rate, 80).ok_or("boundary_overflow")?;
    let end = boundary(rate, 250).ok_or("boundary_overflow")?;
    if pcm.len() / channels < end {
        return Err("incomplete_window");
    }
    let first = power(&pcm[..split * channels], channels);
    let last = power(&pcm[split * channels..end * channels], channels);
    if first / split as f64 <= floor || last / (end - split) as f64 <= floor {
        return Err("window_at_or_below_supplied_floor");
    }
    Ok(10.0 * (first / last).log10())
}

pub(crate) fn fit(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    start_ms: u32,
    end_ms: u32,
    floor: f64,
) -> Result<Fit, &'static str> {
    if start_ms >= end_ms
        || end_ms > 6_000
        || !start_ms.is_multiple_of(10)
        || !end_ms.is_multiple_of(10)
    {
        return Err("fit_range_invalid");
    }
    if pcm.len() / channels < boundary(rate, end_ms).ok_or("boundary_overflow")? {
        return Err("incomplete_window");
    }
    let mut points = Vec::new();
    for ms in (start_ms..end_ms).step_by(10) {
        // Independently round each boundary from the fixed origin, never accumulate rounded hops.
        let first = boundary(rate, ms).ok_or("boundary_overflow")?;
        let end = boundary(rate, ms + 10).ok_or("boundary_overflow")?;
        let average =
            power(&pcm[first * channels..end * channels], channels) / (end - first) as f64;
        if average <= floor {
            return Err("fit_at_or_below_supplied_floor");
        }
        let seconds = (first as f64 + end as f64) * 0.5 / f64::from(rate);
        points.push((seconds, 10.0 * average.log10()));
    }
    if points.len() < 10 {
        return Err("fewer_than_ten_points");
    }
    let count = points.len() as f64;
    let mx = points.iter().map(|p| p.0).sum::<f64>() / count;
    let my = points.iter().map(|p| p.1).sum::<f64>() / count;
    let xx = points.iter().map(|p| (p.0 - mx).powi(2)).sum::<f64>();
    let xy = points.iter().map(|p| (p.0 - mx) * (p.1 - my)).sum::<f64>();
    let yy = points.iter().map(|p| (p.1 - my).powi(2)).sum::<f64>();
    let slope = xy / xx;
    let fall = points[0].1 - points[points.len() - 1].1;
    let rise = points
        .windows(2)
        .map(|p| p[1].1 - p[0].1)
        .fold(0.0, f64::max);
    Ok(Fit {
        points: points.len(),
        slope_db_per_second: slope,
        r_squared: if yy > 0.0 {
            (xy * xy / (xx * yy)).clamp(0.0, 1.0)
        } else {
            0.0
        },
        observed_fall_db: fall,
        largest_rise_db: rise,
        d20_seconds: (slope < 0.0 && fall >= 20.0).then(|| 20.0 / -slope),
    })
}

pub(crate) fn analyze(
    pcm: &[f32],
    rate: u32,
    channels: usize,
    start_ms: u32,
    end_ms: u32,
    floor_db: f64,
) -> Result<Facts, &'static str> {
    if !(8_000..=768_000).contains(&rate)
        || !matches!(channels, 1 | 2)
        || !pcm.len().is_multiple_of(channels)
        || pcm.iter().any(|sample| !sample.is_finite())
        || !floor_db.is_finite()
        || !(-300.0..=0.0).contains(&floor_db)
    {
        return Err("invalid_input");
    }
    let floor = 10.0_f64.powf(floor_db / 10.0);
    let early = early(pcm, rate, channels, floor);
    let fit = fit(pcm, rate, channels, start_ms, end_ms, floor);
    Ok(Facts {
        early_db: early.as_ref().ok().copied(),
        early_reason: early.err(),
        fit_reason: fit.as_ref().err().copied(),
        fit: fit.ok(),
    })
}
