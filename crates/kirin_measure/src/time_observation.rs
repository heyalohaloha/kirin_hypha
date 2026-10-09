//! Original, unaggregated TIME facts. Owned only by the non-RT Meter Session.
use std::collections::VecDeque;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};

pub const TIME_RAW_CAPACITY: usize = 64;
pub const TIME_CURRENT_TTL: Duration = Duration::from_millis(400);
pub const TIME_VALUE_COUNT: usize = 6;
pub const TIME_PSR: usize = 3;

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq, Serialize, Deserialize)]
pub struct TimeSourceSpan {
    pub epoch: u64,
    pub incarnation: u64,
    pub generation: u64,
    pub token: u64,
    pub sample_rate: u32,
    pub channels: u8,
}

impl TimeSourceSpan {
    pub fn valid(self) -> bool {
        self.epoch != 0
            && self.incarnation != 0
            && self.generation != 0
            && self.token != 0
            && self.sample_rate != 0
            && matches!(self.channels, 1 | 2 | 6)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
pub struct TimeWirePoint {
    pub span: TimeSourceSpan,
    pub run: u64,
    pub observed: u64,
    pub endpoint: Option<i64>,
    pub clock: u8,
    pub usable: bool,
    /// Complete input coverage in this clock run; absent older publications cannot prove windows.
    #[serde(default)]
    pub continuous_frames: u64,
    #[serde(default)]
    pub latency_known: bool,
    /// LEVEL CREST at the same 400 ms aperture; not an extra TIME ABI value.
    #[serde(default)]
    pub crest: Option<f64>,
    /// M, S, TP, PSR, PLR, CORR, all at this complete 100 ms boundary.
    pub values: [Option<f64>; TIME_VALUE_COUNT],
}

impl TimeWirePoint {
    pub fn valid(self, span: TimeSourceSpan) -> bool {
        self.span == span
            && span.valid()
            && self.run != 0
            && self.observed != 0
            && self.values.into_iter().flatten().all(f64::is_finite)
            && self.crest.is_none_or(f64::is_finite)
            && self.clock <= 2
            && (!self.usable || self.clock == 0 || self.endpoint.is_some())
    }

    pub fn window_proven(self, metric: usize) -> bool {
        let milliseconds = match metric {
            0 | 2 => 400,
            1 | 3 | 5 => 3000,
            _ => return false,
        };
        // LEVEL/TIME retain the host-clock pairing contract even when the optional
        // presentation-latency callback is absent. Aperture coverage is still mandatory.
        self.continuous_frames >= (u64::from(self.span.sample_rate) * milliseconds).div_ceil(1000)
    }

    pub fn exact_key(self) -> Option<(u8, i64)> {
        (self.usable && matches!(self.clock, 1 | 2)).then_some((self.clock, self.endpoint?))
    }
}

#[derive(Clone, Debug)]
pub struct TimeRawPoint {
    pub wire: TimeWirePoint,
    /// Never serialized or replaced by IO/poll/rejoin time; only the local original is used.
    pub completed: Instant,
}

impl TimeRawPoint {
    pub fn remaining(&self, now: Instant) -> Duration {
        TIME_CURRENT_TTL.saturating_sub(now.saturating_duration_since(self.completed))
    }
}

pub(super) struct TimeObservations {
    pub(super) token: Arc<AtomicU64>,
    points: VecDeque<TimeRawPoint>,
}

impl TimeObservations {
    pub(super) fn new() -> Self {
        Self {
            token: Arc::new(AtomicU64::new(next_token())),
            points: VecDeque::with_capacity(TIME_RAW_CAPACITY),
        }
    }

    pub(super) fn push(&mut self, wire: TimeWirePoint, completed: Instant) {
        if self.points.len() == TIME_RAW_CAPACITY {
            self.points.pop_front();
        }
        self.points.push_back(TimeRawPoint { wire, completed });
    }

    pub(super) fn reset(&mut self) {
        self.token.store(next_token(), Ordering::Release);
        self.points.clear();
    }

    pub(super) fn tail(&self, capacity: usize) -> Vec<TimeRawPoint> {
        self.points
            .iter()
            .skip(
                self.points
                    .len()
                    .saturating_sub(capacity.min(TIME_RAW_CAPACITY)),
            )
            .cloned()
            .collect()
    }

    pub(super) fn latest_observed(&self) -> Option<u64> {
        self.points.back().map(|point| point.wire.observed)
    }

    /// A clock boundary retires current facts, while the same source's history survives.
    pub(super) fn retire_current(&mut self) {
        self.points.clear();
    }

    pub(super) fn retire_other_run(&mut self, run: Option<u64>) {
        if self
            .points
            .back()
            .is_some_and(|point| Some(point.wire.run) != run)
        {
            self.retire_current();
        }
    }
}

fn next_token() -> u64 {
    static NEXT: AtomicU64 = AtomicU64::new(1);
    NEXT.fetch_add(1, Ordering::Relaxed)
}
