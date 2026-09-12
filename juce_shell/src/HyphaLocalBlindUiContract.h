#pragma once

#include "HyphaPluginFormat.h"
#include "local_blind/LocalBlindProductSession.h"

namespace hypha::local_blind_ui
{
// Keep one shared wrapper-aware fact for the POST large-frame product entry; PRE never exposes a
// second entry. AAX remains unavailable until its exact-range project clock and PDC proof is green.
inline constexpr bool productEntryEnabled (juce::AudioProcessor::WrapperType wrapper) noexcept
{
    return plugin_format::supportsLocalBlindProduct (wrapper);
}

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
