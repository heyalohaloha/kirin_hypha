#pragma once

#include "AttackUiBandFixture.h"
#include "AttackUiImageHelpers.h"
#include "AttackUiLaneContract.h"
#include "AttackUiSizeContract.h"
#include "../src/HyphaAttackBandModel.h"
#include "../src/HyphaAttackBandPainter.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaObservatoryContract.h"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>

// DRUM BAND (B-1097, B-1098): the band lanes are the whole-signal lanes' own hits; every cell says
// what the engine stated; the chips, panes, guidance and hover help work at every size and in both
// languages. The LIVE summary and a locked hit: AttackUiBandSummaryContract.h.
namespace hypha::attack_ui_test
{
inline bool expectText (const juce::String& actual, const char* expected, const char* what)
{
    if (actual == expected)
        return true;
    std::cerr << what << ": \"" << actual << "\" instead of \"" << expected << "\"\n";
    return false;
}

inline bool verifyBandModel()
{
    using namespace attack_lanes;
    using attack_lane_painter::cellText;
    // The lanes key each hit by the pair's common onset, 3 ms after PRE's: the band lanes must
    // find the engine's record by exactly that key and show the same columns.
    auto fixture = laneFixture ({ 96'000, 144'000, 192'000, 240'000, 264'000 });
    shiftCommonOnsets (fixture);
    auto lanes = std::make_unique<Model>();
    build (*lanes, *fixture.pairs, *fixture.post, *fixture.pre, 7, 48'000);
    auto batch = bandBatchFor (fixture, 4); // 500 Hz: 2 ms resolution
    auto& hits = batch->hits;
    hits[1].pre = sideIn (KIRIN_ATTACK_BAND_SIDE_NOT_KEPT);
    hits[2].post = sideIn (KIRIN_ATTACK_BAND_SIDE_SILENT);
    hits[3].post.release_state = KIRIN_ATTACK_BAND_RELEASE_AT_LEAST;
    hits[3].post.release_ms = 250.0f;
    hits[4].pre.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_RINGING;
    auto band = std::make_unique<Model>();
    attack_band::build (*band, *lanes, *batch, 4, 7, 48'000);
    if (band->count != lanes->count || ! band->delta)
        return false;
    for (std::uint32_t item = 0; item < band->count; ++item)
        if (band->hits[item].sample != lanes->hits[item].sample
            || band->hits[item].selectable != lanes->hits[item].selectable)
        {
            std::cerr << "band hit " << item << " is not the lanes' hit\n";
            return false;
        }
    const auto text = [&band] (std::size_t hit, Lane lane) {
        return cellText (band->hits[hit], lane, true, true); };
    if (! expectText (text (0, Lane::delay), "+2.5 ms", "DELAY")
        || ! expectText (text (0, Lane::attackTime), "<2 ms", "ATT inside the resolution")
        || ! expectText (text (0, Lane::release), "+23 ms", "REL")
        || ! expectText (text (0, Lane::level), "-0.8 dB", "LEVEL")
        || ! expectText (text (1, Lane::level), "NOT MEASURED", "a hit PRE did not keep")
        || ! expectText (text (2, Lane::level), "<-66.0 dB", "LEVEL against a silent POST")
        || ! expectText (text (2, Lane::delay), "POST NO SOUND", "DELAY against a silent POST")
        || ! expectText (text (3, Lane::release), ">+146 ms", "REL past POST's tail")
        || ! expectText (text (4, Lane::delay), "RINGING", "a start hidden by a ring-out")
        || ! expectText (text (4, Lane::level), "-0.8 dB", "LEVEL of a ringing start"))
        return false;
    // Both sides past the tail, the next hit, a band that only rings on, nothing yet.
    hits[3].pre.release_state = KIRIN_ATTACK_BAND_RELEASE_AT_LEAST;
    hits[0].pre.release_state = KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT;
    hits[4].post = sideIn (KIRIN_ATTACK_BAND_SIDE_RINGS_ON);
    hits[2].post = sideIn (KIRIN_ATTACK_BAND_SIDE_PENDING);
    attack_band::build (*band, *lanes, *batch, 4, 7, 48'000);
    if (! expectText (text (3, Lane::release), "LONG TAIL", "REL past both tails")
        || ! expectText (text (0, Lane::release), "NEXT HIT", "REL cut by the next hit")
        || ! expectText (text (4, Lane::level), "RINGING", "a band that only rings on")
        || ! expectText (text (2, Lane::level), "--", "a hit still measuring"))
        return false;
    // PRE still switching bands: POST - PRE stays the frame, every cell waits.
    auto waiting = bandBatchFor (fixture, 4, KIRIN_ATTACK_BAND_PRE_WAITING);
    attack_band::build (*band, *lanes, *waiting, 4, 7, 48'000);
    if (! band->delta || ! expectText (text (0, Lane::level), "--", "PRE on its way"))
        return false;
    // A PRE that predates bands: POST's own values, and DELAY says what to do.
    auto older = bandBatchFor (fixture, 4, KIRIN_ATTACK_BAND_PRE_PREDATES);
    attack_band::build (*band, *lanes, *older, 4, 7, 48'000);
    if (band->delta || ! expectText (cellText (band->hits[0], Lane::delay, false, true), "UPDATE PRE", "PRE predates")
        || ! expectText (cellText (band->hits[0], Lane::level, false, true), "-6.8 dBFS", "POST LEVEL"))
        return false;
    // A batch for another band, run or rate describes nothing here.
    batch->band = 5;
    attack_band::build (*band, *lanes, *batch, 4, 7, 48'000);
    if (! expectText (text (0, Lane::level), "--", "another band's batch"))
        return false;
    // Without a pair: POST's own details, POST's values, DELAY needing a PRE.
    auto alone = laneFixture ({ 96'000, 192'000 });
    alone.pairs->status = KIRIN_SPECTRUM_NO_PAIR;
    build (*lanes, *alone.pairs, *alone.post, *alone.pre, 7, 48'000);
    auto post = bandBatchFor (alone, 4);
    attack_band::build (*band, *lanes, *post, 4, 7, 48'000);
    const auto plain = [&band] (Lane lane) { return cellText (band->hits[0], lane, false, true); };
    if (band->delta || band->count != 2 || ! expectText (plain (Lane::delay), "NO PAIR", "DELAY alone")
        || ! expectText (plain (Lane::attackTime), "5.6 ms", "ATT") || ! expectText (plain (Lane::release), "127 ms", "REL")
        || ! expectText (plain (Lane::level), "-6.8 dBFS", "LEVEL"))
        return false;
    using attack_band_painter::nameText;
    using attack_band_painter::rangeText;
    const juce::String texts[] { rangeText (1), rangeText (5), rangeText (8), nameText (5),
        attack_band_painter::playText (1), attack_lane_painter::resolutionText (0.125f),
        attack_lane_painter::boundText (16.0f) };
    const char* expected[] { "44-88 Hz", "707 Hz-1.41 kHz", "5.66-11.3 kHz", "1 kHz",
        "PLAY TO MEASURE 63 Hz", "0.13 ms", "<16 ms" };
    for (std::size_t item = 0; item < std::size (texts); ++item)
        if (! expectText (texts[item], expected[item], "band text"))
            return false;
    return true;
}
}

#include "AttackUiBandSummaryContract.h"
#include "AttackUiBandInteraction.h"
