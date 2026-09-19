//! Reason-bearing identity for one live POST minus PRE comparison.

use crate::{DeltaMode, DeltaResult};

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum ComparisonState {
    #[default]
    Rejected = 0,
    Preparing = 1,
    Active = 2,
    Holding = 3,
    PostAbsolute = 4,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum ComparisonReason {
    #[default]
    NoPair = 0,
    None = 1,
    AwaitingMeasurement = 2,
    Stale = 3,
    PreBypassed = 4,
    PreInactive = 5,
    LayoutMismatch = 6,
    LayoutUnknown = 7,
    AuditionActive = 8,
    UnsupportedView = 9,
    UnsupportedMetric = 10,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct ComparisonSnapshot {
    pub state: ComparisonState,
    pub reason: ComparisonReason,
    pub generation: u64,
    pub identity: u64,
}

impl ComparisonSnapshot {
    pub fn transition(
        previous: Self,
        delta: &DeltaResult,
        identity: u64,
        reason_override: Option<ComparisonReason>,
    ) -> Self {
        let has_fact = delta.has_current_fact();
        let (state, reason) = match reason_override {
            Some(reason) => (ComparisonState::Rejected, reason),
            None => match delta.mode {
                DeltaMode::Active if has_fact => (ComparisonState::Active, ComparisonReason::None),
                DeltaMode::Active => (
                    ComparisonState::Preparing,
                    ComparisonReason::AwaitingMeasurement,
                ),
                DeltaMode::Stale if delta.last_active.is_some() => {
                    (ComparisonState::Holding, ComparisonReason::Stale)
                }
                DeltaMode::Stale => (ComparisonState::Preparing, ComparisonReason::Stale),
                DeltaMode::NoPre => (ComparisonState::Rejected, ComparisonReason::NoPair),
                DeltaMode::Bypassed => {
                    (ComparisonState::PostAbsolute, ComparisonReason::PreBypassed)
                }
                DeltaMode::PreInactive => {
                    (ComparisonState::PostAbsolute, ComparisonReason::PreInactive)
                }
                DeltaMode::LayoutMismatch => {
                    (ComparisonState::Rejected, ComparisonReason::LayoutMismatch)
                }
                DeltaMode::LayoutUnknown => {
                    (ComparisonState::Rejected, ComparisonReason::LayoutUnknown)
                }
            },
        };
        let changed = previous.generation == 0
            || previous.state != state
            || previous.reason != reason
            || previous.identity != identity;
        Self {
            state,
            reason,
            generation: if changed {
                previous.generation.saturating_add(1).max(1)
            } else {
                previous.generation
            },
            identity,
        }
    }
}

/// Deterministic, process-independent identity for the exact producers and measurement definition.
pub(crate) struct ComparisonIdentity(u64);

impl ComparisonIdentity {
    pub fn new() -> Self {
        Self(0xcbf29ce484222325)
    }

    pub fn text(mut self, value: &str) -> Self {
        self.write(value.as_bytes());
        self.write(&[0xff]);
        self
    }

    pub fn number(mut self, value: u64) -> Self {
        self.write(&value.to_le_bytes());
        self
    }

    pub fn finish(self) -> u64 {
        if self.0 != 0 {
            self.0
        } else {
            1
        }
    }

    fn write(&mut self, bytes: &[u8]) {
        for byte in bytes {
            self.0 ^= u64::from(*byte);
            self.0 = self.0.wrapping_mul(0x100000001b3);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejection_clears_active_state_and_advances_once_per_edge() {
        let active_delta = DeltaResult {
            lufs: Some(1.0),
            mode: DeltaMode::Active,
            ..Default::default()
        };
        let active = ComparisonSnapshot::transition(Default::default(), &active_delta, 7, None);
        assert_eq!(active.state, ComparisonState::Active);
        assert_eq!(active.generation, 1);
        assert_eq!(
            ComparisonSnapshot::transition(active, &active_delta, 7, None).generation,
            1
        );

        let rejected = ComparisonSnapshot::transition(
            active,
            &DeltaResult {
                mode: DeltaMode::LayoutMismatch,
                ..Default::default()
            },
            7,
            None,
        );
        assert_eq!(rejected.state, ComparisonState::Rejected);
        assert_eq!(rejected.reason, ComparisonReason::LayoutMismatch);
        assert_eq!(rejected.generation, 2);
    }

    #[test]
    fn identity_is_stable_and_order_sensitive() {
        let first = ComparisonIdentity::new()
            .text("post")
            .text("pre")
            .number(4)
            .finish();
        let same = ComparisonIdentity::new()
            .text("post")
            .text("pre")
            .number(4)
            .finish();
        let different = ComparisonIdentity::new()
            .text("pre")
            .text("post")
            .number(4)
            .finish();
        assert_eq!(first, same);
        assert_ne!(first, different);
        assert_ne!(first, 0);
    }

    #[test]
    fn stale_only_claims_holding_when_a_matching_value_exists() {
        let preparing = ComparisonSnapshot::transition(
            Default::default(),
            &DeltaResult {
                mode: DeltaMode::Stale,
                ..Default::default()
            },
            11,
            None,
        );
        assert_eq!(preparing.state, ComparisonState::Preparing);

        let holding = ComparisonSnapshot::transition(
            preparing,
            &DeltaResult {
                mode: DeltaMode::Stale,
                last_active: Some(crate::DeltaSnapshot {
                    lufs: Some(0.5),
                    ..Default::default()
                }),
                ..Default::default()
            },
            11,
            None,
        );
        assert_eq!(holding.state, ComparisonState::Holding);
        assert_eq!(holding.reason, ComparisonReason::Stale);
    }
}
