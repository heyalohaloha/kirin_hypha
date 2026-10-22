use super::*;

#[test]
fn unwinding_a_pair_fixture_joins_all_of_its_workers() {
    let fixture = Fixture::new();
    let pre_attack = Arc::downgrade(&fixture.pre_attack);
    let post_attack = Arc::downgrade(&fixture.post_attack);
    let pre_spectrum = Arc::downgrade(&fixture.pre_spectrum);
    let post_spectrum = Arc::downgrade(&fixture.post_spectrum);
    assert!(fixture.pre_attack.set_enabled(true));
    assert!(fixture.post_attack.set_enabled(true));
    assert!(fixture.pre_spectrum.set_enabled(true));
    assert!(fixture.post_spectrum.set_enabled(true));
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(move || {
        let _owned = fixture;
        panic!("fixture assertion failure");
    }));
    assert!(result.is_err());
    let attack = [pre_attack.upgrade(), post_attack.upgrade()];
    let spectrum = [pre_spectrum.upgrade(), post_spectrum.upgrade()];
    let all_joined = attack.iter().all(Option::is_none) && spectrum.iter().all(Option::is_none);
    // A failing negative control must not itself leave the original leaked workers running.
    for runtime in attack.into_iter().flatten() {
        runtime.shutdown_and_join();
    }
    for runtime in spectrum.into_iter().flatten() {
        runtime.shutdown_and_join();
    }
    assert!(all_joined, "worker-owned Arcs survived fixture unwinding");
}
