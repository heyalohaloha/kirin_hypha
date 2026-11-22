#pragma once

#include "../../src/live_compare/LiveCompareMatch.h"

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

    RenderReport render (bool preSelected, float gain, float postPeak = 0, AlignPlayback align = {},
                         LiveMatchSpan::CaptureToken token = {UINT64_MAX, 0})
    {
        const auto block = record (postPeak);
        float* io[] = { post.data() };
        return renderer.render (*ring, key, rate, block, io, 1, preSelected, gain, level, 1.0f, 0.9f, false,
            RecoveryReason::none, nullptr, align, 0, false, false, token);
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
    require (queue.published() == 0 && !queue.incomplete (1), "torn proof is an omitted block, not overflow");
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
void verifySubstitutePost (Require require, std::uint64_t receipt = 0)
{
    Pair pair;
    auto& span = pair.renderer.wholeMatch;
    if (receipt != 0) span.restart(receipt);
    const auto token = span.admitted(span.captureToken(), true, receipt, receipt);
    for (int i = 0; i < 330; ++i)
    {
        pair.render (false, 1.0f, 0.0f, {}, token); // user explicitly selected ordinary POST
        span.service (1, &kirin_hypha_live_match_span_cut);
    }
    KirinLiveMatchSpanFacts before {}, after {};
    require (span.enough() && span.measure (before), "affirmatively selected, proven POST counts");
    const auto old = span.snapshot();
    const auto guarded = pair.render (true, 16.0f, 0.98f, {}, token); // only substitute POST carries the high peak
    require (guarded.guardTripped && guarded.reason == RecoveryReason::ceiling && pair.post[100] >= 0.98f,
             "test really substitutes POST carrying the seeded high peak for requested PRE");
    require (span.snapshot() == old && !span.lost(), "substitute is omitted rather than invalidating a proven span");
    const auto recovering = pair.render (false, 1.0f, 0.0f, {}, token);
    require (recovering.guardTripped && span.snapshot() == old + frames,
             "explicit POST counts before guard recovery completes");
    span.service (1, &kirin_hypha_live_match_span_cut);
    for (int i = 0; i < 19; ++i)
    {
        pair.render (false, 1.0f, 0.0f, {}, token);
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

template<class Require>
void verifyPreClockFence(Require require)
{
    Pair pair;
    for (int i=0; i<32; ++i) pair.render(false, 1.0f);
    const auto view = pair.renderer.historyView();
    require(wholeProofCurrent(*pair.ring, view), "positive PRE generation proof");
    BlockClock noClock; noClock.frames = frames; noClock.playing = true;
    const float* input[] = {pair.pre.data()};
    pair.feeder.feed(*pair.ring, noClock, input, 1);
    require(pair.ring->header.run.load() == view.preRun && pair.renderer.historyView().kValid
        && pair.ring->header.timing.generation.load() != view.preClock,
        "real PRE clock loss changes generation before PCM or POST history");
    require(!wholeProofCurrent(*pair.ring, view), "clock-only PRE change withdraws ALL PLAYED proof");
}

template<class Require>
void verifyRunChangeAndCapacity(Require require)
{
    Pair pair;
    const auto block = pair.record();
    auto& ring = *pair.ring;
    const auto run = ring.header.run.load();
    float* io[] = {pair.post.data()};
    LiveMatchSpanQueue queue(frames);
    const auto torn = queue.stageObserved(ring,run,block.clock,io,1,frames,3,false,[&] {ring.header.run.store(run+1);});
    queue.finish(torn,3);
    require(!torn.copied && !torn.overflow && !queue.incomplete(3), "PCM run replacement is a temporary proof break");
    ring.header.run.store(run);
    const auto good = queue.stage(ring,run,block.clock,io,1,frames,3,true);
    queue.finish(good,3);
    const auto fullButWrongRun = queue.stage(ring,run+1,block.clock,io,1,frames,4,false);
    queue.finish(fullButWrongRun,4);
    require(!fullButWrongRun.overflow && !queue.incomplete(4), "proof failure, even with a full queue, isn't an overflow");
    const auto fullAndProven = queue.stage(ring,run,block.clock,io,1,frames,5,false);
    queue.finish(fullAndProven,5);
    require(fullAndProven.overflow && queue.incomplete(5), "only verified PCM failure marks a permanent overflow");
    bool first = true, marked = false;
    queue.drainThrough(queue.published(),frames,[&](const LiveMatchSpanQueue::Frame& frame) {
        if (first) {marked = frame.gapBefore; first = false;}
    });
    require(marked, "boundary marker is carried by the first admitted frame");
}

template<class Require>
void verifyAlignWaitingForSelectedPost(Require require)
{
    Pair pair;
    for (int i=0; i<32; ++i) pair.render(false, 1.0f);
    auto& span = pair.renderer.wholeMatch;
    const auto token = span.captureToken();
    const auto initial = span.snapshot();
    const AlignPlayback checking {{}, true, false, true};
    const auto postWait = pair.render(false, 1.0f, 0.98f, checking, token);
    require(postWait.verdict == Verdict::accepted && postWait.alignWaiting && span.snapshot() == initial + frames,
        "named POST remains eligible during verify with valid raw clock proof");
    const auto preWait = pair.render(true, 1.0f, 0.98f, checking, token);
    require(preWait.verdict == Verdict::accepted && preWait.alignWaiting && span.snapshot() == initial + frames,
        "the same verify-wait must not count fallback for selected PRE");
}

template <class Require>
void verify (Require require)
{
    verifyQueueReplacement (require);
    verifyPreClockFence (require);
    verifyRunChangeAndCapacity (require);
    verifyAlignWaitingForSelectedPost (require);
    verifySeqlock (require);
    verifyBlindAndFrozenSnapshot (require);
    verifySubstitutePost (require);
    verifySubstitutePost (require, 7);
}
}
