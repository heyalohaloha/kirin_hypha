//! Real DSP in the producer order: selected work runs before background cache refresh.
use super::super::*;
use crate::attack_perception::band::{AttackBand, AttackBandRing, BandSound, BandSpanEnd};
use crate::attack_runtime::single::AttackSingleRequest;
use crate::attack_runtime::snapshot::{AttackFinish, AttackSourceKey};

#[test]
fn new_single_rechecks_missing_cache_but_keeps_the_prior_terminal_copy() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    let band = AttackBand::from_index(3).unwrap();
    runtime.set_band(Some(band));
    let event = AttackEvent {
        generation: 1,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: [1; 32],
        event_sample: 20_000,
        decision_sample: 21_000,
        value: 1.0,
    };
    let source = AttackSourceKey {
        incarnation: [1; 16],
        generation: 1,
        sample_rate: 48_000,
        channels: 2,
        odf_hash: [1; 32],
    };
    let request = AttackSingleRequest {
        key_source: source,
        key_event_sample: 20_000,
        key_token: 1,
        local_source: source,
        pre_source: None,
        event,
        band: Some(band),
        requested_end: 34_400,
        measurement_end: 34_400,
        span_end: BandSpanEnd::Window,
        pre: None,
        pre_detail: None,
        pair_kind: 4,
        target: 0,
        pair_authority_revision: 1,
        proof_token: [0; 32],
    };
    let mut audio = Vec::with_capacity(96_000);
    for frame in 0..48_000 {
        let t = (f64::from(frame) - 20_000.0) / 48_000.0;
        let envelope = if t < 0.0 {
            0.0
        } else if t < 0.006 {
            t / 0.006
        } else {
            (-(t - 0.006) / 0.045).exp()
        };
        let value = (0.5 * envelope * (std::f64::consts::TAU * 250.0 * t).sin()) as f32;
        audio.extend_from_slice(&[value, value]);
    }
    let mut worker = band_worker::BandWorker::new();
    worker.band = Some(band);
    worker.generation = 1;
    worker.events.push(event);
    worker.results = AttackBandResults::new(Some(band), 1);
    let mut ring = AttackBandRing::new(48_000, 2);
    ring.push_block(0, &audio[..22_000 * 2]);
    worker.ring = Some(ring);
    let first = runtime.selected.lock().unwrap().begin(request.clone(), 0);
    runtime.service_single(&mut worker, true, Some(22_000));
    let old = runtime.selected.lock().unwrap().current.clone().unwrap();
    assert_eq!(old.token, first);
    assert_eq!(old.finish, AttackFinish::NotKept);
    assert_eq!(old.band_observation.unwrap().measure, None);
    worker
        .ring
        .as_mut()
        .unwrap()
        .push_block(22_000, &audio[22_000 * 2..]);
    // Existing terminal requests are immutable even when their PCM becomes available.
    runtime.service_single(&mut worker, false, Some(48_000));
    assert_eq!(
        runtime.selected.lock().unwrap().current.as_ref(),
        Some(&old)
    );
    let next = runtime
        .selected
        .lock()
        .unwrap()
        .begin(request.clone(), runtime.single_clock_millis());
    assert_ne!(first, next);
    // No background service has run; selected work must reject the stale None cache itself.
    runtime.service_single(&mut worker, false, Some(48_000));
    let fresh = runtime.selected.lock().unwrap().current.clone().unwrap();
    assert_eq!(fresh.finish, AttackFinish::Full);
    assert!(matches!(
        fresh.band_observation.unwrap().measure.unwrap().sound,
        BandSound::Rises { .. }
    ));
    assert_eq!(runtime.stats().band_measurements, 1);
    assert!(worker.results.own_at(20_000).unwrap().measure.is_some());
    // A stale anchored miss must not hide a valid own cache with the identical physical key.
    worker.results.put_anchored(AttackBandDetail {
        event,
        band,
        span_end_sample: 34_400,
        measure: None,
    });
    runtime
        .selected
        .lock()
        .unwrap()
        .begin(request, runtime.single_clock_millis());
    runtime.service_single(&mut worker, false, Some(48_000));
    assert_eq!(
        runtime
            .selected
            .lock()
            .unwrap()
            .current
            .as_ref()
            .unwrap()
            .finish,
        AttackFinish::Full
    );
    assert_eq!(
        runtime.stats().band_measurements,
        1,
        "reuse the own cache after rejecting stale anchored None"
    );
    runtime.shutdown_and_join();
}
