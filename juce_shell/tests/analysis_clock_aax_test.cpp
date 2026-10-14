#include "../src/live_compare/PairedAnalysisClock.h"
#undef NDEBUG
#include <cassert>
#include <iostream>
using namespace hypha::live_compare;

namespace
{
// Twenty linear callbacks of one PRE/POST pair; returns the last evidence.
TimingEvidence proveLinear (BlockClock& pre, BlockClock& post, TimingHeader& header,
                            TimingPublisher& publisher, TimingObserver& observer, PairedAnalysisClock& analysis)
{
    TimingEvidence evidence;
    for (int i = 0; i < 20; ++i)
    {
        publisher.observe (header, pre, 48000);
        TimingSnapshot snapshot; assert (readTiming (header, snapshot));
        evidence = observer.observe (snapshot, post, 48000, 480000);
        analysis.observeLocal (post, 48000, true);
        analysis.observePair (post, evidence, qualifiedAaxPair (snapshot.block, post, evidence));
        pre.clock += 256; post.clock += 256; pre.project += 256; post.project += 256;
    }
    return evidence;
}
}

int main()
{
    BlockClock pre;
    pre.clock = 960000; pre.project = 0; pre.frames = 256;
    pre.clockValid = pre.projectValid = pre.playing = true;
    pre.clockBasis = static_cast<std::uint8_t> (ClockBasis::aaxEngine);
    pre.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::boundedAaxEngine);
    pre.maximumDelaySamples = 16383;
    auto post = pre; post.project = -1024;
    TimingHeader header; header.ownerA.store (7); header.ownerB.store (11);
    TimingPublisher publisher; TimingObserver observer;
    PairedAnalysisClock analysis;
    auto evidence = proveLinear (pre, post, header, publisher, observer, analysis);
    assert (evidence.valid && evidence.k == 1024 && evidence.kind == LoopEntryKind::linear);
    assert (analysis.local.valid && analysis.paired.valid);
    assert (analysis.paired.start == analysis.local.start - 1024);
    const auto originalRevision = analysis.paired.revision;
    auto changed = evidence; changed.ownerB += 1;
    analysis.observePair (post, changed, false);
    assert (! analysis.paired.valid);
    changed.postClock = post.clock;
    analysis.observePair (post, changed, true);
    assert (analysis.paired.valid && analysis.paired.revision != originalRevision);
    // Certified pair: any proven K within the documented bound, never a negative one.
    for (auto k : { std::int64_t (-1), std::int64_t (16384) })
    {
        changed.k = k;
        assert (! qualifiedAaxPair (pre, post, changed));
        analysis.observePair (post, changed, qualifiedAaxPair (pre, post, changed));
        assert (! analysis.paired.valid);
    }
    changed.k = 16383;
    assert (qualifiedAaxPair (pre, post, changed));
    changed.k = 0;
    assert (qualifiedAaxPair (pre, post, changed));
    changed.kind = LoopEntryKind::boundedEngine;
    assert (qualifiedAaxPair (pre, post, changed)); // a certified initial LOOP K
    post.clockValid = false;
    analysis.observeLocal (post, 48000, true);
    assert (! analysis.local.valid);
    assert (! qualifiedAaxPair (pre, post, changed));
    post.clockValid = true;
    analysis.observeLocal (post, 48000, true);
    assert (! analysis.local.valid); // first callback after missing clock fences the old run
    post.clock += 256; post.project += 256;
    analysis.observeLocal (post, 48000, true);
    assert (analysis.local.valid);
    const auto continuousRevision = analysis.local.revision;
    post.clock += 256; post.project += 4096;
    analysis.observeLocal (post, 48000, true);
    assert (! analysis.local.valid);
    post.clock += 256; post.project += 256;
    analysis.observeLocal (post, 48000, true);
    assert (analysis.local.valid && analysis.local.revision != continuousRevision);

    // Any other AAX build: the engine clock still gives PRE its local coordinate, and the pair
    // accepts only LISTEN's linear proof outside a loop; the certified bound no longer applies.
    auto plainPre = pre, plainPost = post;
    plainPre.clockAuthority = plainPost.clockAuthority = static_cast<std::uint8_t> (ClockAuthority::none);
    plainPre.maximumDelaySamples = plainPost.maximumDelaySamples = 0;
    analysis.observeLocal (plainPost, 48000, true);
    assert (! analysis.local.valid); // the changed proof fences the run once
    plainPost.clock += 256; plainPost.project += 256;
    analysis.observeLocal (plainPost, 48000, true);
    assert (analysis.local.valid);
    auto linear = evidence; linear.postClock = plainPost.clock; linear.kind = LoopEntryKind::linear;
    assert (qualifiedAaxPair (plainPre, plainPost, linear));
    linear.k = 40000;
    assert (qualifiedAaxPair (plainPre, plainPost, linear)); // no documented bound to apply
    linear.k = -1;
    assert (! qualifiedAaxPair (plainPre, plainPost, linear));
    linear.k = 1024;
    for (auto kind : { LoopEntryKind::boundedEngine, LoopEntryKind::presentationLatency,
                       LoopEntryKind::contentClock, LoopEntryKind::none })
    {
        auto loopEntry = linear; loopEntry.kind = kind;
        assert (! qualifiedAaxPair (plainPre, plainPost, loopEntry));
    }
    auto looping = plainPost; looping.loop.active = true;
    assert (! qualifiedAaxPair (plainPre, looping, linear));
    auto loopingPre = plainPre; loopingPre.loop.active = true;
    assert (! qualifiedAaxPair (loopingPre, plainPost, linear));
    auto invalid = linear; invalid.valid = false;
    assert (! qualifiedAaxPair (plainPre, plainPost, invalid));
    // A certified PRE snapshot with an uncertified POST (or the reverse) is an ordinary pair.
    assert (qualifiedAaxPair (pre, plainPost, linear));
    analysis.observePair (plainPost, linear, qualifiedAaxPair (plainPre, plainPost, linear));
    assert (analysis.paired.valid && analysis.paired.start == plainPost.clock - 1024);
    // Another clock basis is never an engine coordinate, certified or not.
    auto vst = plainPost; vst.clockBasis = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
    assert (! continuousAaxClock (vst) && ! qualifiedAaxPair (plainPre, vst, linear));

    // The host's loop-playback setting without a range names no loop.
    LoopContext noRange; noRange.valid = true; noRange.bpm = 120.0; noRange.start = noRange.end = 8.0;
    assert (! noRange.hasRange());
    LoopContext range = noRange; range.end = 16.0;
    assert (range.hasRange());
    LoopContext missing = range; missing.valid = false;
    assert (! missing.hasRange());
    std::cout << "AAX analysis clock: PASS\n";
}
