#pragma once
#include "LiveCompareAlignStormTest.h"

// Diagnostic until all fixed and existing cases pass; no thresholds or other host assumptions change.
inline void alignTransportStormWithoutHostStability()
{
    long blocks = 0;
    for (std::uint32_t seed = 1; seed <= 300; ++seed)
    {
        live_compare_storm_test::World world (seed);
        world.allowClockChangesBeforeAnswer = true;
        live_compare_storm_test::storm (world, 6000); blocks += world.blocks;
    }
    std::printf ("unrestricted answering diagnostic: seeds 1..300, %ld blocks passed\n", blocks);
}
