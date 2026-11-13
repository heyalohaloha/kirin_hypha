#pragma once
#include "CandidateRecoveryTest.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include <cstring>

namespace hypha::tests
{
template<class Require> void verifyCandidateLoopGuidance (Require require)
{
    using namespace live_compare;
    for (const bool loop : { false, true })
    {
        ProductTestClock wall;
        CandidateHost<Require> host (wall, require, 2, 480);
        host.pair(); auto& post = host.post();
        if (loop) for (auto& clock : host.clocks) clock.loopLength = 48000;
        else host.clocks[0].available = false;
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "loop guidance starts through the actual A/B entry");
        post.selectLiveComparePre (true);
        host.render (299, false, true, false);
        require (!post.liveCompareStatus().prolongedWait, "A/B guidance stays quiet before three seconds");
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage scoped (language);
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
            editor->setVisible (true); candidateTick (*editor);
            const auto warning = text_style::shownText (
                dynamic_cast<KirinHyphaEditor&> (*editor).*candidateEditorTick (CandidateEditorWarning {}));
            require (!warning.contains (text_style::shownText ("A/B: timing unavailable; check PRE1 and PRE2"))
                && !warning.contains (text_style::shownText ("Loop timing unavailable; turn loop off to compare (POST)")),
                     "the actual A/B editor hides recovery guidance during initial preparation");
            post.editorBeingDeleted (editor.get());
        }
        host.render (2, false, true, false);
        const auto status = post.liveCompareStatus();
        require (status.prolongedWait && status.preSelected && !status.preAudible
            && status.verdict != Verdict::accepted, "unconfirmed A/B keeps the selection and POST after three seconds");
        requireCandidateRawPost (host, require);
        const char* expected = loop ? "Loop timing unavailable; turn loop off to compare (POST)"
                                    : "A/B: timing unavailable; check PRE1 and PRE2";
        require (status.observation == (loop ? RecoveryReason::loopClockUnavailable : RecoveryReason::notWritten),
                 "loop clock failure takes notice priority; other candidate failures keep their reason");
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage scoped (language);
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
            editor->setVisible (true); candidateTick (*editor);
            const auto warning = dynamic_cast<KirinHyphaEditor&> (*editor).*candidateEditorTick (CandidateEditorWarning {});
            require (text_style::shownText (warning) == text_style::shownText (expected),
                     "the actual A/B editor shows the loop remedy instead of the generic PRE1/PRE2 notice in both languages");
            post.editorBeingDeleted (editor.get());
        }
    }
}

template<class Require> void verifyCandidateRecoveryPriority (Require require)
{
    verifyCandidateLoopGuidance (require);
    using namespace live_compare;
    using namespace live_compare_ui;
    Status named; named.active = named.prolongedWait = named.preSelected = named.preWaiting = true;
    named.verdict = Verdict::noClock;
    LiveBlindStatus blind; blind.candidate = blind.prolongedWait = true;
    const auto check = [&] (const char* namedText, const char* blindText)
    {
        const auto current = currentNamedPresentation (named);
        const auto result = candidateNamedPresentation (named, current, true, false, false);
        require (std::strcmp (result.instruction, namedText) == 0, "named A/B keeps the correct recovery priority");
        require (std::strcmp (blindRecovery (blind, false).instruction, blindText) == 0, "BLIND keeps the correct recovery priority");
    };
    named.compensationOff = blind.compensationOff = true;
    check ("Compensation off: enable it (POST)", "Enable DAW delay compensation");
    named.compensationOff = blind.compensationOff = false;
    named.loopBlock = blind.loopBlock = LoopPairingBlock::auCycle;
    check ("Turn Cycle off to compare (POST)", "Turn Cycle off to compare");
    named.loopBlock = blind.loopBlock = LoopPairingBlock::aaxLoop;
    check ("Loop Playback on: turn it off to compare (POST)", "Turn off Loop Playback to compare");
    named.loopBlock = blind.loopBlock = LoopPairingBlock::none;
    named.observation = blind.observation = RecoveryReason::reaperDelayUnverified;
    named.reaperRestriction = blind.reaperRestriction = ReaperChainRestriction::routing;
    check ("Loop needs a plain serial chain (POST)", "Loop needs a plain serial chain (POST)");
    named.observation = blind.observation = RecoveryReason::none;
    named.clockUnavailable = blind.clockUnavailable = true;
    check ("This DAW has no verified audio clock; PRE is unavailable (POST)", "This DAW has no verified audio clock; PRE is unavailable");
    named.clockUnavailable = blind.clockUnavailable = false;
    named.observation = blind.observation = RecoveryReason::contentAmbiguous;
    check ("Position unconfirmed; B plays", "Position unconfirmed; B plays");
    named.observation = blind.observation = RecoveryReason::stopped;
    check ("DAW stopped: play to resume (POST)", "DAW stopped: play to resume (POST)");
    for (const auto reason : { RecoveryReason::ceiling, RecoveryReason::nonFinite })
    {
        named.observation = blind.observation = reason;
        check ("Check chain levels; resumes automatically (POST)", "Check chain levels; resumes automatically (POST)");
    }
    named.observation = blind.observation = RecoveryReason::none;
    named.signalWait = blind.signalWait = true;
    check ("MATCH waits for more signal", "MATCH waits for more signal");
    named.signalWait = blind.signalWait = false;
    named.observation = blind.observation = RecoveryReason::callbackGap;
    check (candidateRecoveryText (false, false), candidateRecoveryText (false, false));
    for (const bool inactive : { false, true })
    {
        const auto expected = candidateRecoveryText (inactive, !inactive);
        require (std::strcmp (candidateNamedPresentation (named, currentNamedPresentation (named), true, inactive, !inactive).instruction, expected) == 0,
                 "named A/B reports a confirmed host disablement or bypass");
        blind.candidateHostInactive = inactive; blind.candidateBypassed = !inactive;
        require (std::strcmp (blindRecovery (blind, false).instruction, expected) == 0,
                 "BLIND reports a confirmed host disablement or bypass");
    }
    blind.candidateHostInactive = blind.candidateBypassed = false;
    const auto original = currentNamedPresentation (named);
    require (std::strcmp (candidateNamedPresentation (named, original, false, false, false).instruction, original.instruction) == 0,
             "non-candidate presentation is unchanged");
    blind.candidate = false;
    require (std::strcmp (blindRecovery (blind, false).instruction, "Keep playing; resumes when confirmed") == 0,
             "ordinary BLIND presentation is unchanged");
    blind.candidate = true; blind.prolongedWait = named.prolongedWait = false;
    require (!*candidateNamedPresentation (named, currentNamedPresentation (named), true, false, false).instruction
        && !*blindRecovery (blind, false).instruction, "short waits remain quiet");
    named.prolongedWait = blind.prolongedWait = true;
    named.finishing = true;
    const auto finishing = currentNamedPresentation (named);
    require (std::strcmp (candidateNamedPresentation (named, finishing, true, true, false).instruction, finishing.instruction) == 0,
             "ending guidance is not replaced");
    named.finishing = false; named.interrupted = true;
    const auto interrupted = currentNamedPresentation (named);
    require (std::strcmp (candidateNamedPresentation (named, interrupted, true, true, false).instruction, interrupted.instruction) == 0
        && std::strcmp (blindRecovery (blind, true).instruction, "END, then start BLIND again") == 0,
             "invalidated guidance is not replaced");
}
}
