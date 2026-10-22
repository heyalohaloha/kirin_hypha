use super::*;

#[test]
fn a_late_measure_edge_skips_only_an_explicitly_abandoned_lane() {
    let ingress = crate::RecordIngress::new(32);
    assert!(matches!(
        claim_record_lane(&ingress, 1),
        RecordLaneEntry::Pending
    ));
    assert!(ingress.prepare_for_generation(1));
    assert!(ingress.abandon_unattached_from_control(1));
    assert!(matches!(
        claim_record_lane(&ingress, 1),
        RecordLaneEntry::Abandoned
    ));
    assert!(ingress.prepare_for_generation(2));
    assert!(matches!(
        claim_record_lane(&ingress, 2),
        RecordLaneEntry::Attached(_)
    ));
    // An occupied lane is not abandonment and must keep the existing retry behavior.
    assert!(matches!(
        claim_record_lane(&ingress, 2),
        RecordLaneEntry::Pending
    ));
}
