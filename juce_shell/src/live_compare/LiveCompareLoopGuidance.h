#pragma once
#include "LiveCompareBlockClock.h"
#include "LiveCompareClock.h"
#include "LiveCompareTimingWitness.h"

namespace hypha::live_compare
{
enum class LoopPairingBlock : std::uint8_t { none, aaxLoop, auCycle };

// AU guidance uses the observer's established K, including metadata writer overlap and temporary
// post-wrap evidence gaps. Only the renderer authorises each PCM block; analysis still requires
// current callback evidence. AAX retains its separate certificate boundary. The analysis format
// filter is not used here, so a proven mixed-format audition stays valid.
inline LoopPairingBlock pairingLoopBlock (const BlockClock& block, bool audioUnit, bool aax,
                                          const TimingEvidence& timing, bool eligible) noexcept
{
    if (! block.playing || ! block.loop.active) return LoopPairingBlock::none;
    if (audioUnit && eligible && ! timing.hasEstablishedK()) return LoopPairingBlock::auCycle;
    if (aax && block.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::none))
        return LoopPairingBlock::aaxLoop;
    return LoopPairingBlock::none;
}
}
