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

pub struct MeterSession {
    engine: MeasureEngine,
    sample_rate: u32,
    n_channels: usize,
    generation: u64,
    active_frames: u64,
    state: MeterSessionState,
    current: MeasureResult,
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
                let previous_clip_events = self.stereo.clip_events();
                let stereo_advanced = self.stereo.push_observation(observed_samples);
                let stereo_snapshot = stereo_advanced.then(|| self.stereo.snapshot());
                let clip_event_count = stereo_snapshot.map_or([0; 2], |snapshot| {
                    std::array::from_fn(|channel| {
                        snapshot.clip_events[channel]
                            .saturating_sub(previous_clip_events[channel])
                            .min(MAX_HISTORY_CLIP_EVENTS) as u32
                    })
                });
                self.current = current.clone();
                self.max_lufs_m = max_lufs_m;
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
        self.max_lufs_m = None;
        self.summary = SessionSummary::default();
        self.observed_frames = 0;
        self.stereo.reset();
        self.clock.reset();
        self.history.reset();
    }

    pub fn snapshot(&self) -> MeterSessionSnapshot {
        MeterSessionSnapshot {
            generation: self.generation,
            state: self.state,
            sample_rate: self.sample_rate,
            active_frames: self.active_frames,
            observed_frames: self.observed_frames,
            current: self.current.clone(),
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
mod tests {
    use super::*;

    const SR: u32 = 48_000;

    fn stereo_sine(seconds: f64, amplitude: f64) -> Vec<f64> {
        let frames = (seconds * SR as f64).round() as usize;
        let mut samples = Vec::with_capacity(frames * 2);
        for frame in 0..frames {
            let sample =
                amplitude * (2.0 * std::f64::consts::PI * 1_000.0 * frame as f64 / SR as f64).sin();
            samples.extend_from_slice(&[sample, sample]);
        }
        samples
    }

    fn stereo_constant(left: f64, right: f64) -> Vec<f64> {
        [left, right]
            .into_iter()
            .cycle()
            .take(SR as usize / 10 * 2)
            .collect()
    }

    fn close(left: Option<f64>, right: Option<f64>, tolerance: f64) -> bool {
        match (left, right) {
            (Some(a), Some(b)) => (a - b).abs() <= tolerance,
            (None, None) => true,
            _ => false,
        }
    }

    fn project_clock(position_samples: i64, epoch: u64) -> MeterClockStart {
        MeterClockStart {
            position_samples: Some(position_samples),
            epoch: Some(epoch),
            source: crate::CaptureClockSource::ProjectTimeline,
        }
    }

    #[test]
    fn starts_empty_and_only_active_audio_advances_time() {
        let mut session = MeterSession::new(SR, 2).unwrap();
        let initial = session.snapshot();
        assert_eq!(initial.state, MeterSessionState::Empty);
        assert_eq!(initial.generation, 1);
        assert_eq!(initial.active_frames, 0);
        assert_eq!(initial.active_seconds(), 0.0);
        assert!(initial.max_lufs_m.is_none());

        assert!(session.push_active(&stereo_sine(0.5, 0.25)));
        let active = session.snapshot();
        assert_eq!(active.state, MeterSessionState::Active);
        assert_eq!(active.active_frames, SR as u64 / 2);
        assert!((active.active_seconds() - 0.5).abs() < 1.0e-12);

        session.pause();
        let paused = session.snapshot();
        assert_eq!(paused.state, MeterSessionState::Paused);
        assert_eq!(paused.active_frames, active.active_frames);
        assert!(session.push_active(&stereo_sine(0.25, 0.25)));
        assert_eq!(session.snapshot().state, MeterSessionState::Active);
        assert_eq!(session.snapshot().active_frames, SR as u64 * 3 / 4);
    }

    #[test]
    fn integrated_lra_peak_and_plr_share_one_session_fact() {
        let samples = stereo_sine(12.0, 0.5);
        let mut reference = MeasureEngine::new(SR, 2).unwrap();
        let _ = reference.push(&samples);
        let expected = reference.finalize();

        let mut session = MeterSession::new(SR, 2).unwrap();
        for chunk in samples.chunks(SR as usize / 5) {
            assert!(session.push_active(chunk));
        }
        let actual = session.snapshot();
        assert!(close(actual.summary.lufs_i, expected.lufs_i, 1.0e-12));
        assert!(close(actual.summary.lra, expected.lra, 1.0e-12));
        assert!(close(
            actual.summary.max_true_peak,
            expected.max_true_peak,
            1.0e-12
        ));
        let expected_plr = expected
            .max_true_peak
            .zip(expected.lufs_i)
            .map(|(peak, integrated)| peak - integrated);
        assert!(close(actual.plr, expected_plr, 1.0e-12));
    }

    #[test]
    fn reset_is_the_only_explicit_discard_boundary() {
        let mut session = MeterSession::new(SR, 2).unwrap();
        assert!(session.push_active(&stereo_sine(1.0, 0.5)));
        session.pause();
        let before = session.snapshot();
        session.reset();
        let reset = session.snapshot();
        assert_eq!(reset.generation, before.generation + 1);
        assert_eq!(reset.state, MeterSessionState::Empty);
        assert_eq!(reset.active_frames, 0);
        assert!(reset.summary.lufs_i.is_none());
        assert!(reset.summary.lra.is_none());
        assert!(reset.summary.max_true_peak.is_none());
        assert!(reset.max_lufs_m.is_none());
        assert!(reset.plr.is_none());
    }

    #[test]
    fn maximum_momentary_is_an_official_session_fact_until_explicit_reset() {
        let loud = stereo_sine(1.0, 0.5);
        let quiet = stereo_sine(1.0, 0.05);
        let mut reference = MeasureEngine::new(SR, 2).unwrap();
        let _ = reference.push(&loud);
        let expected = reference.max_lufs_m();

        let mut session = MeterSession::new(SR, 2).unwrap();
        assert!(session.push_active(&loud));
        let loudest = session.snapshot().max_lufs_m;
        assert!(close(loudest, expected, 1.0e-12));

        session.pause();
        assert!(close(session.snapshot().max_lufs_m, loudest, 1.0e-12));
        assert!(session.push_active(&quiet));
        assert!(close(session.snapshot().max_lufs_m, loudest, 1.0e-12));

        session.reset();
        assert!(session.snapshot().max_lufs_m.is_none());
    }

    #[test]
    fn maximum_momentary_never_runs_ahead_of_the_public_observation_boundary() {
        let mut session = MeterSession::new(SR, 2).unwrap();
        assert!(session.push_active(&stereo_sine(1.0, 0.05)));
        let complete = session.snapshot();

        assert!(session.push_active(&stereo_sine(0.05, 0.9)));
        let partial = session.snapshot();
        assert_eq!(partial.observed_frames, complete.observed_frames);
        assert!(close(partial.max_lufs_m, complete.max_lufs_m, 1.0e-12));

        assert!(session.push_active(&stereo_sine(0.05, 0.9)));
        let next_complete = session.snapshot();
        assert_eq!(
            next_complete.observed_frames,
            complete.observed_frames + SR as u64 / 10
        );
        assert!(next_complete.max_lufs_m.unwrap() > complete.max_lufs_m.unwrap());
    }

    #[test]
    fn malformed_input_fails_without_partial_session_mutation() {
        let mut session = MeterSession::new(SR, 2).unwrap();
        assert!(!session.push_active(&[]));
        assert!(!session.push_active(&[0.0]));
        assert!(!session.push_active(&[0.0, f64::NAN]));
        let snapshot = session.snapshot();
        assert_eq!(snapshot.state, MeterSessionState::Empty);
        assert_eq!(snapshot.active_frames, 0);
    }

    #[test]
    fn history_timestamps_new_channel_clip_runs_at_the_exact_observation() {
        let mut session = MeterSession::new(SR, 2).unwrap();
        assert!(session.push_active_at(&stereo_constant(1.0, 0.5), project_clock(0, 1)));
        assert!(session.push_active_at(&stereo_constant(0.5, 0.5), project_clock(4_800, 1)));
        assert!(session.push_active_at(&stereo_constant(0.5, -1.0), project_clock(9_600, 1)));

        let history = session.recent_history(MeterHistoryResolution::Hz10, 10);
        assert_eq!(history.len(), 3);
        assert_eq!(history[0].last_timeline_endpoint_samples, Some(4_800));
        assert_eq!(history[0].clip_event_count, [1, 0]);
        assert_eq!(history[1].clip_event_count, [0, 0]);
        assert_eq!(history[2].last_timeline_endpoint_samples, Some(14_400));
        assert_eq!(history[2].clip_event_count, [0, 1]);
    }

    #[test]
    fn channel_and_stereo_facts_do_not_depend_on_caller_chunking() {
        let mut samples = stereo_sine(3.2, 0.5);
        for frame in (SR as usize / 2)..(SR as usize / 2 + 37) {
            samples[frame * 2] = 1.1;
        }
        let mut whole = MeterSession::new(SR, 2).unwrap();
        assert!(whole.push_active(&samples));

        let mut chunked = MeterSession::new(SR, 2).unwrap();
        for chunk in samples.chunks(742) {
            assert!(chunked.push_active(chunk));
        }
        let whole_history = whole.recent_history(MeterHistoryResolution::Hz10, 100);
        let chunked_history = chunked.recent_history(MeterHistoryResolution::Hz10, 100);
        assert_eq!(whole_history.len(), chunked_history.len());
        for (whole_point, chunked_point) in whole_history.iter().zip(&chunked_history) {
            assert!(close(whole_point.plr.mean, chunked_point.plr.mean, 1.0e-12));
        }
        let whole = whole.snapshot();
        let chunked = chunked.snapshot();
        assert_eq!(whole.observed_frames, chunked.observed_frames);
        assert_eq!(whole.stereo.clip_events, chunked.stereo.clip_events);
        for channel in 0..2 {
            assert!(close(
                whole.stereo.sample_peak_hold_dbfs[channel],
                chunked.stereo.sample_peak_hold_dbfs[channel],
                1.0e-12
            ));
            assert!(close(
                whole.stereo.max_true_peak_dbtp[channel],
                chunked.stereo.max_true_peak_dbtp[channel],
                1.0e-12
            ));
        }
        assert!(close(
            whole.stereo.balance_db,
            chunked.stereo.balance_db,
            1.0e-12
        ));
        assert!(close(
            whole.stereo.correlation,
            chunked.stereo.correlation,
            1.0e-12
        ));
        assert!(close(whole.plr, chunked.plr, 1.0e-12));
        assert!(close(whole.max_lufs_m, chunked.max_lufs_m, 1.0e-12));
    }

    #[test]
    fn history_retains_exact_and_multi_resolution_facts_while_editor_is_absent() {
        let samples = stereo_sine(3.2, 0.5);
        let mut session = MeterSession::new(SR, 2).unwrap();
        let mut position = 120_000_i64;
        for chunk in samples.chunks(742) {
            assert!(session.push_active_at(chunk, project_clock(position, 9)));
            position += (chunk.len() / 2) as i64;
        }
        let exact = session.recent_history(MeterHistoryResolution::Hz10, 100);
        assert_eq!(exact.len(), 32);
        assert_eq!(exact[0].last_observed_frames, 4_800);
        assert_eq!(exact[0].last_timeline_endpoint_samples, Some(124_800));
        assert_eq!(exact[31].last_observed_frames, 153_600);
        assert_eq!(exact[31].last_timeline_endpoint_samples, Some(273_600));
        assert!(exact[31].plr.mean.is_some());
        assert!(close(exact[31].plr.mean, session.snapshot().plr, 1.0e-12));

        let one_second = session.recent_history(MeterHistoryResolution::Hz1, 100);
        assert_eq!(one_second.len(), 4);
        assert_eq!(one_second[0].observation_count, 10);
        assert_eq!(one_second[3].observation_count, 2);
        assert!(one_second[0].plr.mean.is_some());
        let ten_seconds = session.recent_history(MeterHistoryResolution::Hz0_1, 100);
        assert_eq!(ten_seconds.len(), 1);
        assert_eq!(ten_seconds[0].observation_count, 32);

        session.reset();
        assert!(session
            .recent_history(MeterHistoryResolution::Hz10, 100)
            .is_empty());
    }

    #[test]
    fn history_never_joins_one_observation_across_a_transport_jump() {
        let half_observation = stereo_sine(0.05, 0.5);
        let one_observation = stereo_sine(0.1, 0.5);
        let mut session = MeterSession::new(SR, 2).unwrap();
        assert!(session.push_active_at(&half_observation, project_clock(10_000, 1)));
        assert!(session.push_active_at(&half_observation, project_clock(50_000, 2)));
        assert!(session
            .recent_history(MeterHistoryResolution::Hz10, 10)
            .is_empty());

        assert!(session.push_active_at(&one_observation, project_clock(52_400, 2)));
        let history = session.recent_history(MeterHistoryResolution::Hz10, 10);
        assert_eq!(history.len(), 1);
        assert_eq!(history[0].run_id, 2);
        assert_eq!(history[0].last_timeline_endpoint_samples, Some(57_200));
    }
}
