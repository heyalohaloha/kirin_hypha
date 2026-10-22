use super::tests::function_body;

#[test]
fn producer_meter_activity_and_take_promotion_avoid_blocking_operations() {
    let activity = include_str!("../../crates/kirin_measure/src/record_take_meter_activity.rs");
    let render = include_str!("../../crates/kirin_measure/src/record_take_render.rs");
    let promotion = include_str!("../../crates/kirin_measure/src/record_take_promotion.rs");
    let mapping = include_str!("../../crates/kirin_measure/src/record_take_audio_mapping.rs");
    for (source, signature) in [
        (activity, "pub fn note_meter_active("),
        (activity, "pub fn rebind_meter_active_after_clock_reset("),
        (render, "pub fn note_block("),
        (render, "fn select_record_epoch("),
        (promotion, "fn apply(&self,"),
        (promotion, "fn apply_take_promotion("),
        (promotion, "fn promotion_prefix("),
        (mapping, "fn seed_proven_prefix("),
    ] {
        let body = function_body(source, signature);
        for forbidden in [
            "std::fs",
            "File::",
            ".lock(",
            ".try_lock(",
            "Vec::",
            "Box::",
            "String::",
            "format!",
            "thread::",
            "spawn",
            "sleep",
            "join",
            ".wait(",
        ] {
            assert!(
                !body.contains(forbidden),
                "{signature} must not contain {forbidden}"
            );
        }
    }
}
