//! Derived presentation facts; only the existing exact history join may supply pairs.
//! This is not a compressor detector, a quality score, or an audio processor.
use std::collections::VecDeque;

pub const CAPACITY: usize = 600;

#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Status {
    #[default]
    Unavailable = 0,
    Syncing = 1,
    Active = 2,
    Hold = 3,
    Ambiguous = 4,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Crossing {
    Missing = 0,
    None = 1,
    PreOnly = 2,
    PostOnly = 3,
    Both = 4,
}

/// 0 missing, 1 at/below -1, 2 strictly above -1, 3 strictly above 0.
pub fn severity(value: Option<f64>) -> u8 {
    match value.filter(|v| v.is_finite()) {
        None => 0,
        Some(v) if v > 0.0 => 3,
        Some(v) if v > -1.0 => 2,
        Some(_) => 1,
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Point {
    pub pre_epoch: u64,
    pub post_epoch: u64,
    pub pre_incarnation: u64,
    pub pre_generation: u64,
    pub post_generation: u64,
    pub pre_run: u64,
    pub post_run: u64,
    pub pre_observed: u64,
    pub post_observed: u64,
    pub endpoint: i64,
    pub source: u8,
    pub pre_m: Option<f64>,
    pub post_m: Option<f64>,
    pub pre_tp: Option<f64>,
    pub post_tp: Option<f64>,
}

impl Point {
    pub fn delta_m(&self) -> Option<f64> {
        difference(self.post_m, self.pre_m)
    }
    pub fn delta_tp(&self) -> Option<f64> {
        difference(self.post_tp, self.pre_tp)
    }
    pub fn relation(&self) -> Option<f64> {
        difference(self.delta_tp(), self.delta_m())
    }
    pub fn crossing(&self) -> Crossing {
        match (severity(self.pre_tp), severity(self.post_tp)) {
            (0, _) | (_, 0) => Crossing::Missing,
            (1, 1) => Crossing::None,
            (1, _) => Crossing::PostOnly,
            (_, 1) => Crossing::PreOnly,
            _ => Crossing::Both,
        }
    }
}

fn difference(post: Option<f64>, pre: Option<f64>) -> Option<f64> {
    post.filter(|v| v.is_finite())
        .zip(pre.filter(|v| v.is_finite()))
        .map(|(post, pre)| post - pre)
        .filter(|v| v.is_finite())
}

/// Compute once when the producer admits a new point, never for every UI poll.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct MatchedPoint {
    pub raw: Point,
    pub delta_m: Option<f64>,
    pub delta_tp: Option<f64>,
    pub relation: Option<f64>,
    pub crossing: Crossing,
    pub pre_severity: u8,
    pub post_severity: u8,
}

impl From<Point> for MatchedPoint {
    fn from(raw: Point) -> Self {
        let delta_m = raw.delta_m();
        let delta_tp = raw.delta_tp();
        Self {
            raw,
            delta_m,
            delta_tp,
            relation: difference(delta_tp, delta_m),
            crossing: raw.crossing(),
            pre_severity: severity(raw.pre_tp),
            post_severity: severity(raw.post_tp),
        }
    }
}

#[derive(Clone, Debug)]
pub struct Snapshot {
    pub revision: u64,
    pub binding: u64,
    pub status: Status,
    pub sample_rate: u32,
    pub post_observed: u64,
    pub points: Vec<MatchedPoint>,
}

/// Monotonic sample progress, not poll counts or wall-clock matching, determines freshness.
pub(super) struct History {
    pub revision: u64,
    pub binding: u64,
    pub status: Status,
    rate: u32,
    now: u64,
    points: VecDeque<MatchedPoint>,
}

impl Default for History {
    fn default() -> Self {
        Self {
            revision: 1,
            binding: 0,
            status: Status::Unavailable,
            rate: 0,
            now: 0,
            // PRE/unpaired/unsupported instances need no compound-history allocation.
            points: VecDeque::new(),
        }
    }
}

impl History {
    pub fn clear(&mut self, binding: u64, status: Status) {
        self.points.clear();
        self.binding = binding;
        self.now = 0;
        self.status = status;
        self.touch();
    }

    pub fn advance(&mut self, rate: u32, observed: u64) {
        if self.rate != rate || self.now != observed {
            self.rate = rate;
            self.now = observed;
            self.touch();
        }
        while self
            .points
            .front()
            .is_some_and(|p| observed.saturating_sub(p.raw.post_observed) >= u64::from(rate) * 60)
        {
            self.points.pop_front();
        }
    }

    pub fn unavailable(&mut self) {
        if self.status != Status::Unavailable || !self.points.is_empty() {
            self.clear(self.binding.wrapping_add(1).max(1), Status::Unavailable);
        }
    }

    pub fn push(&mut self, point: Point) {
        if self
            .points
            .back()
            .is_some_and(|p| p.raw.post_observed >= point.post_observed)
        {
            return;
        }
        if self.points.len() == CAPACITY {
            self.points.pop_front();
        }
        if self.points.capacity() == 0 {
            self.points.reserve_exact(CAPACITY);
        }
        self.points.push_back(point.into());
        self.touch();
    }

    pub fn set_status(&mut self, next: Status) {
        if self.status != next {
            self.status = next;
            self.touch();
        }
    }

    pub fn update_freshness(&mut self, active: bool) {
        let next = match self.points.back() {
            Some(p)
                if active
                    && self
                        .now
                        .checked_sub(p.raw.post_observed)
                        .is_some_and(|lag| lag <= u64::from(self.rate) / 5) =>
            {
                Status::Active
            }
            Some(_) => Status::Hold,
            None => Status::Syncing,
        };
        self.set_status(next);
    }

    pub fn snapshot(&self, known_revision: u64) -> Option<Snapshot> {
        (known_revision != self.revision).then(|| Snapshot {
            revision: self.revision,
            binding: self.binding,
            status: self.status,
            sample_rate: self.rate,
            post_observed: self.now,
            points: self.points.iter().copied().collect(),
        })
    }

    fn touch(&mut self) {
        self.revision = self.revision.wrapping_add(1).max(1);
    }
}

#[cfg(test)]
#[path = "chain_observation_tests.rs"]
mod tests;
