use super::{function_body, EDITOR_LIVE_COMPARE_CPP, PROCESSOR_CPP};
const CONTENT_CPP: &str = include_str!("../../juce_shell/src/PluginProcessorContentTiming.cpp");
const BLIND_CPP: &str = include_str!("../../juce_shell/src/PluginProcessorLiveBlind.cpp");

// INV-LC7 / LC10: the content offset is measured off the Audio Thread and only shown; a jump the
// monitor settles holds POST, with PRE still selected, until playback stops.
#[test]
fn live_compare_offset_is_shown_and_a_jump_holds_post_until_playback_restarts() {
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    assert!(rt.contains("preSelected && ! contentHeld"));
    assert!(rt.contains("if (! block.playing)"));
    assert!(rt.contains("liveCompare.contentHold.stopped();"));
    assert!(!rt.contains("measureOffset") && !rt.contains("estimateOffset"));
    let monitor = function_body(
        CONTENT_CPP,
        "void KirinHyphaProcessorBase::serviceLiveContentTiming",
    );
    assert!(monitor.contains("if (step.jumped) holdLiveCompareForContentJump (estimate)"));
    assert!(monitor.contains("step.recovered"));
    assert!(monitor.contains("estimate.timelineGeneration == liveCompare.timelineGeneration.load"));
    assert!(monitor.contains("liveCompare.contentHold.clear (estimate.stopGeneration)"));
    assert!(!EDITOR_LIVE_COMPARE_CPP.contains("holdLiveCompareForContentJump (estimate)"));
}

#[test]
fn timeline_reset_notification_is_bounded_and_requires_explicit_stop() {
    let body = function_body(
        CONTENT_CPP,
        "void KirinHyphaProcessorBase::kirinHostTimelineReset",
    );
    assert!(body.contains("if (transportKnown && ! playing)"));
    assert!(body.contains("liveCompare.contentHold.stopped();"));
    assert!(body.contains("liveCompare.hostResetGeneration.fetch_add"));
    for forbidden in [
        "ScopedLock",
        "new ",
        "prepare",
        "reset()",
        "renderer.",
        "ring.",
        "kirin_hypha_",
    ] {
        assert!(
            !body.contains(forbidden),
            "host notification must only publish atomics: {forbidden}"
        );
    }
}

#[test]
fn blind_preparation_keeps_intent_during_a_content_hold() {
    let begin = function_body(
        BLIND_CPP,
        "StartResult KirinHyphaProcessorBase::beginLiveBlind",
    );
    assert!(!begin.contains("return StartResult::contentHeld"));
    let service = function_body(BLIND_CPP, "void KirinHyphaProcessorBase::serviceLiveBlind");
    assert!(service.contains("status.contentHeld"));
    assert!(service.contains("liveCompare.blindWaiting = MatchFailure::notProven"));
    let preparation = include_str!("../../juce_shell/src/live_compare/LiveBlindPreparation.h");
    assert!(preparation.contains("if (endsComparison (loss))"));
}
