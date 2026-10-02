use super::{function_body, without_line_comments, CORRESPONDENCE_H, RING_H, SESSION_H};

const BLOCK_H: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareBlockClock.h");
const TIMING_H: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareTimingWitness.h");
const PREPARATION_H: &str =
    include_str!("../../juce_shell/src/live_compare/LiveCompareTimingPreparation.h");

#[test]
fn clock_preparation_and_shared_projection_stay_realtime_safe() {
    for source in [
        BLOCK_H,
        TIMING_H,
        function_body(PREPARATION_H, "TimingEvidence observe ("),
    ] {
        let code = without_line_comments(source);
        for forbidden in [
            "make_unique",
            "new ",
            "delete ",
            "mutex",
            ".lock(",
            ".lock (",
            "malloc",
            "calloc",
            "realloc",
            "free(",
            "std::vector",
            "std::string",
            "std::thread",
            "std::filesystem",
            "fstream",
            "fopen",
            "printf",
            "sleep",
            "wait(",
            "wait (",
            "juce::",
            ".open (",
            ".close (",
        ] {
            assert!(
                !code.contains(forbidden),
                "clock preparation contains {forbidden}"
            );
        }
    }
    // AU's measured clamp may normalize corroboration coordinates, NEVER the PCM address/K.
    let projection = function_body(BLOCK_H, "inline bool corroborateLoop (");
    for required in [
        "if (! LoopContext::identical (block.loop.ppq, block.loop.start)",
        "|| static_cast<double> (block.project) >= loopStart",
        "std::abs (static_cast<double> (block.project) - (expected - length)) > 1.01",
        "std::abs (expected) >= 9007199254740992.0",
    ] {
        assert!(
            projection.contains(required),
            "AU projection lost {required}"
        );
    }
    // VST3's opposite clamp can ONLY corroborate an independently certified K=0 address.
    for required in [
        "preProof != nullptr && block.loop.ppq < block.loop.start",
        "ClockBasis::vst3Continuous",
        "ClockAuthority::certifiedContent",
        "start != block.clock",
        "preProof->clockAuthority != certified || block.clockAuthority != certified",
        "preProof->presentationSource != 1 || block.presentationSource != 1",
        "preProof->outputPresentationSamples <= block.outputPresentationSamples",
        "anchor.loopSamples <= 0",
        "std::abs (length - static_cast<double> (anchor.loopSamples)) > 1.01",
        "std::abs (static_cast<double> (block.project) - loopStart) > 1.01",
        "tail > static_cast<double> (delay) + 1.01",
        "std::abs (block.loop.ppq + block.loop.end - block.loop.start - ppq) > tolerance",
    ] {
        assert!(
            projection.contains(required),
            "VST3 projection lost {required}"
        );
    }
    let loop_join = function_body(CORRESPONDENCE_H, "static bool loopJoin (");
    for required in [
        "timing.generation != generation",
        "! timing.anchor.loop.sameRange (context)",
        "corroborateLoop (timing.anchor, block, start, rate, verified, &timing.block)",
        "h.seq.load (std::memory_order_acquire) == seq",
    ] {
        assert!(
            loop_join.contains(required),
            "PCM generation fence lost {required}"
        );
    }
    assert!(
        function_body(CORRESPONDENCE_H, "static Verdict read (").contains("if (start < runStart)")
    );
    let observe = function_body(TIMING_H, "TimingEvidence observe (");
    assert!(observe.contains("postCycles.observe (corroborated ? verified : post, rate)"));
    assert!(observe.contains("timeline.observe (corroborated ? verified : post, rate"));
}

#[test]
fn clock_preparation_never_copies_pcm_or_grants_output_permission() {
    let publisher = function_body(TIMING_H, "TimelineStep observe (TimingHeader&");
    for forbidden in [
        "samples[",
        "input[",
        "demand",
        "gain",
        "selection",
        "renderer",
    ] {
        assert!(
            !without_line_comments(publisher).contains(forbidden),
            "metadata publisher uses {forbidden}"
        );
    }
    let observe = function_body(PREPARATION_H, "TimingEvidence observe (");
    for forbidden in [
        "demand.store",
        "gain.store",
        "selection",
        "renderer",
        "arm (",
    ] {
        assert!(
            !observe.contains(forbidden),
            "clock-only observer authorises {forbidden}"
        );
    }
    assert!(PREPARATION_H.contains("peer->mapping.open (pairKey, rate, false)"));
    assert!(
        SESSION_H.find("timing.observe (").unwrap()
            < SESSION_H.find("ring.header.demand.load").unwrap()
    );
    assert!(SESSION_H
        .contains("publisher.publishAfterClock (ring, block, input, channels, continuity)"));
    assert!(!function_body(SESSION_H, "bool feed (").contains("timeline.observe"));
    let consume = function_body(CORRESPONDENCE_H, "bool adoptInitialTiming (");
    for required in [
        "! initialAdmission",
        "evidence.postClock != block.clock",
        "! initialPcmReady (ring)",
        "current.generation != evidence.preGeneration",
        "current.ownerA != evidence.ownerA",
        "current.ownerB != evidence.ownerB",
        "ownerClosed.load",
        "initialAdmission = false;",
    ] {
        assert!(consume.contains(required), "entry lost {required}");
    }
    assert!(RING_H.contains("timingGeneration.store (h.timing.generation.load"));
    assert!(CORRESPONDENCE_H.contains(
        "h.writeEnd.load (std::memory_order_relaxed) == current.block.clock + current.block.frames"
    ));
    let process = function_body(CORRESPONDENCE_H, "Decision process (");
    assert_eq!(
        process
            .matches("! preparedTimingCurrent (ring.header)")
            .count(),
        2,
        "PRE generation/lifetime must fence both before and after the PCM copy"
    );
    assert!(
        process
            .rfind("! preparedTimingCurrent (ring.header)")
            .unwrap()
            > process.find("decision.verdict = read (").unwrap()
    );
}

#[test]
fn preparation_uses_the_existing_low_rate_service_timer() {
    let service = include_str!("../../juce_shell/src/PluginProcessorService.cpp");
    assert!(service.contains("idleTiming ? 250 : 50"));
    assert!(!PREPARATION_H.contains("Timer") && !TIMING_H.contains("Timer"));
    let observer = function_body(TIMING_H, "TimingEvidence observe (");
    assert!(observer.contains("! post.loop.active"));
    assert!(observer.contains("coherent && active &&"));
    assert!(TIMING_H.contains("CalibrationProfile profile"));
    assert!(CORRESPONDENCE_H.contains("CalibrationProfile profile"));
}

#[test]
fn full_processor_probe_keeps_display_queries_off_the_audio_thread() {
    let probe = include_str!("../../juce_shell/tests/live_compare_processor_benchmark.cpp");
    let group = include_str!("../../juce_shell/tests/live_compare_aax_group_product_test.cpp");
    for (source, signature) in [(probe, "void process()"), (group, "void processAudio()")] {
        let audio = without_line_comments(function_body(source, signature));
        assert!(!audio.contains("->liveCompareStatus()"));
        assert!(!audio.contains("->liveBlindStatus()"));
    }
    let audio = function_body(probe, "void process()");
    assert!(audio.contains("const bool preOutput = mode > 0;"));
    // Audit the actual oracle, not its user-facing failure text or a float tolerance.
    assert!(audio.contains("hypha::test::exactScaledPcm (actual, input, matchedGain)"));
    assert!(probe.contains("require (hypha::test::exactPcmControls()"));
    let timing = include_str!("../../juce_shell/tests/live_compare_timing_product_test.cpp");
    for signature in [
        "void processAudio()",
        "AuditedBlindCommand auditedBlindCommand()",
    ] {
        let audio = without_line_comments(function_body(timing, signature));
        assert!(!audio.contains("->liveCompareStatus()"));
        assert!(!audio.contains("->liveBlindStatus()"));
    }
}

#[test]
fn synthetic_clock_policy_and_first_loop_are_portable_not_diagnostic_side_effects() {
    let timing = include_str!("../../juce_shell/tests/live_compare_timing_product_test.cpp");
    let fixture = include_str!("../../juce_shell/tests/LiveTimingFixtureAccess.h");
    let cmake = include_str!("../../juce_shell/cmake/LocalBlind.cmake");
    assert!(timing.contains("LiveTimingFixtureAccess::configureStudioProClock (*instance)"));
    assert!(timing.contains("LiveTimingFixtureAccess::initialObservationRequested (*post)"));
    assert!(!timing.contains("#if defined (KIRIN_HYPHA_TIMING_PRODUCT_DIAGNOSTIC)"));
    assert!(fixture.contains("hypha::live_compare_clock_policy::classify ("));
    assert!(fixture.contains("\"Studio Pro\", \"8.1.2.113407\", 48000"));
    assert!(fixture.contains(
        "certificate.authority != hypha::live_compare::ClockAuthority::certifiedContent"
    ));
    assert!(fixture.contains("processor.liveCompare.blind.command()"));
    assert!(cmake.contains("add_test(NAME kirin_live_initial_loop_product"));
    let start = cmake
        .find("add_test(NAME kirin_live_initial_loop_product")
        .unwrap();
    let preceding = &cmake[..start];
    assert!(!preceding[preceding
        .rfind("set_tests_properties(kirin_live_timing_product")
        .unwrap()..]
        .contains("if(KIRIN_HYPHA_TIMING_PRODUCT_DIAGNOSTIC)"));
}

#[test]
fn pre_mapping_ownership_is_non_rt_and_cannot_restamp_readers() {
    let mapping = include_str!("../../juce_shell/src/live_compare/LiveCompareSharedRing.cpp");
    let header = include_str!("../../juce_shell/src/live_compare/LiveCompareSharedRing.h");
    let claim = include_str!("../../juce_shell/src/live_compare/LiveCompareOwnerClaim.h");
    assert!(header.contains("std::unique_ptr<OwnerClaim> claim;"));
    assert!(!header.contains("#include \"LiveCompareOwnerClaim.h\""));
    assert!(!RING_H.contains("OwnerClaim") && !SESSION_H.contains("OwnerClaim"));
    assert!(!mapping.contains("mapped = fresh ?"));
    assert!(mapping.contains("if (! ownership->acquire (pairKey)) return false;"));
    assert!(mapping.contains("if (existing == nullptr || ! fresh)"));
    assert!(mapping.contains("O_CREAT | O_EXCL | O_RDWR"));
    assert!(claim.contains("LOCK_EX | LOCK_NB") && claim.contains("O_NOFOLLOW"));
    assert!(claim.contains("ERROR_ALREADY_EXISTS"));
    assert_eq!(mapping.matches("claim.reset();").count(), 2);
}
