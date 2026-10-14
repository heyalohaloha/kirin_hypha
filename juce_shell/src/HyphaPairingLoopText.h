#pragma once
#include "live_compare/LiveCompareLoopGuidance.h"
#include "kirin_hypha_ffi.h"

namespace hypha::pairing_loop_text
{
inline const char* status (live_compare::LoopPairingBlock block) noexcept
{
    switch (block)
    {
        case live_compare::LoopPairingBlock::auCycle: return "Turn Cycle off to compare";
        case live_compare::LoopPairingBlock::aaxLoop: return "Loop Playback on: turn it off to compare";
        case live_compare::LoopPairingBlock::none: return "";
    }
    return "";
}
inline const char* analysisStatus (std::uint8_t state, live_compare::LoopPairingBlock block) noexcept
{
    if (state == KIRIN_SPECTRUM_WARMING_UP
        || (block == live_compare::LoopPairingBlock::auCycle
            && (state == KIRIN_SPECTRUM_ACTIVE || state == KIRIN_SPECTRUM_HIDDEN)))
        return status (block);
    return ""; // Preserve missing pair, unavailable analysis and occupied-slot reasons.
}
}
