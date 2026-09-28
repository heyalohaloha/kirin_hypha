//! One octave band of a DRUM hit: where the band arrives, how long it rises, how long it rings
//! and how loud it peaks, measured on PRE and POST at one onset so the two can be compared.
//!
//! Nothing here runs unless a band is chosen. The audio ring is allocated on the ATTACK worker
//! only while a band is selected, each confirmed hit is measured once for that one band, and the
//! Audio Thread is untouched. A band has an octave's worth of time resolution: about one period
//! of its centre frequency (16 ms at 63 Hz, 0.125 ms at 8 kHz), which every measure carries.
//!
//! Bands follow IEC 61260's base-two octave series with ISO 266 nominal labels: 63, 125, 250,
//! 500, 1k, 2k, 4k and 8k Hz, each from centre / √2 to centre × √2, so they tile without gaps.

use std::collections::VecDeque;

use super::ATTACK_LEVEL_FLOOR_DBFS;

pub const ATTACK_BAND_COUNT: u8 = 8;
/// The head envelope sent to the other side and drawn: [onset - 20 ms, onset + 40 ms).
pub const ATTACK_BAND_HEAD_POINTS: usize = 96;
/// The tail envelope: [onset, onset + 300 ms).
pub const ATTACK_BAND_TAIL_POINTS: usize = 64;
pub const ATTACK_BAND_HEAD_LEAD_MICROS: i64 = 20_000;
pub const ATTACK_BAND_HEAD_SPAN_MICROS: i64 = 60_000;
pub const ATTACK_BAND_TAIL_MICROS: i64 = 300_000;
/// The peak is looked for where the loupe looks: up to 130 ms after the onset.
pub const ATTACK_BAND_PEAK_SEARCH_MICROS: i64 = 130_000;
/// A band whose peak stays below this has nothing to measure in this hit.
pub const ATTACK_BAND_PRESENCE_FLOOR_DBFS: f32 = -72.0;
pub const ATTACK_BAND_HISTORY_CAPACITY: usize = 64;
/// The ring reaches back as far as the bins do, so POST can be measured at a PRE onset.
pub const ATTACK_BAND_RETENTION_MICROS: i64 = 7_000_000;
const SETTLE_PERIODS: i64 = 4;

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct AttackBand {
    index: u8,
}

impl AttackBand {
    /// 1 = 63 Hz … 8 = 8 kHz. 0 is no band.
    pub fn from_index(index: u8) -> Option<Self> {
        (1..=ATTACK_BAND_COUNT)
            .contains(&index)
            .then_some(Self { index })
    }

    pub fn index(self) -> u8 {
        self.index
    }

    pub fn centre_hz(self) -> f64 {
        62.5 * f64::from(1_u32 << (self.index - 1))
    }

    pub fn nominal_label(self) -> &'static str {
        ["63", "125", "250", "500", "1k", "2k", "4k", "8k"][usize::from(self.index - 1)]
    }

    pub fn lower_hz(self) -> f64 {
        self.centre_hz() / std::f64::consts::SQRT_2
    }

    pub fn upper_hz(self) -> f64 {
        self.centre_hz() * std::f64::consts::SQRT_2
    }

    /// One period of the centre frequency in whole frames: the envelope's smoothing window and
    /// the time resolution this band can claim.
    pub fn period_frames(self, sample_rate: u32) -> i64 {
        ((f64::from(sample_rate) / self.centre_hz()).round() as i64).max(1)
    }

    pub fn resolution_micros(self) -> u32 {
        (1_000_000.0 / self.centre_hz()).round() as u32
    }

    fn settle_frames(self, sample_rate: u32) -> i64 {
        self.period_frames(sample_rate) * SETTLE_PERIODS
    }
}

pub(crate) fn frames_for_micros(sample_rate: u32, micros: i64) -> i64 {
    ((i64::from(sample_rate) * micros + 500_000) / 1_000_000).max(1)
}

/// A second-order section in direct form I, from the RBJ cookbook.
#[derive(Clone, Copy, Debug)]
struct Biquad {
    b0: f64,
    b1: f64,
    b2: f64,
    a1: f64,
    a2: f64,
}

impl Biquad {
    fn new(high_pass: bool, frequency_hz: f64, sample_rate: u32) -> Self {
        let frequency_hz = frequency_hz.min(f64::from(sample_rate) * 0.475);
        let w0 = std::f64::consts::TAU * frequency_hz / f64::from(sample_rate);
        let alpha = w0.sin() / (2.0 * std::f64::consts::FRAC_1_SQRT_2);
        let cos = w0.cos();
        let a0 = 1.0 + alpha;
        let (b0, b1) = if high_pass {
            ((1.0 + cos) / 2.0, -(1.0 + cos))
        } else {
            ((1.0 - cos) / 2.0, 1.0 - cos)
        };
        Self {
            b0: b0 / a0,
            b1: b1 / a0,
            b2: b0 / a0,
            a1: -2.0 * cos / a0,
            a2: (1.0 - alpha) / a0,
        }
    }

    /// |H| at `frequency_hz`.
    fn magnitude(&self, frequency_hz: f64, sample_rate: u32) -> f64 {
        let w = std::f64::consts::TAU * frequency_hz / f64::from(sample_rate);
        let (c1, s1, c2, s2) = (w.cos(), w.sin(), (2.0 * w).cos(), (2.0 * w).sin());
        let numerator = (
            self.b0 + self.b1 * c1 + self.b2 * c2,
            -(self.b1 * s1 + self.b2 * s2),
        );
        let denominator = (
            1.0 + self.a1 * c1 + self.a2 * c2,
            -(self.a1 * s1 + self.a2 * s2),
        );
        numerator.0.hypot(numerator.1) / denominator.0.hypot(denominator.1)
    }
}

/// Butterworth high-pass at the band's lower edge into Butterworth low-pass at its upper edge,
/// scaled so the centre passes at 0 dB: about -1.4 dB at the nominal edges and 12 dB per octave
/// beyond them. A sine at the centre reads its own level.
pub(crate) struct BandFilter {
    stages: [Biquad; 2],
    gain: f64,
}

impl BandFilter {
    pub(crate) fn new(band: AttackBand, sample_rate: u32) -> Self {
        let stages = [
            Biquad::new(true, band.lower_hz(), sample_rate),
            Biquad::new(false, band.upper_hz(), sample_rate),
        ];
        let centre = stages
            .iter()
            .map(|stage| stage.magnitude(band.centre_hz(), sample_rate))
            .product::<f64>();
        Self {
            stages,
            gain: if centre > 0.0 { 1.0 / centre } else { 1.0 },
        }
    }

    /// Filters `input` (one channel) into `output` from a zero state.
    pub(crate) fn run(&self, input: &[f32], output: &mut Vec<f64>) {
        output.clear();
        output.reserve(input.len());
        let mut state = [[0.0_f64; 4]; 2];
        for &sample in input {
            let mut value = f64::from(sample);
            for (stage, memory) in self.stages.iter().zip(state.iter_mut()) {
                let [x1, x2, y1, y2] = *memory;
                let y = stage.b0 * value + stage.b1 * x1 + stage.b2 * x2
                    - stage.a1 * y1
                    - stage.a2 * y2;
                *memory = [value, x1, y, y1];
                value = y;
            }
            output.push(value * self.gain);
        }
    }
}

/// Interleaved audio kept back on the worker while a band is selected, addressed by content
/// sample position. A gap restarts it: the ring never spans two runs.
pub struct AttackBandRing {
    channels: usize,
    capacity_frames: usize,
    first: i64,
    samples: VecDeque<f32>,
}

impl AttackBandRing {
    pub fn new(sample_rate: u32, channels: usize) -> Self {
        Self {
            channels,
            capacity_frames: frames_for_micros(sample_rate, ATTACK_BAND_RETENTION_MICROS) as usize,
            first: 0,
            samples: VecDeque::new(),
        }
    }

    pub fn first(&self) -> i64 {
        self.first
    }

    pub fn end(&self) -> i64 {
        self.first + (self.samples.len() / self.channels) as i64
    }

    pub fn is_empty(&self) -> bool {
        self.samples.is_empty()
    }

    pub fn push_block(&mut self, start: i64, interleaved: &[f32]) {
        if interleaved.is_empty() || !interleaved.len().is_multiple_of(self.channels) {
            return;
        }
        if self.samples.is_empty() || start != self.end() {
            self.samples.clear();
            self.first = start;
        }
        self.samples.extend(interleaved.iter().copied());
        let frames = self.samples.len() / self.channels;
        if frames > self.capacity_frames {
            let drop_frames = frames - self.capacity_frames;
            self.samples.drain(..drop_frames * self.channels);
            self.first += drop_frames as i64;
        }
    }

    /// Copies [from, to) into `into`; false when any of it is not retained.
    pub fn copy_frames(&self, from: i64, to: i64, into: &mut Vec<f32>) -> bool {
        into.clear();
        if from < self.first || to > self.end() || from >= to {
            return false;
        }
        let offset = (from - self.first) as usize * self.channels;
        let count = (to - from) as usize * self.channels;
        into.extend(self.samples.range(offset..offset + count).copied());
        true
    }
}

/// One side of one hit in one band. Times are frames relative to the onset; `None` is a fact
/// the audio did not contain (a band never below its peak - 20 dB before the peak, a ring-out
/// cut by the next onset), never a substitute.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackBandMeasure {
    pub band: AttackBand,
    pub sample_rate: u32,
    pub channels: u8,
    pub event_sample: i64,
    /// Exclusive end of the measured tail: 300 ms after the onset, or the next onset.
    pub span_end_sample: i64,
    pub peak_frames: f32,
    pub level_dbfs: f32,
    /// Where the band rises through its peak - 20 dB.
    pub arrival_frames: Option<f32>,
    /// From peak - 20 dB up to peak - 0.9 dB (10 % to 90 % of the peak amplitude).
    pub attack_frames: Option<f32>,
    /// From the peak down to peak - 20 dB.
    pub release_frames: Option<f32>,
    pub head_dbfs: [f32; ATTACK_BAND_HEAD_POINTS],
    pub tail_dbfs: [f32; ATTACK_BAND_TAIL_POINTS],
}

impl AttackBandMeasure {
    pub fn has_valid_layout(&self) -> bool {
        self.sample_rate > 0
            && matches!(self.channels, 1 | 2)
            && self.span_end_sample > self.event_sample
            && self.peak_frames.is_finite()
            && self.level_dbfs.is_finite()
            && self.level_dbfs > ATTACK_BAND_PRESENCE_FLOOR_DBFS
            && [self.arrival_frames, self.attack_frames, self.release_frames]
                .into_iter()
                .flatten()
                .all(|value| value.is_finite())
            && self.attack_frames.is_some() == self.arrival_frames.is_some()
            && self
                .head_dbfs
                .iter()
                .chain(self.tail_dbfs.iter())
                .all(|value| value.is_finite())
    }

    pub fn resolution_micros(&self) -> u32 {
        self.band.resolution_micros()
    }
}

/// The ring frames one measurement reads: the head lead, the filter's settling time and half the
/// smoothing window before the onset, and half the window after the span end.
pub(crate) fn analysis_range(
    band: AttackBand,
    sample_rate: u32,
    onset: i64,
    span_end: i64,
) -> (i64, i64) {
    let half_window = band.period_frames(sample_rate) / 2 + 1;
    let lead = frames_for_micros(sample_rate, ATTACK_BAND_HEAD_LEAD_MICROS);
    (
        onset - lead - band.settle_frames(sample_rate) - half_window,
        span_end + half_window,
    )
}

/// The tail end a hit measures to: 300 ms after its onset, or the next onset when that comes
/// first.
pub(crate) fn span_end_for(sample_rate: u32, onset: i64, next_onset: Option<i64>) -> i64 {
    let limit = onset + frames_for_micros(sample_rate, ATTACK_BAND_TAIL_MICROS);
    next_onset
        .filter(|next| *next > onset && *next < limit)
        .unwrap_or(limit)
}

/// Measures one band of one hit from `audio`, interleaved frames covering exactly
/// [`analysis_range`]. `None` when the band holds nothing in this hit.
pub(crate) fn measure_band(
    band: AttackBand,
    sample_rate: u32,
    channels: usize,
    onset: i64,
    span_end: i64,
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
    if level_dbfs <= ATTACK_BAND_PRESENCE_FLOOR_DBFS {
        return None;
    }
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
    let arrival_frames = backward(peak * 0.1);
    let attack_frames = arrival_frames
        .and_then(|arrival| backward(peak * 0.9).map(|rise_end| (rise_end - arrival).max(0.0)));
    let release_frames = (peak_frame + 1..span_end)
        .find(|frame| envelope(*frame) <= peak * 0.1)
        .map(|frame| {
            let (high, low) = (envelope(frame - 1), envelope(frame));
            let fraction = if high > low {
                ((high - peak * 0.1) / (high - low)).clamp(0.0, 1.0)
            } else {
                1.0
            };
            (frame - 1 - peak_frame) as f32 + fraction as f32
        });

    let head_span = frames_for_micros(sample_rate, ATTACK_BAND_HEAD_SPAN_MICROS);
    let tail_span = frames_for_micros(sample_rate, ATTACK_BAND_TAIL_MICROS);
    let mut head_dbfs = [ATTACK_LEVEL_FLOOR_DBFS; ATTACK_BAND_HEAD_POINTS];
    let mut tail_dbfs = [ATTACK_LEVEL_FLOOR_DBFS; ATTACK_BAND_TAIL_POINTS];
    for (index, point) in head_dbfs.iter_mut().enumerate() {
        let frame = search_start
            + (head_span * (2 * index as i64 + 1)) / (2 * ATTACK_BAND_HEAD_POINTS as i64);
        if frame < span_end {
            *point = dbfs(envelope(frame));
        }
    }
    for (index, point) in tail_dbfs.iter_mut().enumerate() {
        let frame =
            onset + (tail_span * (2 * index as i64 + 1)) / (2 * ATTACK_BAND_TAIL_POINTS as i64);
        if frame < span_end {
            *point = dbfs(envelope(frame));
        }
    }
    let measure = AttackBandMeasure {
        band,
        sample_rate,
        channels: channels as u8,
        event_sample: onset,
        span_end_sample: span_end,
        peak_frames: (peak_frame - onset) as f32,
        level_dbfs,
        arrival_frames,
        attack_frames,
        release_frames,
        head_dbfs,
        tail_dbfs,
    };
    measure.has_valid_layout().then_some(measure)
}

/// Work buffers reused across hits so a measurement allocates nothing after the first.
#[derive(Default)]
pub(crate) struct BandScratch {
    pub(crate) audio: Vec<f32>,
    channel: Vec<f32>,
    filtered: Vec<f64>,
    power: Vec<f64>,
    prefix: Vec<f64>,
}

/// POST measured at a PRE onset over the PRE measure's span, from this side's ring.
pub(crate) fn measure_from_ring(
    ring: &AttackBandRing,
    band: AttackBand,
    sample_rate: u32,
    channels: usize,
    onset: i64,
    span_end: i64,
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
                &audio,
                scratch,
            )
        })
        .flatten();
    scratch.audio = audio;
    measure
}

/// POST minus PRE, in frames: when each side's band rises through its own peak - 20 dB.
pub fn band_delay_frames(pre: &AttackBandMeasure, post: &AttackBandMeasure) -> Option<f32> {
    Some(post.arrival_frames? - pre.arrival_frames?)
}

/// One matched hit in the chosen band: PRE's measure and POST measured at the PRE onset over
/// the same span. POST is `None` while its audio is not retained yet, or holds nothing there.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackBandPair {
    pub event_sample: i64,
    pub pre: AttackBandMeasure,
    pub post: Option<AttackBandMeasure>,
    pub delay_frames: Option<f32>,
}

#[cfg(test)]
#[path = "attack_band_tests.rs"]
mod tests;
