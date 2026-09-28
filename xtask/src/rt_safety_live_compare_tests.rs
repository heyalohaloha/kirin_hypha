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
// follows a changed pair or a closed PRE ring, and AAX offers it on stereo instances only until
// every multi-mono channel switches in the same block (INV-LC9).
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
    assert!(supported.contains("wrapperType == wrapperType_AAX && getTotalNumInputChannels() < 2"));
    let start = function_body(
        PROCESSOR_CPP,
        "hypha::live_compare::StartResult KirinHyphaProcessorBase::startLiveCompare",
    );
    assert!(start.contains("if (! liveCompareSupported())"));
    assert!(start.contains("liveCompare.gain.store (1.0f"));
}
