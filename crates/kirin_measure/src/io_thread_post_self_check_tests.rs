use super::{SelfCheckReleaseGate, SELF_CHECK_RELEASE_CONFIRMATIONS};

#[test]
fn conflict_must_repeat_before_release_is_confirmed() {
    let mut gate = SelfCheckReleaseGate::default();

    for i in 1..SELF_CHECK_RELEASE_CONFIRMATIONS {
        assert!(
            !gate.observe_conflict("PRE-A", 100.0),
            "confirmation {i} must not release yet"
        );
    }

    assert!(
        gate.observe_conflict("PRE-A", 100.0),
        "third consecutive same conflict confirms release"
    );
}

#[test]
fn reset_discards_partial_confirmations() {
    let mut gate = SelfCheckReleaseGate::default();

    assert!(!gate.observe_conflict("PRE-A", 100.0));
    assert!(!gate.observe_conflict("PRE-A", 100.0));
    gate.reset();

    assert!(
        !gate.observe_conflict("PRE-A", 100.0),
        "playback/Record/Active gate reset must force a fresh confirmation run"
    );
}

#[test]
fn changed_candidate_restarts_confirmation_count() {
    let mut gate = SelfCheckReleaseGate::default();

    assert!(!gate.observe_conflict("PRE-A", 100.0));
    assert!(!gate.observe_conflict("PRE-A", 100.0));
    assert!(
        !gate.observe_conflict("PRE-B", 100.0),
        "different pair name starts a new candidate"
    );
    assert!(
        !gate.observe_conflict("PRE-B", 100.0),
        "new candidate still needs repeated confirmations"
    );
}
