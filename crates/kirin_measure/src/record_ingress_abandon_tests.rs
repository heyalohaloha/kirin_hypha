use super::*;

#[test]
fn an_aborted_keep_releases_its_unattached_lane_for_the_next_keep() {
    let ingress = RecordIngress::new(32);
    assert!(ingress.prepare_for_generation(2));
    assert!(!ingress.prepare_for_generation(3));
    assert!(!ingress.abandon_unattached_from_control(1));
    assert!(ingress.abandon_unattached_from_control(2));
    assert!(ingress.lane_abandoned(2));
    assert!(ingress.take_consumer_for_measure(2).is_none());
    assert!(!ingress.abandon_unattached_from_control(2));
    assert!(ingress.prepare_for_generation(3));
    assert!(!ingress.abandon_unattached_from_control(2));
    assert!(!ingress.lane_abandoned(3));
    assert!(ingress.take_consumer_for_measure(3).is_some());
}

#[test]
fn a_lane_owned_by_measure_is_not_abandoned_by_control() {
    let ingress = RecordIngress::new(32);
    assert!(ingress.prepare_for_generation(2));
    let _consumer = ingress.take_consumer_for_measure(2).unwrap();
    assert!(!ingress.abandon_unattached_from_control(2));
    assert!(!ingress.lane_abandoned(2));
    assert!(!ingress.prepare_for_generation(3));
}
