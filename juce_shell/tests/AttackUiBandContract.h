#pragma once

#include "AttackUiImageHelpers.h"
#include "AttackUiLaneContract.h"
#include "AttackUiSizeContract.h"
#include "../src/HyphaAttackBandContract.h"
#include "../src/HyphaAttackBandModel.h"
#include "../src/HyphaAttackBandPainter.h"
#include "../src/HyphaMaterialCache.h"
#include "../src/HyphaObservatoryContract.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>

// DRUM BAND (B-1097): the band lanes' model, the chips, the HEAD / TAIL panes, the pointer and
// hover contract, and the five editor sizes in the band view.
namespace hypha::attack_ui_test
{
// One side of one hit in a band: a linear rise to the peak, then an exponential ring-out. The
// engine's times: arrival at 10 % of the peak amplitude, ATT from 10 % to 90 %, REL from the peak
// to -20 dB (an amplitude factor of 10, tau * ln 10).
inline void fillBandSide (KirinAttackBandSide& side, float delayMs, float riseMs, float tauMs,
                          float peakDb, std::int64_t onset)
{
    side.available = side.arrival_available = side.attack_available = side.release_available = 1;
    side.level_dbfs = peakDb;
    side.arrival_ms = delayMs + riseMs * 0.1f;
    side.attack_ms = riseMs * 0.8f;
    side.peak_ms = delayMs + riseMs;
    side.release_ms = tauMs * std::log (10.0f);
    side.span_end_sample = onset + 300 * 48;
    const auto level = [&] (float ms) {
        const auto t = ms - delayMs;
        if (t <= 0.0f) return attack_ui::absoluteFloorDb;
        const auto rise = std::min (1.0f, t / riseMs);
        const auto decay = t > riseMs ? std::exp (-(t - riseMs) / tauMs) : 1.0f;
        return std::max (attack_ui::absoluteFloorDb,
                         peakDb + 20.0f * std::log10 (std::max (1.0e-6f, rise * decay))); };
    for (std::size_t point = 0; point < KIRIN_ATTACK_BAND_HEAD_POINTS; ++point)
        side.head_dbfs[point] = level (attack_band::headPointsFromMs
            + (static_cast<float> (point) + 0.5f) * 60.0f / KIRIN_ATTACK_BAND_HEAD_POINTS);
    for (std::size_t point = 0; point < KIRIN_ATTACK_BAND_TAIL_POINTS; ++point)
        side.tail_dbfs[point] = level ((static_cast<float> (point) + 0.5f) * 300.0f / KIRIN_ATTACK_BAND_TAIL_POINTS);
}

// The band hits at the given onsets, generation 7 at 48 kHz: PRE 6 ms rise, 45 ms tau, -6 dB;
// POST 2.4 ms later, 7 ms rise, 55 ms tau, -6.8 dB (the mock's numbers). Without PRE the hits
// are POST only (kind 2) and the batch says PRE has not sent the band.
inline std::unique_ptr<KirinAttackBandBatch> bandBatch (std::initializer_list<std::int64_t> samples,
                                                        std::uint8_t band, bool preAvailable = true)
{
    auto batch = std::make_unique<KirinAttackBandBatch>();
    batch->status = KIRIN_SPECTRUM_ACTIVE;
    batch->band = band;
    batch->pre_band_available = preAvailable ? 1 : 0;
    batch->capacity = KIRIN_ATTACK_BAND_BATCH_CAPACITY;
    const auto* entry = attack_band::bandFor (band);
    for (const auto sample : samples)
    {
        auto& hit = batch->hits[batch->count++];
        hit.generation = 7;
        hit.sample_rate = 48'000;
        hit.channels = 2;
        hit.band = band;
        hit.kind = preAvailable ? 0 : 2;
        hit.event_sample = sample;
        hit.resolution_micros = static_cast<std::uint32_t> (std::lround (entry->periodMs() * 1'000.0f));
        fillBandSide (hit.post, 2.4f, 7.0f, 55.0f, -6.8f, sample);
        if (preAvailable)
        {
            fillBandSide (hit.pre, 0.0f, 6.0f, 45.0f, -6.0f, sample);
            hit.delay_available = 1;
            hit.delay_ms = hit.post.arrival_ms - hit.pre.arrival_ms;
        }
    }
    return batch;
}

inline bool verifyBandModel()
{
    using namespace attack_lanes;
    using attack_lane_painter::cellText;
    using attack_lane_painter::valueText;
    auto batch = bandBatch ({ 96'000, 192'000, 240'000, 260'000 }, 4); // 500 Hz: 2 ms resolution
    auto& hits = batch->hits;
    hits[1].pre.arrival_available = hits[1].pre.attack_available = 0;
    hits[1].delay_available = 0;
    hits[2].post.release_available = 0;
    hits[3].kind = 2; // POST only inside a paired view
    hits[3].pre = {};
    hits[3].delay_available = 0;
    auto& stale = hits[batch->count++];
    stale = hits[0];
    stale.event_sample = 130'000;
    stale.generation = 6;
    auto model = std::make_unique<Model>();
    attack_band::build (*model, *batch, 4, 7, 48'000, true);
    const auto reason = [&model] (std::size_t hit, Lane lane) { return model->hits[hit].cells[index (lane)].reason; };
    const auto value = [&model] (std::size_t hit, Lane lane) { return model->hits[hit].cells[index (lane)].value; };
    if (! model->delta || model->count != 4 || model->hits[0].sample != 96'000
        || ! near (value (0, Lane::delay), 2.5f) || reason (0, Lane::attackTime) != Reason::belowResolution
        || ! near (value (0, Lane::attackTime), 2.0f) || ! near (value (0, Lane::release), 10.0f * std::log (10.0f))
        || ! near (value (0, Lane::level), -0.8f) || ! model->hits[0].selectable)
    {
        std::cerr << "band delta model wrong\n";
        return false;
    }
    if (reason (1, Lane::delay) != Reason::ringing || reason (1, Lane::attackTime) != Reason::ringing
        || reason (1, Lane::release) != Reason::value || reason (2, Lane::release) != Reason::nextHit
        || reason (2, Lane::delay) != Reason::value)
    {
        std::cerr << "band withheld reasons wrong\n";
        return false;
    }
    for (const auto lane : bandLanes)
        if (reason (3, lane) != Reason::noMatch)
        {
            std::cerr << "POST-only band hit not withheld\n";
            return false;
        }
    if (attack_lane_painter::reasonText (model->hits[3], Reason::noMatch) != "POST ONLY"
        || attack_lane_painter::reasonText (model->hits[1], Reason::ringing) != "RINGING"
        || cellText (model->hits[0], Lane::attackTime, true, true) != "<2 ms"
        || cellText (model->hits[0], Lane::delay, true, true) != "+2.5 ms"
        || cellText (model->hits[0], Lane::release, true, true) != "+23 ms"
        || cellText (model->hits[0], Lane::level, true, true) != "-0.8 dB"
        || valueText (Lane::release, 126.6f, false, true) != "127 ms")
    {
        std::cerr << "band cell text wrong: " << cellText (model->hits[0], Lane::attackTime, true, true)
                  << " " << cellText (model->hits[0], Lane::delay, true, true) << '\n';
        return false;
    }
    // Another band than the view's, or none, contributes nothing.
    attack_band::build (*model, *batch, 5, 7, 48'000, true);
    const auto otherBand = model->count;
    attack_band::build (*model, *batch, 0, 7, 48'000, true);
    if (otherBand != 0 || model->count != 0)
    {
        std::cerr << "another band's batch shown\n";
        return false;
    }
    // Without a pair: POST's own band values, and DELAY has no meaning.
    attack_band::build (*model, *batch, 4, 7, 48'000, false);
    if (model->delta || model->count != 4 || reason (0, Lane::delay) != Reason::noMatch
        || ! near (value (0, Lane::attackTime), 5.6f) || ! near (value (0, Lane::level), -6.8f)
        || cellText (model->hits[0], Lane::level, false, true) != "-6.8 dBFS"
        || cellText (model->hits[0], Lane::delay, false, true) != "NO PAIR")
    {
        std::cerr << "band absolute model wrong\n";
        return false;
    }
    // Paired, but PRE has not sent the band: POST's values and the reason on DELAY.
    auto pending = bandBatch ({ 96'000 }, 1, false); // 63 Hz: 16 ms resolution
    attack_band::build (*model, *pending, 1, 7, 48'000, true);
    if (model->delta || model->count != 1 || reason (0, Lane::delay) != Reason::noPreBand
        || cellText (model->hits[0], Lane::delay, false, false) != "PRE NO BAND"
        || reason (0, Lane::attackTime) != Reason::belowResolution
        || cellText (model->hits[0], Lane::attackTime, false, true) != "<16 ms")
    {
        std::cerr << "band pending model wrong\n";
        return false;
    }
    using attack_band_painter::nameText;
    using attack_band_painter::rangeText;
    const juce::String texts[] {
        rangeText (1), rangeText (4), rangeText (5), rangeText (6), rangeText (8), nameText (1),
        nameText (5), nameText (0), attack_lane_painter::resolutionText (0.125f),
        attack_lane_painter::resolutionText (0.5f), attack_lane_painter::boundText (16.0f),
        attack_band::labelFor (8) };
    // Edges follow the engine's exact centres (62.5 Hz * 2^n), not the nominal labels.
    const char* expected[] { "44-88 Hz", "354-707 Hz", "707 Hz-1.41 kHz", "1.41-2.83 kHz",
        "5.66-11.3 kHz", "63 Hz", "1 kHz", "ALL", "0.13 ms", "0.5 ms", "<16 ms", "8k" };
    for (std::size_t item = 0; item < std::size (texts); ++item)
        if (texts[item] != expected[item])
        {
            std::cerr << "band text " << item << ": " << texts[item] << " != " << expected[item] << '\n';
            return false;
        }
    return true;
}

// The band view at the five editor sizes: chips from 125%, panes at 200% and 300% with the PRE
// trace and the POST body in them, the band lanes named, and the six seconds kept below 200%.
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
        component.setBand (4);
        component.setBandSnapshot (*bandBatch ({ 96'000, 192'000, 240'000 }, 4));
        const auto banded = renderAttack (component);
        if (const auto* previews = std::getenv ("KIRIN_ATTACK_UI_BAND_PREVIEW_DIR"); previews != nullptr)
        {
            juce::FileOutputStream output { juce::File { previews }.getChildFile (
                juce::String (preset.label) + "_band500.png") };
            juce::PNGImageFormat png;
            if (! output.openedOk() || ! png.writeImageToStream (banded, output))
                return false;
        }
        const auto chips = attack_band::chipRow (layout, context);
        const bool compact = preset.density == observatory::Density::compact;
        if (chips.empty() != compact)
        {
            std::cerr << "band chips at " << preset.label << '\n';
            return false;
        }
        const auto history = historyRect (layout);
        if (compact)
        {
            // 100% names the band in HISTORY, top left, where HOLD would stand top right.
            const auto caption = history.reduced (6, 3).withWidth (60).withHeight (16);
            if (differences (plain, banded, caption) == 0)
            {
                std::cerr << "band not named at 100%\n";
                return false;
            }
        }
        else
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
        {
            std::cerr << "panes at " << preset.label << '\n';
            return false;
        }
        if (panes)
        {
            const auto head = rectangle (attack_band::headPane (layout));
            const auto tail = rectangle (attack_band::tailPane (layout));
            if (countColour (banded, head, juce::Colour (attack_ui::preTraceColour), 40) == 0
                || countColour (banded, head, juce::Colour (attack_ui::waveformColour), 40) == 0
                || countColour (banded, tail, juce::Colour (attack_ui::preTraceColour), 40) == 0
                || countColour (banded, tail, juce::Colour (attack_ui::waveformColour), 40) == 0)
            {
                std::cerr << "panes empty at " << preset.label << '\n';
                return false;
            }
            // VIEW 2 ROWS splits each pane into PRE above POST.
            component.setOverlayMode (false);
            if (differences (banded, renderAttack (component), head) == 0)
            {
                std::cerr << "2 ROWS unchanged at " << preset.label << '\n';
                return false;
            }
            component.setOverlayMode (true);
        }
        else if (differences (plain, banded, history.reduced (0, 20)) != 0 && ! compact)
        {
            std::cerr << "HISTORY changed without panes at " << preset.label << '\n';
            return false;
        }
        if (layout.arrangement == attack_ui::Arrangement::lanes)
        {
            const auto label = rectangle (attack_ui::labelCell (layout, layout.lanes[0]));
            const auto readout = rectangle (attack_ui::readoutCell (layout, layout.lanes[2]));
            if (countColour (banded, label, juce::Colour (attack_ui::transientColour), 40) == 0
                || countColour (banded, readout, COL_OBSERVATORY_VALUE, 40) == 0)
            {
                std::cerr << "band lanes unlabelled at " << preset.label << '\n';
                return false;
            }
        }
        if (! verifyThinSelection (banded, layout))
            return false;
        // ALL returns to the whole-signal view exactly.
        component.setBand (0);
        if (differences (plain, renderAttack (component)) != 0)
        {
            std::cerr << "ALL differs from before at " << preset.label << '\n';
            return false;
        }
    }
    return true;
}

// Chips choose, panes do not select, lanes still do; the hover help names what the pointer is on.
inline bool verifyBandInteraction()
{
    auto scene = presetScene (observatory::sizePresets.back());
    auto& component = *scene.component;
    const auto& layout = scene.layout;
    const auto context = presentation::forEditor (900, 600);
    auto fixture = laneFixture ({ 96'000, 192'000, 240'000 });
    fixture.submit (component);
    int chosen = -1;
    component.onBandChange = [&chosen] (std::uint8_t band) { chosen = band; };
    const auto click = [&component] (juce::Rectangle<int> area) {
        const auto centre = area.getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();
        component.mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), centre, {}, 0.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, &component, &component, now, centre, now, 0, false }); };
    const auto hover = [&component] (juce::Rectangle<int> area) {
        const auto centre = area.getCentre().toFloat();
        const auto now = juce::Time::getCurrentTime();
        component.mouseMove ({ juce::Desktop::getInstance().getMainMouseSource(), centre, {}, 0.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, &component, &component, now, centre, now, 0, false }); };
    click (rectangle (attack_band::chipCell (layout, context, 3)));
    if (component.band() != 3 || chosen != 3)
    {
        std::cerr << "chip click did not choose: band " << int (component.band()) << " chosen " << chosen << '\n';
        return false;
    }
    component.setBandSnapshot (*bandBatch ({ 96'000, 192'000, 240'000 }, 3));
    component.keyPressed (juce::KeyPress (juce::KeyPress::homeKey));
    const auto locked = renderAttack (component);
    click (rectangle (attack_band::headPane (layout)));
    if (differences (locked, renderAttack (component)) != 0)
    {
        std::cerr << "a click in the HEAD pane changed the selection\n";
        return false;
    }
    const auto lane = laneRect (layout, 1);
    click (lane.withX (lane.getRight() - 8).withWidth (4));
    if (differences (locked, renderAttack (component)) == 0)
    {
        std::cerr << "a click in a lane no longer selects\n";
        return false;
    }
    const auto expectTip = [&component] (const juce::String& expected, const char* where) {
        if (component.getTooltip() == expected)
            return true;
        std::cerr << "hover help wrong over " << where << ": " << component.getTooltip() << '\n';
        return false; };
    hover (rectangle (attack_band::chipCell (layout, context, 5)));
    if (! expectTip (attack_band_painter::chipTooltip (5), "a chip")) return false;
    hover (rectangle (attack_band::headPane (layout)));
    if (! expectTip (attack_band_painter::paneTooltip (true), "HEAD")) return false;
    hover (rectangle (attack_band::tailPane (layout)));
    if (! expectTip (attack_band_painter::paneTooltip (false), "TAIL")) return false;
    hover (rectangle (layout.lanes[0]));
    if (! expectTip (attack_band_painter::laneTooltip (attack_lanes::Lane::delay), "DELAY")) return false;
    component.mouseExit ({ juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, &component, &component, juce::Time::getCurrentTime(), {},
                           juce::Time::getCurrentTime(), 0, false });
    if (! expectTip ({}, "nothing")) return false;
    // PRE without the band: the chosen chip explains it, and the view says so.
    component.setBandSnapshot (*bandBatch ({ 96'000, 192'000, 240'000 }, 3, false));
    if (! component.preBandPending())
    {
        std::cerr << "PRE without the band not pending\n";
        return false;
    }
    hover (rectangle (attack_band::chipCell (layout, context, 3)));
    if (! expectTip (attack_band_painter::pendingTooltip(), "the chosen chip while pending")) return false;
    // A batch still in the previous band is not the chosen band's: nothing is shown as it.
    component.setBandSnapshot (*bandBatch ({ 96'000, 192'000, 240'000 }, 2));
    if (! component.preBandPending())
    {
        std::cerr << "another band's batch not pending\n";
        return false;
    }
    click (rectangle (attack_band::chipCell (layout, context, 0)));
    if (component.band() != 0 || chosen != 0 || component.preBandPending())
    {
        std::cerr << "ALL did not return\n";
        return false;
    }
    hover (rectangle (layout.lanes[0]));
    if (component.getTooltip().isNotEmpty())
    {
        std::cerr << "the whole-signal lanes gained hover help\n";
        return false;
    }
    // 100% has no chips: a click where they would stand selects a hit in HISTORY, as it always
    // has, and leaves the band alone.
    auto compact = presetScene (observatory::sizePresets.front());
    fixture.submit (*compact.component);
    compact.component->setBand (2);
    chosen = -1;
    compact.component->onBandChange = [&chosen] (std::uint8_t band) { chosen = band; };
    const auto now = juce::Time::getCurrentTime();
    compact.component->mouseDown ({ juce::Desktop::getInstance().getMainMouseSource(), { 40.0f, 8.0f }, {},
                                    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, compact.component.get(),
                                    compact.component.get(), now, { 40.0f, 8.0f }, now, 0, false });
    if (compact.component->band() != 2 || chosen != -1)
    {
        std::cerr << "a click at 100% changed the band\n";
        return false;
    }
    return true;
}

// Run with KIRIN_ATTACK_FRAME_BUDGET set: the 300% band view with a full batch, changing every
// frame, against the same ceilings as the whole-signal view.
inline bool verifyBandFrameBudget()
{
    if (juce::SystemStats::getEnvironmentVariable ("KIRIN_ATTACK_FRAME_BUDGET", {}).isEmpty())
        return true;
    material_cache::Lifetime materialCache;
    bool withinBudget = true;
    std::initializer_list<std::int64_t> onsets { 4'800, 9'600, 14'400, 19'200, 24'000, 28'800,
        33'600, 38'400, 43'200, 48'000, 52'800, 57'600, 62'400, 67'200, 72'000, 76'800, 81'600,
        86'400, 91'200, 96'000, 100'800, 105'600, 110'400, 115'200, 120'000, 124'800, 129'600,
        134'400, 139'200, 144'000, 148'800, 153'600, 158'400, 163'200, 168'000, 172'800, 177'600,
        182'400, 187'200, 192'000, 196'800, 201'600, 206'400, 211'200, 216'000, 220'800, 225'600,
        230'400, 235'200, 240'000, 244'800, 249'600, 254'400, 259'200, 264'000, 268'800, 273'600,
        278'400, 283'200, 288'000 };
    auto fixture = laneFixture (onsets);
    auto batch = bandBatch (onsets, 4);
    for (const auto dpi : { 1.0f, 2.0f })
    for (const bool overlay : { true, false })
    {
        auto scene = presetScene (observatory::sizePresets.back());
        auto& component = *scene.component;
        component.setOverlayMode (overlay);
        component.setBand (4);
        juce::Image image (juce::Image::ARGB, static_cast<int> (std::ceil (component.getWidth() * dpi)),
                           static_cast<int> (std::ceil (component.getHeight() * dpi)), true);
        std::array<double, 6> samples {};
        for (std::size_t frame = 0; frame < samples.size(); ++frame)
        {
            for (std::uint32_t hit = 0; hit < batch->count; ++hit)
                batch->hits[hit].post.level_dbfs = -6.8f - 0.1f * static_cast<float> (frame);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            fixture.submit (component, 288'000 + static_cast<std::int64_t> (frame) * 480);
            component.setBandSnapshot (*batch);
            component.presentationTickAt (start + 101.0);
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (dpi));
            component.paintEntireComponent (g, true);
            samples[frame] = juce::Time::getMillisecondCounterHiRes() - start;
        }
        std::sort (samples.begin() + 1, samples.end());
        std::cout << "DRUM band frame: dpi=" << dpi << " overlay=" << overlay << " cold_ms=" << samples[0]
                  << " median_ms=" << samples[3] << " max_ms=" << samples[5] << '\n';
#if ! JUCE_DEBUG
        withinBudget = withinBudget && samples[3] <= 12.0 && samples[5] <= 24.0 && samples[0] <= 80.0;
#endif
    }
    return withinBudget;
}
}
