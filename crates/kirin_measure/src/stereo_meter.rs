//! Fixed-cadence channel and stereo facts for the always-on Meter Session.
//!
//! Input is one exact 100 ms observation from `MeasureEngine::push_observed`. This keeps channel
//! peaks, clip events, balance and correlation on the same sample boundary as loudness/session
//! facts without adding work to the Audio Thread.

use std::collections::VecDeque;

use ebur128::{EbuR128, Mode};

const OBSERVATIONS_PER_THREE_SECONDS: usize = 30;
const OBSERVATIONS_PER_TP_WINDOW: usize = 4;
const OBSERVATIONS_PER_VU_WINDOW: usize = 3;
const FIELD_MAX_POINTS_PER_OBSERVATION: usize = 1_024;
pub const STEREO_FIELD_SIZE: usize = 25;
pub const STEREO_FIELD_BINS: usize = STEREO_FIELD_SIZE * STEREO_FIELD_SIZE;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BalanceState {
    Unavailable,
    Numeric,
    LeftOnly,
    RightOnly,
}

#[derive(Debug, Clone, Copy)]
pub struct StereoMeterSnapshot {
    pub channels: u8,
    pub sample_peak_dbfs: [Option<f64>; 2],
    pub sample_peak_hold_dbfs: [Option<f64>; 2],
    pub true_peak_dbtp: [Option<f64>; 2],
    /// True Peak of the latest exact 100 ms observation. The 400 ms `true_peak_dbtp` value
    /// remains available for the conventional level strips.
    pub instant_true_peak_dbtp: [Option<f64>; 2],
    pub max_true_peak_dbtp: [Option<f64>; 2],
    /// Sine-calibrated, full-wave average over the latest exact 300 ms. A sine whose peak is
    /// -18 dBFS therefore reads -18 dBFS and 0 VU at the UI's fixed reference.
    pub vu_dbfs: [Option<f64>; 2],
    /// Session-cumulative contiguous sample-clip runs. Only a full Meter Session reset clears it.
    pub clip_events: [u64; 2],
    /// User-clearable Hybrid VU indicators. These do not replace the cumulative clip facts.
    pub clip_latched: [bool; 2],
    pub balance_db: Option<f64>,
    pub balance_state: BalanceState,
    pub correlation: Option<f64>,
    /// Three-second MID/SIDE density, row-major from top-left. Zero means unoccupied and 255 is
    /// the densest cell in the same factual window; it is a shape display, not a level metric.
    pub field_density: [u8; STEREO_FIELD_BINS],
    pub field_observation_count: u8,
}

#[derive(Debug, Clone, Copy, Default)]
struct EnergyObservation {
    left: f64,
    right: f64,
    cross: f64,
}

#[derive(Debug, Clone, Copy, Default)]
struct VuObservation {
    rectified: [f64; 2],
    frames: u64,
}

#[derive(Debug, Clone)]
struct FieldObservation {
    bins: [u16; STEREO_FIELD_BINS],
}

pub struct StereoMeter {
    ebu: EbuR128,
    channels: usize,
    sample_peak: [f64; 2],
    sample_peak_hold: [f64; 2],
    true_peak_window: VecDeque<[f64; 2]>,
    max_true_peak: [f64; 2],
    vu_window: VecDeque<VuObservation>,
    vu_sum: VuObservation,
    clip_latched: [bool; 2],
    session_clip_events: [u64; 2],
    session_clip_open: [bool; 2],
    energy_window: VecDeque<EnergyObservation>,
    energy_sum: EnergyObservation,
    field_window: VecDeque<FieldObservation>,
    field_sum: [u32; STEREO_FIELD_BINS],
}

impl StereoMeter {
    pub fn new(sample_rate: u32, channels: usize) -> Result<Self, String> {
        if !(1..=2).contains(&channels) {
            return Err(format!("unsupported channel count: {channels}"));
        }
        let ebu = EbuR128::new(channels as u32, sample_rate, Mode::TRUE_PEAK)
            .map_err(|error| format!("EbuR128::new: {error:?}"))?;
        Ok(Self {
            ebu,
            channels,
            sample_peak: [0.0; 2],
            sample_peak_hold: [0.0; 2],
            true_peak_window: VecDeque::with_capacity(OBSERVATIONS_PER_TP_WINDOW + 1),
            max_true_peak: [0.0; 2],
            vu_window: VecDeque::with_capacity(OBSERVATIONS_PER_VU_WINDOW + 1),
            vu_sum: VuObservation::default(),
            clip_latched: [false; 2],
            session_clip_events: [0; 2],
            session_clip_open: [false; 2],
            energy_window: VecDeque::with_capacity(OBSERVATIONS_PER_THREE_SECONDS + 1),
            energy_sum: EnergyObservation::default(),
            field_window: VecDeque::with_capacity(OBSERVATIONS_PER_THREE_SECONDS + 1),
            field_sum: [0; STEREO_FIELD_BINS],
        })
    }

    pub fn push_observation(&mut self, interleaved: &[f64]) -> bool {
        if interleaved.is_empty()
            || !interleaved.len().is_multiple_of(self.channels)
            || interleaved.iter().any(|sample| !sample.is_finite())
        {
            return false;
        }

        let mut peak = [0.0_f64; 2];
        let mut vu = VuObservation::default();
        let mut energy = EnergyObservation::default();
        let mut field = FieldObservation {
            bins: [0; STEREO_FIELD_BINS],
        };
        let mut clip_latched = self.clip_latched;
        let mut session_clip_events = self.session_clip_events;
        let mut session_clip_open = self.session_clip_open;
        let frame_count = interleaved.len() / self.channels;
        const MAX_POINTS: usize = FIELD_MAX_POINTS_PER_OBSERVATION;
        let field_point_count = frame_count.min(MAX_POINTS);
        let mut field_point_index = 0usize;
        let mut next_field_frame = 0usize;
        for (frame_index, frame) in interleaved.chunks_exact(self.channels).enumerate() {
            for channel in 0..self.channels {
                let magnitude = frame[channel].abs();
                peak[channel] = peak[channel].max(magnitude);
                vu.rectified[channel] += magnitude;
                let clipped = magnitude >= 1.0;
                if clipped {
                    clip_latched[channel] = true;
                }
                if clipped && !session_clip_open[channel] {
                    session_clip_events[channel] = session_clip_events[channel].saturating_add(1);
                }
                session_clip_open[channel] = clipped;
            }
            if self.channels == 2 {
                energy.left += frame[0] * frame[0];
                energy.right += frame[1] * frame[1];
                energy.cross += frame[0] * frame[1];
                if field_point_index < field_point_count && frame_index == next_field_frame {
                    accumulate_field_point(&mut field.bins, frame[0], frame[1]);
                    field_point_index += 1;
                    if field_point_index < field_point_count {
                        next_field_frame = field_point_index * frame_count / field_point_count;
                    }
                }
            }
        }

        if self.ebu.add_frames_f64(interleaved).is_err() {
            return false;
        }
        self.clip_latched = clip_latched;
        self.session_clip_events = session_clip_events;
        self.session_clip_open = session_clip_open;
        self.sample_peak = peak;
        for (channel, value) in peak.iter().copied().enumerate().take(self.channels) {
            self.sample_peak_hold[channel] = self.sample_peak_hold[channel].max(value);
        }

        let mut true_peak = [0.0; 2];
        for (channel, value) in true_peak.iter_mut().enumerate().take(self.channels) {
            *value = self.ebu.prev_true_peak(channel as u32).unwrap_or(0.0);
            self.max_true_peak[channel] = self.max_true_peak[channel].max(*value);
        }
        self.true_peak_window.push_back(true_peak);
        while self.true_peak_window.len() > OBSERVATIONS_PER_TP_WINDOW {
            self.true_peak_window.pop_front();
        }
        vu.frames = frame_count as u64;
        self.vu_window.push_back(vu);
        for channel in 0..self.channels {
            self.vu_sum.rectified[channel] += vu.rectified[channel];
        }
        self.vu_sum.frames = self.vu_sum.frames.saturating_add(vu.frames);
        while self.vu_window.len() > OBSERVATIONS_PER_VU_WINDOW {
            if let Some(expired) = self.vu_window.pop_front() {
                for channel in 0..self.channels {
                    self.vu_sum.rectified[channel] =
                        (self.vu_sum.rectified[channel] - expired.rectified[channel]).max(0.0);
                }
                self.vu_sum.frames = self.vu_sum.frames.saturating_sub(expired.frames);
            }
        }

        if self.channels == 2 {
            self.energy_window.push_back(energy);
            self.energy_sum.left += energy.left;
            self.energy_sum.right += energy.right;
            self.energy_sum.cross += energy.cross;
            while self.energy_window.len() > OBSERVATIONS_PER_THREE_SECONDS {
                if let Some(expired) = self.energy_window.pop_front() {
                    self.energy_sum.left = (self.energy_sum.left - expired.left).max(0.0);
                    self.energy_sum.right = (self.energy_sum.right - expired.right).max(0.0);
                    self.energy_sum.cross -= expired.cross;
                }
            }
            for (sum, value) in self.field_sum.iter_mut().zip(field.bins.iter().copied()) {
                *sum = sum.saturating_add(u32::from(value));
            }
            self.field_window.push_back(field);
            while self.field_window.len() > OBSERVATIONS_PER_THREE_SECONDS {
                if let Some(expired) = self.field_window.pop_front() {
                    for (sum, value) in self.field_sum.iter_mut().zip(expired.bins.iter().copied())
                    {
                        *sum = sum.saturating_sub(u32::from(value));
                    }
                }
            }
        }
        true
    }

    pub fn reset(&mut self) {
        self.ebu.reset();
        self.sample_peak = [0.0; 2];
        self.sample_peak_hold = [0.0; 2];
        self.true_peak_window.clear();
        self.max_true_peak = [0.0; 2];
        self.vu_window.clear();
        self.vu_sum = VuObservation::default();
        self.clip_latched = [false; 2];
        self.session_clip_events = [0; 2];
        self.session_clip_open = [false; 2];
        self.energy_window.clear();
        self.energy_sum = EnergyObservation::default();
        self.field_window.clear();
        self.field_sum = [0; STEREO_FIELD_BINS];
    }

    /// Clears only the Hybrid VU's user-resettable TP maximum and clip latch.
    /// Current/recent TP, VU averaging, sample peak hold, and stereo windows remain continuous.
    pub fn clear_peak_clip_holds(&mut self) {
        self.max_true_peak = [0.0; 2];
        self.clip_latched = [false; 2];
    }

    pub fn session_clip_events(&self) -> [u64; 2] {
        self.session_clip_events
    }

    pub fn snapshot(&self) -> StereoMeterSnapshot {
        let sample_peak_dbfs = self.map_channels(self.sample_peak, linear_to_db);
        let sample_peak_hold_dbfs = self.map_channels(self.sample_peak_hold, linear_to_db);
        let mut recent_true_peak = [0.0_f64; 2];
        for observation in &self.true_peak_window {
            for (channel, value) in recent_true_peak.iter_mut().enumerate().take(self.channels) {
                *value = (*value).max(observation[channel]);
            }
        }
        let true_peak_dbtp = self.map_channels(recent_true_peak, linear_to_db);
        let instant_true_peak_dbtp = self.map_channels(
            self.true_peak_window.back().copied().unwrap_or([0.0; 2]),
            linear_to_db,
        );
        let vu_dbfs = self.vu_levels();
        let max_true_peak_dbtp = self.map_channels(self.max_true_peak, linear_to_db);
        let (balance_db, balance_state, correlation) = self.stereo_window_facts();
        StereoMeterSnapshot {
            channels: self.channels as u8,
            sample_peak_dbfs,
            sample_peak_hold_dbfs,
            true_peak_dbtp,
            instant_true_peak_dbtp,
            max_true_peak_dbtp,
            vu_dbfs,
            clip_events: self.session_clip_events,
            clip_latched: self.clip_latched,
            balance_db,
            balance_state,
            correlation,
            field_density: normalized_field_density(&self.field_sum),
            field_observation_count: self.field_window.len() as u8,
        }
    }

    fn map_channels(&self, linear: [f64; 2], map: impl Fn(f64) -> Option<f64>) -> [Option<f64>; 2] {
        let mut result = [None; 2];
        for (channel, value) in linear.iter().copied().enumerate().take(self.channels) {
            result[channel] = map(value);
        }
        result
    }

    fn stereo_window_facts(&self) -> (Option<f64>, BalanceState, Option<f64>) {
        if self.channels != 2 || self.energy_window.len() < OBSERVATIONS_PER_THREE_SECONDS {
            return (None, BalanceState::Unavailable, None);
        }
        let left = self.energy_sum.left;
        let right = self.energy_sum.right;
        let state = match (left > 0.0, right > 0.0) {
            (true, true) => BalanceState::Numeric,
            (true, false) => BalanceState::LeftOnly,
            (false, true) => BalanceState::RightOnly,
            (false, false) => BalanceState::Unavailable,
        };
        let balance = (state == BalanceState::Numeric)
            .then(|| 10.0 * (left / right).log10())
            .filter(|value| value.is_finite());
        let correlation = (left > 0.0 && right > 0.0)
            .then(|| self.energy_sum.cross / (left * right).sqrt())
            .filter(|value| value.is_finite())
            .map(|value| value.clamp(-1.0, 1.0));
        (balance, state, correlation)
    }

    fn vu_levels(&self) -> [Option<f64>; 2] {
        let mut result = [None; 2];
        if self.vu_window.len() < OBSERVATIONS_PER_VU_WINDOW || self.vu_sum.frames == 0 {
            return result;
        }
        for (channel, value) in result.iter_mut().enumerate().take(self.channels) {
            // A full-wave average meter is calibrated so the mean-rectified value of a sine
            // maps back to its peak amplitude: mean(abs(sine)) * PI/2 == sine peak.
            let calibrated = self.vu_sum.rectified[channel] / self.vu_sum.frames as f64
                * std::f64::consts::FRAC_PI_2;
            *value = linear_to_db(calibrated);
        }
        result
    }
}

fn accumulate_field_point(bins: &mut [u16; STEREO_FIELD_BINS], left: f64, right: f64) {
    if left.abs() + right.abs() <= f64::EPSILON {
        return;
    }
    // Rotate L/R into MID/SIDE. Square-root companding makes low-level shape visible while the
    // histogram remains sign- and topology-faithful. Absolute level remains owned by LEVEL.
    let warp = |value: f64| {
        if value.abs() < 1.0e-12 {
            0.0
        } else {
            value.signum() * value.abs().min(1.0).sqrt()
        }
    };
    let mid = warp((left + right) * 0.5);
    let side = warp((left - right) * 0.5);
    let coordinate = |value: f64| {
        (((value.clamp(-1.0, 1.0) + 1.0) * 0.5 * (STEREO_FIELD_SIZE - 1) as f64).round()) as usize
    };
    const LAST: usize = STEREO_FIELD_SIZE - 1;
    let x = coordinate(side);
    let y = LAST - coordinate(mid);
    let index = y * STEREO_FIELD_SIZE + x;
    bins[index] = bins[index].saturating_add(1);
}

fn normalized_field_density(sum: &[u32; STEREO_FIELD_BINS]) -> [u8; STEREO_FIELD_BINS] {
    let mut density = [0; STEREO_FIELD_BINS];
    let maximum = sum.iter().copied().max().unwrap_or(0);
    if maximum == 0 {
        return density;
    }
    for (out, count) in density.iter_mut().zip(sum.iter().copied()) {
        let normalized = (count as f64 / maximum as f64).sqrt();
        *out = (normalized * 255.0).round().clamp(0.0, 255.0) as u8;
    }
    density
}

fn linear_to_db(value: f64) -> Option<f64> {
    (value > 0.0)
        .then(|| 20.0 * value.log10())
        .filter(|value| value.is_finite())
}

#[cfg(test)]
#[path = "stereo_meter_tests.rs"]
mod tests;
