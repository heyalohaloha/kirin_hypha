use super::*;

#[test]
fn annotation_bounds_use_exact_cross_products() {
    assert!(annotation_bounds_pass(0, 1_002_000, 44_100).unwrap());
    assert!(!annotation_bounds_pass(0, 1_002_001, 44_100).unwrap());
    assert!(!annotation_bounds_pass(2, 1, 44_100).unwrap());
}

#[test]
fn identity_and_split_boundaries_are_fail_closed() {
    assert_eq!(
        identity_parts("drummer7/session2/70").unwrap(),
        ("drummer7".to_string(), "drummer7/session2".to_string())
    );
    assert!(identity_parts("drummer7/70").is_err());
    assert_eq!(split("train").unwrap(), DevelopmentSplit::Train);
    assert_eq!(split("validation").unwrap(), DevelopmentSplit::Validation);
    assert!(split("test").is_err());
}
