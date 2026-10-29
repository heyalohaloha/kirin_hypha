#pragma once
#include "HyphaHostAvailability.h"

#include "live_compare/LiveCompareProcessorState.h"

namespace hypha::live_compare_ui
{
using Reason = live_compare::RecoveryReason;

inline bool startNeedsPreChoice (live_compare::StartResult result) noexcept
{ return result == live_compare::StartResult::noPair || result == live_compare::StartResult::preUnavailable; }
inline const char* preUnavailableNotice() noexcept { return "PRE not running; choose again"; }

inline const char* cause (Reason reason) noexcept
{
    switch (reason)
    {
        case Reason::stopped: return "DAW playback stopped";
        case Reason::positionChanged: return "DAW position was discontinuous";
        case Reason::callbackGap: return "Audio callback gap detected";
        case Reason::projectClockMissing: return "DAW position unavailable";
        case Reason::clockMissing: return "Audio clock unavailable";
        case Reason::calibrating: return "PRE timing is not confirmed";
        case Reason::loopUnproven: return "The PRE loop occurrence is not verified";
        case Reason::loopWaiting: return "Loop boundary timing is not confirmed";
        case Reason::loopTooShort: return "The loop is shorter than the verified timing range";
        case Reason::loopClockUnavailable: return "DAW timing cannot identify this loop occurrence";
        case Reason::reaperFirstCycle: return "Checking the loop timing; POST plays";
        case Reason::reaperDelayUnverified: return "Loop delay is not verified; POST plays";
        case Reason::reaperLoopTooShort: return "The loop is shorter than the verified timing range";
        case Reason::cycleUnverified: return "PRE/POST timing is unverified during Cycle.";
        case Reason::writing: return "PRE data is being written";
        case Reason::beforeRun: return "PRE continuity changed";
        case Reason::notWritten: return "PRE data has not arrived";
        case Reason::overwritten: return "PRE data is no longer buffered";
        case Reason::torn: return "PRE data changed during reading";
        case Reason::foreignRing: return "PRE identity or format changed";
        case Reason::pairChanged: return "The selected PRE changed";
        case Reason::preUnavailable: return "PRE connection was lost";
        case Reason::formatChanged: return "Audio format changed";
        case Reason::restored: return "Plugin state was restored";
        case Reason::compensationOff: return "DAW delay compensation is off";
        case Reason::contentChanged: return "Audio timing changed";
        case Reason::bypassed: return "Plugin bypass was reported";
        case Reason::offline: return "Offline rendering was reported";
        case Reason::outputTaken: return "Another audition took the output";
        case Reason::gainChanged: return "The fixed MATCH became invalid";
        case Reason::nonFinite: return "Invalid audio samples detected";
        case Reason::ceiling: return "The approved level limit was exceeded";
        case Reason::blockTooLarge: return "Audio block exceeded prepared size";
        case Reason::randomUnavailable: return "Random assignment was unavailable";
        case Reason::none: case Reason::unknown: return "Comparison could not be verified";
    }
    return "Comparison could not be verified";
}

enum class RecoveryAction { none, automatic, play, stopPlay, compensation, checkPre,
                            checkLevels, selectPre, endBlind, returnLevel, listen, loopOff,
                            noOutputLatency, cycleOff, cycleOffEndBlind, busy };

// After the cause: what plays now and how to go on. The short status in the footer is the same
// story cut to fit; the help line and the status window tell it whole as "LISTEN: <cause>. <next step>"
// (PluginEditorHelpLine.cpp).
inline const char* nextStep (RecoveryAction action) noexcept
{
    switch (action)
    {
        case RecoveryAction::automatic: return "POST plays until PRE is confirmed, then it resumes by itself.";
        case RecoveryAction::play: return "POST plays now. Play the DAW to resume.";
        case RecoveryAction::stopPlay: return "POST plays now. Stop and play the DAW to continue.";
        case RecoveryAction::compensation: return "POST plays now. Turn on the DAW's delay compensation.";
        case RecoveryAction::checkPre: return "POST plays now. Check the PRE in PAIR, then MENU > LISTEN.";
        case RecoveryAction::checkLevels: return "POST plays now. Check chain levels; PRE resumes when safe.";
        case RecoveryAction::selectPre: return "POST plays now. Select PRE to compare again.";
        case RecoveryAction::endBlind: return "Press END, then start BLIND again.";
        case RecoveryAction::returnLevel: return "POST is still lowered. Press RETURN, then LISTEN.";
        case RecoveryAction::listen: return "POST plays now. MENU > LISTEN compares again.";
        case RecoveryAction::loopOff: return "POST plays now. Turn off the DAW's Loop Playback to compare.";
        case RecoveryAction::cycleOff: return "Turn Cycle off to compare (POST)";
        case RecoveryAction::cycleOffEndBlind: return "Turn Cycle off. Press END, then start BLIND again.";
        case RecoveryAction::noOutputLatency: return "POST plays now. PRE timing cannot be verified from this DAW's audio clock.";
        case RecoveryAction::busy: case RecoveryAction::none: break;
    }
    return "";
}
struct RecoveryPresentation
{
    Reason reason = Reason::none; // history, independent of the current remedy
    RecoveryAction action = RecoveryAction::none;
    const char* instruction = "";
};

// One message for both named and anonymous listening. A first pass is an observation,
// whereas an unavailable host API cannot be repaired by extending the loop.
inline RecoveryPresentation loopRecovery (Reason reason, live_compare::ReaperChainRestriction restriction) noexcept
{
    if (reason == Reason::reaperFirstCycle || reason == Reason::reaperDelayUnverified)
    {
        if (const auto* text = live_compare::reaperRestrictionText (restriction); *text)
            return { reason, RecoveryAction::none, text };
        if (reason == Reason::reaperDelayUnverified)
            return { reason, RecoveryAction::loopOff, "Loop timing unavailable; turn loop off to compare (POST)" };
    }
    if (reason == Reason::loopWaiting || reason == Reason::reaperFirstCycle)
        return { reason, RecoveryAction::automatic, "Waiting for the first loop pass (POST)" };
    if (reason == Reason::reaperLoopTooShort || reason == Reason::loopTooShort)
        return { reason, RecoveryAction::none, "Use a longer loop; POST plays" };
    if (reason == Reason::loopClockUnavailable)
        return { reason, RecoveryAction::loopOff, "Loop timing unavailable; turn loop off to compare (POST)" };
    if (reason == Reason::loopUnproven)
        return { reason, RecoveryAction::automatic, "Timing unavailable; check PRE and play (POST)" };
    return {};
}

inline RecoveryPresentation blindRecovery (const live_compare::LiveBlindStatus& state, bool stopped) noexcept
{
    const auto reason = state.reason == Reason::none ? state.observation : state.reason;
    if (!stopped && state.signalWait)
        return { Reason::none, RecoveryAction::none, "MATCH waits for more signal" };
    if (!stopped && !state.prolongedWait) return {};
    if (!stopped && reason == Reason::stopped)
        return { reason, RecoveryAction::play, "DAW stopped: play to resume (POST)" };
    if (!stopped && (reason == Reason::ceiling || reason == Reason::nonFinite))
        return { reason, RecoveryAction::checkLevels, "Check chain levels; resumes automatically (POST)" };
    if (state.contentHeld)
        return { Reason::contentChanged, RecoveryAction::stopPlay, "Timing changed: check chain delay (POST)" };
    // An uncertified AAX host looping a range observes an unidentifiable loop too; loop playback off
    // restores the linear proof, so this actionable remedy comes before the host boundaries below.
    if (reason == Reason::cycleUnverified || (! stopped && state.loopBlock == live_compare::LoopPairingBlock::auCycle))
        return { Reason::cycleUnverified, stopped ? RecoveryAction::cycleOffEndBlind : RecoveryAction::cycleOff,
                 stopped ? "Turn Cycle off. Press END, then start BLIND again." : "Turn Cycle off to compare" };
    if (state.loopBlock == live_compare::LoopPairingBlock::aaxLoop && ! stopped)
        return { reason, RecoveryAction::loopOff, "Turn off Loop Playback to compare" };
    if (!stopped && state.observation == Reason::loopClockUnavailable)
        return { reason, RecoveryAction::loopOff, "Turn off Loop Playback to compare" };
    if (!stopped)
        if (const auto loop = loopRecovery (state.observation, state.reaperRestriction); *loop.instruction)
            return loop;
    if (state.timingUnavailable)
        return { reason, RecoveryAction::none, "Timing unavailable; check PRE and play (POST)" };
    // A host that never reports a clock or its output latency: no stop/play or retry would help.
    if (state.clockUnavailable)
        return { reason, RecoveryAction::noOutputLatency, "This DAW has no verified audio clock; PRE is unavailable" };
    if (state.compensationOff)
        return { reason, RecoveryAction::compensation, stopped ? "Enable compensation; END, then BLIND"
                                                              : "Enable DAW delay compensation" };
    if (state.observation == Reason::loopTooShort
        || state.observation == Reason::loopClockUnavailable)
        return { reason, RecoveryAction::none, "Timing unavailable; check PRE and play (POST)" };
    if (! stopped)
    {
        if (reason == Reason::notWritten) return { reason, RecoveryAction::checkPre, "Play the DAW and check PRE is enabled" };
        return { reason, RecoveryAction::automatic, "Keep playing; resumes when confirmed" };
    }
    if (reason == Reason::pairChanged || reason == Reason::preUnavailable || reason == Reason::foreignRing)
        return { reason, RecoveryAction::checkPre, "Check the PRE pair; END, then BLIND" };
    if (reason == Reason::nonFinite || reason == Reason::ceiling)
        return { reason, RecoveryAction::checkLevels, "Check chain levels; END, then BLIND" };
    if (reason == Reason::bypassed) return { reason, RecoveryAction::endBlind, "Disable bypass; END, then BLIND" };
    return { reason, RecoveryAction::endBlind, "END, then start BLIND again" };
}

// Pure projection: retained history never dictates whether a button currently exists. Current
// blockers survive END; the existing admission fence decides whether re-entry is available.
inline RecoveryPresentation namedPresentation (const live_compare::Status& state,
    live_compare::StartResult admission = live_compare::StartResult::started) noexcept
{
    const auto result = [&] (RecoveryAction action, const char* text)
    { return RecoveryPresentation { state.reason, action, text }; };
    if (state.active && state.signalWait) return result (RecoveryAction::none, "MATCH waits for more signal");
    if (state.active && !state.interrupted && !state.prolongedWait) return {};
    if (state.active && !state.interrupted && !state.preSelected
        && (state.loopBlock != live_compare::LoopPairingBlock::none
            || *loopRecovery (state.observation, state.reaperRestriction).instruction)) return {};
    if (state.finishing) return result (RecoveryAction::busy, "Ended; normal level returns with audio");
    if (state.contentHeld) return result (RecoveryAction::stopPlay, "Timing changed: check chain delay (POST)");
    // An uncertified AAX host looping a range observes an unidentifiable loop too, but loop playback
    // off restores the linear proof: this actionable remedy comes before any host boundary below.
    if (state.active && state.loopBlock == live_compare::LoopPairingBlock::auCycle
        && (state.preWaiting || ! state.preAudible))
        return { Reason::cycleUnverified, RecoveryAction::cycleOff, "Turn Cycle off to compare (POST)" };
    if (state.active && state.loopBlock == live_compare::LoopPairingBlock::aaxLoop && (state.preWaiting || ! state.preAudible))
        return result (RecoveryAction::loopOff, "Loop Playback on: turn it off to compare (POST)");
    if (state.active && !state.interrupted)
        if (const auto loop = loopRecovery (state.observation, state.reaperRestriction); *loop.instruction)
            return loop;
    if (state.active && state.timingUnavailable)
        return result (RecoveryAction::none, host_availability::timingText());
    // The clock is missing on a host that has never reported one, nor its output latency: this is
    // the host's ability, so neither "restart playback" nor "select PRE again" is promised.
    if (state.active && state.clockUnavailable)
        return result (RecoveryAction::noOutputLatency, "This DAW has no verified audio clock; PRE is unavailable (POST)");
    if (state.compensationOff && (state.active || state.reason != Reason::none))
        return result (RecoveryAction::compensation, "Compensation off: enable it (POST)");
    if (state.active && state.interrupted)
        return result (RecoveryAction::checkPre, "PRE changed: check pair; END (POST)");
    if (! state.active && state.reason != Reason::none)
    {
        if (state.postActual < 1.0f || state.postTarget < 1.0f)
            return result (RecoveryAction::returnLevel, "Stopped: RETURN, then LISTEN (POST)");
        if (admission != live_compare::StartResult::started)
            return result (RecoveryAction::busy, "Comparison releasing; POST plays");
        if (state.reason == Reason::restored)
            return result (RecoveryAction::listen, "Reopened: MENU > LISTEN (POST)");
        if (state.reason == Reason::formatChanged)
            return result (RecoveryAction::listen, "Format changed: MENU > LISTEN (POST)");
        if (state.reason == Reason::pairChanged || state.reason == Reason::preUnavailable || state.reason == Reason::foreignRing)
            return result (RecoveryAction::checkPre, "PRE changed: check PAIR, MENU > LISTEN (POST)");
        return result (RecoveryAction::listen, "Stopped: MENU > LISTEN (POST)");
    }
    if (state.active && (state.observation == Reason::ceiling || state.observation == Reason::nonFinite))
        return result (RecoveryAction::checkLevels, "Check chain levels; resumes automatically (POST)");
    if (state.active && state.preSelected && state.preWaiting)
    {
        if (state.observation == Reason::stopped) return result (RecoveryAction::play, "DAW stopped: play to resume (POST)");
        if (state.observation == Reason::clockMissing || state.observation == Reason::projectClockMissing)
            return result (RecoveryAction::stopPlay, "Clock missing: restart playback (POST)");
        if (state.observation == Reason::blockTooLarge) return result (RecoveryAction::busy, "Block too large: reduce buffer (POST)");
        if (state.observation == Reason::foreignRing) return result (RecoveryAction::checkPre, "PRE changed: check pair; END (POST)");
        if (state.observation == Reason::notWritten) return result (RecoveryAction::checkPre, "PRE pending: check PRE is on (POST)");
        return result (RecoveryAction::automatic, "Timing unavailable; check PRE and play (POST)");
    }
    if (state.active && state.matchHeld && ! state.interrupted)
        return result (RecoveryAction::selectPre, "Timing unavailable; check PRE and play (POST)");
    return {};
}
inline const char* namedRecovery (const live_compare::Status& state) noexcept
{ return namedPresentation (state).instruction; }

// Retained revocation reasons protect the next admission; they are not current listening
// activity. Only a live session, an audio return or a still-held output needs a standing remedy.
// An actual session's terminal transition uses namedRecovery once in the existing toast rail.
inline RecoveryPresentation currentNamedPresentation (const live_compare::Status& state,
    live_compare::StartResult admission = live_compare::StartResult::started) noexcept
{
    if (state.finishing || state.active)
        return namedPresentation (state, admission);
    if (state.postActual < 1.0f || state.postTarget < 1.0f)
        return { state.reason, RecoveryAction::returnLevel, "Stopped: RETURN, then LISTEN (POST)" };
    return {};
}
}
