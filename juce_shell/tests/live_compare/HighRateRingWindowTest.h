#pragma once

#include "../../src/live_compare/LiveCompareMatch.h"
#include "../../src/live_compare/LiveComparePin.h"
#include "../../src/live_compare/ReaperTiming.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace hypha::tests
{
// Keep POST's proven four-second window while PRE advances by the verified delay/scheduling
// envelope and another maximum block of copy progress. This uses audio frames, never sleeps.
template <typename Pair, typename Require>
void verifyHighRateRingWindows (Require require)
{
    using namespace live_compare;
    for (const auto rate : { 48000u, 96000u, 176400u, 192000u })
        for (const int blockFrames : { 512, 768 })
        {
            if (blockFrames == 768 && rate != 192000u) continue;
            Pair pair (rate, blockFrames);
            const auto key = pair.ring->header.pairKey.load();
            for (const auto version : { ringVersion - 1, ringVersion + 1 })
            {
                pair.ring->header.version.store (version);
                require (! pair.ring->matches (key, rate), "foreign ring headers fail ABI admission");
            }
            pair.ring->header.version.store (ringVersion);
            require (pair.ring->matches (key, rate), "the current ring ABI is admitted");
            pair.run (5.0, 1.0f, 0.5f);
            const auto postEnd = pair.clock;
            std::vector<float> pcm (static_cast<std::size_t> (blockFrames), 0.025f);
            const float* input[] { pcm.data(), pcm.data() };
            const auto advance = [&] (std::int64_t frames)
            {
                const auto end = pair.clock + frames;
                while (pair.clock < end)
                {
                    BlockClock block;
                    block.clock = block.project = pair.clock;
                    block.frames = blockFrames;
                    block.clockValid = block.projectValid = block.playing = true;
                    pair.feeder.feed (*pair.ring, block, input, 2);
                    pair.clock += blockFrames;
                }
            };
            advance (ReaperTimingLimits::age + ReaperTimingLimits::scheduling);
            const auto match = computeMatch (*pair.ring, pair.renderer, rate);
            require (match.ok() && match.seconds == 4.0,
                     "four-second MATCH survives PRE lead and copy progress at every rate");
            const auto pin = pinLatest (*pair.ring, pair.renderer, 4 * rate, 2);
            require (pin.ok() && pin.frames == 4 * rate && pin.pre.size() == 8 * rate,
                     "PIN keeps the complete four-second stereo window at every rate");
            require (std::abs (pin.post.front() - pin.pre.front() * 0.5f) < 1.0e-7f
                     && std::abs (pin.post.back() - pin.pre.back() * 0.5f) < 1.0e-7f,
                     "PIN endpoints retain the corresponding PRE and POST signal");
            const auto follow = followStep (match, 0.0, match.measuredDb, match.ceilingDbtp, 0.0);
            require (follow.action == FollowAction::move
                     && std::abs (follow.preGainDb - match.measuredDb) < 1.0e-9,
                     "AUTO consumes the same valid high-rate MATCH");
            std::printf ("four-second window rate=%u block=%d PRE advance=%lld: MATCH/PIN/AUTO ok\n",
                         rate, blockFrames, static_cast<long long> (pair.clock - postEnd));
            advance (ringCapacityFrames);
            require (computeMatch (*pair.ring, pair.renderer, rate).failure == MatchFailure::overwritten
                     && pinLatest (*pair.ring, pair.renderer, 4 * rate, 2).failure == PinFailure::overwritten,
                     "MATCH and PIN still refuse an overwritten window");
        }
}
}
