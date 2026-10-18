//! Producer-owned authority order of the take epochs a Record may select.
use super::{bounded_duration_from_block, RecordTakeBlock};

/// Producer-owned preference for one immutable take epoch.
///
/// The numeric order is part of the lock-free selection rule: a producer may upgrade to a more
/// authoritative clock, but a later epoch of the same or lower authority can never replace the
/// already selected take merely because it is longer.
#[repr(u8)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, PartialOrd, Ord)]
pub(super) enum TakeEpochPriority {
    #[default]
    None = 0,
    Rendered = 1,
    Offline = 2,
    NativeRange = 3,
}

impl TakeEpochPriority {
    pub(super) fn from_abi(value: u8) -> Self {
        match value {
            1 => Self::Rendered,
            2 => Self::Offline,
            3 => Self::NativeRange,
            _ => Self::None,
        }
    }

    pub(super) fn for_block(block: &RecordTakeBlock) -> Self {
        if bounded_duration_from_block(block).is_some() {
            Self::NativeRange
        } else if block.offline {
            Self::Offline
        } else {
            Self::Rendered
        }
    }
}
