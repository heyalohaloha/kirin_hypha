use super::{function_body, EDITOR_LIVE_COMPARE_CPP, PROCESSOR_CPP};
const CONTENT_CPP: &str = include_str!("../../juce_shell/src/PluginProcessorContentTiming.cpp");
const BLIND_CPP: &str = include_str!("../../juce_shell/src/PluginProcessorLiveBlind.cpp");

// INV-LC7 / LC10 / LC28: content is measured off the Audio Thread. An alignable change moves only
// the audition address; a late PRE or another jump is a notice and never chooses POST.
#[test]
fn live_compare_offset_is_shown_and_a_content_notice_never_chooses_post() {
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    assert!(rt.contains("liveCompareAutoAlign(), preSelected && ! compensationOff, true"));
    assert!(!rt.contains("contentHeld"));
    assert!(
        !include_str!("../../juce_shell/src/live_compare/LiveCompareInterruption.h")
            .contains("contentChanged")
    );
    assert!(rt.contains("if (! block.playing)"));
    assert!(rt.contains("liveCompare.contentHold.stopped();"));
    assert!(!rt.contains("measureOffset") && !rt.contains("estimateOffset"));
    let monitor = function_body(
        CONTENT_CPP,
        "void KirinHyphaProcessorBase::serviceLiveContentTiming",
    );
    assert!(monitor.contains("step.settled && alignLiveCompare (step, estimate)"));
    assert!(monitor.contains(
        "const bool notice = ! aligned && (step.jumped || (step.settled && liveCompareAutoAlign()));"
    ));
    assert!(monitor.contains("if (notice) noticeLiveCompareContentTiming (estimate);"));
    assert!(!CONTENT_CPP.contains("recheckAlign") && !CONTENT_CPP.contains("reportAlignLate"));
    assert!(monitor.contains("step.recovered"));
    assert!(monitor.contains("estimate.timelineGeneration == liveCompare.timelineGeneration.load"));
    assert!(monitor.contains("liveCompare.contentHold.clear (estimate.stopGeneration)"));
    assert!(!EDITOR_LIVE_COMPARE_CPP.contains("noticeLiveCompareContentTiming (estimate)"));
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
fn blind_preparation_continues_through_a_content_notice() {
    let begin = function_body(
        BLIND_CPP,
        "StartResult KirinHyphaProcessorBase::beginLiveBlind",
    );
    assert!(!begin.contains("return StartResult::contentHeld"));
    let service = function_body(BLIND_CPP, "void KirinHyphaProcessorBase::serviceLiveBlind");
    assert!(!service.contains("status.contentHeld"));
    assert!(service.contains("liveCompare.blindWaiting = MatchFailure::notProven"));
    let preparation = include_str!("../../juce_shell/src/live_compare/LiveBlindPreparation.h");
    assert!(preparation.contains("if (endsComparison (loss))"));
}
