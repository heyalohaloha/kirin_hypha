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
    let processor = concat!(
        include_str!("../../juce_shell/src/ProcessorAudioCallback.h"),
        include_str!("../../juce_shell/src/PluginProcessor.cpp")
    );
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

#[test]
fn candidate_comparison_isolates_normal_input_and_has_bounded_rt_receipts() {
    let callback = include_str!("../../juce_shell/src/ProcessorAudioCallback.h");
    let branch = callback
        .split("if (role == Role::Post && candidateMeasurementIsolated())")
        .nth(1)
        .unwrap()
        .split("#if KIRIN_HYPHA_GUIDE_TRANSPORT")
        .next()
        .unwrap();
    assert!(branch.contains("KIRIN_SIGNAL_STATE_INACTIVE"));
    assert!(branch.contains("feedNoMeasurement ("));
    assert!(branch.contains("hypha::chain_trace::Scope trace (liveCompare"));
    assert!(branch.contains("pushPairedAnalysis (0,"));
    assert!(!branch.contains("processComparisonPaths"));
    assert!(!branch.contains("feedMeasurementInput"));
    assert!(
        branch.find("processLiveCompare (").unwrap() < branch.find("pushPairedAnalysis (").unwrap()
    );
    let receiver = include_str!("../../juce_shell/src/live_compare/CandidateReceiver.h");
    let rt = receiver.split("    void process (").nth(1).unwrap();
    for forbidden in [
        "make_unique",
        "new ",
        "sleep",
        "ScopedLock",
        ".open (",
        ".collect(",
    ] {
        assert!(
            !rt.contains(forbidden),
            "candidate RT receiver contains {forbidden}"
        );
    }
    assert!(rt.contains("receipt.readLease (token, block.wallNanos, observedUntil)"));
    assert!(rt.contains("receipt.released.load (std::memory_order_acquire) != token"));
    assert!(!rt.contains("block.positionInterpolated"));
    assert!(rt.contains("allowed, 1.0f, unity, 1.0f, std::numeric_limits<float>::max()"));
    assert!(rt.contains("receipt.publish (token, run, verifiedStart, verifiedEnd"));
    let realtime = include_str!("../../juce_shell/src/PluginProcessorLiveCompareRealtime.cpp");
    assert!(
        realtime.find("liveCompare.feeder.feed (").unwrap()
            < realtime.find("processCandidateReceiver (").unwrap()
    );
    let renderer = include_str!("../../juce_shell/src/live_compare/LiveCompareSession.h");
    assert!(renderer.contains("ring.header.candidate.covers (candidateToken, decision.run"));
    assert!(renderer.contains("checkedClockSubtract (block.clock, decision.k, inputStart)"));
    assert!(renderer.contains("inputStart, inputEnd"));
    assert!(!renderer.contains("decision.preStart, decision.preStart + block.frames"));
    let drain = include_str!("../../juce_shell/src/live_compare/CandidateReturn.h");
    let drain_rt = format!(
        "{}{}{}{}{}",
        function_body(drain, "bool reached ("),
        function_body(drain, "bool currentProof ("),
        function_body(drain, "bool observe ("),
        function_body(drain, "std::uint64_t advance ("),
        function_body(drain, "inline bool candidateReturnReached ("),
    );
    let drain_code = drain_rt
        .lines()
        .map(|line| line.split("//").next().unwrap_or(""))
        .collect::<String>();
    for forbidden in [
        "make_unique",
        "new ",
        "sleep",
        "ScopedLock",
        ".open (",
        ".collect(",
    ] {
        assert!(
            !drain_code.contains(forbidden),
            "candidate drain uses {forbidden}"
        );
    }
    assert!(drain.contains("transfer.readReturn (token, boundary, origin, generation)"));
    assert!(drain.contains("if (block.loop.active || !continuous) return false;"));
    assert!(drain.contains("!transfer.withdrawn (token)"));
    assert!(drain
        .contains("timing.observe (fresh, block, rate, std::numeric_limits<std::int64_t>::max())"));
    assert!(renderer.contains("consumer.process (ring, pairKey, sampleRate, block"));
    assert!(drain.contains("address >= marker"));
    assert!(realtime.contains("state.returnObserver.withRealtime"));
    assert!(realtime.contains(
        "state.returnContinuity.observe (candidateToken, block, timing, returnTimingFresh)"
    ));
    assert!(realtime.contains("candidateReturnReached (*ring, candidateToken, block, timing,"));
    let control = include_str!("../../juce_shell/src/live_compare/CandidateProcessorControl.h");
    assert!(control.contains("if (!state.receiver.withdraw()) return;"));
    assert!(control.contains("current->token, current->lastOutput, current->lastGeneration"));
    let observation = function_body(drain, "bool reached (");
    assert!(observation.contains("proofAvailable = false;"));
    assert!(observation.contains("proofAvailable = proof.valid;"));
    assert!(observation
        .contains("block.wallNanos - fresh.block.wallNanos > CandidateTransfer::leaseNanos"));
    let projection = function_body(drain, "bool currentProof (");
    assert!(projection.contains("fresh.block.wallNanos > previous.wallNanos"));
    assert!(drain.contains("linear && timing.valid && freshProof"));
    assert!(drain.contains("timing.valid && (continuous || freshProof)"));
    assert!(!drain.contains("return sourceProofAvailable ? 0 : ringCapacityFrames;"));
    assert!(drain.contains("std::optional<std::uint64_t> candidateReturnDrainFrames"));
    assert!(
        !rt.contains("hardwareA"),
        "hardware placement alone cannot forbid verified audio"
    );
    let blind = include_str!("../../juce_shell/src/PluginProcessorLiveBlind.cpp");
    let needs_service = function_body(
        blind,
        "bool KirinHyphaProcessorBase::liveCompareNeedsService()",
    );
    assert!(
        needs_service
            .find("ScopedTryLock ownership (handleLock)")
            .unwrap()
            < needs_service.find("liveCompare.ring.control()").unwrap()
    );
    assert!(needs_service.contains("if (! ownership.isLocked()) return true;"));
}

// INV-LC6 (2026-09-28): the live compare never keeps the host awake. PRE and POST report no tail
// and AAX never asks for AlwaysProcess; after a host sleep the correspondence rules return PRE.
#[test]
fn live_compare_never_reports_an_infinite_tail() {
    let processor = concat!(
        include_str!("../../juce_shell/src/ProcessorAudioCallback.h"),
        include_str!("../../juce_shell/src/PluginProcessor.cpp")
    );
    assert!(processor
        .contains("double KirinHyphaProcessorBase::getTailLengthSeconds() const { return 0.0; }"));
    let cmake = include_str!("../../juce_shell/CMakeLists.txt");
    assert!(!cmake.contains("JucePlugin_AAXDisableDynamicProcessing=1"));
    assert!(!cmake.contains("AAX_eProperty_Constraint_AlwaysProcess"));
}

// INV-LC8: Pro Tools reports its delay compensation as a whole on or off (JUCE patch 0009). While
// it is off POST sounds and PRE waits; a change proves K again, and the offset monitor claims no
// jump from it.
