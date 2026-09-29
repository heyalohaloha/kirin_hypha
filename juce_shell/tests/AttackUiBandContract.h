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
// languages.
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

// The band view at the five editor sizes: chips from 125%, panes at 200% and 300% drawn from the
// record of the selected hit, the band lanes named, the guidance when nothing was measured, and
// ALL returning exactly to the whole-signal view.
inline bool verifyBandRendering()
{
    for (const auto& preset : observatory::sizePresets)
    {
        auto scene = presetScene (preset);
        auto& component = *scene.component;
        const auto& layout = scene.layout;
        const auto context = presentation::forEditor (preset.width, preset.height);
        auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
        fixture.submit (component);
        component.presentationTickAt (juce::Time::getMillisecondCounterHiRes() + 1'000.0);
        const auto plain = renderAttack (component);
        auto batch = bandBatchFor (fixture, 4);
        component.bandEnvelopeSource = EnvelopeSource { batch.get() };
        component.setBand (4);
        component.setBandSnapshot (*batch);
        const auto banded = renderAttack (component);
        for (std::uint32_t item = 0; item < batch->count; ++item)
            batch->hits[item].pre = sideIn (KIRIN_ATTACK_BAND_SIDE_NOT_KEPT);
        component.setBandSnapshot (*batch);
        const auto unmeasured = renderAttack (component);
        // The newest hit with three withheld values beside one: each says why at every size.
        auto mixed = bandBatchFor (fixture, 4);
        mixed->hits[2].pre.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_RINGING;
        mixed->hits[2].post.release_state = KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT;
        component.bandEnvelopeSource = EnvelopeSource { mixed.get() };
        component.setBandSnapshot (*mixed);
        const auto reasons = renderAttack (component);
        if (differences (banded, reasons, rectangle (attack_ui::lineCell (layout, 0))) == 0
            && layout.arrangement != attack_ui::Arrangement::lanes)
        {
            std::cerr << "a withheld DELAY reads like a value at " << preset.label << '\n';
            return false;
        }
        if (const auto* previews = std::getenv ("KIRIN_ATTACK_UI_BAND_PREVIEW_DIR"); previews != nullptr)
            for (const auto& [name, image] : { std::pair { "_band500", &banded }, std::pair { "_play", &unmeasured },
                                               std::pair { "_reasons", &reasons } })
            {
                juce::FileOutputStream output { juce::File { previews }.getChildFile (juce::String (preset.label) + name + ".png") };
                if (! output.openedOk() || ! juce::PNGImageFormat().writeImageToStream (*image, output))
                    return false;
            }
        const bool compact = preset.density == observatory::Density::compact;
        if (attack_band::chipRow (layout, context).empty() != compact)
        {
            std::cerr << "band chips at " << preset.label << '\n';
            return false;
        }
        const auto history = historyRect (layout);
        if (differences (banded, unmeasured, history) == 0)
        {
            std::cerr << "no guidance when nothing was measured at " << preset.label << '\n';
            return false;
        }
        if (! compact)
        {
            // The choice moved from ALL to 500: both chips change, an unchosen one does not.
            const auto chosen = rectangle (attack_band::chipCell (layout, context, 4));
            const auto all = rectangle (attack_band::chipCell (layout, context, 0));
            const auto other = rectangle (attack_band::chipCell (layout, context, 2));
            if (differences (plain, banded, chosen) == 0 || differences (plain, banded, all) == 0
                || differences (plain, banded, other) != 0)
            {
                std::cerr << "chip choice not shown at " << preset.label << '\n';
                return false;
            }
        }
        const bool panes = attack_band::panesShown (layout);
        if (panes != (preset.width >= 600))
            return false;
        if (panes)
        {
            const auto head = rectangle (attack_band::headPane (layout));
            const auto tail = rectangle (attack_band::tailPane (layout));
            for (const auto& pane : { head, tail })
                if (countColour (banded, pane, juce::Colour (attack_ui::preTraceColour), 40) == 0
                    || countColour (banded, pane, juce::Colour (attack_ui::waveformColour), 40) == 0)
                {
                    std::cerr << "panes empty at " << preset.label << '\n';
                    return false;
                }
        }
        if (layout.arrangement == attack_ui::Arrangement::lanes)
        {
            const auto label = rectangle (attack_ui::labelCell (layout, layout.lanes[0]));
            if (countColour (banded, label, juce::Colour (attack_ui::transientColour), 40) == 0)
            {
                std::cerr << "band lanes unlabelled at " << preset.label << '\n';
                return false;
            }
        }
        if (! verifyThinSelection (banded, layout))
            return false;
        component.setBand (0);
        if (differences (plain, renderAttack (component)) != 0)
        {
            std::cerr << "ALL differs from before at " << preset.label << '\n';
            return false;
        }
    }
    return true;
}
}

#include "AttackUiBandInteraction.h"
