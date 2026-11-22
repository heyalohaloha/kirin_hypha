#pragma once

#include "../../src/live_compare/LiveCompareSession.h"

#include <array>
#include <cmath>
#include <limits>
#include <memory>

namespace hypha::tests::match_span_audit
{
using namespace live_compare;
inline constexpr std::uint32_t rate = 48000;
inline constexpr std::uint64_t key = 0xa31b17;
inline constexpr int frames = 512;

struct Pair
{
    std::unique_ptr<Ring> ring = std::make_unique<Ring>();
    PreFeeder feeder;
    PostRenderer renderer;
    PostLevel level;
    std::array<float, frames> pre {}, post {};
    std::int64_t clock = 0;

    Pair()
    {
        ring->initialise (key, rate);
        ring->header.demand.store (1);
        renderer.prepare (4096, rate);
        renderer.wholeMatch.prepare (true, rate);
    }

    BlockClock record (float postPeak = 0)
    {
        for (int i = 0; i < frames; ++i)
        {
            const auto t = static_cast<double> ((clock + i) % 48);
            pre[static_cast<std::size_t> (i)] = static_cast<float> (0.08 * std::sin (t * 0.1308996938995747));
            post[static_cast<std::size_t> (i)] = pre[static_cast<std::size_t> (i)] * 0.5f;
        }
        if (postPeak > 0) post[100] = postPeak;
        BlockClock block;
        block.clock = block.project = clock;
        block.frames = frames;
        block.clockValid = block.projectValid = block.playing = true;
        const float* input[] = { pre.data() };
        feeder.feed (*ring, block, input, 1);
        clock += frames;
        return block;
    }

    RenderReport render (bool preSelected, float gain, float postPeak = 0)
    {
        const auto block = record (postPeak);
        float* io[] = { post.data() };
        return renderer.render (*ring, key, rate, block, io, 1, preSelected, gain, level, 1.0f, 0.9f);
    }

    void stage (LiveMatchSpan::CaptureToken token, bool discontinuity = false)
    {
        const auto block = record();
        float* io[] = { post.data() };
        auto& span = renderer.wholeMatch;
        span.finish (token, span.stage (token, *ring, ring->header.run.load(),
                     block.clock, io, 1, frames, discontinuity), true);
    }
};

template <class Require>
void verifySeqlock (Require require)
{
    Pair pair;
    const auto block = pair.record();
    auto& ring = *pair.ring;
    const auto run = ring.header.run.load();
    const auto seq = ring.header.seq.load();
    const auto end = ring.header.writeEnd.load();
    float* io[] = { pair.post.data() };
    LiveMatchSpanQueue queue (frames * 2);
    unsigned interrupted = 0;
    auto staged = queue.stageObserved (ring, run, block.clock, io, 1, frames, 1, false, [&] {
        ++interrupted;
        ring.header.seq.store (seq + 1);
        ring.samples[sampleSlot (block.clock + 100, 0)].store (0.98f);
        // Same run and still the same published end; the odd seqlock alone catches this overwrite.
    });
    require (interrupted == 1 && ring.header.writeEnd.load() == end && !staged.copied,
             "same-run PRE overwrite during copy is rejected even before writeEnd advances");
    queue.finish (staged, 1);
    require (queue.published() == 0 && queue.incomplete (1), "torn data never enters the published measurement");
    require (!queue.stage (ring, run, block.clock, io, 1, frames, 2, false).copied,
             "an already odd ring cannot be read");
    ring.header.seq.store (seq + 2);
    staged = queue.stage (ring, run, block.clock, io, 1, frames, 2, false);
    queue.finish (staged, 2);
    require (staged.copied && queue.published() == frames && !queue.incomplete (2),
             "stable replacement proves the negative fixture did not reject all input");
}

template <class Require>
void verifyBlindAndFrozenSnapshot (Require require)
{
    Pair pair;
    auto& span = pair.renderer.wholeMatch;
    const auto listen = span.captureToken(); // real callback-entry snapshot, before BLIND request
    span.restart (7);
    const auto pending = span.captureToken();
    pair.stage (span.admitted (listen, true, 0, 0)); // late old callback completion
    pair.stage (span.admitted (pending, true, 7, 6)); // new request, still old proof
    require (pending.epoch != listen.epoch && pending.receipt == 7 && span.snapshot() == 0,
             "late old callback and a pending BLIND proof publish nothing");
    span.service (1, &kirin_hypha_live_match_span_cut);
    require (span.frames() == 0, "new BLIND meter excludes late LISTEN and unacknowledged BLIND PCM");
    auto capture = span.admitted (pending, true, 7, 7);
    for (int i = 0; i < 288; ++i)
    {
        pair.stage (capture);
        span.service (1, &kirin_hypha_live_match_span_cut);
    }
    require (span.enough() && span.frames() == 288 * frames, "only fresh acknowledged BLIND proof fills its own three seconds");
    const auto target = span.snapshot();
    require (span.serviceThrough (1, target, 65536, &kirin_hypha_live_match_span_cut),
             "consumer caught up to the frozen MATCH target");
    pair.stage (capture); // producer publishes immediately after drain, before measurement
    KirinLiveMatchSpanFacts facts {};
    require (span.snapshot() > target && span.measure (facts) && span.frames() == 288 * frames,
             "post-drain producer cannot invalidate or widen an earlier MATCH snapshot");
    span.service (1, &kirin_hypha_live_match_span_cut);
    require (span.frames() == 289 * frames, "late frame remains eligible for the next MATCH");
}

template <class Require>
void verifySubstitutePost (Require require)
{
    Pair pair;
    auto& span = pair.renderer.wholeMatch;
    for (int i = 0; i < 330; ++i)
    {
        pair.render (false, 1.0f); // user explicitly selected ordinary POST
        span.service (1, &kirin_hypha_live_match_span_cut);
    }
    KirinLiveMatchSpanFacts before {}, after {};
    require (span.enough() && span.measure (before), "affirmatively selected, proven POST counts");
    const auto old = span.snapshot();
    const auto guarded = pair.render (true, 16.0f, 0.98f); // only substitute POST carries the high peak
    require (guarded.guardTripped && guarded.reason == RecoveryReason::ceiling && pair.post[100] >= 0.98f,
             "test really substitutes POST carrying the seeded high peak for requested PRE");
    require (span.snapshot() == old && !span.lost(), "substitute is omitted rather than invalidating a proven span");
    for (int i = 0; i < 20; ++i)
    {
        pair.render (false, 1.0f);
        span.service (1, &kirin_hypha_live_match_span_cut);
    }
    require (span.snapshot() > old && span.measure (after) && after.post_peak < -15.0
        && std::abs (before.post_peak - after.post_peak) < 0.1,
        "recovered POST counts without inheriting the excluded peak at the break");
}

template <class Require>
void verifyQueueReplacement (Require require)
{
    Pair pair;
    auto& span = pair.renderer.wholeMatch;
    const auto old = span.captureToken(); // callback opened before its ring lease
    span.prepare (false, rate); // ALL PLAYED → LAST 4 replaces the backing queue
    require (span.admitted (old, true, 0, 0).epoch == 0, "old callback cannot touch missing queue");
    pair.stage (old); // defense in stage/finish even if invoked with a stale token
    require (span.snapshot() == 0 && !span.enabled(), "LAST 4 never receives ALL PLAYED PCM");
    span.prepare (true, rate);
    require (span.admitted (old, true, 0, 0).epoch == 0, "old callback cannot touch replacement queue");
    pair.stage (span.admitted (span.captureToken(), true, 0, 0));
    require (span.snapshot() == frames, "only callback from replacement epoch publishes");
}

template <class Require>
void verify (Require require)
{
    verifyQueueReplacement (require);
    verifySeqlock (require);
    verifyBlindAndFrozenSnapshot (require);
    verifySubstitutePost (require);
}
}
