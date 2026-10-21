//! Synchronous content fixtures. These call the real worker algorithms without starting a worker.
use crate::attack_perception::band::{
    measure_from_ring, span_end_for, AttackBand, AttackBandRing, BandScratch,
};
use crate::attack_runtime::snapshot::AttackBandObservation;
use crate::attack_runtime::{bins::AttackBins, detail::AttackDetailTracker};
use crate::{AttackDetailedEvent, AttackEvent};
use std::sync::Mutex;

pub fn details(audio: &[f32], events: &[AttackEvent]) -> Vec<AttackDetailedEvent> {
    let first = events.first().expect("fixture has onsets");
    let channels = first.channels as usize;
    assert!(audio.len().is_multiple_of(channels));
    let mut tracker = AttackDetailTracker::new(first.sample_rate, channels);
    let bins = Mutex::new(AttackBins::new(first.sample_rate, channels));
    let mut result: Vec<AttackDetailedEvent> = Vec::new();
    let mut next = 0;
    for (block, samples) in audio.chunks(256 * channels).enumerate() {
        let start = (block * 256) as i64;
        let end = start + (samples.len() / channels) as i64;
        tracker.begin_block(start, first.generation);
        for frame in samples.chunks_exact(channels) {
            tracker
                .push_frame(frame[0], (channels == 2).then(|| frame[1]))
                .unwrap();
        }
        while next < events.len() && events[next].decision_sample <= end {
            tracker.queue_event(events[next]);
            next += 1;
        }
        tracker.note_decided_before(end);
        for detail in tracker.flush(&bins) {
            if let Some(old) = result.iter_mut().find(|old| old.event == detail.event) {
                *old = detail;
            } else {
                result.push(detail);
            }
        }
    }
    result
}

pub fn band(audio: &[f32], events: &[AttackEvent], band: AttackBand) -> Vec<AttackBandObservation> {
    let first = events.first().expect("fixture has onsets");
    let rate = first.sample_rate;
    let channels = first.channels as usize;
    let mut ring = AttackBandRing::new(rate, channels);
    ring.push_block(0, audio);
    let mut scratch = BandScratch::default();
    events
        .iter()
        .enumerate()
        .map(|(index, event)| {
            let (end, reason) = span_end_for(
                rate,
                event.event_sample,
                events.get(index + 1).map(|e| e.event_sample),
            );
            let measure = measure_from_ring(
                &ring,
                band,
                rate,
                channels,
                event.event_sample,
                end,
                reason,
                &mut scratch,
            )
            .expect("fixture retains the entire analysis range");
            AttackBandObservation::measured(*event, end, 1, Some(measure))
        })
        .collect()
}
