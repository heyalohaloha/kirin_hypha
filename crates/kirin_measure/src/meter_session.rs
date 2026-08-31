//! Always-on mastering meter session.
//!
//! This state is independent from Record/Keep, Watch playback passes, pairing, and Guide. Only
//! active audio advances it; inactive/bypassed time is a pause, and only an explicit reset clears
//! accumulated statistics. The owner lives outside the replaceable Measure worker so a worker
//! restart does not implicitly discard the session.

use crate::{MeasureEngine, MeasureResult, SessionSummary};

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
    pub summary: SessionSummary,
    pub plr: Option<f64>,
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
    summary: SessionSummary,
}

impl MeterSession {
    pub fn new(sample_rate: u32, n_channels: usize) -> Result<Self, String> {
        Ok(Self {
            engine: MeasureEngine::new(sample_rate, n_channels)?,
            sample_rate,
            n_channels,
            generation: 1,
            active_frames: 0,
            state: MeterSessionState::Empty,
            current: MeasureResult::default(),
            summary: SessionSummary::default(),
        })
    }

    /// Adds one direct, active input span. Replayed Record pre-roll must never call this method.
    /// Invalid spans fail closed without advancing either time or EBU state.
    pub fn push_active(&mut self, interleaved: &[f64]) -> bool {
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
        if let Some(current) = self.engine.push(interleaved) {
            self.current = current;
            self.summary = self.engine.finalize();
        }
        true
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
        self.summary = SessionSummary::default();
    }

    pub fn snapshot(&self) -> MeterSessionSnapshot {
        MeterSessionSnapshot {
            generation: self.generation,
            state: self.state,
            sample_rate: self.sample_rate,
            active_frames: self.active_frames,
            observed_frames: if self.sample_rate == 0 {
                0
            } else {
                let cadence = (self.sample_rate as u64 / 10).max(1);
                self.active_frames / cadence * cadence
            },
            current: self.current.clone(),
            summary: self.summary,
            plr: self
                .summary
                .max_true_peak
                .zip(self.summary.lufs_i)
                .map(|(peak, integrated)| peak - integrated)
                .filter(|value| value.is_finite()),
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

    fn close(left: Option<f64>, right: Option<f64>, tolerance: f64) -> bool {
        match (left, right) {
            (Some(a), Some(b)) => (a - b).abs() <= tolerance,
            (None, None) => true,
            _ => false,
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
        assert!(reset.plr.is_none());
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
}
