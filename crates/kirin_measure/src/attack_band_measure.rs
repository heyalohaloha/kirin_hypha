//! The band measurement of one hit: the octave filter, a one-period envelope, and every outcome
//! stated (rises, rings on, silent; start timed or hidden by a ring-out; fall timed, cut by the
//! next hit, or longer than the measured tail).

use super::*;

/// Work buffers reused across hits so a measurement allocates nothing after the first.
#[derive(Default)]
pub(crate) struct BandScratch {
    pub(crate) audio: Vec<f32>,
    channel: Vec<f32>,
    filtered: Vec<f64>,
    power: Vec<f64>,
    prefix: Vec<f64>,
}

/// Measures one band of one hit from `audio`, interleaved frames covering exactly
/// [`analysis_range`]. `None` only for input that cannot describe a hit.
#[allow(clippy::too_many_arguments)]
pub(crate) fn measure_band(
    band: AttackBand,
    sample_rate: u32,
    channels: usize,
    onset: i64,
    span_end: i64,
    span_reason: BandSpanEnd,
    audio: &[f32],
    scratch: &mut BandScratch,
) -> Option<AttackBandMeasure> {
    let (analysis_start, analysis_end) = analysis_range(band, sample_rate, onset, span_end);
    let frames = usize::try_from(analysis_end - analysis_start).ok()?;
    if !matches!(channels, 1 | 2) || audio.len() != frames * channels || frames < 2 {
        return None;
    }
    let filter = BandFilter::new(band, sample_rate);
    scratch.power.clear();
    scratch.power.resize(frames, 0.0);
    for channel in 0..channels {
        scratch.channel.clear();
        scratch
            .channel
            .extend(audio.iter().skip(channel).step_by(channels).copied());
        filter.run(&scratch.channel, &mut scratch.filtered);
        for (power, value) in scratch.power.iter_mut().zip(&scratch.filtered) {
            *power += value * value / channels as f64;
        }
    }
    // Prefix sums give the mean power over any window in constant time.
    scratch.prefix.clear();
    scratch.prefix.reserve(frames + 1);
    scratch.prefix.push(0.0);
    let mut sum = 0.0;
    for power in &scratch.power {
        sum += power;
        scratch.prefix.push(sum);
    }
    let window = band.period_frames(sample_rate).max(1) as usize;
    let envelope = |frame: i64| -> f64 {
        let index = (frame - analysis_start).clamp(0, frames as i64 - 1) as usize;
        let from = index.saturating_sub(window / 2);
        let to = (from + window).min(frames);
        let from = to.saturating_sub(window);
        ((scratch.prefix[to] - scratch.prefix[from]) / (to - from) as f64).sqrt()
    };
    let floor = 10.0_f64.powf(f64::from(ATTACK_LEVEL_FLOOR_DBFS) / 20.0);
    let dbfs = |amplitude: f64| (20.0 * amplitude.max(floor).log10()) as f32;

    let lead = frames_for_micros(sample_rate, ATTACK_BAND_HEAD_LEAD_MICROS);
    let search_start = onset - lead;
    let search_end =
        span_end.min(onset + frames_for_micros(sample_rate, ATTACK_BAND_PEAK_SEARCH_MICROS));
    let mut peak_frame = search_start;
    let mut peak = 0.0_f64;
    for frame in search_start..search_end {
        let amplitude = envelope(frame);
        if amplitude > peak {
            peak = amplitude;
            peak_frame = frame;
        }
    }
    let level_dbfs = dbfs(peak);
    // Where the envelope crosses `threshold` between two frames, as a fractional frame.
    let crossing = |before: i64, after: i64, threshold: f64| -> f32 {
        let (low, high) = (envelope(before), envelope(after));
        let fraction = if high > low {
            ((threshold - low) / (high - low)).clamp(0.0, 1.0)
        } else {
            0.0
        };
        (before - onset) as f32 + fraction as f32
    };
    let backward = |threshold: f64| -> Option<f32> {
        (search_start..peak_frame)
            .rev()
            .find(|frame| envelope(*frame) < threshold)
            .map(|frame| crossing(frame, frame + 1, threshold))
    };
    let start_level = envelope(search_start);
    let rises =
        start_level <= 0.0 || 20.0 * (peak / start_level).log10() >= f64::from(ATTACK_BAND_RISE_DB);
    let sound = if level_dbfs <= ATTACK_BAND_PRESENCE_FLOOR_DBFS {
        BandSound::Silent
    } else if !rises {
        BandSound::RingsOn
    } else {
        let arrival = match backward(peak * ARRIVAL_AMPLITUDE_RATIO) {
            Some(arrival_frames) => BandArrival::At {
                arrival_frames,
                attack_frames: backward(peak * ATTACK_UPPER_AMPLITUDE_RATIO)
                    .map_or(0.0, |rise_end| (rise_end - arrival_frames).max(0.0)),
            },
            None => BandArrival::Ringing,
        };
        let release = (peak_frame + 1..span_end)
            .find(|frame| envelope(*frame) <= peak * RELEASE_AMPLITUDE_RATIO)
            .map(|frame| {
                let (high, low) = (envelope(frame - 1), envelope(frame));
                let fraction = if high > low {
                    ((high - peak * RELEASE_AMPLITUDE_RATIO) / (high - low)).clamp(0.0, 1.0)
                } else {
                    1.0
                };
                BandRelease::At((frame - 1 - peak_frame) as f32 + fraction as f32)
            })
            .unwrap_or(match span_reason {
                BandSpanEnd::NextHit => BandRelease::CutByNextHit,
                BandSpanEnd::Window | BandSpanEnd::AudioEnd => {
                    BandRelease::AtLeast((span_end - peak_frame) as f32)
                }
            });
        BandSound::Rises { arrival, release }
    };

    let head_span = frames_for_micros(sample_rate, ATTACK_BAND_HEAD_SPAN_MICROS);
    let tail_span = frames_for_micros(sample_rate, ATTACK_BAND_TAIL_MICROS);
    let mut shape = BandEnvelope::default();
    for (index, point) in shape.head.iter_mut().enumerate() {
        let frame = search_start
            + (head_span * (2 * index as i64 + 1)) / (2 * ATTACK_BAND_HEAD_POINTS as i64);
        if frame < span_end {
            *point = centi_db(dbfs(envelope(frame)));
        }
    }
    for (index, point) in shape.tail.iter_mut().enumerate() {
        let frame =
            onset + (tail_span * (2 * index as i64 + 1)) / (2 * ATTACK_BAND_TAIL_POINTS as i64);
        if frame < span_end {
            *point = centi_db(dbfs(envelope(frame)));
        }
    }
    let measure = AttackBandMeasure {
        band,
        sample_rate,
        channels: channels as u8,
        event_sample: onset,
        span_end_sample: span_end,
        span_end: span_reason,
        peak_frames: (peak_frame - onset) as f32,
        level_dbfs,
        sound,
        envelope: shape,
    };
    measure.has_valid_layout().then_some(measure)
}

/// One hit measured from the kept audio; `None` when any of its analysis range is not kept.
#[allow(clippy::too_many_arguments)]
pub(crate) fn measure_from_ring(
    ring: &AttackBandRing,
    band: AttackBand,
    sample_rate: u32,
    channels: usize,
    onset: i64,
    span_end: i64,
    span_reason: BandSpanEnd,
    scratch: &mut BandScratch,
) -> Option<AttackBandMeasure> {
    let (from, to) = analysis_range(band, sample_rate, onset, span_end);
    let mut audio = std::mem::take(&mut scratch.audio);
    let copied = ring.copy_frames(from, to, &mut audio);
    let measure = copied
        .then(|| {
            measure_band(
                band,
                sample_rate,
                channels,
                onset,
                span_end,
                span_reason,
                &audio,
                scratch,
            )
        })
        .flatten();
    scratch.audio = audio;
    measure
}

/// POST minus PRE, in frames: when each side's band rises through its own peak - 20 dB. Both
/// must rise at the hit with a timed start.
pub fn band_delay_frames(pre: &AttackBandMeasure, post: &AttackBandMeasure) -> Option<f32> {
    let arrival = |measure: &AttackBandMeasure| match measure.sound {
        BandSound::Rises {
            arrival: BandArrival::At { arrival_frames, .. },
            ..
        } => Some(arrival_frames),
        _ => None,
    };
    Some(arrival(post)? - arrival(pre)?)
}
