//! Admission at the existing join, not a second matcher. Local run numbers are never equated.
use super::*;
use chain::{History, Point, Status};

#[derive(Clone, Copy, PartialEq, Eq)]
struct Identity {
    pre_epoch: u64,
    pre_incarnation: u64,
    pre_generation: u64,
    post_epoch: u64,
    post_incarnation: u64,
    post_generation: u64,
}

#[derive(Default)]
pub(super) struct Admission {
    pub history: History,
    identity: Option<Identity>,
    watermark: Option<(u64, u64)>,
    anchor: Option<(u64, u64)>,
    high_water: Option<i64>,
    last_occurrence: Option<i64>,
    last_post_observed: u64,
    ambiguous_through: Option<i64>,
    rate: u32,
    active: bool,
    supported: bool,
}

impl Admission {
    pub fn clear(&mut self, binding: u64) {
        let revision = self.history.revision;
        *self = Self::default();
        self.history.revision = revision;
        self.history.clear(binding, Status::Unavailable);
    }

    pub fn begin(
        &mut self,
        pre: &Publication,
        post: &[MeterHistoryEntry],
        post_clocks: &[crate::meter_clock::MeterClockWitness],
        snapshot: &crate::MeterSessionSnapshot,
        incarnation: u64,
    ) {
        self.rate = snapshot.sample_rate;
        self.active = snapshot.state == crate::MeterSessionState::Active;
        self.history.advance(self.rate, snapshot.observed_frames);
        self.supported = false;
        let Some(p) = pre.points.last() else {
            self.identity = None;
            self.history.unavailable();
            return;
        };
        let Some(w) = p
            .window
            .filter(|_| pre.schema == METER_HISTORY_EXCHANGE_SCHEMA)
        else {
            self.identity = None;
            self.history.unavailable();
            return;
        };
        let Some(_q) = post.last() else {
            self.finish();
            return;
        };
        let identity = Identity {
            pre_epoch: w.measurement_epoch,
            pre_incarnation: w.incarnation,
            pre_generation: p.generation,
            post_epoch: snapshot.measurement_epoch,
            post_incarnation: incarnation,
            post_generation: snapshot.generation,
        };
        if self.identity != Some(identity) {
            let same_session = self.identity.is_some_and(|previous| {
                previous.pre_incarnation == identity.pre_incarnation
                    && previous.post_incarnation == identity.post_incarnation
            });
            let previous_high_water = self.high_water.filter(|_| same_session);
            self.identity = Some(identity);
            self.watermark = Some((p.observed_frames, snapshot.observed_frames));
            self.anchor = None;
            self.high_water = previous_high_water;
            self.last_occurrence = None;
            self.last_post_observed = snapshot.observed_frames;
            self.ambiguous_through = previous_high_water;
            self.history.clear(
                self.history.binding.wrapping_add(1).max(1),
                if previous_high_water.is_some() {
                    Status::Ambiguous
                } else {
                    Status::Syncing
                },
            );
            self.history.advance(self.rate, snapshot.observed_frames);
        }
        self.supported = w.content_clock_qualified
            && window::complete_content_window(post, post_clocks, post.len() - 1, self.rate)
            && w.incarnation > 0
            && w.measurement_epoch > 0
            && snapshot.measurement_epoch > 0;
        if !self.supported {
            self.history.unavailable();
        }
    }

    pub fn ingest(
        &mut self,
        pre: &[WirePoint],
        post: &[MeterHistoryEntry],
        post_clocks: &[crate::meter_clock::MeterClockWitness],
    ) {
        if !self.supported {
            return;
        }
        for (post_index, q) in post.iter().enumerate() {
            if q.last_observed_frames <= self.last_post_observed
                || !window::complete_content_window(post, post_clocks, post_index, self.rate)
            {
                continue;
            }
            let mut matches = pre.iter().filter(|p| {
                p.window
                    .is_some_and(|w| w.complete_400ms && w.content_clock_qualified)
                    && common_occurrence(p, q, post_clocks).is_some()
            });
            let Some(matched) = matches.next() else {
                continue;
            };
            if matches.next().is_some() {
                let occurrence = common_occurrence(matched, q, post_clocks);
                self.high_water = self.high_water.max(occurrence);
                self.ambiguous_through = self.high_water;
                self.anchor = None;
                self.last_post_observed = q.last_observed_frames;
                self.history.clear(
                    self.history.binding.wrapping_add(1).max(1),
                    Status::Ambiguous,
                );
                self.history.advance(self.rate, q.last_observed_frames);
                continue;
            }
            self.observe(matched, q, post_clocks);
        }
    }

    fn observe(
        &mut self,
        p: &WirePoint,
        q: &MeterHistoryEntry,
        post_clocks: &[crate::meter_clock::MeterClockWitness],
    ) {
        let Some(w) = p
            .window
            .filter(|w| w.complete_400ms && w.content_clock_qualified)
        else {
            return;
        };
        let Some(identity) = self.identity else {
            return;
        };
        if identity.pre_epoch != w.measurement_epoch
            || identity.pre_incarnation != w.incarnation
            || identity.pre_generation != p.generation
            || identity.post_epoch != q.measurement_epoch
            || identity.post_generation != q.generation
        {
            return;
        }
        let Some(occurrence) = common_occurrence(p, q, post_clocks) else {
            return;
        };
        if self.last_occurrence.is_some_and(|last| occurrence <= last) {
            self.ambiguous_through = self.high_water;
            self.anchor = None;
            self.watermark = Some((p.observed_frames, q.last_observed_frames));
            self.history.clear(
                self.history.binding.wrapping_add(1).max(1),
                Status::Ambiguous,
            );
            self.history.advance(self.rate, q.last_observed_frames);
        }
        self.last_occurrence = Some(occurrence);
        self.high_water = Some(self.high_water.unwrap_or(i64::MIN).max(occurrence));
        self.last_post_observed = q.last_observed_frames;
        let window = u64::from(self.rate) * 2 / 5;
        let Some((pre_start, post_start)) = self.watermark else {
            return;
        };
        if p.observed_frames
            .checked_sub(pre_start)
            .is_none_or(|n| n < window)
            || q.last_observed_frames
                .checked_sub(post_start)
                .is_none_or(|n| n < window)
        {
            return;
        }
        if self.ambiguous_through.is_some_and(|end| {
            occurrence
                .checked_sub(end)
                .is_none_or(|n| n < window as i64)
        }) {
            return;
        }
        // A consistency check in addition to content-clock authority, never a substitute
        // for proving a shared occurrence on both sides.
        if let Some((pre_anchor, post_anchor)) = self.anchor {
            if p.observed_frames.checked_sub(pre_anchor)
                != q.last_observed_frames.checked_sub(post_anchor)
            {
                self.history.set_status(Status::Ambiguous);
                self.ambiguous_through = self.high_water;
                return;
            }
        } else {
            self.anchor = Some((p.observed_frames, q.last_observed_frames));
        }
        self.ambiguous_through = None;
        self.history.push(Point {
            pre_epoch: w.measurement_epoch,
            post_epoch: q.measurement_epoch,
            pre_incarnation: w.incarnation,
            pre_generation: p.generation,
            post_generation: q.generation,
            pre_run: p.run_id,
            post_run: q.run_id,
            pre_observed: p.observed_frames,
            post_observed: q.last_observed_frames,
            endpoint: occurrence,
            source: p.source,
            pre_m: p.lufs_m,
            post_m: finite(q.lufs_m.mean),
            pre_tp: p.true_peak,
            post_tp: finite(q.true_peak.mean),
        });
    }

    pub fn finish(&mut self) {
        if self.ambiguous_through.is_some() {
            self.history.set_status(Status::Ambiguous);
        } else if self.supported {
            self.history.update_freshness(self.active);
        }
    }

    pub fn progress(&mut self, snapshot: &crate::MeterSessionSnapshot) {
        if self.identity.is_some_and(|id| {
            id.post_epoch != snapshot.measurement_epoch || id.post_generation != snapshot.generation
        }) {
            self.clear(self.history.binding.wrapping_add(1).max(1));
        }
        self.active = snapshot.state == crate::MeterSessionState::Active;
        self.history
            .advance(snapshot.sample_rate, snapshot.observed_frames);
        self.finish();
    }
}

fn common_occurrence(
    pre: &WirePoint,
    post: &MeterHistoryEntry,
    post_clocks: &[crate::meter_clock::MeterClockWitness],
) -> Option<i64> {
    let post_clock = window::witness_for(post, post_clocks)?;
    let pre_auxiliary = pre.auxiliary_endpoint_samples?;
    let post_auxiliary = post_clock.auxiliary_endpoint_samples?;
    pre.output_presentation_samples?;
    post_clock.presentation_latency.output?;
    (pre.auxiliary_source == post_clock.auxiliary_source as u8
        && pre.presentation_source == post_clock.presentation_latency.source as u8
        && matches!((pre.auxiliary_source, pre.presentation_source), (1, 1)))
    .then_some(())?;
    (post_auxiliary == pre_auxiliary).then_some(pre_auxiliary)
}
