use super::*;
use crate::RecordIngress;

#[test]
fn a_closed_entry_releases_its_unattached_lane_for_retry() {
    let sm = RecordStateMachine::new();
    let ingress = RecordIngress::new(32);
    assert!(ingress.prepare_for_generation(1));
    let token = sm
        .begin_enter(1, None, None, Some("cancelled-entry".into()))
        .unwrap();
    sm.exit_record();
    assert!(sm.finish_enter(token).is_err());
    crate::cleanup::retire_failed_record_entry(&sm, &ingress, 1);
    assert!(
        ingress.prepare_for_generation(2),
        "Stop before Record publication must not poison the next Keep"
    );
}

#[test]
fn an_entry_still_owned_by_another_transition_keeps_its_lane() {
    let sm = RecordStateMachine::new();
    let ingress = RecordIngress::new(32);
    assert!(ingress.prepare_for_generation(1));
    let token = sm
        .begin_enter(1, None, None, Some("live-entry".into()))
        .unwrap();
    assert!(!crate::cleanup::retire_failed_record_entry(
        &sm, &ingress, 1
    ));
    sm.finish_enter(token).unwrap();
    assert!(!crate::cleanup::retire_failed_record_entry(
        &sm, &ingress, 1
    ));
    assert!(!ingress.prepare_for_generation(2));
}
