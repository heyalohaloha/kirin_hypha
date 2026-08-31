//! Exact PRE/POST DRUM ODF join and common causal event decision.
//!
//! This is the first product-side delta stage. It never shifts traces by correlation: callers
//! must provide frames already mapped to the same content sample and definition.

use std::collections::VecDeque;
use std::fmt;

use super::peak::AttackPeakPicker;
use super::state::AttackOdfFrame;

const MATCH_TOLERANCE_MICROS: i64 = 25_000;
const CANDIDATE_RETENTION_MICROS: i64 = 100_000;
const CANDIDATE_CAPACITY: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
#[repr(u8)]
pub enum AttackPairEventKind {
    Matched = 0,
    PreOnly = 1,
    PostOnly = 2,
    Ambiguous = 3,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AttackPairEvent {
    pub pair_generation: u64,
    pub pre_generation: u64,
    pub post_generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub definition_hash: [u8; 32],
    pub event_sample: i64,
    pub decision_sample: i64,
    pub kind: AttackPairEventKind,
    pub pre_event_sample: Option<i64>,
    pub post_event_sample: Option<i64>,
    pub pre_value: Option<f32>,
    pub post_value: Option<f32>,
    pub delta_value: Option<f32>,
}

impl AttackPairEvent {
    pub fn has_valid_layout(&self) -> bool {
        let presence_matches_kind = match self.kind {
            AttackPairEventKind::Matched => {
                self.pre_event_sample.is_some() && self.post_event_sample.is_some()
            }
            AttackPairEventKind::PreOnly => {
                self.pre_event_sample.is_some() && self.post_event_sample.is_none()
            }
            AttackPairEventKind::PostOnly => {
                self.pre_event_sample.is_none() && self.post_event_sample.is_some()
            }
            AttackPairEventKind::Ambiguous => {
                self.pre_event_sample.is_none() && self.post_event_sample.is_none()
            }
        };
        let delta_matches_values = match (self.pre_value, self.post_value, self.delta_value) {
            (Some(pre), Some(post), Some(delta)) => delta.to_bits() == (post - pre).to_bits(),
            (_, _, None) => true,
            _ => false,
        };
        let tolerance = samples_for_micros(self.sample_rate, MATCH_TOLERANCE_MICROS) as u64;
        self.pair_generation > 0
            && self.pre_generation > 0
            && self.post_generation > 0
            && self.sample_rate > 0
            && matches!(self.channels, 1 | 2)
            && self.decision_sample >= self.event_sample
            && self.pre_value.is_some() == self.pre_event_sample.is_some()
            && self.post_value.is_some() == self.post_event_sample.is_some()
            && presence_matches_kind
            && self.delta_value.is_some() == matches!(self.kind, AttackPairEventKind::Matched)
            && delta_matches_values
            && self
                .pre_event_sample
                .into_iter()
                .chain(self.post_event_sample)
                .all(|sample| sample.abs_diff(self.event_sample) <= tolerance)
            && self
                .pre_value
                .into_iter()
                .chain(self.post_value)
                .chain(self.delta_value)
                .all(f32::is_finite)
            && self
                .pre_value
                .into_iter()
                .chain(self.post_value)
                .all(|value| value >= 0.0)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AttackPairError {
    InvalidPreFrame,
    InvalidPostFrame,
    DefinitionMismatch,
    ContentTimeMismatch,
}

impl fmt::Display for AttackPairError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        let message = match self {
            Self::InvalidPreFrame => "invalid PRE ATTACK frame",
            Self::InvalidPostFrame => "invalid POST ATTACK frame",
            Self::DefinitionMismatch => "PRE/POST ATTACK definitions do not match",
            Self::ContentTimeMismatch => "PRE/POST ATTACK content samples do not match",
        };
        formatter.write_str(message)
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct PairIdentity {
    pre_generation: u64,
    post_generation: u64,
    sample_rate: u32,
    channels: u8,
    definition_hash: [u8; 32],
    window_samples: u32,
    hop_samples: u32,
}

#[derive(Clone, Copy, Debug)]
struct CandidateFrame {
    sample: i64,
    pre_candidate: bool,
    post_candidate: bool,
    pre_value: f32,
    post_value: f32,
}

pub struct AttackPairJoiner {
    identity: Option<PairIdentity>,
    pair_generation: u64,
    pre_picker: AttackPeakPicker,
    post_picker: AttackPeakPicker,
    common_picker: AttackPeakPicker,
    candidates: VecDeque<CandidateFrame>,
    last_pre_match: Option<i64>,
    last_post_match: Option<i64>,
}

impl Default for AttackPairJoiner {
    fn default() -> Self {
        Self::new()
    }
}

impl AttackPairJoiner {
    pub fn new() -> Self {
        Self {
            identity: None,
            pair_generation: 0,
            pre_picker: AttackPeakPicker::new(),
            post_picker: AttackPeakPicker::new(),
            common_picker: AttackPeakPicker::new(),
            candidates: VecDeque::with_capacity(CANDIDATE_CAPACITY),
            last_pre_match: None,
            last_post_match: None,
        }
    }

    pub fn reset(&mut self) {
        self.identity = None;
        self.reset_trace();
    }

    pub fn push(
        &mut self,
        pre: AttackOdfFrame,
        post: AttackOdfFrame,
    ) -> Result<Option<AttackPairEvent>, AttackPairError> {
        let identity = match validate_pair(pre, post) {
            Ok(identity) => identity,
            Err(error) => {
                self.reset();
                return Err(error);
            }
        };
        if self.identity != Some(identity) {
            self.pair_generation = self.pair_generation.saturating_add(1).max(1);
            self.reset_trace();
            self.identity = Some(identity);
        }

        let pre_observation = self.pre_picker.observe(pre);
        let post_observation = self.post_picker.observe(post);
        let common_observation = self.common_picker.observe(AttackOdfFrame {
            generation: self.pair_generation,
            value: pre.value.max(post.value),
            ..pre
        });
        self.remember(CandidateFrame {
            sample: pre.event_sample,
            pre_candidate: pre_observation.candidate,
            post_candidate: post_observation.candidate,
            pre_value: pre.value,
            post_value: post.value,
        });
        let Some(common) = common_observation.emitted else {
            return Ok(None);
        };
        let tolerance = samples_for_micros(identity.sample_rate, MATCH_TOLERANCE_MICROS);
        let pre_match = best_match(
            &self.candidates,
            common.event_sample,
            tolerance,
            true,
            self.last_pre_match,
        );
        let post_match = best_match(
            &self.candidates,
            common.event_sample,
            tolerance,
            false,
            self.last_post_match,
        );
        if let Some(candidate) = pre_match {
            self.last_pre_match = Some(candidate.sample);
        }
        if let Some(candidate) = post_match {
            self.last_post_match = Some(candidate.sample);
        }
        let kind = match (pre_match.is_some(), post_match.is_some()) {
            (true, true) => AttackPairEventKind::Matched,
            (true, false) => AttackPairEventKind::PreOnly,
            (false, true) => AttackPairEventKind::PostOnly,
            (false, false) => AttackPairEventKind::Ambiguous,
        };
        let pre_value = pre_match.map(|candidate| candidate.pre_value);
        let post_value = post_match.map(|candidate| candidate.post_value);
        let delta_value = pre_value.zip(post_value).map(|(pre, post)| post - pre);
        Ok(Some(AttackPairEvent {
            pair_generation: self.pair_generation,
            pre_generation: identity.pre_generation,
            post_generation: identity.post_generation,
            sample_rate: identity.sample_rate,
            channels: identity.channels,
            definition_hash: identity.definition_hash,
            event_sample: common.event_sample,
            decision_sample: common.decision_sample,
            kind,
            pre_event_sample: pre_match.map(|candidate| candidate.sample),
            post_event_sample: post_match.map(|candidate| candidate.sample),
            pre_value,
            post_value,
            delta_value,
        }))
    }

    fn reset_trace(&mut self) {
        self.pre_picker.reset();
        self.post_picker.reset();
        self.common_picker.reset();
        self.candidates.clear();
        self.last_pre_match = None;
        self.last_post_match = None;
    }

    fn remember(&mut self, candidate: CandidateFrame) {
        let retention = self
            .identity
            .map(|identity| samples_for_micros(identity.sample_rate, CANDIDATE_RETENTION_MICROS))
            .unwrap_or(0);
        while self
            .candidates
            .front()
            .is_some_and(|oldest| candidate.sample - oldest.sample > retention)
        {
            self.candidates.pop_front();
        }
        if self.candidates.len() == CANDIDATE_CAPACITY {
            self.candidates.pop_front();
        }
        self.candidates.push_back(candidate);
    }
}

fn validate_pair(
    pre: AttackOdfFrame,
    post: AttackOdfFrame,
) -> Result<PairIdentity, AttackPairError> {
    if !pre.has_valid_layout() {
        return Err(AttackPairError::InvalidPreFrame);
    }
    if !post.has_valid_layout() {
        return Err(AttackPairError::InvalidPostFrame);
    }
    if pre.sample_rate != post.sample_rate
        || pre.channels != post.channels
        || pre.definition_hash != post.definition_hash
        || pre.window_samples != post.window_samples
        || pre.hop_samples != post.hop_samples
    {
        return Err(AttackPairError::DefinitionMismatch);
    }
    if pre.support_start_samples != post.support_start_samples
        || pre.support_end_samples != post.support_end_samples
        || pre.event_sample != post.event_sample
    {
        return Err(AttackPairError::ContentTimeMismatch);
    }
    Ok(PairIdentity {
        pre_generation: pre.generation,
        post_generation: post.generation,
        sample_rate: pre.sample_rate,
        channels: pre.channels,
        definition_hash: pre.definition_hash,
        window_samples: pre.window_samples,
        hop_samples: pre.hop_samples,
    })
}

fn best_match(
    candidates: &VecDeque<CandidateFrame>,
    common_sample: i64,
    tolerance: i64,
    pre: bool,
    last_match: Option<i64>,
) -> Option<CandidateFrame> {
    candidates
        .iter()
        .copied()
        .filter(|candidate| {
            (if pre {
                candidate.pre_candidate
            } else {
                candidate.post_candidate
            }) && Some(candidate.sample) != last_match
                && candidate.sample.abs_diff(common_sample) <= tolerance as u64
        })
        .min_by(|left, right| {
            let left_distance = left.sample.abs_diff(common_sample);
            let right_distance = right.sample.abs_diff(common_sample);
            let left_value = if pre { left.pre_value } else { left.post_value };
            let right_value = if pre {
                right.pre_value
            } else {
                right.post_value
            };
            left_distance
                .cmp(&right_distance)
                .then_with(|| right_value.total_cmp(&left_value))
                .then_with(|| left.sample.cmp(&right.sample))
        })
}

fn samples_for_micros(sample_rate: u32, micros: i64) -> i64 {
    (i64::from(sample_rate) * micros + 500_000) / 1_000_000
}

#[cfg(test)]
#[path = "attack_pair_tests.rs"]
mod tests;
