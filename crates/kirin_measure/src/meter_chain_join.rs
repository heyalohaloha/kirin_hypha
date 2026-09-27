//! Exact content-window admission. The current TIME absolute-history join remains independent.
//! Neither a local 100 ms endpoint nor a PDC number relabels a completed value here.

use super::*;
use crate::meter_clock::ClockRunOrigin;
use chain::{History, Point, Status};

#[derive(Clone, Copy, PartialEq, Eq)]
struct Identity {
    pre_policy: u8,
    post_policy: u8,
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
    pair_run: Option<(u64, u64)>,
    /// A one-sided clock jump is not a comparison rebind. Both local runs must leave the
    /// prior pair before the new playback occurrence can even be considered for admission.
    awaiting_rejoin_from: Option<(u64, u64)>,
    high_water: Option<i64>,
    /// A new comparison run cannot reuse any PRE window already in the publication tail
    /// when its boundary was observed. PRE and POST frame counters are local, not comparable.
    fresh_pre_after: Option<u64>,
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
        post: &[ContentWirePoint],
        snapshot: &crate::MeterSessionSnapshot,
        incarnation: u64,
        local_policy: u8,
    ) {
        self.rate = snapshot.sample_rate;
        self.active = snapshot.state == crate::MeterSessionState::Active;
        self.history.advance(self.rate, snapshot.observed_frames);
        self.supported = false;
        let Some(p) = pre.content_windows.last() else {
            self.history.unavailable();
            return;
        };
        let Some(q) = post.last() else {
            self.finish();
            return;
        };
        let identity = Identity {
            pre_policy: pre.clock_policy,
            post_policy: local_policy,
            pre_epoch: p.measurement_epoch,
            pre_incarnation: p.incarnation,
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
            self.pair_run = None;
            self.awaiting_rejoin_from = None;
            self.high_water = previous_high_water;
            self.fresh_pre_after = Some(p.observed_frames);
            self.last_occurrence = None;
            self.last_post_observed = q.observed_frames;
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
        self.supported = identity.pre_policy == CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3
            && identity.post_policy == identity.pre_policy
            && p.valid(self.rate)
            && q.valid(self.rate)
            && identity.pre_epoch > 0
            && identity.post_epoch > 0
            && host_clocks_present(p, q);
        if !self.supported {
            self.history.unavailable();
        }
    }

    pub fn ingest(&mut self, pre: &[ContentWirePoint], post: &[ContentWirePoint]) {
        if !self.supported {
            return;
        }
        let pre_frontier = pre.iter().map(|point| point.observed_frames).max();
        let pre_tail_endpoint = pre.last().map(|point| point.endpoint_samples);
        let post_tail_endpoint = post.last().map(|point| point.endpoint_samples);
        if let (Some(previous), Some(pre_tail), Some(post_tail)) =
            (self.pair_run, pre.last(), post.last())
        {
            let pre_changed = pre_tail.run_id != previous.0;
            let post_changed = post_tail.run_id != previous.1;
            // Inspect the new run before looking for an equal endpoint. Otherwise a paused
            // side can seek ahead, publish alone, and later adopt the peer's catch-up window.
            if pre_changed != post_changed
                && pre_tail.endpoint_samples != post_tail.endpoint_samples
            {
                self.awaiting_rejoin_from = Some(previous);
                self.enter_ambiguous(post_tail.observed_frames, pre_frontier);
                return;
            }
        }
        for q in post {
            if q.observed_frames <= self.last_post_observed {
                continue;
            }
            let Some(identity) = self.identity else {
                return;
            };
            if q.measurement_epoch != identity.post_epoch
                || q.incarnation != identity.post_incarnation
                || q.generation != identity.post_generation
                || !q.valid(self.rate)
            {
                continue;
            }
            let post_occurrences = post
                .iter()
                .filter(|candidate| candidate.endpoint_samples == q.endpoint_samples)
                .count();
            let mut pre_matches = pre.iter().filter(|p| {
                p.endpoint_samples == q.endpoint_samples
                    && p.measurement_epoch == identity.pre_epoch
                    && p.incarnation == identity.pre_incarnation
                    && p.generation == identity.pre_generation
                    && host_clocks_compatible(p, q)
            });
            let Some(p) = pre_matches.next() else {
                continue;
            };
            if post_occurrences != 1 || pre_matches.next().is_some() {
                self.enter_ambiguous(q.observed_frames, pre_frontier);
                continue;
            }
            self.observe(p, q, pre_frontier, pre_tail_endpoint, post_tail_endpoint);
        }
    }

    fn observe(
        &mut self,
        p: &ContentWirePoint,
        q: &ContentWirePoint,
        pre_frontier: Option<u64>,
        pre_tail_endpoint: Option<i64>,
        post_tail_endpoint: Option<i64>,
    ) {
        let occurrence = q.endpoint_samples;
        let run = (p.run_id, q.run_id);
        if let Some(previous) = self.pair_run {
            let pre_changed = previous.0 != run.0;
            let post_changed = previous.1 != run.1;
            if self.awaiting_rejoin_from.is_none() && pre_changed != post_changed {
                let raw_jump = (pre_changed
                    && p.run_origin == ClockRunOrigin::ClockDiscontinuity as u8)
                    || (post_changed && q.run_origin == ClockRunOrigin::ClockDiscontinuity as u8);
                // A paused side can resume after a seek. If the continuing peer has already
                // published beyond its first complete window, later matching coordinates are
                // not proof that both observations belong to the same playback occurrence.
                let peer_was_ahead = (pre_changed
                    && post_tail_endpoint.is_some_and(|end| end > occurrence))
                    || (post_changed && pre_tail_endpoint.is_some_and(|end| end > occurrence));
                if raw_jump || peer_was_ahead {
                    self.awaiting_rejoin_from = Some(previous);
                }
            }
        }
        if self.pair_run.is_some_and(|previous| previous != run)
            || self.last_occurrence.is_some_and(|last| occurrence <= last)
        {
            self.enter_ambiguous(q.observed_frames, pre_frontier);
        }
        self.pair_run = Some(run);
        self.last_post_observed = q.observed_frames;
        self.last_occurrence = Some(occurrence);
        let old_high_water = self.high_water;
        self.high_water = Some(old_high_water.unwrap_or(i64::MIN).max(occurrence));
        if let Some((old_pre, old_post)) = self.awaiting_rejoin_from {
            if p.run_id == old_pre || q.run_id == old_post {
                return;
            }
            self.awaiting_rejoin_from = None;
        }
        if self
            .fresh_pre_after
            .is_some_and(|frontier| p.observed_frames <= frontier)
        {
            return;
        }
        // Reused host coordinates are not a fresh occurrence. This guard is intentionally
        // fail-closed until a new run has advanced past all previously published content.
        if self.ambiguous_through.is_some_and(|end| {
            occurrence
                .checked_sub(end)
                .is_none_or(|distance| distance < i64::from(self.rate) * 2 / 5)
        }) {
            return;
        }
        self.ambiguous_through = None;
        self.fresh_pre_after = None;
        let Some(identity) = self.identity else {
            return;
        };
        self.history.push(Point {
            pre_epoch: p.measurement_epoch,
            post_epoch: q.measurement_epoch,
            pre_incarnation: p.incarnation,
            pre_generation: p.generation,
            post_generation: q.generation,
            pre_run: p.run_id,
            post_run: q.run_id,
            pre_observed: p.observed_frames,
            post_observed: q.observed_frames,
            endpoint: occurrence,
            source: p.auxiliary_source,
            pre_m: p.lufs_m,
            post_m: q.lufs_m,
            pre_tp: p.true_peak,
            post_tp: q.true_peak,
        });
        debug_assert_eq!(identity.pre_epoch, p.measurement_epoch);
    }

    fn enter_ambiguous(&mut self, observed_frames: u64, pre_frontier: Option<u64>) {
        self.ambiguous_through = self.high_water;
        self.fresh_pre_after = pre_frontier;
        self.pair_run = None;
        self.last_occurrence = None;
        self.last_post_observed = observed_frames;
        self.history.clear(
            self.history.binding.wrapping_add(1).max(1),
            Status::Ambiguous,
        );
        self.history.advance(self.rate, observed_frames);
    }

    pub fn finish(&mut self) {
        if self.ambiguous_through.is_some() || self.awaiting_rejoin_from.is_some() {
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

fn host_clocks_compatible(pre: &ContentWirePoint, post: &ContentWirePoint) -> bool {
    if !host_clocks_present(pre, post) {
        return false;
    }
    let (Some(pre_project), Some(post_project), Some(pre_output), Some(post_output)) = (
        pre.timeline_endpoint_samples,
        post.timeline_endpoint_samples,
        pre.output_presentation_samples,
        post.output_presentation_samples,
    ) else {
        return false;
    };
    // Studio Pro's observed VST3 mapping: at one content endpoint, the POST project clock
    // leads PRE by the difference in reported output presentation delays. A one-sample
    // clock error must fail closed even though both streams hit the same grid coordinate.
    post_project.checked_sub(pre_project)
        == i64::from(pre_output).checked_sub(i64::from(post_output))
}

fn host_clocks_present(pre: &ContentWirePoint, post: &ContentWirePoint) -> bool {
    pre.auxiliary_source == post.auxiliary_source
        && pre.presentation_source == post.presentation_source
        && matches!((pre.auxiliary_source, pre.presentation_source), (1, 1))
        && pre.timeline_source == post.timeline_source
        && pre.timeline_source == 1
        && pre.timeline_endpoint_samples.is_some()
        && post.timeline_endpoint_samples.is_some()
        && pre.output_presentation_samples.is_some()
        && post.output_presentation_samples.is_some()
}
