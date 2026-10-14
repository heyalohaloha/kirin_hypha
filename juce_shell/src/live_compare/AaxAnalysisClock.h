#pragma once
#include "LiveCompareTimingWitness.h"

namespace hypha::live_compare
{
// AddClock is an engine coordinate, never an invented output-presentation latency.
// A local observation needs continuous engine/project movement from the host's engine clock.
inline bool continuousAaxClock (const BlockClock& block) noexcept
{
    return block.clockBasis == static_cast<std::uint8_t> (ClockBasis::aaxEngine)
        && block.playing && block.clockValid && block.projectValid && block.frames > 0 && ! block.afterGap;
}

// The measured host build additionally carries the certificate and its documented delay bound.
inline bool certifiedAaxClock (const BlockClock& block) noexcept
{
    return continuousAaxClock (block)
        && block.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::boundedAaxEngine)
        && block.maximumDelaySamples != 0;
}

// A difference additionally needs the exact PRE lifetime/generation and the SAME K proof used by
// LISTEN's timing preparation. A certified pair accepts any proven K within the documented bound,
// including an initial LOOP. Any other AAX host accepts only the ordinary linear project-time
// proof outside a loop: the same K that already decides which PRE samples LISTEN plays.
inline bool qualifiedAaxPair (const BlockClock& pre, const BlockClock& post,
                              const TimingEvidence& evidence) noexcept
{
    if (! evidence.valid || evidence.postClock != post.clock || evidence.preGeneration == 0
        || (evidence.ownerA == 0 && evidence.ownerB == 0) || evidence.k < 0)
        return false;
    if (certifiedAaxClock (pre) && certifiedAaxClock (post))
    {
        const auto bound = std::min (pre.maximumDelaySamples, post.maximumDelaySamples);
        return evidence.k <= static_cast<std::int64_t> (bound);
    }
    return continuousAaxClock (pre) && continuousAaxClock (post)
        && evidence.kind == LoopEntryKind::linear && ! post.loop.active && ! pre.loop.active;
}

}
