#pragma once

#include "../src/HyphaAttackBandSummaryPainter.h"
#include "../src/HyphaAttackLanePainter.h"

// DRUM band summary (2026-09-29): while LIVE the band view reads the engine's summary of the recent
// hits that rise in the band; a locked hit reads itself beside the same number lines; END returns.
// Included by AttackUiBandContract.h.
namespace hypha::attack_ui_test
{
// Ten matched hits half a second apart whose band varies as a real kick's does (one of them only
// rings on and is left out), and the engine's summary of them.
struct SummaryFixture
{
    LaneFixture lanes;
    std::unique_ptr<KirinAttackBandBatch> batch;
    KirinAttackBandSummary summary {};

    void resum() { summary = summaryFor (*batch); }
};

inline SummaryFixture summaryFixture (std::uint8_t band, std::uint8_t preBand = KIRIN_ATTACK_BAND_PRE_SAME,
                                      bool paired = true)
{
    SummaryFixture fixture;
    fixture.lanes = laneFixture ({ 48'000, 72'000, 96'000, 120'000, 144'000, 168'000, 192'000, 216'000,
                                   240'000, 264'000 });
    if (! paired)
        fixture.lanes.pairs->status = KIRIN_SPECTRUM_NO_PAIR;
    fixture.batch = bandBatchFor (fixture.lanes, band, preBand);
    varyHits (*fixture.batch, true);
    fixture.resum();
    return fixture;
}

// What the editor gives the view each tick: the hits, the band's outcome and its summary.
inline void showSummary (AttackComponent& component, const SummaryFixture& fixture)
{
    fixture.lanes.submit (component);
    component.bandEnvelopeSource = EnvelopeSource { fixture.batch.get() };
    component.setBand (fixture.batch->band);
    component.setBandSnapshot (*fixture.batch);
    component.setBandSummary (fixture.summary);
}

// Where a lane's value is read: its readout beside the number line, or its cell of the line.
inline juce::Rectangle<int> laneReadout (const attack_ui::Layout& layout, std::size_t lane)
{
    return rectangle (layout.arrangement == attack_ui::Arrangement::lanes
                          ? attack_ui::readoutCell (layout, layout.lanes[lane])
                          : attack_ui::lineCell (layout, lane));
}

// Where the summary is said in words: the card beside the panes, else the HISTORY row.
inline juce::Rectangle<int> readingArea (const attack_ui::Layout& layout)
{
    if (attack_band::panesShown (layout))
        return rectangle (layout.loupe ? attack_ui::loupeArea (layout)
                                       : attack_ui::readoutCell (layout, layout.history));
    return rectangle (layout.history);
}

inline bool writeBandPreview (const char* label, const char* name, const juce::Image& image)
{
    const auto* previews = std::getenv ("KIRIN_ATTACK_UI_BAND_PREVIEW_DIR");
    if (previews == nullptr)
        return true;
    juce::FileOutputStream output { juce::File { previews }.getChildFile (juce::String (label) + name + ".png") };
    return output.openedOk() && juce::PNGImageFormat().writeImageToStream (image, output);
}

// The band view at the five editor sizes. LIVE: the chips from 125%, the average panes from 200%,
// the summary in words, and number lines of the summed hits with their medians; nothing summed says
// why. A locked hit: its own values, its dot ringed, a withheld value saying why. END returns to
// the same summary, and ALL exactly to the whole-signal view.
inline bool verifyBandRendering()
{
    for (const auto& preset : observatory::sizePresets)
    {
        auto scene = presetScene (preset);
        auto& component = *scene.component;
        const auto& layout = scene.layout;
        const auto context = presentation::forEditor (preset.width, preset.height);
        const auto fail = [&preset] (const char* what) {
            std::cerr << what << " at " << preset.label << '\n';
            return false;
        };
        auto fixture = summaryFixture (4);
        fixture.lanes.submit (component);
        component.presentationTickAt (juce::Time::getMillisecondCounterHiRes() + 1'000.0);
        const auto plain = renderAttack (component);
        showSummary (component, fixture);
        const auto live = renderAttack (component);
        // Nothing summed: no hit kept PRE's band.
        auto unkept = summaryFixture (4);
        for (std::uint32_t item = 0; item < unkept.batch->count; ++item)
            unkept.batch->hits[item].pre = sideIn (KIRIN_ATTACK_BAND_SIDE_NOT_KEPT);
        unkept.resum();
        showSummary (component, unkept);
        const auto waiting = renderAttack (component);
        showSummary (component, fixture);
        // The newest hit but one, locked; then the same hit with its start hidden by a ring-out.
        component.keyPressed (juce::KeyPress (juce::KeyPress::leftKey));
        const auto locked = renderAttack (component);
        auto hidden = summaryFixture (4);
        hidden.batch->hits[8].pre.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_RINGING;
        hidden.resum();
        showSummary (component, hidden);
        const auto reasons = renderAttack (component);
        component.keyPressed (juce::KeyPress (juce::KeyPress::endKey));
        showSummary (component, fixture);
        const auto back = renderAttack (component);
        for (const auto& [name, image] : { std::pair { "_summary", &live }, std::pair { "_waiting", &waiting },
                                           std::pair { "_locked", &locked }, std::pair { "_reasons", &reasons } })
            if (! writeBandPreview (preset.label, name, *image))
                return false;
        if (fixture.summary.count != 8 || fixture.summary.left_out != 1 || unkept.summary.count != 0)
            return fail ("the fixture's summary");
        if (differences (live, back) != 0)
            return fail ("END does not return to the same summary");
        if (differences (live, waiting, readingArea (layout)) == 0)
            return fail ("the summary says nothing different when nothing is summed");
        for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
            if (differences (live, waiting, laneReadout (layout, lane)) == 0)
                return fail ("a lane's median reads like nothing summed");
        if (differences (live, locked, laneReadout (layout, 3)) == 0)
            return fail ("a locked hit reads like the median");
        const auto mode = layout.arrangement == attack_ui::Arrangement::lanes
            ? rectangle (attack_ui::readoutCell (layout, layout.axis)) : rectangle (layout.axis);
        if (! mode.isEmpty() && differences (live, locked, mode) == 0)
            return fail ("NOW does not tell LIVE from a locked hit");
        if (differences (locked, reasons, laneReadout (layout, 0)) == 0)
            return fail ("a withheld DELAY reads like a value");
        const auto rows = attack_band_summary_painter::rowPlots (rectangle (layout.history), context);
        if (layout.arrangement == attack_ui::Arrangement::lanes || layout.arrangement == attack_ui::Arrangement::line)
            for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
            {
                const bool lanes = layout.arrangement == attack_ui::Arrangement::lanes;
                const auto plot = lanes ? rectangle (attack_ui::lanePlot (layout, lane)) : rows[lane];
                const auto colour = attack_lane_painter::colourFor (attack_lanes::bandLanes[lane]);
                if (countColour (live, plot, colour, 40) <= countColour (waiting, plot, colour, 40))
                    return fail ("a number line without its hits");
                // Locked, the lanes ring the hit's dot; at 125% the six seconds return in their place.
                if (differences (live, locked, plot) == 0)
                    return fail ("the locked hit's dot not ringed");
            }
        const bool compact = preset.density == observatory::Density::compact;
        if (attack_band::chipRow (layout, context).empty() != compact)
            return fail ("band chips");
        if (! compact)
        {
            // The choice moved from ALL to 500: both chips change, an unchosen one does not.
            const auto chosen = rectangle (attack_band::chipCell (layout, context, 4));
            const auto all = rectangle (attack_band::chipCell (layout, context, 0));
            const auto other = rectangle (attack_band::chipCell (layout, context, 2));
            if (differences (plain, live, chosen) == 0 || differences (plain, live, all) == 0
                || differences (plain, live, other) != 0)
                return fail ("chip choice not shown");
        }
        const bool panes = attack_band::panesShown (layout);
        if (panes != (preset.width >= 600))
            return false;
        for (const auto* image : { &live, &locked })
            for (const auto pane : { attack_band::headPane (layout), attack_band::tailPane (layout) })
                if (panes && (countColour (*image, rectangle (pane), juce::Colour (attack_ui::preTraceColour), 40) == 0
                              || countColour (*image, rectangle (pane), juce::Colour (attack_ui::waveformColour), 40) == 0))
                    return fail ("panes empty");
        if (layout.arrangement == attack_ui::Arrangement::lanes
            && countColour (live, rectangle (attack_ui::labelCell (layout, layout.lanes[0])),
                            juce::Colour (attack_ui::transientColour), 40) == 0)
            return fail ("band lanes unlabelled");
        if (! verifyThinSelection (live, layout) || ! verifyThinSelection (locked, layout))
            return false;
        component.setBand (0);
        if (differences (plain, renderAttack (component)) != 0)
            return fail ("ALL differs from before");
    }
    return true;
}

// Why a lane has no value, at every size while LIVE: DELAY without PRE says NO PAIR or UPDATE PRE,
// and a lane none of whose hits has a value says the reason most of them share. The other lanes
// read exactly alike.
inline bool verifySummaryReasons()
{
    const auto readouts = [] (const observatory::SizePreset& preset, const SummaryFixture& fixture) {
        auto scene = presetScene (preset);
        showSummary (*scene.component, fixture);
        return std::pair { renderAttack (*scene.component), scene.layout };
    };
    const auto onlyLaneDiffers = [] (const juce::Image& a, const juce::Image& b, const attack_ui::Layout& layout,
                                     std::size_t lane) {
        for (std::size_t other = 0; other < attack_ui::laneCount; ++other)
            if ((differences (a, b, laneReadout (layout, other)) != 0) != (other == lane))
                return false;
        return true;
    };
    auto older = summaryFixture (4, KIRIN_ATTACK_BAND_PRE_PREDATES);
    auto alone = summaryFixture (4, KIRIN_ATTACK_BAND_PRE_SAME, false);
    auto cut = summaryFixture (4);
    auto past = summaryFixture (4);
    for (std::uint32_t item = 0; item < cut.batch->count; ++item)
    {
        cut.batch->hits[item].post.release_state = KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT;
        past.batch->hits[item].post.release_state = KIRIN_ATTACK_BAND_RELEASE_AT_LEAST;
    }
    cut.resum();
    past.resum();
    if (cut.summary.lanes[2].withheld != KIRIN_ATTACK_BAND_HELD_NEXT_HIT
        || past.summary.lanes[2].withheld != KIRIN_ATTACK_BAND_HELD_LONG_TAIL)
        return false;
    for (const auto& preset : observatory::sizePresets)
    {
        const auto [olderImage, layout] = readouts (preset, older);
        if (! onlyLaneDiffers (olderImage, readouts (preset, alone).first, layout, 0))
        {
            std::cerr << "DELAY without PRE does not say why at " << preset.label << '\n';
            return false;
        }
        if (! onlyLaneDiffers (readouts (preset, cut).first, readouts (preset, past).first, layout, 2))
        {
            std::cerr << "a lane without values does not say why at " << preset.label << '\n';
            return false;
        }
    }
    return true;
}
}
