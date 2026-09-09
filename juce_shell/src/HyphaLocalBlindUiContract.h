#pragma once

#include "local_blind/LocalBlindProductSession.h"

namespace hypha::local_blind_ui
{
// C1 product entry remains closed until the same exact-range PDC proof passes in macOS AU.
// The complete UI is compiled and tested now; changing this one fact opens the large-frame entry.
inline constexpr bool productEntryEnabled = false;

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
