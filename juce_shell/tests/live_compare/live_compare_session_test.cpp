#include "../../src/live_compare/LiveCompareSession.h"
#include "../../src/live_compare/LiveCompareSharedRing.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

using namespace hypha::live_compare;

static void require (bool ok, const char* message)
{
    if (! ok) { std::cerr << "FAIL: " << message << '\n'; std::abort(); }
}

namespace
{
constexpr int frames = 512;
constexpr std::uint64_t key = 0xC0FFEEull;

float preValue (std::int64_t clock, int channel) { return 0.25f + 0.0001f * static_cast<float> (clock % 1000) + 0.1f * static_cast<float> (channel); }
constexpr float postValue = -0.5f;

// An adjacent pair (K = 0): PRE publishes a block; POST renders the same clock range.
struct Pair
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    PreFeeder feeder;
    PostRenderer renderer;
    std::vector<float> pre[2], post[2];
    std::int64_t clock = 0;

    Pair()
    {
        ring->initialise (key, 48000);
        renderer.prepare (4096, 48000.0);
        for (auto* set : { pre, post })
            for (int c = 0; c < 2; ++c) set[c].assign (frames, 0.0f);
    }

    RenderReport step (bool demand, bool preSelected, float gain, bool afterGap = false, int channels = 2)
    {
        ring->header.demand.store (demand ? 1u : 0u);
        BlockClock b;
        b.clock = b.project = clock;
        b.frames = frames;
        b.clockValid = b.projectValid = b.playing = true;
        b.afterGap = afterGap;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < frames; ++i)
            {
                pre[c][size_t (i)] = preValue (clock + i, c);
                post[c][size_t (i)] = postValue;
            }
        const float* in[] = { pre[0].data(), pre[1].data() };
        feeder.feed (*ring, b, in, 2);
        float* io[] = { post[0].data(), post[1].data() };
        const auto report = renderer.render (*ring, key, 48000, b, io, channels, preSelected, gain);
        clock += frames;
        return report;
    }

    // Steps until the first proven block (the calibration needs its streak of equal candidates).
    RenderReport calibrate (bool preSelected, float gain)
    {
        for (int i = 0; i < 16; ++i)
        {
            const auto r = step (true, preSelected, gain);
            if (r.verdict == Verdict::accepted)
                return r;
            require (r.verdict == Verdict::calibrating && r.preWaiting == preSelected && ! r.preAudible,
                     "calibration keeps POST and reports PRE waiting");
            require (postUntouched(), "an unproven block leaves POST bit-identical");
        }
        require (false, "the pair never calibrated");
        return {};
    }

    bool postUntouched() const
    {
        for (int c = 0; c < 2; ++c)
            for (float v : post[c]) if (v != postValue) return false;
        return true;
    }
};
}

// INV-LC4: PRE sounds only in proven blocks, returns over the symmetric 5 ms fade, and is replaced by
// POST at the block start when the proof is lost, without using unproven PRE samples.
static void preSoundsOnlyWhenProven()
{
    Pair pair;
    const auto first = pair.calibrate (true, 1.0f);
    require (first.verdict == Verdict::accepted && first.preAudible && ! first.preWaiting, "the first proven block returns to PRE");
    const float fadeStart = pair.post[0][0];
    const float expectedStart = postValue * (1.0f - 1.0f / 240.0f) + preValue (pair.clock - frames, 0) * (1.0f / 240.0f);
    require (std::fabs (fadeStart - expectedStart) < 1.0e-6f, "the return starts the 5 ms fade from POST");
    require (pair.post[0][frames - 1] == preValue (pair.clock - 1, 0), "after 5 ms the output is exactly PRE");
    const auto full = pair.step (true, true, 1.0f);
    require (full.preAudible && pair.post[1][0] == preValue (pair.clock - frames, 1), "a proven block plays PRE sample for sample");

    const auto lost = pair.step (true, true, 1.0f, true);
    require (lost.verdict == Verdict::calibrating && lost.preWaiting && ! lost.preAudible, "a gap invalidates K and switches to POST");
    require (pair.postUntouched(), "losing the proof switches at the block start without unproven PRE");
}

// The user's PRE to POST switch fades out over proven PRE samples; POST stays untouched afterwards.
static void userSwitchFadesBothWays()
{
    Pair pair;
    pair.calibrate (true, 1.0f);
    pair.step (true, true, 1.0f);
    const auto out = pair.step (true, false, 1.0f);
    require (out.verdict == Verdict::accepted && ! out.preAudible, "selecting POST fades PRE out within the block");
    require (pair.post[0][frames - 1] == postValue, "the fade ends exactly at POST");
    pair.step (true, false, 1.0f);
    require (pair.postUntouched(), "POST selected leaves POST bit-identical");
}

// The approved gain applies to the PRE copy only.
static void approvedGainAppliesToPre()
{
    Pair pair;
    pair.calibrate (true, 0.5f);
    pair.step (true, true, 0.5f);
    require (pair.post[0][7] == preValue (pair.clock - frames + 7, 0) * 0.5f, "PRE is played at the approved gain");
}

// More than two channels never render PRE; without demand PRE publishes nothing.
static void unsupportedLayoutsAndNoDemandKeepPost()
{
    Pair pair;
    pair.calibrate (true, 1.0f);
    pair.step (true, true, 1.0f, false, 3);
    require (pair.postUntouched(), "three channels keep POST");
    Pair idle;
    for (int i = 0; i < 12; ++i) idle.step (false, true, 1.0f);
    require (idle.postUntouched(), "no demand, no PRE input, POST untouched");
    require (idle.ring->header.published.load() == 0, "PRE publishes only while a session demands it");
}

// PRE creates the ring for its identity; POST opens it for the same key and rate only.
static void sharedRingPairsOnlyTheSameIdentityAndRate()
{
    const auto name = sharedRingName (pairKeyForPreInstance ("pre-instance-id"));
    require (name.size() <= 31 && name.rfind ("/kh-lc-", 0) == 0, "the name fits the POSIX limit");
    require (pairKeyForPreInstance ("a") != pairKeyForPreInstance ("b"), "different PRE identities give different keys");
    const auto pairKey = pairKeyForPreInstance ("live-compare-session-test");
#if defined (_WIN32)
    SharedRingMapping unavailable;
    require (! sharedRingAvailable() && ! unavailable.create (pairKey, 48000),
             "Windows has no live compare transport until its stage");
#else
    require (sharedRingAvailable(), "macOS maps the ring");
    SharedRingMapping pre, post, wrongRate;
    require (pre.create (pairKey, 48000), "PRE creates its ring");
    require (post.open (pairKey, 48000), "POST opens the ring of its PRE");
    require (! wrongRate.open (pairKey, 44100), "a ring for another rate is refused");
    post.ring()->header.demand.store (1);
    require (pre.ring()->header.demand.load() == 1, "both roles see the same memory");
    post.close();
    require (pre.ring()->header.demand.load() == 0, "closing POST clears its demand");
    require (post.open (pairKey, 48000) && post.ring()->header.ownerClosed.load() == 0,
             "an open PRE ring is live");
    pre.close();
    require (post.ring()->header.ownerClosed.load() == 1, "POST learns that PRE closed its ring");
    SharedRingMapping late;
    require (! late.open (pairKey, 48000), "the owner's close removes the name");
    post.close();
#endif
}

int main()
{
    preSoundsOnlyWhenProven();
    userSwitchFadesBothWays();
    approvedGainAppliesToPre();
    unsupportedLayoutsAndNoDemandKeepPost();
    sharedRingPairsOnlyTheSameIdentityAndRate();
    std::printf ("live compare session: all checks passed\n");
    return 0;
}
