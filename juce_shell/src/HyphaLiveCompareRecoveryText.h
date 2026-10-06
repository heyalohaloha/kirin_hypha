#pragma once

#include "live_compare/LiveCompareProcessorState.h"

namespace hypha::live_compare_ui
{
using Reason = live_compare::RecoveryReason;

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
                            checkLevels, selectPre, endBlind, returnLevel, listen, busy };

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
        case RecoveryAction::checkLevels: return "POST plays now. Check the chain's levels, then MATCH again.";
        case RecoveryAction::selectPre: return "POST plays now. Select PRE to compare again.";
        case RecoveryAction::endBlind: return "Press END, then start BLIND again.";
        case RecoveryAction::returnLevel: return "POST is still lowered. Press RETURN, then LISTEN.";
        case RecoveryAction::listen: return "POST plays now. MENU > LISTEN compares again.";
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

inline RecoveryPresentation blindRecovery (const live_compare::LiveBlindStatus& state, bool stopped) noexcept
{
    const auto reason = state.reason == Reason::none ? state.observation : state.reason;
    if (state.contentHeld)
        return { reason, RecoveryAction::stopPlay, stopped ? "Stop/play DAW; END, then BLIND"
                                                          : "Stop and play the DAW to continue" };
    if (state.compensationOff)
        return { reason, RecoveryAction::compensation, stopped ? "Enable compensation; END, then BLIND"
                                                              : "Enable DAW delay compensation" };
    if (state.observation == Reason::loopUnproven
        || state.observation == Reason::loopTooShort
        || state.observation == Reason::loopClockUnavailable)
        return { reason, RecoveryAction::none, "POST plays; END closes comparison" };
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
    if (state.finishing) return result (RecoveryAction::busy, "Ended; normal level returns with audio");
    if (state.contentHeld) return result (RecoveryAction::stopPlay, "Timing changed: stop/play DAW (POST)");
    if (state.compensationOff && (state.active || state.reason != Reason::none))
        return result (RecoveryAction::compensation, "Compensation off: enable it (POST)");
    if (state.active && state.interrupted)
    {
        if (state.reason == Reason::ceiling) return result (RecoveryAction::checkLevels, "Level limit: rematch, select PRE (POST)");
        if (state.reason == Reason::nonFinite) return result (RecoveryAction::checkLevels, "Invalid audio: check chain (POST)");
        if (state.reason == Reason::bypassed) return result (RecoveryAction::selectPre, "Bypassed: enable, select PRE (POST)");
        if (state.reason == Reason::offline) return result (RecoveryAction::selectPre, "Offline: play, select PRE (POST)");
        if (state.reason == Reason::outputTaken) return result (RecoveryAction::selectPre, "Other audition: end it, select PRE");
        if (state.reason == Reason::callbackGap) return result (RecoveryAction::selectPre, "Audio gap: select PRE again (POST)");
        if (state.reason == Reason::clockMissing || state.reason == Reason::projectClockMissing)
            return result (RecoveryAction::selectPre, "Clock missing: select PRE again (POST)");
        return result (RecoveryAction::selectPre, "Select PRE again");
    }
    if (state.active && (state.observation == Reason::loopUnproven
        || state.observation == Reason::loopTooShort
        || state.observation == Reason::loopClockUnavailable))
        return state.timingReentryPending && state.observation == Reason::loopUnproven
            ? result (RecoveryAction::automatic, "Checking PRE: auto-resume; POST plays")
            : result (RecoveryAction::none, state.observation == Reason::loopTooShort
            ? "Loop too short for verified timing; POST plays"
            : "LOOP timing unverified; POST; END closes");
    if (state.active && state.observation == Reason::loopWaiting)
        return result (RecoveryAction::automatic, state.matched || state.matchHeld
            ? "Loop timing pending; MATCH held; POST plays" : "Checking PRE: auto-resume; POST plays");
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
    if (state.active && state.preSelected && state.preWaiting)
    {
        if (state.observation == Reason::stopped) return result (RecoveryAction::play, "DAW stopped: play to resume (POST)");
        if (state.observation == Reason::clockMissing || state.observation == Reason::projectClockMissing)
            return result (RecoveryAction::stopPlay, "Clock missing: restart playback (POST)");
        if (state.observation == Reason::blockTooLarge) return result (RecoveryAction::busy, "Block too large: reduce buffer (POST)");
        if (state.observation == Reason::foreignRing) return result (RecoveryAction::checkPre, "PRE changed: check pair; END (POST)");
        if (state.observation == Reason::notWritten) return result (RecoveryAction::checkPre, "PRE pending: check PRE is on (POST)");
        return result (RecoveryAction::automatic, "Checking PRE: auto-resume; POST plays");
    }
    if (state.active && state.matchHeld && ! state.interrupted)
        return result (RecoveryAction::selectPre, "MATCH held; select PRE to check timing");
    return {};
}
inline const char* namedRecovery (const live_compare::Status& state) noexcept
{ return namedPresentation (state).instruction; }
}
