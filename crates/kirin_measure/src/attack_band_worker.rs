//! The band side of the ATTACK worker. It alone owns the audio ring while a band is chosen, runs
//! every band measurement, and publishes the results; no other thread measures or touches the
//! ring, so a reader never waits on one and nothing is measured twice.
//!
//! - Each hit of the run is measured once per band, after its tail is final: 300 ms after the
//!   onset, or the next onset. POST also measures each PRE onset it is asked for once.
//! - A band change keeps the ring (its audio does not depend on the band) and measures every hit
//!   the ring still holds again in the new band, even while the transport is stopped.
//! - Work is bounded: at most `WORK_BUDGET` of measuring per call, newest first, so the worker
//!   returns to the audio long before the ingress fills.
//! - When the run's audio stops, each hit it cannot finish is measured with the audio kept (its
//!   tail then ends where the audio did) or stated as not kept. No hit waits forever.

use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::{Duration, Instant};

use super::band_results::{AttackBandDetail, AttackBandResults, BandAnchor};
use super::detail::AttackDetailTracker;
use super::state::AttackEvent;
use super::AttackRuntime;
use crate::attack_perception::band::{
    analysis_range, audio_end_span, frames_for_micros, measure_from_ring, span_end_for, AttackBand,
    AttackBandMeasure, AttackBandRing, BandScratch, BandSpanEnd, ATTACK_BAND_HISTORY_CAPACITY,
    ATTACK_BAND_TAIL_MICROS,
};

const WORK_BUDGET: Duration = Duration::from_millis(4);
/// No audio for this long: the run has stopped.
const IDLE_AFTER: Duration = Duration::from_millis(200);
/// Partial results are published at most this often while a batch of measurements runs.
const PUBLISH_INTERVAL: Duration = Duration::from_millis(30);

pub(super) struct BandWorker {
    band: Option<AttackBand>,
    /// The run the ring, the hits and the results belong to.
    generation: u64,
    ring: Option<AttackBandRing>,
    /// The run's confirmed hits while a band is chosen, oldest first.
    events: Vec<AttackEvent>,
    anchors: Vec<BandAnchor>,
    anchor_revision: Option<u64>,
    results: AttackBandResults,
    dirty: bool,
    published_at: Option<Instant>,
    audio_at: Option<Instant>,
    scratch: BandScratch,
}

impl BandWorker {
    pub(super) fn new() -> Self {
        Self {
            band: None,
            generation: 0,
            ring: None,
            events: Vec::new(),
            anchors: Vec::new(),
            anchor_revision: None,
            results: AttackBandResults::default(),
            dirty: false,
            published_at: None,
            audio_at: None,
            scratch: BandScratch::default(),
        }
    }

    /// A confirmed hit of the current run.
    pub(super) fn note_event(&mut self, event: AttackEvent) {
        if self.band.is_none()
            || event.generation != self.generation
            || self
                .events
                .last()
                .is_some_and(|last| last.event_sample >= event.event_sample)
        {
            return;
        }
        if self.events.len() == ATTACK_BAND_HISTORY_CAPACITY {
            self.events.remove(0);
        }
        self.events.push(event);
    }
}

enum Plan {
    Wait,
    NotKept,
    Measure(i64, BandSpanEnd),
}

/// What can be done now for one hit over [onset, span_end): measure it, state that its audio was
/// not kept, or wait for its tail and audio.
#[allow(clippy::too_many_arguments)]
fn plan(
    ring: Option<&AttackBandRing>,
    band: AttackBand,
    sample_rate: u32,
    onset: i64,
    span_end: i64,
    reason: BandSpanEnd,
    tail_known: bool,
    idle: bool,
) -> Plan {
    let Some(ring) = ring.filter(|ring| !ring.is_empty()) else {
        return if idle { Plan::NotKept } else { Plan::Wait };
    };
    let (from, to) = analysis_range(band, sample_rate, onset, span_end);
    if from < ring.first() {
        return Plan::NotKept;
    }
    if tail_known && ring.end() >= to {
        return Plan::Measure(span_end, reason);
    }
    if !idle {
        return Plan::Wait;
    }
    // The run's audio ended: measure with what was kept, or state that it was not.
    match audio_end_span(band, sample_rate, onset, span_end, ring.end()) {
        Some(end) if end < span_end => Plan::Measure(end, BandSpanEnd::AudioEnd),
        Some(end) => Plan::Measure(end, reason),
        None => Plan::NotKept,
    }
}

impl AttackRuntime {
    /// Before a block's frames: the band choice is taken up, and a new run starts its hits and
    /// results over.
    pub(super) fn band_begin_block(&self, worker: &mut BandWorker, generation: u64) {
        self.sync_band(worker);
        worker.audio_at = Some(Instant::now());
        if generation == worker.generation {
            return;
        }
        worker.generation = generation;
        worker.events.clear();
        worker.anchors.clear();
        worker.anchor_revision = None;
        if let Some(ring) = worker.ring.as_mut() {
            ring.clear();
        }
        if worker.band.is_some() {
            worker.results = AttackBandResults::new(worker.band, generation);
            worker.dirty = true;
        }
    }

    /// Before the tracker flushes its block: the block's audio goes to the ring.
    pub(super) fn band_block_audio(&self, worker: &mut BandWorker, tracker: &AttackDetailTracker) {
        let (start, audio) = tracker.block_audio();
        if let Some(ring) = worker.ring.as_mut() {
            ring.push_block(start, audio);
        }
    }

    /// After each block, and while no audio arrives: measure what can be measured within the
    /// budget and publish.
    pub(super) fn service_band(&self, worker: &mut BandWorker, decided_before: Option<i64>) {
        self.sync_band(worker);
        let Some(band) = worker.band else {
            self.publish_band(worker, true);
            return;
        };
        self.sync_anchors(worker);
        let idle = worker.audio_at.is_none_or(|at| at.elapsed() >= IDLE_AFTER);
        let deadline = Instant::now() + WORK_BUDGET;
        let mut drained = true;
        // Anchors first: while a pair is active they are what DRUM shows.
        for index in (0..worker.anchors.len()).rev() {
            let anchor = worker.anchors[index];
            let onset = anchor.event.event_sample;
            if worker
                .results
                .anchored_at(onset, anchor.span_end_sample)
                .is_some()
            {
                continue;
            }
            if Instant::now() >= deadline {
                drained = false;
                break;
            }
            let measure = match plan(
                worker.ring.as_ref(),
                band,
                self.sample_rate,
                onset,
                anchor.span_end_sample,
                anchor.span_end,
                true,
                idle,
            ) {
                Plan::Wait => continue,
                Plan::NotKept => None,
                Plan::Measure(end, reason) => {
                    self.measure_band_at(worker, band, onset, end, reason)
                }
            };
            worker.dirty |= worker.results.put_anchored(AttackBandDetail {
                event: anchor.event,
                band,
                span_end_sample: anchor.span_end_sample,
                measure,
            });
        }
        let limit = frames_for_micros(self.sample_rate, ATTACK_BAND_TAIL_MICROS);
        for index in (0..worker.events.len()).rev() {
            let event = worker.events[index];
            let onset = event.event_sample;
            if worker.results.own_at(onset).is_some() {
                continue;
            }
            if Instant::now() >= deadline {
                drained = false;
                break;
            }
            let next = worker.events.get(index + 1).map(|next| next.event_sample);
            let tail_known = next.is_some_and(|next| next < onset + limit)
                || decided_before.is_some_and(|decided| decided >= onset + limit);
            let (span_end, reason) = span_end_for(self.sample_rate, onset, next);
            let measure = match plan(
                worker.ring.as_ref(),
                band,
                self.sample_rate,
                onset,
                span_end,
                reason,
                tail_known,
                idle,
            ) {
                Plan::Wait => continue,
                Plan::NotKept => None,
                Plan::Measure(end, reason) => {
                    self.measure_band_at(worker, band, onset, end, reason)
                }
            };
            worker.dirty |= worker.results.put_own(AttackBandDetail {
                event,
                band,
                span_end_sample: span_end,
                measure,
            });
        }
        self.publish_band(worker, drained);
    }

    /// The worker stopped: no ring, no results.
    pub(super) fn reset_band(&self, worker: &mut BandWorker) {
        let changed = worker.band.is_some()
            || worker.ring.is_some()
            || worker.results != AttackBandResults::default();
        let published_at = worker.published_at;
        *worker = BandWorker::new();
        worker.published_at = published_at;
        self.band_ring_frames.store(0, Ordering::Release);
        if changed {
            worker.dirty = true;
            self.publish_band(worker, true);
        }
    }

    fn measure_band_at(
        &self,
        worker: &mut BandWorker,
        band: AttackBand,
        onset: i64,
        span_end: i64,
        reason: BandSpanEnd,
    ) -> Option<AttackBandMeasure> {
        let BandWorker { ring, scratch, .. } = worker;
        let ring = ring.as_ref()?;
        self.band_measurements.fetch_add(1, Ordering::Relaxed);
        measure_from_ring(
            ring,
            band,
            self.sample_rate,
            self.num_channels,
            onset,
            span_end,
            reason,
            scratch,
        )
    }

    /// Takes up a new band choice. ALL frees the ring; another band keeps it and measures every
    /// hit of the run again, or states it as not kept.
    fn sync_band(&self, worker: &mut BandWorker) {
        let requested = self.band();
        if requested == worker.band {
            return;
        }
        worker.band = requested;
        worker.anchors.clear();
        worker.anchor_revision = None;
        worker.dirty = true;
        let Some(band) = requested else {
            worker.ring = None;
            worker.events.clear();
            worker.results = AttackBandResults::default();
            self.band_ring_frames.store(0, Ordering::Release);
            return;
        };
        if worker.ring.is_none() {
            let ring = AttackBandRing::new(self.sample_rate, self.num_channels);
            self.band_ring_frames
                .store(ring.capacity_frames() as u64, Ordering::Release);
            worker.ring = Some(ring);
        }
        worker.results = AttackBandResults::new(Some(band), worker.generation);
        worker.events = match self.history.lock() {
            Ok(history) => history
                .events()
                .filter(|event| event.generation == worker.generation)
                .copied()
                .collect(),
            Err(poisoned) => poisoned
                .into_inner()
                .events()
                .filter(|event| event.generation == worker.generation)
                .copied()
                .collect(),
        };
    }

    fn sync_anchors(&self, worker: &mut BandWorker) {
        let revision = self.band_anchor_revision.load(Ordering::Acquire);
        if worker.anchor_revision == Some(revision) {
            return;
        }
        worker.anchor_revision = Some(revision);
        let request = match self.band_anchors.lock() {
            Ok(request) => request.clone(),
            Err(poisoned) => poisoned.into_inner().clone(),
        };
        worker.anchors = if request.band == worker.band {
            request
                .anchors
                .into_iter()
                .filter(|anchor| anchor.event.generation == worker.generation)
                .collect()
        } else {
            Vec::new()
        };
    }

    fn publish_band(&self, worker: &mut BandWorker, drained: bool) {
        if !worker.dirty
            || (!drained
                && worker
                    .published_at
                    .is_some_and(|at| at.elapsed() < PUBLISH_INTERVAL))
        {
            return;
        }
        let results = Arc::new(worker.results.clone());
        match self.band_results.lock() {
            Ok(mut published) => *published = results,
            Err(poisoned) => *poisoned.into_inner() = results,
        }
        if let Ok(mut history) = self.history.lock() {
            history.touch();
        }
        worker.dirty = false;
        worker.published_at = Some(Instant::now());
    }
}

#[cfg(test)]
#[path = "attack_band_worker_tests.rs"]
mod tests;
