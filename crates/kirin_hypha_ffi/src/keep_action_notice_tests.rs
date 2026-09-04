use super::*;

#[test]
fn user_action_notice_is_one_shot_and_never_pollutes_persistent_io_error() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    *engine
        .keep_action_notice
        .write()
        .expect("keep action notice lock") = Some("Another Keep is active".to_string());

    assert_eq!(engine.record_error_message(), None);
    assert_eq!(
        engine.drain_keep_action_notice().as_deref(),
        Some("Another Keep is active")
    );
    assert_eq!(engine.drain_keep_action_notice(), None);
    assert_eq!(engine.record_error_message(), None);
}
