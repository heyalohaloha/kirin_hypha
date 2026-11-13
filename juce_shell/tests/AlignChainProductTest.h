#pragma once
#include "AlignSessionProductTest.h"
#include "ChainMusic.h"
#include "../src/HyphaLiveBlindComponent.h"

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

// A linear-phase EQ at high oversampling on a host that does not compensate POST's position: the
// audio lies far beyond the fine search, here up to two seconds either way, through the same EQ and
// limiter. The wide search finds it: an early PRE is read that much later; a late PRE keeps playing
// at its clock position with the notice. Ten seconds is beyond the reach: after the one-second
// bound PRE plays at K and LISTEN and BLIND say the delay was not measured (BLIND also when POST is
// lowered). After a stop and start, a seek back or forward, PRE's new run cannot serve a two-second
// correction at once: PRE plays at its clock position as soon as the clock is proven again, and the
// correction returns once the run reaches it (BLIND at the listener's next switch, as any new
// address). The estimate never puts POST in PRE's place; every frame is audited at the address the
// renderer claims.
inline void chainFindsLongDelays (int channels)
{
    using namespace hypha::live_compare;
    const auto tracks = hypha::tests::chain_music::render (1068544 - 250000, 1400000, { 12.0, 12.0, 240, -12.0 });
    struct Case { std::int64_t delay; bool blind; };
    for (const Case c : { Case { 4096, false }, Case { 16384, false }, Case { 96000, false }, Case { 96000, true },
                          Case { -32768, false }, Case { -96000, false }, Case { -480000, false }, Case { -480000, true } })
    {
        ChunkedPost chain (channels);
        chain.music = &tracks;
        chain.setDelay (512);
        chain.contentK = 512 + c.delay; // positive: POST's audio is later than its position, PRE early
        auto& post = *chain.post;
        selectPreparedPair (chain, "select the long-delay chain");
        require ((c.blind ? post.beginLiveBlind() : post.startLiveCompare()) == StartResult::started, "the comparison starts");
        if (c.blind)
        {
            chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; }, "BLIND prepares");
            LiveTimingFixtureAccess::assignBlind (post, true);
            require (post.selectLiveBlind (1), "choose PRE anonymously");
        }
        else post.selectLiveComparePre (true);
        // BLIND moves PRE's address only while the listener is away from PRE.
        const auto corrected = [&] (int chunks, const char* why)
        {
            if (c.blind)
            {
                chain.run (chunks);
                require (post.selectLiveBlind (2) && (chain.run (6), post.selectLiveBlind (1)), "switch away and back");
                chunks = 60;
            }
            chain.until (chunks, [&] { return post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == c.delay; }, why);
        };
        if (c.delay > 0)
            corrected (c.blind ? 240 : 400, "an early PRE is found far beyond the fine search and read that much later");
        if (c.delay >= 96000)
            for (int move = 0; move < 3; ++move)
            {
                if (move == 0) { chain.setPlaying (false); chain.run (8); chain.setPlaying (true); }
                else chain.seek (move == 1 ? chain.transport / 2 : chain.transport + 480000);
                chain.until (40, [&] { return post.liveCompareStatus().preAudible; }, "PRE plays again soon after the transport moved");
                require (post.liveCompareAlignFrames() == 0, "until PRE's new run reaches the correction, PRE plays at K");
                corrected (c.blind ? 110 : 200, "the correction returns once PRE's run reaches it");
            }
        if (c.delay < 0 && c.delay > -100000)
            chain.until (400, [&] { const auto s = post.liveCompareStatus();
                                    return s.preAudible && s.contentHeld && s.prolongedWait && s.contentJumpLagFrames == -c.delay; },
                         "a late PRE plays at its clock position and the notice gives the amount");
        else if (c.delay < 0)
        {
            chain.until (90, [&] { return post.liveCompareStatus().contentUnmeasured; }, "beyond the reach, the bound ends with the label");
            for (int i = 0; i < 120; ++i)
            {
                chain.chunk();
                const auto s = post.liveCompareStatus();
                require (s.preAudible && s.contentUnmeasured && ! s.contentHeld && post.liveCompareAlignFrames() == 0,
                         "PRE keeps playing at its clock position while the label stays");
            }
            const auto label = juce::String (hypha::live_compare_ui::unmeasuredNotice()) + " / " + hypha::live_compare_ui::chainDelayNotice();
            for (int source : { 1, 2 })
            {
                if (! c.blind) break;
                auto status = post.liveBlindStatus();
                status.trial.selected = source;
                // At normal level the label has the lower line; with POST lowered that line names the
                // level and the label takes the guidance line. Either way, the same for both sources.
                const auto normal = hypha::live_blind_ui::screenFor (status, true, 1.0f);
                const auto lowered = hypha::live_blind_ui::screenFor (status, true, 0.5f);
                require (normal.detail == label && lowered.instruction == label && lowered.detail != label,
                         "BLIND shows the same label for both sources, POST lowered or not");
            }
        }
        chain.clean ("every PRE sample is at the address the renderer claims; unproven blocks are POST");
    }
}

// Opposite-polarity stereo (R = -L) has no mono sum: the wide search, like the fine one, reads each
// channel and still finds PRE early by 32768 frames.
inline void wideSearchReadsEachChannel (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    chain.oppositePolarity = true;
    chain.setDelay (512);
    chain.contentK = 512 + 32768;
    auto& post = *chain.post;
    selectPreparedPair (chain, "select the opposite-polarity PRE");
    require (post.startLiveCompare() == StartResult::started, "LISTEN starts");
    post.selectLiveComparePre (true);
    chain.until (300, [&] { return post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 32768; },
                 "opposite-polarity stereo is found beyond the fine search");
    chain.clean ("PRE is read 32768 frames later");
}

// Exchanged channels (independent stereo through an L/R swap), PRE early by two seconds: each POST
// channel against PRE's other one finds it. Channels delayed differently (32768 and 96000 frames)
// contradict each other: PRE plays at its clock position with the label, never one channel's delay
// for both, also while the history reaches only the nearer one.
inline void wideSearchWeighsEveryCandidate (int channels)
{
    using namespace hypha::live_compare;
    if (channels < 2) return;
    {
        ChunkedPost swapped (channels);
        swapped.swappedChannels = true;
        swapped.setDelay (512);
        swapped.contentK = 512 + 96000;
        auto& listen = *swapped.post;
        selectPreparedPair (swapped, "select the exchanged PRE");
        require (listen.startLiveCompare() == StartResult::started, "LISTEN starts");
        listen.selectLiveComparePre (true);
        swapped.until (400, [&] { return listen.liveCompareStatus().preAudible && listen.liveCompareAlignFrames() == 96000; },
                       "exchanged channels are found two seconds away");
        swapped.clean ("PRE is read 96000 frames later");
    }
    ChunkedPost chain (channels);
    chain.setDelay (512);
    chain.contentK = 512 + 32768;
    chain.rightContentShift = 96000 - 32768;
    auto& post = *chain.post;
    selectPreparedPair (chain, "select the contradictory PRE");
    require (post.startLiveCompare() == StartResult::started, "LISTEN starts");
    post.selectLiveComparePre (true);
    chain.until (90, [&] { return post.liveCompareStatus().contentUnmeasured; }, "the bound ends with the label");
    for (int i = 0; i < 400; ++i)
    {
        chain.chunk();
        const auto s = post.liveCompareStatus();
        require (s.preAudible && s.contentUnmeasured && post.liveCompareAlignFrames() == 0,
                 "contradictory channels move nothing: PRE plays at its clock position with the label");
    }
    chain.clean ("every PRE sample is at its clock position");
}
}
