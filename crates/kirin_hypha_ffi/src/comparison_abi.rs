//! Reason-bearing live-comparison snapshot and its C ABI projection.

use kirin_measure::{ComparisonReason, ComparisonSnapshot, ComparisonState, DeltaResult};

use crate::{KirinDelta, KirinMeterSession};

pub const KIRIN_COMPARISON_STATE_REJECTED: u8 = 0;
pub const KIRIN_COMPARISON_STATE_PREPARING: u8 = 1;
pub const KIRIN_COMPARISON_STATE_ACTIVE: u8 = 2;
pub const KIRIN_COMPARISON_STATE_HOLDING: u8 = 3;
pub const KIRIN_COMPARISON_STATE_POST_ABSOLUTE: u8 = 4;
pub const KIRIN_COMPARISON_REASON_NO_PAIR: u8 = 0;
pub const KIRIN_COMPARISON_REASON_NONE: u8 = 1;
pub const KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT: u8 = 2;
pub const KIRIN_COMPARISON_REASON_STALE: u8 = 3;
pub const KIRIN_COMPARISON_REASON_PRE_BYPASSED: u8 = 4;
pub const KIRIN_COMPARISON_REASON_PRE_INACTIVE: u8 = 5;
pub const KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH: u8 = 6;
pub const KIRIN_COMPARISON_REASON_LAYOUT_UNKNOWN: u8 = 7;
pub const KIRIN_COMPARISON_REASON_AUDITION_ACTIVE: u8 = 8;
pub const KIRIN_COMPARISON_REASON_UNSUPPORTED_VIEW: u8 = 9;
pub const KIRIN_COMPARISON_REASON_UNSUPPORTED_METRIC: u8 = 10;

/// Observatory display facts returned by one non-RT poll.
#[repr(C)]
pub struct KirinObservatoryFrame {
    pub version: u32,
    pub signal_state: u8,
    pub lra_state: u8,
    pub delta_available: u8,
    pub comparison_state: u8,
    pub lra_elapsed_seconds: f64,
    pub meter: KirinMeterSession,
    pub delta: KirinDelta,
    pub comparison_reason: u8,
    pub comparison_reserved: [u8; 7],
    pub comparison_generation: u64,
    pub comparison_identity: u64,
}

pub(crate) struct ComparisonProjection {
    pub state: u8,
    pub reason: u8,
    pub generation: u64,
    pub identity: u64,
}

pub(crate) fn comparison_projection(
    delta: &DeltaResult,
    measurement_epoch: u64,
    meter_generation: u64,
) -> ComparisonProjection {
    let snapshot = if delta.comparison.generation == 0 {
        ComparisonSnapshot::transition(Default::default(), delta, delta.comparison.identity, None)
    } else {
        delta.comparison
    };
    let identity =
        snapshot.identity ^ measurement_epoch.rotate_left(17) ^ meter_generation.rotate_left(41);
    ComparisonProjection {
        state: state_to_abi(snapshot.state),
        reason: reason_to_abi(snapshot.reason),
        generation: snapshot.generation,
        identity: if identity != 0 { identity } else { 1 },
    }
}

fn state_to_abi(state: ComparisonState) -> u8 {
    match state {
        ComparisonState::Rejected => KIRIN_COMPARISON_STATE_REJECTED,
        ComparisonState::Preparing => KIRIN_COMPARISON_STATE_PREPARING,
        ComparisonState::Active => KIRIN_COMPARISON_STATE_ACTIVE,
        ComparisonState::Holding => KIRIN_COMPARISON_STATE_HOLDING,
        ComparisonState::PostAbsolute => KIRIN_COMPARISON_STATE_POST_ABSOLUTE,
    }
}

fn reason_to_abi(reason: ComparisonReason) -> u8 {
    match reason {
        ComparisonReason::NoPair => KIRIN_COMPARISON_REASON_NO_PAIR,
        ComparisonReason::None => KIRIN_COMPARISON_REASON_NONE,
        ComparisonReason::AwaitingMeasurement => KIRIN_COMPARISON_REASON_AWAITING_MEASUREMENT,
        ComparisonReason::Stale => KIRIN_COMPARISON_REASON_STALE,
        ComparisonReason::PreBypassed => KIRIN_COMPARISON_REASON_PRE_BYPASSED,
        ComparisonReason::PreInactive => KIRIN_COMPARISON_REASON_PRE_INACTIVE,
        ComparisonReason::LayoutMismatch => KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH,
        ComparisonReason::LayoutUnknown => KIRIN_COMPARISON_REASON_LAYOUT_UNKNOWN,
        ComparisonReason::AuditionActive => KIRIN_COMPARISON_REASON_AUDITION_ACTIVE,
        ComparisonReason::UnsupportedView => KIRIN_COMPARISON_REASON_UNSUPPORTED_VIEW,
        ComparisonReason::UnsupportedMetric => KIRIN_COMPARISON_REASON_UNSUPPORTED_METRIC,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use kirin_measure::{DeltaMode, DeltaResult};

    #[test]
    fn projection_maps_reason_and_binds_meter_execution() {
        let delta = DeltaResult {
            mode: DeltaMode::LayoutMismatch,
            comparison: ComparisonSnapshot {
                state: ComparisonState::Rejected,
                reason: ComparisonReason::LayoutMismatch,
                generation: 4,
                identity: 9,
            },
            ..Default::default()
        };
        let first = comparison_projection(&delta, 3, 5);
        let next_execution = comparison_projection(&delta, 4, 5);
        assert_eq!(first.state, KIRIN_COMPARISON_STATE_REJECTED);
        assert_eq!(first.reason, KIRIN_COMPARISON_REASON_LAYOUT_MISMATCH);
        assert_eq!(first.generation, 4);
        assert_ne!(first.identity, next_execution.identity);
    }
}
