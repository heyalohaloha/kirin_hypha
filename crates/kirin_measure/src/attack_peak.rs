//! Fixed causal DRUM peak decision selected by the B-553 development pilot.

use std::collections::VecDeque;

use super::state::{AttackEvent, AttackOdfFrame};

const LOCAL_MEAN_DELTA: f32 = 0.006_25;
const PRE_MAX_HOPS: usize = 3;
const PRE_AVG_HOPS: usize = 24;
const REFRACTORY_MICROS: i64 = 30_000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct TraceIdentity {
    generation: u64,
    sample_rate: u32,
    channels: u8,
    definition_hash: [u8; 32],
}

pub(super) struct AttackPeakPicker {
    identity: Option<TraceIdentity>,
    previous_values: VecDeque<f32>,
    pending: Option<AttackEvent>,
}

impl AttackPeakPicker {
    pub(super) fn new() -> Self {
        Self {
            identity: None,
            previous_values: VecDeque::with_capacity(PRE_AVG_HOPS),
            pending: None,
        }
    }

    pub(super) fn reset(&mut self) {
        self.identity = None;
        self.previous_values.clear();
        self.pending = None;
    }

    pub(super) fn push(&mut self, frame: AttackOdfFrame) -> Option<AttackEvent> {
        let identity = TraceIdentity {
            generation: frame.generation,
            sample_rate: frame.sample_rate,
            channels: frame.channels,
            definition_hash: frame.definition_hash,
        };
        if self.identity != Some(identity) {
            self.reset();
            self.identity = Some(identity);
        }

        let eligible = self.is_eligible(frame.value);
        self.remember(frame.value);
        let candidate = eligible.then(|| AttackEvent::from_odf(frame));
        self.advance(candidate, frame.event_sample, frame.sample_rate)
    }

    fn is_eligible(&self, value: f32) -> bool {
        let sum = self.previous_values.iter().copied().sum::<f32>() + value;
        let local_mean = sum / (PRE_AVG_HOPS + 1) as f32;
        let strict_previous_max = self
            .previous_values
            .iter()
            .rev()
            .take(PRE_MAX_HOPS)
            .all(|previous| value > *previous);
        value >= local_mean + LOCAL_MEAN_DELTA && strict_previous_max
    }

    fn remember(&mut self, value: f32) {
        if self.previous_values.len() == PRE_AVG_HOPS {
            self.previous_values.pop_front();
        }
        self.previous_values.push_back(value);
    }

    fn advance(
        &mut self,
        candidate: Option<AttackEvent>,
        current_sample: i64,
        sample_rate: u32,
    ) -> Option<AttackEvent> {
        let refractory = refractory_samples(sample_rate);
        if let Some(candidate) = candidate {
            if let Some(pending) = self.pending.as_mut() {
                if candidate.event_sample - pending.event_sample <= refractory {
                    if candidate.value > pending.value {
                        *pending = candidate;
                    }
                    return None;
                }
            }
            let emitted = self.pending.replace(candidate);
            return emitted.map(|event| event.decided_at(current_sample));
        }
        if self
            .pending
            .is_some_and(|pending| current_sample - pending.event_sample > refractory)
        {
            return self
                .pending
                .take()
                .map(|event| event.decided_at(current_sample));
        }
        None
    }
}

fn refractory_samples(sample_rate: u32) -> i64 {
    (i64::from(sample_rate) * REFRACTORY_MICROS + 500_000) / 1_000_000
}

#[cfg(test)]
mod tests {
    use super::*;

    fn frame(index: i64, generation: u64, value: f32) -> AttackOdfFrame {
        AttackOdfFrame {
            generation,
            sample_rate: 1_000,
            channels: 1,
            definition_hash: [7; 32],
            window_samples: 20,
            hop_samples: 10,
            support_start_samples: index * 10 - 10,
            support_end_samples: index * 10 + 10,
            event_sample: index * 10,
            value,
        }
    }

    #[test]
    fn local_mean_and_strict_previous_max_are_causal() {
        let mut picker = AttackPeakPicker::new();
        assert!(picker.push(frame(0, 1, 0.0)).is_none());
        assert!(picker.push(frame(1, 1, 0.1)).is_none());
        assert!(picker.push(frame(2, 1, 0.1)).is_none());
        assert!(picker.push(frame(3, 1, 0.05)).is_none());
        assert!(picker.push(frame(4, 1, 0.0)).is_none());
        let event = picker.push(frame(5, 1, 0.0)).unwrap();
        assert_eq!(event.event_sample, 10);
        assert_eq!(event.decision_sample, 50);
    }

    #[test]
    fn refractory_keeps_the_larger_candidate_then_emits_after_thirty_ms() {
        let mut picker = AttackPeakPicker::new();
        for (index, value) in [0.0, 0.1, 0.0, 0.2, 0.0, 0.0, 0.0, 0.0]
            .into_iter()
            .enumerate()
        {
            let emitted = picker.push(frame(index as i64, 1, value));
            if index == 7 {
                let event = emitted.unwrap();
                assert_eq!(event.event_sample, 30);
                assert_eq!(event.value, 0.2);
                assert_eq!(event.decision_sample, 70);
            } else {
                assert!(emitted.is_none());
            }
        }
    }

    #[test]
    fn generation_change_discards_an_unpublished_old_candidate() {
        let mut picker = AttackPeakPicker::new();
        assert!(picker.push(frame(0, 1, 0.2)).is_none());
        for index in 0..6 {
            assert!(picker.push(frame(index, 2, 0.0)).is_none());
        }
    }
}
