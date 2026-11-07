#pragma once
#include "../src/HyphaLiveCompareRecoveryText.h"
#include <cstring>

namespace hypha::tests
{
template<class Require> void verifyCandidateRecoveryPriority (Require require)
{
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
    named.observation = blind.observation = RecoveryReason::preLate;
    check ("Position unconfirmed; B plays", "Position unconfirmed; B plays"); // the short wait never blames the chain
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
