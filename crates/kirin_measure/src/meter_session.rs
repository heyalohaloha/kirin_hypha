//! Always-on mastering meter session.
//!
//! This state is independent from Record/Keep, Watch playback passes, pairing, and Guide. Only
//! active audio advances it; inactive/bypassed time is a pause, and only an explicit reset clears
//! accumulated statistics. The owner lives outside the replaceable Measure worker so a worker
//! restart does not implicitly discard the session.

use crate::meter_clock::MeterClockTracker;
use crate::meter_history::MeterHistory;
use crate::{
    MeasureEngine, MeasureResult, MeterClockStart, MeterHistoryAux, MeterHistoryEntry,
    MeterHistoryResolution, SessionSummary, StereoMeter, StereoMeterSnapshot,
};
use std::sync::{RwLock, TryLockError};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MeterSessionState {
    Empty,
    Active,
    Paused,
}

#[derive(Debug, Clone)]
pub struct MeterSessionSnapshot {
    pub generation: u64,
    pub state: MeterSessionState,
    pub sample_rate: u32,
    pub active_frames: u64,
    /// Session-relative endpoint shared by `current` and `summary` (100 ms engine cadence).
    pub observed_frames: u64,
    /// Latest complete 100 ms observation from the same engine as `summary`.
    pub current: MeasureResult,
    /// Producer-owned Session maxima. S/Crest are observed at 100 ms, M at 10 ms.
    pub maximum: MeasureResult,
    /// EBU Mode Maximum Momentary through the same complete 100 ms observation boundary.
    pub max_lufs_m: Option<f64>,
    pub summary: SessionSummary,
    pub plr: Option<f64>,
    pub stereo: StereoMeterSnapshot,
}

impl MeterSessionSnapshot {
    pub fn active_seconds(&self) -> f64 {
        if self.sample_rate == 0 {
            0.0
        } else {
            self.active_frames as f64 / self.sample_rate as f64
        }
    }
}

/// A completed MeterSession observation published independently from the live calculation lock.
///
/// The Measure Thread builds the snapshot while it owns `MeterSession`, then replaces this small
/// immutable value after releasing the live calculation lock. UI readers never contend with EBU
/// processing; a rare publication handoff collision is a silent skipped poll and the shell keeps
/// displaying its last complete frame.
pub struct MeterSessionPublication {
    latest: RwLock<MeterSessionSnapshot>,
}

impl MeterSessionPublication {
    pub fn new(initial: MeterSessionSnapshot) -> Self {
        Self {
            latest: RwLock::new(initial),
        }
    }

    pub fn publish(&self, snapshot: MeterSessionSnapshot) {
        *self
            .latest
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = snapshot;
    }

    pub fn try_snapshot(&self) -> Option<MeterSessionSnapshot> {
        match self.latest.try_read() {
            Ok(snapshot) => Some(snapshot.clone()),
            Err(TryLockError::Poisoned(poisoned)) => Some(poisoned.into_inner().clone()),
            Err(TryLockError::WouldBlock) => None,
        }
    }
}

pub struct MeterSession {
    engine: MeasureEngine,
    sample_rate: u32,
    n_channels: usize,
    generation: u64,
    active_frames: u64,
    state: MeterSessionState,
    current: MeasureResult,
    maximum: MeasureResult,
    max_lufs_m: Option<f64>,
    summary: SessionSummary,
    observed_frames: u64,
    stereo: StereoMeter,
    clock: MeterClockTracker,
    history: MeterHistory,
}

impl MeterSession {
    pub fn new(sample_rate: u32, n_channels: usize) -> Result<Self, String> {
        let engine = MeasureEngine::new(sample_rate, n_channels)?;
        let stereo = StereoMeter::new(sample_rate, n_channels)?;
        Ok(Self {
            engine,
            sample_rate,
            n_channels,
            generation: 1,
            active_frames: 0,
            state: MeterSessionState::Empty,
            current: MeasureResult::default(),
            maximum: MeasureResult::default(),
            max_lufs_m: None,
            summary: SessionSummary::default(),
            observed_frames: 0,
            stereo,
            clock: MeterClockTracker::new(),
            history: MeterHistory::new(),
        })
    }

    /// Adds one direct, active input span. Replayed Record pre-roll must never call this method.
    /// Invalid spans fail closed without advancing either time or EBU state.
    pub fn push_active(&mut self, interleaved: &[f64]) -> bool {
        self.push_active_at(interleaved, MeterClockStart::unknown())
    }

    /// Adds one direct, active input span with its optional DAW presentation-clock start.
    pub fn push_active_at(&mut self, interleaved: &[f64], clock: MeterClockStart) -> bool {
        if interleaved.is_empty()
            || !interleaved.len().is_multiple_of(self.n_channels)
            || interleaved.iter().any(|sample| !sample.is_finite())
        {
            return false;
        }
        self.active_frames = self
            .active_frames
            .saturating_add((interleaved.len() / self.n_channels) as u64);
        self.state = MeterSessionState::Active;
        self.clock
            .push_span((interleaved.len() / self.n_channels) as u64, clock);
        let mut advanced = false;
        self.engine.push_observed_with_session_facts(
            interleaved,
            |_, current, observed_samples, plr, max_lufs_m| {
                const MAX_HISTORY_CLIP_EVENTS: u64 = u32::MAX as u64;
                let previous_clip_events = self.stereo.session_clip_events();
                let stereo_advanced = self.stereo.push_observation(observed_samples);
                let stereo_snapshot = stereo_advanced.then(|| self.stereo.snapshot());
                let session_clip_events = self.stereo.session_clip_events();
                let clip_event_count =
                    stereo_advanced
                        .then_some(session_clip_events)
                        .map_or([0; 2], |events| {
                            std::array::from_fn(|channel| {
                                events[channel]
                                    .saturating_sub(previous_clip_events[channel])
                                    .min(MAX_HISTORY_CLIP_EVENTS)
                                    as u32
                            })
                        });
                self.current = current.clone();
                self.max_lufs_m = max_lufs_m;
                self.maximum.lufs_m = max_lufs_m;
                self.maximum.true_peak = current.tp_session_max;
                for (maximum, value) in [
                    (&mut self.maximum.lufs_s, current.lufs_s),
                    (&mut self.maximum.crest, current.crest),
                ] {
                    if let Some(value) = value.filter(|v| v.is_finite()) {
                        *maximum = Some(maximum.map_or(value, |old| old.max(value)));
                    }
                }
                self.observed_frames = self
                    .observed_frames
                    .saturating_add((observed_samples.len() / self.n_channels) as u64);
                let clock = self
                    .clock
                    .consume_observation((observed_samples.len() / self.n_channels) as u64);
                if clock.usable_for_history {
                    let correlation = stereo_snapshot.and_then(|snapshot| snapshot.correlation);
                    self.history.push(
                        self.generation,
                        clock.run_id,
                        self.observed_frames,
                        (clock.timeline_endpoint_samples, clock.timeline_source),
                        current,
                        MeterHistoryAux {
                            correlation,
                            plr,
                            clip_event_count,
                        },
                    );
                }
                advanced = true;
            },
        );
        if advanced {
            self.summary = self.engine.finalize();
        }
        true
    }

    pub fn recent_history(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
    ) -> Vec<MeterHistoryEntry> {
        self.history.recent(resolution, max_entries)
    }

    pub fn recent_history_decimated(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
        max_output: usize,
    ) -> Vec<MeterHistoryEntry> {
        self.history
            .recent_decimated(resolution, max_entries, max_output)
    }

    pub fn pause(&mut self) {
        if self.state != MeterSessionState::Empty {
            self.state = MeterSessionState::Paused;
        }
    }

    pub fn reset(&mut self) {
        self.engine.reset();
        self.generation = self.generation.wrapping_add(1).max(1);
        self.active_frames = 0;
        self.state = MeterSessionState::Empty;
        self.current = MeasureResult::default();
        self.maximum = MeasureResult::default();
        self.max_lufs_m = None;
        self.summary = SessionSummary::default();
        self.observed_frames = 0;
        self.stereo.reset();
        self.clock.reset();
        self.history.reset();
    }

    /// Clears the user-resettable Hybrid VU TP maximum and Clip latch without changing the
    /// Meter Session, its history, or Record/Keep-independent cumulative facts.
    pub fn clear_peak_clip_holds(&mut self) {
        self.stereo.clear_peak_clip_holds();
    }

    pub fn snapshot(&self) -> MeterSessionSnapshot {
        MeterSessionSnapshot {
            generation: self.generation,
            state: self.state,
            sample_rate: self.sample_rate,
            active_frames: self.active_frames,
            observed_frames: self.observed_frames,
            current: self.current.clone(),
            maximum: self.maximum.clone(),
            max_lufs_m: self.max_lufs_m,
            summary: self.summary,
            plr: self
                .summary
                .max_true_peak
                .zip(self.summary.lufs_i)
                .map(|(peak, integrated)| peak - integrated)
                .filter(|value| value.is_finite()),
            stereo: self.stereo.snapshot(),
        }
    }
}

#[cfg(test)]
#[path = "meter_session_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "meter_session_maximum_tests.rs"]
mod maximum_tests;
