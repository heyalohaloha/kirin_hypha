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

#[test]
fn space_live_copy_is_optional_bounded_and_outside_persisted_facts() {
    let tap = include_str!("../../juce_shell/src/HyphaSpaceLiveTap.h");
    let ingress = include_str!("../../juce_shell/src/PluginProcessorMeasurementIngress.cpp");
    let display = include_str!("../../juce_shell/src/HyphaSpaceLiveDisplay.h");
    let prepare = include_str!("../../juce_shell/src/PluginProcessorFormat.cpp");
    assert!(prepare.contains("spaceLiveTap.prepare (sampleRate);"));
    assert!(ingress.contains("if (spaceLiveTap.enabled() && ! isNonRealtime())"));
    let feed = function_body(
        ingress,
        "bool KirinHyphaProcessorBase::feedMeasurementInput",
    );
    assert!(feed.contains("clock.clockSource, liveCompare.hostResetGeneration.load"));
    assert!(feed.contains(
        "wrapperType == juce::AudioProcessor::wrapperType_AAX ? &displayClock : nullptr"
    ));
    assert!(feed.contains(
        "displayClock.clock = auxiliary.samples; displayClock.clockValid = auxiliary.valid;"
    ));
    assert!(feed.contains(
        "kirin_hypha_note_capture_window (hyphaHandle, clock.hasPosition, windowPositionSamples,"
    ));
    assert!(!feed.contains("windowPositionSamples = displayClock"));
    let push = function_body(tap, "void push (const float* input");
    for rule in [
        "live_compare::certifiedAaxClock (*aaxClock)",
        "live_compare::sameClockProof (lastAaxClock, *aaxClock)",
        "aaxClock->clock != nextEngine",
        "if (! continuous) chunkPosition.reset();",
        "chunkPosition.apply (displayClock, rate);",
    ] {
        assert!(push.contains(rule), "SPACE display continuity needs {rule}");
    }
    assert!(
        push.find("chunkPosition.apply").unwrap() < push.find("position != (advancing").unwrap()
    );
    let editor = include_str!("../../juce_shell/src/PluginEditorLifecycle.cpp");
    let tick = function_body(editor, "void KirinHyphaEditor::timerCallback()");
    assert!(
        tick.find("observatoryView.refreshSpaceVisibility();")
            .unwrap()
            < tick.find("if (!isVisible()").unwrap()
    );
    assert!(
        function_body(ingress, "void KirinHyphaProcessorBase::feedNoMeasurement")
            .contains("spaceLiveTap.interrupt();")
    );
    assert!(tap.contains("std::atomic<std::uint64_t>::is_always_lock_free"));
    for signature in [
        "void push (const float* input",
        "void interrupt() noexcept",
        "void reset() noexcept",
        "void begin() noexcept",
        "void publish (",
        "void setEnabled (bool next) noexcept",
    ] {
        let body = function_body(tap, signature);
        for forbidden in [
            ".lock",
            "mutex",
            "new ",
            "malloc",
            "resize",
            "assign",
            "push_back",
            "sleep",
            "wait",
            "while (",
            "juce::",
        ] {
            assert!(
                !body.contains(forbidden),
                "{signature} must not contain {forbidden}"
            );
        }
    }
    assert!(display.contains("refreshHz = 60"));
    assert!(tap.contains("windowSeconds = 0.080"));
    assert!(function_body(tap, "void begin() noexcept")
        .contains("std::atomic_thread_fence (std::memory_order_release)"));
    assert!(function_body(tap, "Read readObserved (")
        .contains("std::atomic_thread_fence (std::memory_order_acquire)"));
    assert!(function_body(tap, "Read read (").contains("readObserved (output, now,"));
    let read = function_body(tap, "Read readObserved (");
    assert!(read.contains("if (now < stamp) return Read::busy;"));
    assert!(read.find("if (sequence.load").unwrap() < read.find("if (now < stamp)").unwrap());
    assert!(tap.contains("advancing ? expectedPosition : lastPosition"));
    assert!(
        tap.contains("output.callbackNanos = stamp; output.lifetimeNanos = traceLifetime (block);")
    );
    assert!(display.contains("result == Read::busy && snapshot.expired (nanos)"));
    assert!(display.contains("nativeShowing (*owner.getPeer())"));
    assert!(display.contains("owner.isEnabled()"));
    assert!(display.contains("void componentEnablementChanged"));
    assert!(!display.contains("kirin_hypha_"));
    let view = include_str!("../../juce_shell/src/HyphaObservatoryView.h");
    assert!(view.contains("repaint (spaceTraceBounds())"));
    let painter = include_str!("../../juce_shell/src/HyphaSpacePainter.cpp");
    let native = function_body(painter, "bool Display::nativeShowing (");
    for property in [
        "window",
        "isVisible",
        "isMiniaturized",
        "isHiddenOrHasHiddenAncestor",
    ] {
        assert!(native.contains(&format!("sel_registerName (\"{property}\")")));
    }
    for removed in ["MID > 0", "MID < 0", "SIDE > 0", "SIDE < 0", "FIELD "] {
        assert!(!painter.contains(removed));
    }
}
