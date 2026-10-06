use super::{function_body, without_line_comments, PROCESSOR_CPP};

#[test]
fn existing_blind_loop_audio_oracle_also_avoids_message_thread_state() {
    let fixture = include_str!("../../juce_shell/tests/LiveBlindAudioFixture.h");
    let audio = function_body(fixture, "void processAudio()");
    assert!(!audio.contains("liveCompareStatus"));
    assert_eq!(
        audio.matches("LiveTimingFixtureAccess::audioView").count(),
        2
    );
    let product = include_str!("../../juce_shell/tests/live_blind_product_test.cpp");
    assert!(product.contains("#include \"LiveTimingFixtureAccess.h\""));
    let view = function_body(
        include_str!("../../juce_shell/tests/LiveTimingFixtureAccess.h"),
        "static AudioView audioView",
    );
    for required in [
        "sessionActive.load",
        "completion.pending()",
        "preWaiting.load",
        "gains.post",
        "gainRevision.load",
        "authority.ticket()",
    ] {
        assert!(
            view.contains(required),
            "atomic audio oracle lost {required}"
        );
    }
    for forbidden in [
        "blindStage",
        "blindPreparationReason",
        "String",
        "renderer.",
    ] {
        assert!(!without_line_comments(view).contains(forbidden));
    }
    assert!(audio.contains("rawPostErrors.fetch_add") && audio.contains("loopPcmErrors.fetch_add"));
}

const GAIN: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareGainApproval.h");
const REENTRY: &str = include_str!("../../juce_shell/src/live_compare/LiveCompareReentry.h");
const BLIND: &str = include_str!("../../juce_shell/src/PluginProcessorLiveBlind.cpp");

#[test]
fn direct_live_action_results_follow_ambient_state_acknowledgement() {
    let editor = include_str!("../../juce_shell/src/PluginEditorLiveCompare.cpp");
    let start = function_body(editor, "observatoryView.onLiveCompareStart =");
    assert_eq!(start.matches("refreshLiveCompare()").count(), 1);
    assert!(start.contains("performAction") && start.contains("ActionFaultScope::duringAction"));
    assert!(start.contains("currentNamedAction") && start.contains("generation, false"));
    assert!(start.find("refreshLiveCompare()").unwrap() < start.find("showToast").unwrap());
    let header = include_str!("../../juce_shell/src/PluginEditor.h");
    let refused = function_body(
        header,
        "bool outputRefused (hypha::output_owner::Activity activity)",
    );
    assert!(refused
        .contains("if (hypha::output_owner::liveCause (decision.cause)) refreshLiveCompare();"));
    for (source, method) in [
        (editor, "void KirinHyphaEditor::matchLiveCompare"),
        (editor, "void KirinHyphaEditor::applyLiveCompareChoice"),
        (editor, "void KirinHyphaEditor::pinLiveCompareForBlind"),
        (
            header,
            "bool outputRefused (hypha::output_owner::Activity activity)",
        ),
    ] {
        let body = function_body(source, method);
        assert!(body.find("refreshLiveCompare()").unwrap() < body.find("showToast").unwrap());
        let after_notice = body.rsplit("showToast").next().unwrap();
        assert!(!after_notice.contains("refreshLiveCompare()"));
    }
    let blind_editor = include_str!("../../juce_shell/src/PluginEditorLiveBlind.cpp");
    let open = function_body(blind_editor, "void KirinHyphaEditor::openLiveBlind");
    assert!(open.find("refreshLiveCompare()").unwrap() < open.find("showToast").unwrap());
    let auto_editor = include_str!("../../juce_shell/src/PluginEditorLiveCompareAuto.cpp");
    let follow = function_body(
        auto_editor,
        "void KirinHyphaEditor::chooseLiveCompareFollow",
    );
    assert_eq!(follow.matches("refreshLiveCompare()").count(), 1);
    assert!(follow.find("refreshLiveCompare()").unwrap() < follow.find("showToast").unwrap());
    assert!(
        follow.contains("performAction")
            && follow.contains("ActionFaultScope::includingOpeningRefresh")
    );
    assert!(follow.contains("autoReadiness (current, generation)"));
    assert!(
        follow.find("autoReadiness").unwrap() < follow.find("liveCompareAuto.on = true").unwrap()
    );
    assert!(follow.contains("ActionCompletion::ineligible && refusal.isNotEmpty()"));
    assert!(follow.contains(
        "! opening.active || opening.finishing || opening.sessionGeneration != generation"
    ));
    assert!(follow.contains("matchLiveCompare (generation)"));
    let before_stop = follow.find("safe->stopLiveCompareAuto ({})").unwrap();
    assert!(follow[..before_stop].contains("ActionOutcome::stale"));
    assert!(follow[..before_stop].contains("currentNamedAction (current, generation, false)"));
    assert!(
        !function_body(auto_editor, "void KirinHyphaEditor::stopLiveCompareAuto")
            .contains("refreshLiveCompare")
    );
}

#[test]
fn live_action_boundary_separates_old_history_from_new_success_faults() {
    let action = include_str!("../../juce_shell/src/HyphaLiveCompareActionResult.h");
    let perform = function_body(action, "ActionCompletion performAction");
    let opening = perform
        .find("const bool openingFault = synchronize()")
        .unwrap();
    let operation = perform.find("const auto outcome = action()").unwrap();
    let closing = perform
        .find("const bool closingFault = synchronize()")
        .unwrap();
    let publish = perform.find("publish()").unwrap();
    assert!(opening < operation && operation < closing && closing < publish);
    assert!(
        perform.contains("ActionOutcome::success)")
            && perform.contains("includingOpeningRefresh && openingFault")
    );
    assert!(perform.contains("ActionOutcome::stale) return ActionCompletion::stale"));
    assert!(
        perform.find("return ActionCompletion::newFault").unwrap()
            < perform.find("current()").unwrap()
    );
    let current = function_body(action, "bool currentNamedAction");
    assert!(!without_line_comments(current).contains("reason") && !current.contains("interrupted"));
    let auto = function_body(action, "AutoReadiness autoReadiness");
    for required in [
        "matchReady",
        "matchLimited",
        "Verdict::accepted",
        "state.interrupted",
        "state.observation",
        "contentHeld",
        "compensationOff",
    ] {
        assert!(
            auto.contains(required),
            "AUTO current eligibility lost {required}"
        );
    }
    let editor = include_str!("../../juce_shell/src/PluginEditorLiveCompare.cpp");
    let refresh = function_body(editor, "bool KirinHyphaEditor::refreshLiveCompare");
    assert!(refresh.contains("return newFault"));
    assert!(refresh.contains("monitorLiveCompareOffset (status, now) || newFault"));
    assert!(refresh.contains("followLiveCompare (status, now) || newFault"));
    assert!(refresh.contains("if (newFault) liveCompareAuto.on = false"));
    let apply = function_body(editor, "void KirinHyphaEditor::applyLiveCompareChoice");
    assert!(apply.contains("performAction") && apply.contains("generation, true"));
    let matching = function_body(editor, "void KirinHyphaEditor::matchLiveCompare");
    assert!(
        matching.find("refreshLiveCompare").unwrap() < matching.find("measureLiveCompare").unwrap()
    );
    assert!(matching
        .contains("currentNamedAction (processorRef.liveCompareStatus(), menuGeneration, false)"));
    assert!(matching.contains("! result.generationBound || result.generation != menuGeneration"));
    let fixture =
        include_str!("../../juce_shell/tests/live_compare/live_compare_authority_test.cpp");
    for required in [
        "negative control reproduces the old hidden new fault",
        "observed OR unobserved old retained ceiling",
        "opening OR closing AUTO follow stop",
        "unresolved guard even when MATCH and epoch never changed",
        "each current AUTO safety blocker",
        "obsolete MATCH-again OR Stop-AUTO menu",
        "same-scope menu positive control",
        "a new guard discovered by closing refresh",
        "current AUTO receipt/proof refusal is explicit",
    ] {
        assert!(
            fixture.contains(required),
            "causal action regression lost {required}"
        );
    }
    let aax = include_str!("../../juce_shell/tests/live_compare_aax_group_product_test.cpp");
    assert!(aax.contains("direct refusal survives repeated editor/message/audio refresh ticks"));
    assert!(aax.contains("refusedAt < std::chrono::milliseconds (250)"));
    let catalog = include_str!("../../juce_shell/src/HyphaJapaneseNotices.cpp");
    for notice in [
        "AUTO not started: MATCH pending",
        "AUTO not started: PRE timing pending",
    ] {
        assert!(action.contains(notice) && catalog.contains(notice));
    }
    let screen = include_str!("../../juce_shell/tests/LiveCompareActionNoticeContractTest.h");
    assert!(screen.contains("Language::japanese") && screen.contains("observatory::sizePresets"));
    assert!(
        screen.contains("shownWidth")
            && screen.contains("statusStripBounds")
            && screen.contains("createComponentSnapshot")
    );
    assert!(
        include_str!("../../juce_shell/tests/LiveCompareFooterContractTest.cpp")
            .contains("verifyLiveCompareActionNotices()")
    );
}

#[test]
fn live_reentry_keeps_authority_separate_from_transient_approval_readiness() {
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    let eligibility = rt
        .split("const bool namedEligible =")
        .nth(1)
        .unwrap()
        .split(';')
        .next()
        .unwrap();
    for required in [
        "permitted",
        "sessionActive",
        "! finishing",
        "! blindCommand.active()",
    ] {
        assert!(eligibility.contains(required));
    }
    assert!(
        !eligibility.contains("coherent"),
        "odd writer must not discard known named intent"
    );
    for required in [
        "renderer.renewNamedTiming()",
        "renderer.revokeTiming()",
        "reentry.lost (report.loss",
        "gains.identity.same",
        "reentry.accepted (coherent",
        "TimelineBreak::compensationChanged",
        "namedEligible && ! callbackGap",
        "gainCurrent && report.gainSettled",
    ] {
        assert!(rt.contains(required), "named re-entry lost {required}");
    }
    assert!(REENTRY.contains("renewing && approvalReady"));
    assert!(REENTRY.contains("if (approvalReady) renewing = false"));
    let causes = include_str!("../../juce_shell/src/live_compare/LiveCompareLoop.h");
    let permit = function_body(causes, "inline bool namedTransportRestart");
    assert!(
        permit.contains("stopped")
            && permit.contains("projectMoved")
            && permit.contains("compensationChanged")
    );
    for forbidden in ["callbackGap", "clockMissing", "sourceChanged", "unknown"] {
        assert!(!permit.contains(forbidden));
    }
}

#[test]
fn approved_gain_has_one_bounded_writer_and_one_current_audio_receipt() {
    for required in [
        "compare_exchange_strong",
        "revision + 1",
        "revision + 2",
        "memory_order_acq_rel",
        "result.post = state.postTarget.load",
        "result.identity = state.gainApproval.read",
        "std::atomic_thread_fence (std::memory_order_acquire)",
        "state.gainReceipt.load",
        "state.matchGeneration.load",
        "state.matchRun.load",
        "state.blindGainRevision.load",
    ] {
        assert!(
            GAIN.contains(required),
            "approval publication lost {required}"
        );
    }
    for writer in [
        "void KirinHyphaProcessorBase::setLiveCompareGain",
        "hypha::live_compare::MatchApplication KirinHyphaProcessorBase::applyLiveCompareMatch",
        "bool KirinHyphaProcessorBase::followLiveCompareGain",
    ] {
        assert!(function_body(PROCESSOR_CPP, writer)
            .contains("GainUpdate update (liveCompare.gainRevision)"));
    }
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    assert!(rt.find("readGainSnapshot").unwrap() < rt.find("float postTarget").unwrap());
    assert!(rt.contains("if (! coherent) postTarget = liveCompare.postLevel.value();"));
    assert!(rt.contains("if (update) liveCompare.postTarget.store"));
    for body in [GAIN, REENTRY] {
        let code = without_line_comments(body);
        for forbidden in [
            "new ",
            "delete ",
            "mutex",
            "while (",
            "sleep",
            "fstream",
            "std::vector",
        ] {
            assert!(
                !code.contains(forbidden),
                "RT approval/re-entry contains {forbidden}"
            );
        }
    }
    let service = function_body(BLIND, "void KirinHyphaProcessorBase::serviceLiveBlind");
    assert!(service.find("currentGainReceipt").unwrap() < service.find("blind.startWith").unwrap());
    assert!(service.matches("currentGainReceipt").count() >= 2);
    for method in [
        "LiveBlindStatus KirinHyphaProcessorBase::liveBlindStatus",
        "bool KirinHyphaProcessorBase::selectLiveBlind",
        "bool KirinHyphaProcessorBase::revealLiveBlind",
    ] {
        assert!(function_body(BLIND, method).contains("currentBlindReceipt (liveCompare)"));
    }
}

#[test]
fn unity_idle_reads_only_a_coherent_post_subset_after_clock_and_match_invalidation() {
    let idle = include_str!("../../juce_shell/src/live_compare/LiveCompareIdle.h");
    let subset = function_body(idle, "bool coherentUnityPostTarget");
    let revision = subset.find("state.gainRevision.load").unwrap();
    let target = subset.find("state.postTarget.load").unwrap();
    let fence = subset.find("std::atomic_thread_fence").unwrap();
    let last_revision = subset.rfind("state.gainRevision.load").unwrap();
    assert!(revision < target && target < fence && fence < last_revision);
    assert_eq!(subset.matches("state.gainRevision.load").count(), 2);
    assert_eq!(subset.matches("memory_order_acquire").count(), 4);
    assert!(subset.contains("(revision & 1u) != 0") && subset.contains("target == 1.0f"));
    for forbidden in [
        "gainApproval",
        "ceilingLinear",
        "matchLimited",
        "matchRetained",
        "gainReceipt",
        "state.gain.load",
    ] {
        assert!(!without_line_comments(subset).contains(forbidden));
    }
    let routing = function_body(idle, "bool unchangedUnityPostOnly");
    assert!(
        routing.find("unchangedPostOnly").unwrap()
            < routing.find("&& coherentUnityPostTarget").unwrap()
    );
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    let early = rt.find("unchangedUnityPostOnly").unwrap();
    assert!(rt.find("liveCompare.preparation.observe").unwrap() < early);
    assert!(rt.find("liveCompare.matched.store (false").unwrap() < early);
    assert!(early < rt.find("readGainSnapshot").unwrap());
    assert!(rt.contains("finishing, blindCommand.active(), liveCompare.postLevel.value()"));
    let early_body = function_body(rt, "if (hypha::live_compare::unchangedUnityPostOnly");
    assert!(early_body.contains("return;"));
    for forbidden in [
        "gainReceipt",
        "matched.store",
        "renderer",
        "blind.observe",
        "buffer.",
        "GainSnapshot",
    ] {
        assert!(!without_line_comments(early_body).contains(forbidden));
    }
    let fixture =
        include_str!("../../juce_shell/tests/live_compare/live_compare_gain_snapshot_test.cpp");
    for proof in [
        "idle load mask excludes",
        "short-circuits every gain load",
        "odd opening revision",
        "writer after first revision OR target",
        "fresh full tuple with no partial-field reuse",
        "writer still odd at final revision",
        "publication after final sampled revision",
        "unity after authority revoke",
        "revoked retained attenuation",
    ] {
        assert!(
            fixture.contains(proof),
            "unity-idle causal control lost {proof}"
        );
    }
}

#[test]
fn reentry_product_audio_oracle_reads_only_atomic_fixture_observations() {
    let fixture = include_str!("../../juce_shell/tests/live_compare_reentry_product_test.cpp");
    let audio = function_body(fixture, "void processAudio()");
    assert!(!audio.contains("liveCompareStatus"));
    assert_eq!(
        audio.matches("LiveTimingFixtureAccess::audioView").count(),
        2
    );
    let view = function_body(
        include_str!("../../juce_shell/tests/LiveTimingFixtureAccess.h"),
        "static AudioView audioView",
    );
    for forbidden in [
        "blindStage",
        "blindPreparationReason",
        "String",
        "renderer.",
    ] {
        assert!(!without_line_comments(view).contains(forbidden));
    }
    let pair = include_str!("../../juce_shell/src/PluginProcessorPairing.cpp");
    for method in [
        "bool KirinHyphaProcessorBase::setPairCandidate",
        "void KirinHyphaProcessorBase::clearPairCandidate",
    ] {
        let body = function_body(pair, method);
        assert!(
            body.find("invalidateLiveComparePair()").unwrap() < body.find("handleLock").unwrap()
        );
    }
    let blind = function_body(BLIND, "StartResult KirinHyphaProcessorBase::beginLiveBlind");
    assert!(
        blind.contains("blindTiming.begin (requiredAdmission)")
            && blind.contains("blindTimingAuthority")
            && blind.contains("blindTimingRequest")
    );
    assert!(!blind.contains("historyView().kValid"));
    let opening = blind.find("blindTiming.begin (requiredAdmission)").unwrap();
    let publication = blind.find("blindTimingRequest.fetch_add").unwrap();
    assert!(blind.find("matched.store (false").unwrap() < opening);
    assert!(blind.find("sessionGeneration.fetch_add").unwrap() < opening);
    assert!(opening < publication);
    let preparation = include_str!("../../juce_shell/src/live_compare/LiveBlindPreparation.h");
    assert!(preparation.contains("command.requiredAdmission != admissionReceipt"));
    let rt = function_body(
        PROCESSOR_CPP,
        "void KirinHyphaProcessorBase::processLiveCompare",
    );
    assert_eq!(rt.matches("blindTiming.command()").count(), 1);
    assert!(rt.contains("! liveCompare.blindTiming.failedAndOpen()"));
    for method in [
        "void KirinHyphaProcessorBase::finishLiveCompare",
        "void KirinHyphaProcessorBase::stopLiveCompare (",
    ] {
        let source = if method.contains("finish") {
            BLIND
        } else {
            PROCESSOR_CPP
        };
        assert!(function_body(source, method).contains("cancelTimingAdmission()"));
    }
}
