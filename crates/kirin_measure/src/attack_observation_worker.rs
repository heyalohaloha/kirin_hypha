//! Worker publication and coverage masks, separate from the legacy drawing envelopes.
use super::band_worker::BandWorker;
use super::snapshot::{AttackBandObservation, AttackObservationSnapshot, AttackSourceEvidence};
use super::{AttackHistory, AttackRuntime};
use crate::attack_perception::band::span_end_for;
use std::sync::atomic::Ordering;
use std::sync::Arc;

impl AttackRuntime {
    /// Audio Thread: a conservative source boundary only; never modifies audio or Record.
    pub fn note_clock_policy_from_audio(&self, policy: u64, force_new_epoch: bool) {
        if self.clock_policy.swap(policy, Ordering::AcqRel) != policy || force_new_epoch {
            self.generation.fetch_add(1, Ordering::AcqRel);
            self.latest_presentation_end
                .store(super::NO_PRESENTATION_POSITION, Ordering::Release);
        }
    }
    pub fn try_observation_snapshot(&self) -> Option<Arc<AttackObservationSnapshot>> {
        let guard = self.observations.try_lock().ok()?;
        let snapshot = Arc::clone(&guard);
        let source = snapshot.source?;
        (self.is_enabled()
            && self.worker_running.load(Ordering::Acquire)
            && source.source.generation == self.generation.load(Ordering::Acquire)
            && snapshot.band == self.band())
        .then_some(snapshot)
    }

    pub fn request_observation_anchors(
        &self,
        band: Option<crate::attack_perception::band::AttackBand>,
        anchors: Vec<super::snapshot::AttackObservationAnchor>,
    ) {
        let physical = anchors
            .iter()
            .map(|anchor| super::BandAnchor {
                event: anchor.event,
                span_end_sample: anchor.measurement_end,
                span_end: anchor.span_end,
            })
            .collect();
        if let Ok(mut current) = self.observation_anchors.lock() {
            *current = anchors;
        }
        self.request_band_anchors(band, physical);
    }

    pub(super) fn publish_observations(&self, worker: &mut BandWorker) {
        let history = match self.history.lock() {
            Ok(value) => value.clone(),
            Err(_) => return,
        };
        let Some((first, last)) = continuous_frames(&history) else {
            return;
        };
        if last.generation != self.generation.load(Ordering::Acquire) {
            return;
        }
        let (pcm_start, pcm_end) = match worker.ring.as_ref() {
            Some(ring) if !ring.is_empty() => (ring.first(), ring.end()),
            _ => match self.bins.lock() {
                Ok(bins) => bins.coverage(),
                Err(_) => return,
            },
        };
        let cutoff = pcm_end;
        let source = AttackSourceEvidence::from_frames(
            self.incarnation,
            first,
            last,
            pcm_start,
            pcm_end,
            cutoff,
            self.clock_policy.load(Ordering::Acquire),
        );
        if !source.valid() {
            return;
        }
        let mut snapshot = AttackObservationSnapshot {
            source: Some(source),
            band: worker.band,
            revision: worker.observation_revision,
            ..Default::default()
        };
        for (index, &event) in worker.events.iter().enumerate() {
            let next = worker.events.get(index + 1).map(|next| next.event_sample);
            let (end, _) = span_end_for(self.sample_rate, event.event_sample, next);
            let entry = worker.results.own_at(event.event_sample);
            snapshot.own.push(entry.map_or_else(
                || AttackBandObservation::pending(event, end, snapshot.revision),
                |detail| {
                    AttackBandObservation::measured(
                        event,
                        detail.span_end_sample,
                        snapshot.revision,
                        detail.measure,
                    )
                },
            ));
        }
        let logical_anchors = match self.observation_anchors.lock() {
            Ok(value) => value.clone(),
            Err(_) => return,
        };
        for anchor in &worker.anchors {
            let requested_end = logical_anchors
                .iter()
                .find(|logical| {
                    logical.event == anchor.event
                        && logical.measurement_end == anchor.span_end_sample
                })
                .map_or(anchor.span_end_sample, |logical| logical.requested_end);
            let entry = worker
                .results
                .anchored_at(anchor.event.event_sample, anchor.span_end_sample);
            snapshot.anchored.push(entry.map_or_else(
                || AttackBandObservation::pending(anchor.event, requested_end, snapshot.revision),
                |detail| {
                    AttackBandObservation::measured(
                        anchor.event,
                        requested_end,
                        snapshot.revision,
                        detail.measure,
                    )
                },
            ));
        }
        if let Ok(mut published) = self.observations.lock() {
            if self.generation.load(Ordering::Acquire) != source.source.generation {
                return;
            }
            if **published != snapshot {
                worker.observation_revision = worker.observation_revision.wrapping_add(1);
                snapshot.revision = worker.observation_revision;
                for entry in snapshot.own.iter_mut().chain(snapshot.anchored.iter_mut()) {
                    entry.revision = snapshot.revision;
                }
                *published = Arc::new(snapshot);
            }
        }
    }
}

fn continuous_frames(
    history: &AttackHistory,
) -> Option<(&super::AttackOdfFrame, &super::AttackOdfFrame)> {
    let mut frames = history.frames();
    let mut first = frames.next()?;
    let mut previous = first;
    for frame in frames {
        if frame.generation != previous.generation
            || frame.definition_hash != previous.definition_hash
            || frame.sample_rate != previous.sample_rate
            || frame.channels != previous.channels
            || frame.event_sample
                != previous
                    .event_sample
                    .checked_add(i64::from(frame.hop_samples))?
        {
            first = frame;
        }
        previous = frame;
    }
    Some((first, previous))
}

#[cfg(test)]
#[path = "attack_observation_worker_tests.rs"]
mod tests;
