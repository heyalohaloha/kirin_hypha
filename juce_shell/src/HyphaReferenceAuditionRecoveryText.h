#pragma once
#include "HyphaLiveCompareRecoveryText.h"

// LISTEN while REF plays B, C or V (2026-10-18). Both can be on: the audition keeps POST's output
// (INV-S53) and LISTEN keeps the user's choice of PRE or POST on screen (INV-LC21: a short wait is
// not shown). Only when the audition has held the output past the usual 3 s does LISTEN say what
// plays and how to hear its choice again: A in REF hands the output back, and LISTEN resumes by
// itself once PRE is confirmed. `outputSlot` is what the Audio Thread actually played in its last
// block (1 V, 2 C, 3 B, 0 none), not what is selected.
namespace hypha::live_compare_ui
{
inline const char* auditionHoldingOutput (int outputSlot) noexcept
{
    switch (outputSlot)
    {
        case 1: return "V plays from REF: press A there to listen";
        case 2: return "C plays from REF: press A there to listen";
        case 3: return "B plays from REF: press A there to listen";
        default: return "";
    }
}

// The whole story for the help line and the status window: what plays now and how to hear the
// LISTEN choice (PRE or POST) again.
inline const char* auditionNextStep (int outputSlot, bool preSelected) noexcept
{
    switch (outputSlot)
    {
        case 1: return preSelected ? "V plays now. Press A in REF to hear PRE." : "V plays now. Press A in REF to hear POST.";
        case 2: return preSelected ? "C plays now. Press A in REF to hear PRE." : "C plays now. Press A in REF to hear POST.";
        case 3: return preSelected ? "B plays now. Press A in REF to hear PRE." : "B plays now. Press A in REF to hear POST.";
        default: return "";
    }
}

inline bool auditionHoldsListen (const live_compare::Status& state, int outputSlot) noexcept
{
    return state.active && ! state.finishing && state.prolongedWait && *auditionHoldingOutput (outputSlot) != 0;
}

// LISTEN's standing notice: the audition's, when it has held the output past 3 s, else the named one.
inline RecoveryPresentation currentListenPresentation (const live_compare::Status& state,
    live_compare::StartResult admission, int outputSlot) noexcept
{
    if (auditionHoldsListen (state, outputSlot))
        return { Reason::outputTaken, RecoveryAction::none, auditionHoldingOutput (outputSlot) };
    return currentNamedPresentation (state, admission);
}
}
