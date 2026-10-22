use super::*;

#[test]
fn fixture_audio_end_is_independent_of_control_thread_delays() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    let now = Instant::now();
    let old_audio = Some(now - Duration::from_secs(30));
    assert!(runtime.band_audio_is_idle(old_audio, now));
    assert!(!runtime.band_audio_is_idle(Some(now), now));
    runtime.set_audio_end_for_test(false);
    assert!(
        !runtime.band_audio_is_idle(old_audio, now),
        "slow control work is not audio end"
    );
    runtime.set_audio_end_for_test(true);
    assert!(
        runtime.band_audio_is_idle(Some(now), now),
        "an explicit end completes the tail without sleeping"
    );
    runtime.set_audio_end_for_test(false);
    assert!(!runtime.band_audio_is_idle(None, now));
}
