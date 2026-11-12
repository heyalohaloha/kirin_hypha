#pragma once
#include "AlignSessionProductTest.h"
#include "ChainMusic.h"

namespace aax_analysis_product
{
// Pro Tools-shaped host: PRE -> a seven-band EQ -> a brickwall limiter -> POST, the limiter's
// lookahead compensated, so K is the content position. Bass-heavy music after such a chain gives
// windows that are narrow and no scaled copy; their strong peaks still confirm the position. LISTEN
// and BLIND confirm PRE at its clock position before the one-second bound runs out, raise no doubt
// and no notice, and nothing starts the wait again while the clock mapping stays. A changed mapping
// is confirmed the same way. Every frame is audited: blocks the clock cannot prove are POST.
inline void chainConfirmsPosition (int channels)
{
    using namespace hypha::live_compare;
    using hypha::tests::chain_music::Chain;
    struct Case { Chain chain; bool blind; };
    // 6 and 12 dB bands; 7 to 11 dB of average gain reduction, with 5 ms of lookahead and without.
    // BLIND's chains end in an output trim, so that MATCH lowers PRE: the frame audit models POST
    // at unity, and a raised PRE over the ceiling would lower POST instead (checked elsewhere).
    const Case cases[] = { { { 6.0, 12.0, 240 }, false }, { { 12.0, 12.0, 240, -12.0 }, true },
                           { { -12.0, 14.0, 0 }, false }, { { -12.0, 14.0, 0, -12.0 }, true } };
    for (const auto& c : cases)
    {
        const auto tracks = hypha::tests::chain_music::render (1068544 - 70000, 760000, c.chain);
        require (tracks.averageReductionDb >= 6.0, "the limiter really reduces gain");
        ChunkedPost chain (channels);
        chain.music = &tracks;
        chain.requireContentPosition = true;
        chain.setDelay (512);
        auto& post = *chain.post;
        selectPreparedPair (chain, "select the mastering-chain PRE");
        require ((c.blind ? post.beginLiveBlind() : post.startLiveCompare()) == StartResult::started, "the chain comparison starts");
        int chunks = 0;
        const auto confirmed = [&] {
            ++chunks;
            if (c.blind) post.serviceLiveBlind();
            const auto step = post.liveContentTimingObservation();
            return step.settled && step.lagFrames == 0 && ! post.liveCompareAlignPending(); };
        if (c.blind)
        {
            chain.until (400, [&] { const bool done = confirmed(); return done && post.liveBlindStatus().stage == BlindStage::active; },
                         "BLIND prepares and confirms the position through the chain");
            LiveTimingFixtureAccess::assignBlind (post, true);
            require (post.selectLiveBlind (1), "choose PRE anonymously");
            chain.until (4, [&] { return post.liveCompareStatus().preAudible; }, "PRE sounds at once in BLIND");
        }
        else
        {
            post.selectLiveComparePre (true);
            chain.until (40, confirmed, "the content confirms PRE's position through the chain");
            chain.until (4, [&] { return post.liveCompareStatus().preAudible; }, "PRE sounds once its position is confirmed");
        }
        // About 0.85 s of history makes the first window; the bound would end the wait at 1 s of proven playback.
        require (c.blind || chunks <= 40, "confirmed within the first second, not by the bound");
        require (post.liveCompareAlignFrames() == 0, "the confirmed position is the clock position: nothing to move");
        const auto revision = LiveTimingFixtureAccess::alignRevision (post);
        for (int i = 0; i < 160; ++i)
        {
            if (c.blind && i % 40 == 39) require (post.selectLiveBlind (post.liveBlindStatus().trial.selected == 1 ? 2 : 1), "switch the sources");
            chain.chunk();
            const auto status = post.liveCompareStatus();
            require (status.observation != RecoveryReason::contentAmbiguous && ! status.prolongedWait && ! status.contentHeld
                     && ! post.liveCompareAlignPending(), "no doubt, no wait and no notice through the chain");
        }
        require (LiveTimingFixtureAccess::alignRevision (post) == revision, "nothing starts the wait again under the same mapping");
        require (post.liveContentTimingObservation().lagFrames == 0, "every later estimate keeps the position");
        if (c.blind)
        {
            require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(), "both sources sound and REVEAL is earned");
            require (! post.liveBlindStatus().trial.usedPostFallback, "RESULT names no POST substitute");
        }
        else
        {
            // A new clock mapping, the same content position: the chain's windows confirm it again.
            chain.setDelay (1024); chain.contentK = 1024;
            chain.until (90, [&] { return post.liveCompareAlignPending(); }, "the new mapping is rechecked");
            chunks = 0;
            chain.until (40, confirmed, "the chain's windows confirm the new mapping before the bound");
            chain.until (4, [&] { return post.liveCompareStatus().preAudible; }, "PRE returns at the new mapping");
        }
        chain.clean ("every PRE sample is at the content position; unproven blocks are POST");
    }
}

// The same chain with PRE early by 1024 frames, as when the host's position runs ahead of the
// audio (an I/O insert): the strong peaks through EQ and limiter find it and the audition reads
// PRE that much later.
inline void chainAlignsEarlyPre (int channels)
{
    using namespace hypha::live_compare;
    const auto tracks = hypha::tests::chain_music::render (1068544 - 70000, 500000, { 12.0, 12.0, 240 });
    ChunkedPost chain (channels);
    chain.music = &tracks;
    chain.requireContentPosition = true;
    auto& post = *chain.post;
    selectPreparedPair (chain, "select the early-PRE chain");
    require (post.startLiveCompare() == StartResult::started, "LISTEN starts");
    post.selectLiveComparePre (true);
    chain.until (60, [&] { return post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 1024; },
                 "an early PRE is aligned through EQ and limiter");
    chain.run (60);
    require (post.liveCompareAlignFrames() == 1024 && ! post.liveCompareStatus().prolongedWait, "the alignment holds");
    chain.clean ("PRE is read 1024 frames later");
}
}
