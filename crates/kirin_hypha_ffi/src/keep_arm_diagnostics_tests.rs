use super::*;

#[test]
fn an_arm_diagnostic_does_not_advance_phase_or_consume_the_action_notice() {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    engine.record_sm.try_enter_record(License::Os).unwrap();
    let generation = engine.record_sm.generation();
    engine.record_sm.exit_record();
    engine
        .keep_record_generation
        .store(generation, Ordering::Release);
    engine
        .keep_phase
        .store(KIRIN_KEEP_PHASE_ARMED, Ordering::Release);
    *engine.keep_action_notice.write().unwrap() = Some("Failed to arm PRE and POST".into());

    let snapshot = engine.__keep_arm_diagnostics_for_test();
    assert_eq!(snapshot["keep_phase"], KIRIN_KEEP_PHASE_ARMED);
    assert_eq!(
        engine.keep_phase.load(Ordering::Acquire),
        KIRIN_KEEP_PHASE_ARMED
    );
    assert_eq!(
        engine.keep_record_generation.load(Ordering::Acquire),
        generation
    );
    assert_eq!(
        engine.drain_keep_action_notice().as_deref(),
        Some("Failed to arm PRE and POST")
    );
    assert_eq!(engine.drain_keep_action_notice(), None);
}
