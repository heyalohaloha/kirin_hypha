// Live PRE/POST compare: the Audio Thread transport and correspondence rules stay free of
// allocation, locks, blocking I/O and thread waits (AGENTS R-12, INV-LC1 to INV-LC3).

const RING_H: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareRing.h");
const CORRESPONDENCE_H: &str =
    include_str!("../../juce_shell/src/live_compare/LiveCompareCorrespondence.h");
const CLOCK_H: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareClock.h");
const SESSION_H: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareSession.h");
const PROCESSOR_CPP: &str = include_str!("../../juce_shell/src/PluginProcessorLiveCompare.cpp");
const AUDITION_CPP: &str = include_str!("../../juce_shell/src/PluginProcessorAudition.cpp");
const EDITOR_LIFECYCLE_CPP: &str = include_str!("../../juce_shell/src/PluginEditorLifecycle.cpp");
const EDITOR_LOCAL_BLIND_CPP: &str =
    include_str!("../../juce_shell/src/PluginEditorLocalBlind.cpp");
const EDITOR_LIVE_COMPARE_CPP: &str =
    include_str!("../../juce_shell/src/PluginEditorLiveCompare.cpp");
const EDITOR_REFERENCE_CPP: &str = include_str!("../../juce_shell/src/PluginEditorReference.cpp");
const PROCESSOR_PIN_CPP: &str =
    include_str!("../../juce_shell/src/PluginProcessorLiveComparePin.cpp");
const PIN_CPP: &str = include_str!("../../juce_shell/src/live_compare/LiveComparePin.cpp");
const EDITOR_AUTO_CPP: &str = include_str!("../../juce_shell/src/PluginEditorLiveCompareAuto.cpp");
const MATCH_CPP: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareMatch.cpp");

// The body of the first function whose definition starts with signature (brace matched).
fn function_body<'a>(source: &'a str, signature: &str) -> &'a str {
    let start = source.find(signature).expect("the function exists");
    let open = start + source[start..].find('{').expect("the body opens");
    let mut depth = 0usize;
    for (offset, ch) in source[open..].char_indices() {
        match ch {
            '{' => depth += 1,
            '}' => {
                depth -= 1;
                if depth == 0 {
                    return &source[open..open + offset + 1];
                }
            }
            _ => {}
        }
    }
    panic!("unbalanced body for {signature}");
}

fn without_line_comments(source: &str) -> String {
    source
        .lines()
        .map(|line| line.split("//").next().unwrap_or(""))
        .collect::<Vec<_>>()
        .join("\n")
}

#[test]
fn live_compare_audio_thread_code_avoids_blocking_work() {
    for (name, source) in [
        ("LiveCompareRing.h", RING_H),
        ("LiveCompareCorrespondence.h", CORRESPONDENCE_H),
        ("LiveCompareClock.h", CLOCK_H),
        (
            "PluginProcessorLiveCompare.cpp processLiveCompare",
            function_body(
                PROCESSOR_CPP,
                "void KirinHyphaProcessorBase::processLiveCompare",
            ),
        ),
    ] {
        let code = without_line_comments(source);
        for forbidden in [
            "mutex",
            ".lock(",
            ".lock (",
            "new ",
            "delete ",
            "malloc",
            "calloc",
            "realloc",
            "free(",
            "std::vector",
            "std::string",
            "std::function",
            "std::thread",
            "std::filesystem",
            "fstream",
            "fopen",
            "printf",
            "sleep",
            "wait",
            "juce::",
        ] {
            assert!(
                !code.contains(forbidden),
                "{name} must not contain {forbidden}"
            );
        }
    }
}

#[test]
fn live_compare_shared_state_is_atomic_and_proven_before_output() {
    for required in [
        "static_assert (std::atomic<float>::is_always_lock_free);",
        "std::atomic<float> samples[",
        "h.seq.store (seq + 1, std::memory_order_relaxed);",
        "h.seq.store (seq + 2, std::memory_order_release);",
    ] {
        assert!(
            RING_H.contains(required),
            "LiveCompareRing.h lost {required}"
        );
    }
    for required in [
        "if (! ring.matches (pairKey, sampleRate))",
        "if (block.afterGap)",
        "profile.invalidateOnDisagreement",
        "if (start < runStart)",
        "return Verdict::torn;",
    ] {
        assert!(
            CORRESPONDENCE_H.contains(required),
            "LiveCompareCorrespondence.h lost {required}"
        );
    }
    assert!(CLOCK_H.contains("ClockBasis::pluginFrames"));
}

#[test]
fn live_compare_output_is_proven_last_and_non_rt_setup_stays_off_the_audio_thread() {
    // INV-LC4: PRE only in proven blocks; a lost proof switches at the block start.
    for required in [
        "if (decision.verdict != Verdict::accepted)",
        "weight = 0.0f; // switch to POST at the block start",
        "fadeSeconds = 0.005",
        "if (ring.header.demand.load (std::memory_order_acquire) == 0)",
    ] {
        assert!(
            SESSION_H.contains(required),
            "LiveCompareSession.h lost {required}"
        );
    }
    // The only allocation in the session header is the non-RT prepare.
    let render = function_body(SESSION_H, "RenderReport render (");
    let feed = function_body(SESSION_H, "bool feed (");
    for body in [render, feed] {
        for forbidden in ["make_unique", "new ", "mutex", "sleep", "wait"] {
            assert!(
                !body.contains(forbidden),
                "RT body must not contain {forbidden}"
            );
        }
    }
    // The comparison path keeps A observed before any B output and gives the live compare the
    // last word only when no other audition rendered; the PRE product only feeds.
    let reference = AUDITION_CPP
        .find("referenceAuditionController->observeAInput")
        .unwrap();
    let live_post = AUDITION_CPP
        .find("processLiveCompare (buffer, clock, bypassed, nonRealtimeMode, referenceRendered);")
        .unwrap();
    assert!(reference < live_post);
    assert!(AUDITION_CPP
        .contains("processLiveCompare (buffer, clock, bypassed, nonRealtimeMode, false);"));
    // Mapping, naming, sleeping and waiting stay in the message-thread functions.
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    for forbidden in [
        "collectWithin",
        "publishMapping",
        "SharedRingMapping>()",
        "sleep_for",
    ] {
        assert!(
            !rt.contains(forbidden),
            "processLiveCompare must not contain {forbidden}"
        );
    }
}

// INV-LC12: a session never outlives the screen that controls it, never overlaps Blind, never
// follows a changed pair or a closed PRE ring, and AAX offers it only where no multi-mono channel
// takes part (INV-LC9).
#[test]
fn live_compare_sessions_end_where_the_user_cannot_see_them() {
    let destructor = function_body(EDITOR_LIFECYCLE_CPP, "KirinHyphaEditor::~KirinHyphaEditor");
    assert!(destructor.contains("processorRef.stopLiveCompare();"));
    let blind = function_body(
        EDITOR_LOCAL_BLIND_CPP,
        "void KirinHyphaEditor::openLocalBlindProduct",
    );
    assert!(blind.contains("processorRef.stopLiveCompare();"));
    let refresh = function_body(
        EDITOR_LIVE_COMPARE_CPP,
        "void KirinHyphaEditor::refreshLiveCompare",
    );
    assert!(refresh.contains("processorRef.serviceLiveCompare();"));
    assert!(refresh.contains("processorRef.takeLiveComparePreWait()"));
    let process = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    assert!(process.contains("liveCompare.preWaitSeen.store (true"));
    let service = function_body(
        PROCESSOR_CPP,
        "bool KirinHyphaProcessorBase::serviceLiveCompare",
    );
    for required in ["pairKeyForPreInstance", "ownerClosed", "stopLiveCompare();"] {
        assert!(
            service.contains(required),
            "serviceLiveCompare must check {required}"
        );
    }
    let supported = function_body(
        PROCESSOR_CPP,
        "bool KirinHyphaProcessorBase::liveCompareSupported",
    );
    assert!(supported.contains("! aaxMultiMonoMember()"));
    let start = function_body(
        PROCESSOR_CPP,
        "hypha::live_compare::StartResult KirinHyphaProcessorBase::startLiveCompare",
    );
    assert!(start.contains("if (! liveCompareSupported())"));
    assert!(start.contains("liveCompare.gain.store (1.0f"));
}

// INV-LC14: POST is lowered only by an approved MATCH choice, the attenuation stays until the
// explicit RETURN, offline render, bypass and another audition's output are never touched, a PRE
// block over the guard never sounds, and no other audition starts on top of a held attenuation.
#[test]
fn live_compare_post_attenuation_is_approved_held_and_never_offline() {
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    let ring_section = rt.find("liveCompare.ring.withRealtime").unwrap();
    let held = rt
        .find("if (! rendered && usable && ! outputTaken)")
        .unwrap();
    assert!(ring_section < held);
    assert!(rt.contains("liveCompare.postLevel.apply (buffer.getArrayOfWritePointers(), channels, frames, postTarget);"));
    assert!(rt.contains("if (report.guardTripped)"));
    let apply = function_body(
        PROCESSOR_CPP,
        "bool KirinHyphaProcessorBase::applyLiveCompareMatch",
    );
    assert!(apply.contains("plan.needsApproval == (choice == MatchChoice::basis)"));
    assert!(apply.contains("std::min (1.0f, linear (postDb))"));
    let stop = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::stopLiveCompare",
    );
    assert!(
        !stop.contains("postTarget"),
        "ending a session must not release the held attenuation"
    );
    let render = function_body(SESSION_H, "RenderReport render (");
    assert!(render.contains("! guardPasses (block.frames, preLevel.peak (preGain), ceilingLinear)"));
    assert!(render.contains("report.guardTripped = true;"));
    let blind = function_body(
        EDITOR_LOCAL_BLIND_CPP,
        "void KirinHyphaEditor::openLocalBlindProduct",
    );
    assert!(blind.contains("if (liveCompareHoldBlocksAudition())"));
    assert_eq!(
        EDITOR_REFERENCE_CPP
            .matches("if (liveCompareHoldBlocksAudition()) return;")
            .count(),
        3,
        "Reference B, C and Blind wait for RETURN"
    );
}

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
    assert!(rt.contains("liveCompare.contentHold.store (false, std::memory_order_release);"));
    assert!(!rt.contains("measureOffset") && !rt.contains("estimateOffset"));
    let monitor = function_body(
        EDITOR_LIVE_COMPARE_CPP,
        "void KirinHyphaEditor::monitorLiveCompareOffset",
    );
    assert!(monitor.contains("if (step.jumped)"));
    assert!(monitor.contains("processorRef.holdLiveCompareForContentJump();"));
    assert_eq!(
        EDITOR_LIVE_COMPARE_CPP
            .matches("holdLiveCompareForContentJump")
            .count(),
        1,
        "only the settled jump holds POST"
    );
}

// INV-LC15: PIN fixes one project range without padding or joining, goes through Local Blind's own
// admission and acceptance, never starts on a held attenuation, and ends the live session.
#[test]
fn live_compare_pin_hands_one_range_to_blind_through_its_own_admission() {
    assert!(PIN_CPP.contains("project.runStart > start"));
    assert!(PIN_CPP.contains("PinFailure::notOneRange"));
    let pin = function_body(
        PROCESSOR_PIN_CPP,
        "hypha::live_compare::LivePinResult KirinHyphaProcessorBase::pinLiveCompareForBlind",
    );
    let pinned = pin.find("hypha::live_compare::pinLatest").unwrap();
    let admitted = pin.find("localBlindCaptureAvailability()").unwrap();
    let accepted = pin
        .find("localBlindProductSession.acceptCapturedPair")
        .unwrap();
    assert!(pinned < admitted && admitted < accepted);
    assert!(pin.contains("releaseLocalBlindProductScope (scopeEpoch);"));
    assert!(pin.contains("localBlindProductSession.failCaptureRequest();"));
    let editor = function_body(
        EDITOR_LIVE_COMPARE_CPP,
        "void KirinHyphaEditor::pinLiveCompareForBlind",
    );
    let held = editor.find("if (liveCompareHoldBlocksAudition())").unwrap();
    let request = editor.find("processorRef.pinLiveCompareForBlind").unwrap();
    let stop = editor.find("processorRef.stopLiveCompare();").unwrap();
    assert!(held < request && request < stop);
}

// INV-LC16: AUTO moves PRE's gain only, through the approved point (the ceiling and the reach of the
// last explicit MATCH), ramps it on the Audio Thread, and stops wherever the session or Blind starts.
#[test]
fn live_compare_auto_follows_pre_only_within_the_approved_point() {
    let follow = function_body(EDITOR_AUTO_CPP, "void KirinHyphaEditor::followLiveCompare");
    assert!(follow.contains("hypha::live_compare::followStep ("));
    assert!(follow.contains("a.ceilingDbtp"));
    assert!(follow.contains("processorRef.followLiveCompareGain (step.preGainDb);"));
    for forbidden in [
        "applyLiveCompareMatch",
        "returnLiveComparePostToNormal",
        "postTarget.store",
        "ceilingLinear",
    ] {
        assert!(!follow.contains(forbidden), "AUTO must not use {forbidden}");
    }
    let gain = function_body(
        PROCESSOR_CPP,
        "bool KirinHyphaProcessorBase::followLiveCompareGain",
    );
    assert!(gain.contains("setLiveCompareGain ("));
    assert!(!gain.contains("postTarget") && !gain.contains("ceilingLinear"));
    let step = function_body(MATCH_CPP, "FollowStep followStep (");
    assert!(
        !step.contains("result.ceilingDbtp"),
        "a window's own ceiling never raises the approved one"
    );
    assert!(step.contains("followReachDb") && step.contains("followToleranceDb"));
    assert!(
        EDITOR_LIVE_COMPARE_CPP
            .matches("liveCompareAuto = {};")
            .count()
            >= 3,
        "LISTEN, END and PIN reset AUTO"
    );
    let render = function_body(SESSION_H, "RenderReport render (");
    assert!(render.contains("preLevel.settle (preGain);"));
    assert!(render.contains("const float gain = preLevel.next (preGain);"));
}

// INV-LC6 (2026-09-28): the live compare never keeps the host awake. PRE and POST report no tail
// and AAX never asks for AlwaysProcess; after a host sleep the correspondence rules return PRE.
#[test]
fn live_compare_never_reports_an_infinite_tail() {
    let processor = include_str!("../../juce_shell/src/PluginProcessor.cpp");
    assert!(processor
        .contains("double KirinHyphaProcessorBase::getTailLengthSeconds() const { return 0.0; }"));
    let cmake = include_str!("../../juce_shell/CMakeLists.txt");
    assert!(!cmake.contains("JucePlugin_AAXDisableDynamicProcessing=1"));
    assert!(!cmake.contains("AAX_eProperty_Constraint_AlwaysProcess"));
}

// INV-LC8: Pro Tools reports its delay compensation as a whole on or off (JUCE patch 0009). While
// it is off POST sounds and PRE waits; a change proves K again, and the offset monitor claims no
// jump from it.
#[test]
fn live_compare_holds_pre_while_host_delay_compensation_is_off() {
    let patch = include_str!("../../juce_shell/patches/0009-aax-delay-compensation-state.patch");
    assert!(patch.contains("case AAX_eNotificationEvent_DelayCompensationState:"));
    assert!(patch.contains("size == sizeof (int32_t)"));
    assert!(patch.contains(
        "kirinHostDelayCompensationStateChanged (*static_cast<const int32_t*> (data) != 0);"
    ));
    let apply = include_str!("../../scripts/apply_juce_patches.sh");
    let verify = include_str!("../../scripts/verify_juce_patch_state.sh");
    assert!(apply.contains("0009-aax-delay-compensation-state.patch"));
    assert!(verify
        .contains("0009-aax-delay-compensation-state.patch::--unidiff-zero --ignore-whitespace"));
    let hook = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::kirinHostDelayCompensationStateChanged",
    );
    assert!(
        hook.contains("liveCompare.compensationOff.store (! enabled, std::memory_order_release);")
    );
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    assert!(rt.contains("block.afterGap = true;"));
    assert!(rt.contains("preSelected && ! contentHeld && ! compensationOff"));
    assert!(rt.contains("(contentHeld || compensationOff)"));
    let monitor = function_body(
        EDITOR_LIVE_COMPARE_CPP,
        "void KirinHyphaEditor::monitorLiveCompareOffset",
    );
    assert!(monitor.contains("|| status.compensationOff)"));
    assert!(monitor.contains("\"Delay compensation is off in Pro Tools\""));
}

// INV-LC9: Pro Tools processes the channels of a multi-mono set on parallel threads, so AAX offers
// the live compare only on stereo instances and on the only instance of a group (a mono track).
// JUCE patch 0010 names the group before the first prepare; a PRE that is one channel of a set
// stamps its ring, and POST refuses it with the reason.
#[test]
fn live_compare_never_runs_on_one_channel_of_a_multi_mono_set() {
    let patch = include_str!("../../juce_shell/patches/0010-aax-instance-group.patch");
    assert!(patch.contains("Controller()->GetInstanceGroupID (&kirinGroup) == AAX_SUCCESS"));
    assert!(patch.contains("kirinGroup != kAAX_InstanceGroupID_Undefined"));
    assert!(patch.contains("pluginInstance->kirinHostInstanceGroup (static_cast<uint64> (kirinGroup), kirinGroupKnown);"));
    // Line 870 of the wrapper after patch 0009 is EffectInit's `processingSidechainChange = false;`,
    // just before `auto err = preparePlugin();`: the group is named before the first prepare.
    assert!(patch.contains("@@ -870,0 +871,4 @@"));
    let apply = include_str!("../../scripts/apply_juce_patches.sh");
    let verify = include_str!("../../scripts/verify_juce_patch_state.sh");
    assert!(apply.contains("0010-aax-instance-group.patch"));
    assert!(verify.contains("0010-aax-instance-group.patch::--unidiff-zero --ignore-whitespace"));
    let hook = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::kirinHostInstanceGroup",
    );
    assert!(hook.contains("liveCompare.aaxGroup.assign"));
    let member = function_body(
        PROCESSOR_CPP,
        "bool KirinHyphaProcessorBase::aaxMultiMonoMember",
    );
    assert!(member.contains("wrapperType == wrapperType_AAX && getTotalNumInputChannels() < 2 && ! liveCompare.aaxGroup.alone()"));
    let prepare = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::prepareLiveCompareForPreparedFormat",
    );
    assert!(
        prepare.contains("aaxMultiMonoMember() ? hypha::live_compare::ringSourceMultiMono : 0u")
    );
    let start = function_body(
        PROCESSOR_CPP,
        "hypha::live_compare::StartResult KirinHyphaProcessorBase::startLiveCompare",
    );
    let refused = start
        .find("return StartResult::preMultiMono;")
        .expect("POST refuses a multi-mono PRE");
    let demand = start
        .find("header.demand.store (1")
        .expect("POST raises demand");
    assert!(refused < demand, "a refused PRE never sees demand");
    let group = include_str!("../../juce_shell/src/live_compare/LiveCompareAaxGroup.cpp");
    assert!(group.contains("it->second == 1"));
}
