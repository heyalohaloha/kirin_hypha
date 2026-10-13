#pragma once
#include "../../src/live_compare/LiveCompareWaitNotice.h"

// The actual renderer, with alternating safe/unsafe PRE, for longer than the notice delay.
// A sparse safe callback must neither restart PRE nor erase the continuing fault episode.
template <class PairType> void guardRecoveryEpisode()
{
    using namespace hypha::live_compare;
    for (int channels : { 1, 2 }) for (bool anonymous : { false, true })
    for (bool invalid : { false, true })
    {
        PairType pair;
        pair.calibrate (true, 1.0f);
        WaitNotice notice;
        std::uint64_t now = 1'000'000'000;
        notice.begin (now);
        for (int i = 0; i < 320; ++i)
        {
            const bool fault = i % 2 == 0;
            const float samples[] { fault && !invalid ? 0.8f : 0.1f, -0.5f };
            const auto report = pair.step (true, true, 2.0f, false, channels,
                                          fault && invalid, anonymous, false, samples);
            require (!report.preAudible && pair.postUntouched(), "guard episode never chatters into PRE on isolated safe blocks");
            require (report.reason == (invalid ? RecoveryReason::nonFinite : RecoveryReason::ceiling),
                     "guard episode retains the actual level fault");
            notice.observe (report.guardTripped, now);
            if (i < 281) require (!notice.prolonged (now), "guard guidance never precedes three seconds");
            now += 10'666'667;
        }
        require (notice.prolonged (now), "alternating guards receive the three-second guidance");
        bool returned = false;
        const float safe[] { 0.1f, -0.5f };
        for (int i = 0; i < 32; ++i)
        {
            const auto report = pair.step (true, true, 2.0f, false, channels, false, anonymous, false, safe);
            notice.observe (report.guardTripped, now); now += 10'666'667;
            if (report.stableSource && report.preAudible)
            {
                returned = true;
                for (int c = 0; c < channels; ++c) for (auto sample : pair.post[c])
                    require (sample == 0.2f, "recovered PRE uses the original approved gain and current safe PCM");
            }
        }
        require (returned && !notice.prolonged (now), "sustained safe PCM resumes automatically and clears guidance");
    }
}
