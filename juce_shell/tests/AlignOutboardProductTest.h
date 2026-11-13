#pragma once
#include "AlignSessionProductTest.h"
#include "ChainMusic.h"
#include "../src/HyphaLiveBlindComponent.h"

#include <algorithm>
#include <cstdio>

namespace aax_analysis_product
{
// INV-LC28: PRE -> analog outboard -> POST with 1065 frames the clocks do not show. The drums confirm
// it; the dense sixteenth-note loop that follows confirms no position (every beat looks like the next).
// What the listener hears, block by block: how far PRE sounds earlier than POST's matching audio.
struct HeardRow
{
    int audible = 0, early = 0, late = 0, unmeasured = 0, notRemeasured = 0;
    std::int64_t maxEarly = 0, lastAlign = 0;
};

inline HeardRow heardOver (ChunkedPost& chain, int chunks, bool blind = false)
{
    HeardRow row;
    for (int i = 0; i < chunks; ++i)
    {
        chain.chunk();
        auto& post = *chain.post;
        if (blind) post.serviceLiveBlind();
        const auto s = post.liveCompareStatus();
        const auto align = post.liveCompareAlignFrames();
        row.lastAlign = align;
        if (s.contentUnmeasured) ++row.unmeasured;
        if (s.contentNotRemeasured && (! blind || post.liveBlindStatus().contentNotRemeasured)) ++row.notRemeasured;
        if (! s.preAudible) continue;
        ++row.audible;
        const auto early = chain.contentK - (chain.k + align);
        if (early > 1) ++row.early;
        if (early < -1) ++row.late;
        row.maxEarly = std::max (row.maxEarly, early);
    }
    return row;
}

// kept: PRE at K plus the kept correction in every audible block, told as not re-measured;
// otherwise within the confirming session (no label) or, with nothing kept, at K with the label.
enum class Heard { confirmed, kept, unmeasured };
inline void requireHeard (ChunkedPost& chain, const char* what, int chunks, Heard heard, bool blind = false)
{
    const auto r = heardOver (chain, chunks, blind);
    std::printf ("outboard %-30s audible %4d early %4d late %3d unmeasured %4d not re-measured %4d PRE early max %lld align %lld\n",
                 what, r.audible, r.early, r.late, r.unmeasured, r.notRemeasured, static_cast<long long> (r.maxEarly),
                 static_cast<long long> (r.lastAlign));
    const bool ok = heard == Heard::unmeasured
        ? r.audible > 0 && r.lastAlign == 0 && r.maxEarly == 1065 && r.unmeasured > 0 && r.notRemeasured == 0
        : r.audible > 0 && r.early == 0 && r.late == 0 && r.lastAlign == 1065 && r.unmeasured == 0
            && (heard == Heard::kept ? r.notRemeasured > 0 : r.notRemeasured == 0);
    require (ok, what);
}

inline void finishListen (ChunkedPost& chain)
{
    chain.post->finishLiveCompare();
    chain.until (60, [&] { const auto s = chain.post->liveCompareStatus(); return ! s.active && ! s.finishing; }, "LISTEN ends");
}

inline void startListen (ChunkedPost& chain, const char* why)
{
    require (chain.post->startLiveCompare() == hypha::live_compare::StartResult::started, why);
    chain.post->selectLiveComparePre (true);
}

inline void outboardKeepsTheConfirmedCorrection (int channels)
{
    using namespace hypha::live_compare;
    using namespace hypha::tests::chain_music;
    constexpr std::int64_t uncompensated = 1065;
    const std::int64_t from = 1068544 - 70000, drumsUntil = 1068544 + 6 * 48000;
    const auto tracks = renderOutboard (from, 2400000, drumsUntil, Outboard {}, 6000);
    const auto prepared = [&] (ChunkedPost& chain)
    {
        chain.music = &tracks;
        chain.setDelay (512);
        chain.contentK = 512 + uncompensated;
        selectPreparedPair (chain, "select the outboard PRE");
    };
    const auto intoTheMix = [&] (ChunkedPost& chain) { chain.run (static_cast<int> ((drumsUntil - chain.engine) / ChunkedPost::chunkFrames) + 4); };
    {
        ChunkedPost chain (channels);
        prepared (chain);
        auto& post = *chain.post;
        startListen (chain, "LISTEN starts on the drums");
        requireHeard (chain, "LISTEN drums", 200, Heard::confirmed);
        requireHeard (chain, "LISTEN into the 2-mix", 300, Heard::confirmed);
        finishListen (chain);
        require (post.beginLiveBlind() == StartResult::started, "BLIND starts");
        chain.until (400, [&] { post.serviceLiveBlind(); return post.liveBlindStatus().stage == BlindStage::active; }, "BLIND prepares");
        LiveTimingFixtureAccess::assignBlind (post, true);
        require (post.selectLiveBlind (1), "choose PRE anonymously");
        requireHeard (chain, "BLIND 2-mix PRE", 150, Heard::kept, true);
        for (int source : { 1, 2 })
        {
            auto status = post.liveBlindStatus();
            status.trial.selected = source;
            require (hypha::live_blind_ui::screenFor (status, true, 1.0f).detail == hypha::live_compare_ui::notRemeasuredNotice(),
                     "BLIND shows the same not re-measured label for both sources");
        }
        require (post.selectLiveBlind (2), "switch to POST");
        chain.run (6);
        require (post.selectLiveBlind (1), "back to PRE");
        requireHeard (chain, "BLIND 2-mix PRE after a switch", 150, Heard::kept, true);
        post.closeLiveBlind();
        chain.until (60, [&] { return ! post.liveCompareStatus().active && ! post.liveCompareStatus().finishing; }, "BLIND ends");
        // A history read that misses right at the bound (its snapshot changed) is read again.
        static int misses = 0;
        misses = 2;
        keptViewMissForTest = [] { return misses > 0 && misses-- > 0; };
        startListen (chain, "LISTEN starts again");
        requireHeard (chain, "LISTEN again on the 2-mix", 150, Heard::kept);
        keptViewMissForTest = nullptr;
        require (misses == 0, "the kept position was read again after the misses");
        chain.setPlaying (false); chain.run (8); chain.setPlaying (true);
        requireHeard (chain, "LISTEN 2-mix after stop/start", 150, Heard::kept);
        chain.seek (chain.transport / 2);
        requireHeard (chain, "LISTEN 2-mix after seek", 150, Heard::kept);
        chain.enterLoop (4 * 48000);
        requireHeard (chain, "LISTEN 2-mix in a loop", 500, Heard::kept);
        chain.leaveLoop();
        chain.setDelay (512 + 256); chain.contentK = 512 + 256 + uncompensated;
        requireHeard (chain, "LISTEN 2-mix after a new K", 150, Heard::kept);
        // A read that keeps missing ends in K and the label within a short while.
        finishListen (chain);
        keptViewMissForTest = [] { return true; };
        startListen (chain, "LISTEN starts with the history unreadable");
        requireHeard (chain, "LISTEN with the kept read missing", 150, Heard::unmeasured);
        keptViewMissForTest = nullptr;
        // A pair change ends the kept correction: the next session is a first one.
        finishListen (chain);
        post.clearPairCandidate();
        chain.run (4);
        selectPreparedPair (chain, "pair the outboard PRE again");
        startListen (chain, "LISTEN starts after the pair change");
        requireHeard (chain, "LISTEN after a pair change", 150, Heard::unmeasured);
    }
    {
        // A format change ends it as well.
        ChunkedPost chain (channels);
        prepared (chain);
        auto& post = *chain.post;
        startListen (chain, "LISTEN starts on the drums");
        requireHeard (chain, "LISTEN drums before a format change", 200, Heard::confirmed);
        finishListen (chain);
        post.prepareToPlay (44100, ChunkedPost::chunkFrames);
        post.prepareToPlay (48000, ChunkedPost::chunkFrames);
        LiveTimingFixtureAccess::configureAaxClock (post);
        LiveTimingFixtureAccess::enableFixtureWrites (post);
        chain.chunk (false);
        LiveTimingFixtureAccess::serviceTiming (post);
        intoTheMix (chain);
        startListen (chain, "LISTEN starts after the format change");
        requireHeard (chain, "LISTEN after a format change", 150, Heard::unmeasured);
    }
    {
        // Nothing confirmed yet: a first LISTEN on the 2-mix plays at K with the label, as before.
        ChunkedPost chain (channels);
        chain.music = &tracks;
        chain.setDelay (512);
        chain.contentK = 512 + uncompensated;
        intoTheMix (chain);
        selectPreparedPair (chain, "select the outboard PRE on the 2-mix");
        startListen (chain, "LISTEN starts on the 2-mix");
        requireHeard (chain, "first LISTEN on the 2-mix", 300, Heard::unmeasured);
    }
}
// A match on a repeated waveform proves no unique position (INV-LC28): it is never kept. A later
// session on audio that confirms nothing plays PRE at K with the label, never POST behind rechecks.
inline void periodicMatchIsNotKept (int channels)
{
    using namespace hypha::live_compare;
    ChunkedPost chain (channels);
    chain.tonal = true;
    auto& post = *chain.post;
    selectPreparedPair (chain, "select the periodic PRE");
    startListen (chain, "LISTEN starts on periodic audio");
    chain.until (120, [&] { return post.liveCompareStatus().preAudible && post.liveContentTimingObservation().verifySamples
                                   && post.liveCompareAlignFrames() > 0; }, "periodic audio is aligned and checked per block");
    finishListen (chain);
    chain.tonal = false;
    chain.unrelatedPost = true;
    startListen (chain, "LISTEN starts on audio that confirms nothing");
    int audible = 0, unmeasured = 0, notRemeasured = 0;
    for (int i = 0; i < 200; ++i)
    {
        chain.chunk();
        const auto s = post.liveCompareStatus();
        audible += s.preAudible ? 1 : 0; unmeasured += s.contentUnmeasured ? 1 : 0; notRemeasured += s.contentNotRemeasured ? 1 : 0;
    }
    std::printf ("outboard periodic then unmeasurable: audible %d unmeasured %d not re-measured %d align %lld\n", audible,
                 unmeasured, notRemeasured, static_cast<long long> (post.liveCompareAlignFrames()));
    require (audible > 100 && unmeasured > 100 && notRemeasured == 0 && post.liveCompareAlignFrames() == 0,
             "a periodic match is not kept: PRE plays at K with the label");
    chain.clean ("PRE plays only proven clock positions");
}
}
