//! Sized snapshot ABI vocabulary. Numeric fields have meaning only with their explicit tags.

pub const KIRIN_SNAPSHOT_SUCCESS: u8 = 0;
pub const KIRIN_SNAPSHOT_BUSY: u8 = 1;
pub const KIRIN_SNAPSHOT_INVALID_REQUEST: u8 = 2;
pub const KIRIN_SNAPSHOT_UNSUPPORTED: u8 = 3;
pub const KIRIN_SNAPSHOT_RETIRED: u8 = 4;

pub const KIRIN_ENDPOINT_FINITE: u8 = 0;
pub const KIRIN_ENDPOINT_NEGATIVE_INFINITY: u8 = 1;
pub const KIRIN_ENDPOINT_POSITIVE_INFINITY: u8 = 2;
pub const KIRIN_INTERVAL_MILLISECONDS: u8 = 0;
pub const KIRIN_INTERVAL_DECIBELS: u8 = 1;
pub const KIRIN_SCALAR_EXACT: u8 = 0;
pub const KIRIN_SCALAR_BOUND: u8 = 1;
pub const KIRIN_SCALAR_UNKNOWN: u8 = 2;
pub const KIRIN_SCALAR_PENDING: u8 = 3;
pub const KIRIN_SCALAR_NOT_APPLICABLE: u8 = 4;
pub const KIRIN_FINISH_ACQUIRING: u8 = 0;
pub const KIRIN_FINISH_FULL: u8 = 1;
pub const KIRIN_FINISH_AUDIO_END: u8 = 2;
pub const KIRIN_FINISH_NOT_KEPT: u8 = 3;
pub const KIRIN_FINISH_RETIRED: u8 = 4;
pub const KIRIN_SNAPSHOT_ALL_LIVE: u8 = 0;
pub const KIRIN_SNAPSHOT_BAND_SUMMARY: u8 = 1;
pub const KIRIN_SNAPSHOT_SINGLE: u8 = 2;
pub const KIRIN_SNAPSHOT_TIME: u8 = 3;
pub const KIRIN_TARGET_POST: u8 = 0;
pub const KIRIN_TARGET_DELTA: u8 = 1;
pub const KIRIN_TARGET_PRE: u8 = 2;

// Stable reason codes shared by Summary and Single. The unused count slots remain zero.
pub const KIRIN_REASON_NONE: u8 = 0;
pub const KIRIN_REASON_NO_PAIR: u8 = 1;
pub const KIRIN_REASON_SILENT: u8 = 2;
pub const KIRIN_REASON_BOTH_SILENT: u8 = 3;
pub const KIRIN_REASON_RINGING: u8 = 4;
pub const KIRIN_REASON_NEXT_HIT: u8 = 5;
pub const KIRIN_REASON_NOT_KEPT: u8 = 6;
pub const KIRIN_REASON_MAPPING: u8 = 7;
pub const KIRIN_REASON_CLOCK: u8 = 8;
pub const KIRIN_REASON_SOURCE_CHANGED: u8 = 9;
pub const KIRIN_REASON_WORKER_UNAVAILABLE: u8 = 10;
pub const KIRIN_REASON_REQUEST_DEADLINE: u8 = 11;
pub const KIRIN_REASON_WAITING_AUDIO: u8 = 12;
pub const KIRIN_REASON_WAITING_SERVICE: u8 = 13;
pub const KIRIN_REASON_WAITING_PUBLICATION: u8 = 14;
pub const KIRIN_REASON_SEMANTICS: u8 = 15;
pub const KIRIN_REASON_LONG_TAIL: u8 = 16;
pub const KIRIN_REASON_AUDIO_END: u8 = 17;
pub const KIRIN_REASON_COUNT: usize = 18;
pub const KIRIN_REASON_CAPACITY: usize = 32;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KirinSnapshotSourceKey {
    pub incarnation: [u8; 16],
    pub generation: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub reserved: [u8; 3],
    pub odf_hash: [u8; 32],
}

impl KirinSnapshotSourceKey {
    pub fn is_valid(&self) -> bool {
        self.incarnation != [0; 16]
            && self.generation != 0
            && self.sample_rate != 0
            && matches!(self.channels, 1 | 2)
            && self.reserved == [0; 3]
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KirinSnapshotEventKey {
    pub source: KirinSnapshotSourceKey,
    pub event_sample: i64,
    /// Producer-owned stable alias token; pair kind is deliberately absent from this key.
    pub token: u64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinSnapshotEndpoint {
    pub kind: u8,
    pub closed: u8,
    pub reserved: [u8; 6],
    pub value: f64,
}

impl KirinSnapshotEndpoint {
    pub fn finite(value: f64, closed: bool) -> Self {
        Self {
            value,
            closed: u8::from(closed),
            ..Self::default()
        }
    }

    pub fn negative_infinity() -> Self {
        Self {
            kind: KIRIN_ENDPOINT_NEGATIVE_INFINITY,
            ..Self::default()
        }
    }

    pub fn positive_infinity() -> Self {
        Self {
            kind: KIRIN_ENDPOINT_POSITIVE_INFINITY,
            ..Self::default()
        }
    }

    pub fn is_valid(&self) -> bool {
        self.reserved == [0; 6]
            && self.closed <= 1
            && match self.kind {
                KIRIN_ENDPOINT_FINITE => self.value.is_finite(),
                KIRIN_ENDPOINT_NEGATIVE_INFINITY | KIRIN_ENDPOINT_POSITIVE_INFINITY => {
                    self.closed == 0 && self.value == 0.0
                }
                _ => false,
            }
    }

    pub fn extended_value(&self) -> f64 {
        match self.kind {
            KIRIN_ENDPOINT_NEGATIVE_INFINITY => f64::NEG_INFINITY,
            KIRIN_ENDPOINT_POSITIVE_INFINITY => f64::INFINITY,
            _ => self.value,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinSnapshotInterval {
    pub lower: KirinSnapshotEndpoint,
    pub upper: KirinSnapshotEndpoint,
    pub unit: u8,
    pub reserved: [u8; 7],
}

impl KirinSnapshotInterval {
    pub fn point(value: f64, unit: u8) -> Self {
        Self {
            lower: KirinSnapshotEndpoint::finite(value, true),
            upper: KirinSnapshotEndpoint::finite(value, true),
            unit,
            reserved: [0; 7],
        }
    }

    pub fn is_valid(&self) -> bool {
        if self.unit > KIRIN_INTERVAL_DECIBELS
            || self.reserved != [0; 7]
            || !self.lower.is_valid()
            || !self.upper.is_valid()
            || self.lower.kind == KIRIN_ENDPOINT_POSITIVE_INFINITY
            || self.upper.kind == KIRIN_ENDPOINT_NEGATIVE_INFINITY
        {
            return false;
        }
        let lower = self.lower.extended_value();
        let upper = self.upper.extended_value();
        lower < upper || (lower == upper && self.lower.closed == 1 && self.upper.closed == 1)
    }

    pub fn is_point(&self) -> bool {
        self.is_valid()
            && self.lower.kind == KIRIN_ENDPOINT_FINITE
            && self.upper.kind == KIRIN_ENDPOINT_FINITE
            && self.lower.value == self.upper.value
    }

    pub fn is_all_real(&self) -> bool {
        self.is_valid()
            && self.lower.kind == KIRIN_ENDPOINT_NEGATIVE_INFINITY
            && self.upper.kind == KIRIN_ENDPOINT_POSITIVE_INFINITY
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinSnapshotScalarEvidence {
    pub interval: KirinSnapshotInterval,
    pub measurement_revision: u64,
    pub proof_revision: u64,
    pub requested_start: i64,
    pub requested_end: i64,
    pub actual_start: i64,
    pub actual_end: i64,
    pub resolution: f64,
    pub class: u8,
    pub reason: u8,
    pub has_interval: u8,
    pub finish: u8,
    pub reserved: [u8; 4],
}

impl KirinSnapshotScalarEvidence {
    pub fn unavailable(class: u8, reason: u8) -> Self {
        Self {
            class,
            reason,
            ..Self::default()
        }
    }

    pub fn numeric(interval: KirinSnapshotInterval, reason: u8) -> Self {
        Self {
            class: if interval.is_point() {
                KIRIN_SCALAR_EXACT
            } else {
                KIRIN_SCALAR_BOUND
            },
            interval,
            reason,
            has_interval: 1,
            ..Self::default()
        }
    }

    pub fn is_valid(&self) -> bool {
        self.class <= KIRIN_SCALAR_NOT_APPLICABLE
            && (self.reason as usize) < KIRIN_REASON_COUNT
            && self.finish <= KIRIN_FINISH_RETIRED
            && self.reserved == [0; 4]
            && self.resolution.is_finite()
            && self.resolution >= 0.0
            && match self.class {
                KIRIN_SCALAR_EXACT => self.has_interval == 1 && self.interval.is_point(),
                KIRIN_SCALAR_BOUND => {
                    self.has_interval == 1 && self.interval.is_valid() && !self.interval.is_point()
                }
                _ => self.has_interval == 0 && self.interval == KirinSnapshotInterval::default(),
            }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KirinSnapshotHeader {
    pub version: u32,
    pub struct_size: u32,
    pub kind: u8,
    pub target: u8,
    pub band: u8,
    pub signal_state: u8,
    pub flags: u32,
    pub snapshot_revision: u64,
    pub authority_revision: u64,
    pub cutoff_sample: i64,
    pub source: KirinSnapshotSourceKey,
    pub band_semantic_hash: [u8; 32],
}

/// All error paths are checked before this single write. Caller owns `out_size` writable bytes.
pub(crate) unsafe fn commit_sized<T: Copy>(out: *mut T, out_size: u32, value: T) -> u8 {
    if out.is_null() || (out_size as usize) < std::mem::size_of::<T>() {
        return KIRIN_SNAPSHOT_INVALID_REQUEST;
    }
    if !(out as usize).is_multiple_of(std::mem::align_of::<T>()) {
        return KIRIN_SNAPSHOT_INVALID_REQUEST;
    }
    // SAFETY: required storage size and alignment are checked; FFI callers guarantee writable memory.
    unsafe { out.write(value) };
    KIRIN_SNAPSHOT_SUCCESS
}
