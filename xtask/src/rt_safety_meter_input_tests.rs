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

// The Hybrid VU needle's audio-thread tap: every callback that feeds no measurement ends its
// window, PRE supplies its own callback clock for gaps, and the tap stays atomics and arithmetic.
#[test]
fn hybrid_vu_tap_ends_on_unmeasured_callbacks_and_stays_lock_free() {
    let tap = include_str!("../../juce_shell/src/HyphaVuLiveTap.h");
    let processor = include_str!("../../juce_shell/src/PluginProcessor.cpp");
    let ingress = include_str!("../../juce_shell/src/PluginProcessorMeasurementIngress.cpp");
    let precision = include_str!("../../juce_shell/src/PluginProcessorPrecision.cpp");
    let block = function_body(
        processor,
        "void KirinHyphaProcessorBase::processBlock (juce::AudioBuffer<float>&",
    );
    let feed = function_body(
        ingress,
        "bool KirinHyphaProcessorBase::feedMeasurementInput",
    );
    for (name, body) in [("processBlock", &block), ("feedMeasurementInput", &feed)] {
        assert!(
            body.contains("feedNoMeasurement ((uint32_t) numCh);"),
            "{name}"
        );
        assert!(
            !body.contains("kirin_hypha_push_samples (hyphaHandle, nullptr"),
            "{name} must end the needle's window through feedNoMeasurement"
        );
    }
    assert!(precision.contains("feedNoMeasurement (static_cast<uint32_t> (channels));"));
    assert!(
        function_body(ingress, "void KirinHyphaProcessorBase::feedNoMeasurement")
            .contains("vuLiveTap.interrupt();")
    );
    assert!(feed.contains(": hypha::live_compare::callbackWallNanos()"));
    for signature in [
        "void push (const float* interleaved, int frames, int channels, bool playing",
        "void interrupt() noexcept",
        "void setEnabled (bool next) noexcept",
        "void reset() noexcept",
        "void publish() noexcept",
    ] {
        let body = function_body(tap, signature);
        for forbidden in [
            "mutex",
            ".lock",
            "new ",
            "delete ",
            "malloc",
            "assign",
            "resize",
            "push_back",
            "sleep",
            "wait",
            "chrono",
            "printf",
            "juce::",
        ] {
            assert!(
                !body.contains(forbidden),
                "{signature} must not contain {forbidden}"
            );
        }
    }
}
