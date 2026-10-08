use super::*;
use crate::attack_runtime::snapshot::{AttackSourceEvidence, AttackSourceKey};

fn fixture() -> Arc<AttackRuntime> {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    runtime.fixture_publish_observation(AttackObservationSnapshot {
        revision: 17,
        source: Some(AttackSourceEvidence {
            source: AttackSourceKey {
                incarnation: [1; 16],
                generation: 7,
                sample_rate: 48_000,
                channels: 2,
                odf_hash: [9; 32],
            },
            odf_support_start: 0,
            odf_support_end: 35_000,
            pcm_start: 0,
            pcm_end: 35_000,
            cutoff: 35_000,
            band_semantic_hash: super::super::semantics::band_semantic_hash(),
            clock_policy: 1,
        }),
        ..Default::default()
    });
    runtime
}

#[test]
fn band_transition_keeps_all_navigation_but_not_old_band_scalar_evidence() {
    let runtime = fixture();
    let source = runtime
        .try_navigation_snapshot_result()
        .unwrap()
        .source
        .unwrap();
    runtime.set_band(crate::attack_perception::band::AttackBand::from_index(3));
    assert_eq!(
        runtime
            .try_navigation_snapshot_result()
            .unwrap()
            .source
            .unwrap(),
        source
    );
    assert!(matches!(
        runtime.try_observation_snapshot_result(),
        Err(AttackObservationReadError::SourceUnavailable)
    ));
}

#[test]
fn navigation_busy_is_distinct_from_generation_dead_worker_and_disabled_source() {
    let runtime = fixture();
    runtime.fixture_with_observation_lock(|| {
        assert!(matches!(
            runtime.try_navigation_snapshot_result(),
            Err(AttackObservationReadError::Busy)
        ));
    });
    runtime.worker_running.store(false, Ordering::Release);
    assert!(matches!(
        runtime.try_navigation_snapshot_result(),
        Err(AttackObservationReadError::SourceUnavailable)
    ));
    runtime.worker_running.store(true, Ordering::Release);
    runtime.note_clock_policy_from_audio(1, true);
    assert!(matches!(
        runtime.try_navigation_snapshot_result(),
        Err(AttackObservationReadError::SourceUnavailable)
    ));
    let runtime = fixture();
    assert!(runtime.set_enabled(false));
    assert!(matches!(
        runtime.try_navigation_snapshot_result(),
        Err(AttackObservationReadError::SourceUnavailable)
    ));
}
