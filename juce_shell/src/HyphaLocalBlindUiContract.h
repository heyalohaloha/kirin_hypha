#pragma once

#include "local_blind/LocalBlindProductSession.h"

namespace hypha::local_blind_ui
{
// C1 host proof is complete for Windows VST3, macOS VST3, and macOS AU.
// Keep one shared fact for the POST large-frame product entry; PRE never exposes a second entry.
inline constexpr bool productEntryEnabled = true;

inline bool blocksDisclosure (const local_blind::ProductSessionView& state) noexcept
{
    using Phase = local_blind::ProductSessionPhase;
    return state.phase == Phase::capturing || state.phase == Phase::preparing
        || state.phase == Phase::ready || state.phase == Phase::armed
        || state.phase == Phase::listening || state.phase == Phase::revealed
        || state.phase == Phase::returnPending;
}

inline bool needsRecoveryScreen (const local_blind::ProductSessionView& state) noexcept
{
    return blocksDisclosure (state);
}
}
