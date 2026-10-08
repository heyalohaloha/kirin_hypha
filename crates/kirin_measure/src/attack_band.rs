//! One octave band of a DRUM hit: where the band arrives, how long it rises, how long it rings
//! and how loud it peaks, measured on PRE and POST at one onset so the two can be compared.
//!
//! Nothing here runs unless a band is chosen. The audio ring belongs to the ATTACK worker and
//! exists only while a band is selected; every band measurement runs on that worker, once per
//! hit and band; the Audio Thread is untouched. A band has an octave's worth of time resolution:
//! about one period of its centre frequency (16 ms at 63 Hz, 0.125 ms at 8 kHz).
//!
//! Every outcome is stated, never implied by a missing value: the band rises at the hit, only
//! rings on from before, or is silent; its start is timed or hidden by that ring-out; its
//! release is timed, cut by the next hit, or longer than the tail that was measured.
//!
//! Bands follow IEC 61260's base-two octave series with ISO 266 nominal labels: 63, 125, 250,
//! 500, 1k, 2k, 4k and 8k Hz, each from centre / √2 to centre × √2, so they tile without gaps.

use std::collections::VecDeque;

use super::ATTACK_LEVEL_FLOOR_DBFS;

#[path = "attack_band_measure.rs"]
mod measure;
pub use measure::band_delay_frames;
#[cfg(test)]
pub(crate) use measure::measure_band;
pub(crate) use measure::{measure_from_ring, BandScratch};

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
/// A band whose peak stays at or below this is silent at the hit.
pub const ATTACK_BAND_PRESENCE_FLOOR_DBFS: f32 = -72.0;
/// A band that rises less than this above its level 20 ms before the onset only rings on.
pub const ATTACK_BAND_RISE_DB: f32 = 3.0;
/// One band result for every hit the lanes can show: the event history's own bound.
pub const ATTACK_BAND_HISTORY_CAPACITY: usize = crate::ATTACK_EVENT_HISTORY_CAPACITY;
/// The ring reaches back as far as the bins do, so POST can be measured at a PRE onset and a
/// band change can measure the kept hits again.
pub const ATTACK_BAND_RETENTION_MICROS: i64 = 7_000_000;
pub(crate) const SETTLE_PERIODS: i64 = 4;
pub(crate) const BASE_CENTRE_HZ: f64 = 62.5;
pub(crate) const NYQUIST_FRACTION: f64 = 0.475;
pub(crate) const ARRIVAL_AMPLITUDE_RATIO: f64 = 0.1;
pub(crate) const ATTACK_UPPER_AMPLITUDE_RATIO: f64 = 0.9;
pub(crate) const RELEASE_AMPLITUDE_RATIO: f64 = 0.1;

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
        BASE_CENTRE_HZ * f64::from(1_u32 << (self.index - 1))
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
        let frequency_hz = frequency_hz.min(f64::from(sample_rate) * NYQUIST_FRACTION);
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

/// Interleaved audio kept back on the ATTACK worker while a band is selected, addressed by
/// content sample position. The worker is its only owner. A gap restarts it: the ring never
/// spans two runs. Its whole capacity is reserved when it is made, so it never grows.
pub struct AttackBandRing {
    channels: usize,
    capacity_frames: usize,
    first: i64,
    samples: VecDeque<f32>,
}

impl AttackBandRing {
    pub fn new(sample_rate: u32, channels: usize) -> Self {
        let capacity_frames = frames_for_micros(sample_rate, ATTACK_BAND_RETENTION_MICROS) as usize;
        Self {
            channels,
            capacity_frames,
            first: 0,
            samples: VecDeque::with_capacity(capacity_frames * channels),
        }
    }

    pub fn capacity_frames(&self) -> usize {
        self.capacity_frames
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

    pub fn clear(&mut self) {
        self.samples.clear();
    }

    pub fn push_block(&mut self, start: i64, interleaved: &[f32]) {
        if interleaved.is_empty() || !interleaved.len().is_multiple_of(self.channels) {
            return;
        }
        if self.samples.is_empty() || start != self.end() {
            self.samples.clear();
            self.first = start;
        }
        // A block longer than the whole ring keeps only its newest frames.
        let incoming = interleaved.len() / self.channels;
        let skipped = incoming.saturating_sub(self.capacity_frames);
        if skipped > 0 {
            self.samples.clear();
            self.first = start + skipped as i64;
        }
        let kept = self.samples.len() / self.channels;
        let overflow = (kept + incoming - skipped).saturating_sub(self.capacity_frames);
        if overflow > 0 {
            self.samples.drain(..overflow * self.channels);
            self.first += overflow as i64;
        }
        self.samples
            .extend(interleaved[skipped * self.channels..].iter().copied());
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

/// Why a measured tail ends where it does.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum BandSpanEnd {
    /// 300 ms after the onset.
    Window,
    /// The next onset came first.
    NextHit,
    /// The run's audio ended first: the transport stopped.
    AudioEnd,
}

/// Where the band starts at this hit.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum BandArrival {
    /// Frames from the onset where the band rises through its peak - 20 dB, and from there to
    /// peak - 0.9 dB (10 % to 90 % of the peak amplitude).
    At {
        arrival_frames: f32,
        attack_frames: f32,
    },
    /// The band never fell 20 dB below its peak in the 20 ms before it: the previous hit still
    /// rings, so this hit's start cannot be timed.
    Ringing,
}

/// How the band falls after its peak.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum BandRelease {
    /// Frames from the peak down to peak - 20 dB.
    At(f32),
    /// The next onset came before the band fell 20 dB.
    CutByNextHit,
    /// Still above peak - 20 dB where the measured tail ends: at least this many frames.
    AtLeast(f32),
}

/// What this hit does in the band.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum BandSound {
    /// The band rises at this hit: every measure describes it.
    Rises {
        arrival: BandArrival,
        release: BandRelease,
    },
    /// The band only rings on from before: it rises less than 3 dB above its level 20 ms before
    /// the onset, so its level and fall are the previous hit's.
    RingsOn,
    /// The band stays at or below -72 dBFS at this hit.
    Silent,
}

/// The band's envelope in centi-dBFS, for drawing: [onset - 20 ms, onset + 40 ms) and
/// [onset, onset + 300 ms). Points past the measured tail sit on the -120 dBFS floor.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct BandEnvelope {
    pub head: [i16; ATTACK_BAND_HEAD_POINTS],
    pub tail: [i16; ATTACK_BAND_TAIL_POINTS],
}

impl Default for BandEnvelope {
    fn default() -> Self {
        let floor = centi_db(ATTACK_LEVEL_FLOOR_DBFS);
        Self {
            head: [floor; ATTACK_BAND_HEAD_POINTS],
            tail: [floor; ATTACK_BAND_TAIL_POINTS],
        }
    }
}

impl BandEnvelope {
    pub fn is_valid(&self) -> bool {
        let floor = centi_db(ATTACK_LEVEL_FLOOR_DBFS);
        self.head
            .iter()
            .chain(self.tail.iter())
            .all(|value| *value >= floor)
    }
}

pub fn centi_db(dbfs: f32) -> i16 {
    (dbfs * 100.0)
        .round()
        .clamp(f32::from(i16::MIN), f32::from(i16::MAX)) as i16
}

pub fn dbfs_from_centi(value: i16) -> f32 {
    f32::from(value) / 100.0
}

/// One side of one hit in one band. Times are frames from the onset it was measured at.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackBandMeasure {
    pub band: AttackBand,
    pub sample_rate: u32,
    pub channels: u8,
    pub event_sample: i64,
    /// Exclusive end of the measured tail, and why it ends there.
    pub span_end_sample: i64,
    pub span_end: BandSpanEnd,
    pub peak_frames: f32,
    pub level_dbfs: f32,
    pub sound: BandSound,
    pub envelope: BandEnvelope,
}

impl AttackBandMeasure {
    pub fn has_valid_layout(&self) -> bool {
        let window_end =
            self.event_sample + frames_for_micros(self.sample_rate.max(1), ATTACK_BAND_TAIL_MICROS);
        let span_consistent = match self.span_end {
            BandSpanEnd::Window => self.span_end_sample == window_end,
            BandSpanEnd::NextHit | BandSpanEnd::AudioEnd => self.span_end_sample <= window_end,
        };
        let finite_frames = |value: f32| value.is_finite() && value >= 0.0;
        let sound_consistent = match self.sound {
            BandSound::Silent => self.level_dbfs <= ATTACK_BAND_PRESENCE_FLOOR_DBFS,
            BandSound::RingsOn => self.level_dbfs > ATTACK_BAND_PRESENCE_FLOOR_DBFS,
            BandSound::Rises { arrival, release } => {
                self.level_dbfs > ATTACK_BAND_PRESENCE_FLOOR_DBFS
                    && match arrival {
                        BandArrival::At {
                            arrival_frames,
                            attack_frames,
                        } => arrival_frames.is_finite() && finite_frames(attack_frames),
                        BandArrival::Ringing => true,
                    }
                    && match release {
                        BandRelease::At(frames) => finite_frames(frames),
                        BandRelease::CutByNextHit => self.span_end == BandSpanEnd::NextHit,
                        BandRelease::AtLeast(frames) => {
                            finite_frames(frames) && self.span_end != BandSpanEnd::NextHit
                        }
                    }
            }
        };
        self.sample_rate > 0
            && matches!(self.channels, 1 | 2)
            && self.span_end_sample > self.event_sample
            && span_consistent
            && self.peak_frames.is_finite()
            && self.level_dbfs.is_finite()
            && sound_consistent
            && self.envelope.is_valid()
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
    let half_window = half_window_frames(band, sample_rate);
    let lead = frames_for_micros(sample_rate, ATTACK_BAND_HEAD_LEAD_MICROS);
    (
        onset - lead - band.settle_frames(sample_rate) - half_window,
        span_end + half_window,
    )
}

pub(crate) fn half_window_frames(band: AttackBand, sample_rate: u32) -> i64 {
    band.period_frames(sample_rate) / 2 + 1
}

/// The tail end a hit measures to: 300 ms after its onset, or the next onset when that comes
/// first.
pub fn span_end_for(sample_rate: u32, onset: i64, next_onset: Option<i64>) -> (i64, BandSpanEnd) {
    let limit = onset + frames_for_micros(sample_rate, ATTACK_BAND_TAIL_MICROS);
    next_onset
        .filter(|next| *next > onset && *next < limit)
        .map_or((limit, BandSpanEnd::Window), |next| {
            (next, BandSpanEnd::NextHit)
        })
}

/// The tail a hit can still be measured over when its run's audio ended at `audio_end`: the
/// kept audio less half the smoothing window, or `None` when that does not reach past the peak
/// search, so nothing about the hit's rise and peak would be known.
pub(crate) fn audio_end_span(
    band: AttackBand,
    sample_rate: u32,
    onset: i64,
    span_end: i64,
    audio_end: i64,
) -> Option<i64> {
    let available = audio_end - half_window_frames(band, sample_rate);
    let needed = onset + frames_for_micros(sample_rate, ATTACK_BAND_PEAK_SEARCH_MICROS);
    (available >= needed).then_some(available.min(span_end))
}

#[cfg(test)]
#[path = "attack_band_outcome_tests.rs"]
mod outcome_tests;
#[cfg(test)]
#[path = "attack_band_tests.rs"]
mod tests;
