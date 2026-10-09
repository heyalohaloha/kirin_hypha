#include "HyphaAttackComponent.h"

#include "HyphaAttackBandModel.h"
#include "HyphaAttackBandPainter.h"
#include "HyphaAttackBandSummaryPainter.h"
#include "HyphaAttackDepth.h"
#include "HyphaAttackLanePainter.h"
#include "HyphaAttackLoupePainter.h"
#include "HyphaAttackSnapshotEquality.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// DRUM's band state (B-1097, B-1098, 2026-09-29): the chosen band, the engine's outcome for the
// lanes' own hits, its summary of the recent hits that rise in the band, and the selected hit's
// envelopes. Nothing here measures or guesses; it keeps what the engine stated aligned with the
// hits DRUM already shows. While LIVE the view reads the summary; a locked hit reads itself.
namespace hypha
{
void AttackComponent::setBand (std::uint8_t band)
{
    if (band > attack_band::bandCount || band == chosenBand) return;
    chosenBand = band;
    v2->changeBand (band);
    if (v2->enabled)
    {
        evidenceV2.reset();
        if (onBandChange) onBandChange (band);
        repaint(); return;
    }
    rebuildBandModel();
    if (followLatest)
        selectBoundaryEvent (true);
    if (onBandChange) onBandChange (band);
    refreshBandEnvelope();
    repaint();
}

attack_band::PreBand AttackComponent::preBand() const noexcept
{
    return pairedObservation() && chosenBand != 0 ? attack_band::preBandOf (bandBatch, chosenBand)
                                                  : attack_band::PreBand::off;
}

void AttackComponent::rebuildBandModel() noexcept
{
    attack_band::build (bandModel, laneModel, bandBatch, chosenBand, currentGeneration, rate);
}

bool AttackComponent::setBandSnapshot (const KirinAttackBandBatch& batch)
{
    const auto any = [] (const auto&) { return true; };
    if (bandBatch.status == batch.status && bandBatch.band == batch.band
        && bandBatch.pre_band == batch.pre_band && bandBatch.generation == batch.generation
        && bandBatch.sample_rate == batch.sample_rate
        && bandBatch.resolution_micros == batch.resolution_micros
        && attack_equality::retained (bandBatch.hits, bandBatch.count, batch.hits, batch.count, any))
        return false;
    bandBatch = batch;
    rebuildBandModel();
    if (followLatest)
        selectBoundaryEvent (true);
    refreshBandEnvelope();
    repaint();
    return true;
}

const attack_lanes::Hit* AttackComponent::bandSelection (const attack_lanes::Hit* selected) const noexcept
{
    return selected != nullptr && chosenBand != 0 ? attack_lanes::find (bandModel, selected->sample)
                                                  : nullptr;
}

const KirinAttackBandHitEnvelope* AttackComponent::selectedBandEnvelope (
    const attack_lanes::Hit* selected) const noexcept
{
    return selected != nullptr && bandEnvelopeValid && bandEnvelope.band == chosenBand
                && bandEnvelope.hit.event_sample == selected->sample
        ? &bandEnvelope : nullptr;
}

// Only while a locked hit's panes are shown: at 150% and below nothing here is drawn, and while
// LIVE the panes show the summary's average, so nothing is asked.
void AttackComponent::refreshBandEnvelope()
{
    const auto* selected = visibleSelection();
    if (chosenBand == 0 || summaryShown() || selected == nullptr || bandEnvelopeSource == nullptr
        || ! attack_band::panesShown (layout()))
    {
        bandEnvelopeValid = false;
        return;
    }
    KirinAttackBandHitEnvelope next {};
    bandEnvelopeValid = bandEnvelopeSource (selected->sample, next)
                     && next.band == chosenBand && next.hit.event_sample == selected->sample;
    if (bandEnvelopeValid)
        bandEnvelope = next;
}

int AttackComponent::followRank (std::uint32_t item) const noexcept
{
    const auto& hit = laneModel.hits[item];
    if (! hit.selectable || ! attack_ui::eventIsVisible (hit.sample, latest, rate))
        return -1;
    if (chosenBand == 0 || item >= bandModel.count)
        return 2;
    // With a band: a hit with its band stated first; while none is, the hit still measuring
    // (MEASURING) before one that was never measured (PLAY TO MEASURE).
    switch (attack_band::readiness (bandModel.hits[item]))
    {
        case attack_band::Readiness::stated:      return 2;
        case attack_band::Readiness::pending:     return 1;
        case attack_band::Readiness::notMeasured: return 0;
    }
    return 0;
}

bool AttackComponent::bandNeedsPlay() const noexcept
{
    if (chosenBand == 0)
        return false;
    for (std::uint32_t item = 0; item < bandModel.count; ++item)
    {
        const auto& hit = bandModel.hits[item];
        if (attack_ui::eventIsVisible (hit.sample, latest, rate)
            && attack_band::readiness (hit) != attack_band::Readiness::notMeasured)
            return false;
    }
    return true;
}

bool AttackComponent::setBandSummary (const KirinAttackBandSummary& summary)
{
    if (attack_band_summary::same (bandSummary, summary))
        return false;
    bandSummary = summary;
    repaint();
    return true;
}

const KirinAttackBandSummary& AttackComponent::currentSummary() const noexcept
{
    static const KirinAttackBandSummary empty {};
    return chosenBand != 0 && bandSummary.band == chosenBand && bandSummary.generation == currentGeneration
                   && bandSummary.sample_rate == rate
        ? bandSummary : empty;
}

juce::String AttackComponent::bandWaiting() const
{
    return bandNeedsPlay() ? attack_band_painter::playText (chosenBand) : juce::String ("MEASURING");
}

juce::String AttackComponent::bandDelayReason() const
{
    return preBand() == attack_band::PreBand::predates ? "UPDATE PRE" : "NO PAIR";
}

int AttackComponent::summaryIndexOf (std::int64_t sample) const noexcept
{
    const auto& summary = currentSummary();
    for (std::uint32_t index = 0; index < summary.count && index < KIRIN_ATTACK_BAND_SUMMARY_HITS; ++index)
        if (summary.event_samples[index] == sample)
            return static_cast<int> (index);
    return -1;
}

std::array<juce::Rectangle<int>, attack_ui::laneCount> AttackComponent::summaryPlots (
    const attack_ui::Layout& shape) const
{
    std::array<juce::Rectangle<int>, attack_ui::laneCount> plots {};
    if (chosenBand == 0)
        return plots;
    if (shape.arrangement == attack_ui::Arrangement::lanes)
        for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
            plots[lane] = rectangleOf (attack_ui::lanePlot (shape, lane));
    else if (shape.arrangement == attack_ui::Arrangement::line && summaryShown())
        plots = attack_band_summary_painter::rowPlots (
            rectangleOf (attack_ui::historyWindow (shape)), presentationContext);
    return plots;
}

bool AttackComponent::selectSummaryDot (const attack_ui::Layout& shape, juce::Point<int> point)
{
    const auto plots = summaryPlots (shape);
    for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
    {
        const auto plot = plots[lane];
        if (! plot.contains (point))
            continue;
        const auto& summary = currentSummary();
        const auto dot = attack_band_summary_painter::dotAt (summary, lane, plot, point);
        const auto key = dot >= 0 ? summary.event_samples[static_cast<std::size_t> (dot)] : -1;
        if (dot < 0 || (! followLatest && key == selectedEventSample))
        {
            followLatest = true;
            selectBoundaryEvent (true);
        }
        else
        {
            followLatest = false;
            selectedEventSample = key;
        }
        refreshBandEnvelope();
        repaint();
        return true;
    }
    return false;
}

// The band view. While LIVE: the summary (average panes and a card at 200% and 300%, a reading
// at 150%, small number lines at 125%, the medians at 100%) over number lines of the summed hits. A locked hit: its
// panes, loupe or time, the six seconds at the smaller sizes, and its own values beside the same
// number lines, its dot ringed.
void AttackComponent::paintBand (juce::Graphics& g, const attack_ui::Layout& shape,
                                 const attack_lanes::Hit* selected)
{
    const auto& summary = currentSummary();
    const bool live = summaryShown();
    const auto* hit = bandSelection (selected);
    const attack_lane_painter::Frame frame { bandModel, latest, rate, hit, presentationContext,
                                             attack_lanes::bandLanes };
    const auto history = rectangleOf (attack_ui::historyPlot (shape));
    const auto name = attack_band_painter::nameText (chosenBand);
    const auto waiting = bandWaiting();
    const auto reason = bandDelayReason();
    const bool delta = bandModel.delta;
    const bool lanes = shape.arrangement == attack_ui::Arrangement::lanes;
    // The locked hit's place in the six seconds, where they are shown. At 100% the caption or the
    // summary's title says PLAY TO MEASURE in the same corner.
    const auto paintSixSeconds = [&] {
        paintHistory (g, history);
        if (bandNeedsPlay() && shape.arrangement != attack_ui::Arrangement::glance)
            attack_band_painter::paintPlayGuidance (g, history, presentationContext, chosenBand);
        const auto x = selected != nullptr ? attack_ui::eventX (selected->sample, latest, rate, history.getWidth()) : -1;
        if (x < 0)
            return;
        const auto lineX = static_cast<float> (history.getX() + x) + 0.5f;
        attack_depth::paintHalo (g, history, lineX, juce::Colour (attack_ui::selectionColour));
        attack_lane_painter::paintHypha (g, lineX, static_cast<float> (history.getY() + 2),
                                         static_cast<float> (history.getBottom() - 2),
                                         static_cast<float> (history.getCentreY()), selected->sample);
    };
    const auto readingRow = rectangleOf (attack_ui::historyWindow (shape));
    if (bandPanes (shape))
    {
        if (live)
            attack_band_painter::paintSummaryPanes (g, shape, presentationContext, summary, waiting,
                                                    twoRows() && delta);
        else
            attack_band_painter::paintPanes (g, shape, presentationContext,
                                             { selectedBandEnvelope (selected), selected != nullptr, bandNeedsPlay(),
                                               delta, twoRows() && delta, chosenBand });
        const auto right = rectangleOf (shape.loupe ? attack_ui::loupeArea (shape)
                                                    : attack_ui::readoutCell (shape, shape.history));
        if (live)
            attack_band_summary_painter::paintCard (g, right, summary, name, reason, waiting, presentationContext);
        else if (shape.loupe)
            attack_loupe::paint (g, right, selected != nullptr ? selectedPreDetail() : nullptr,
                                 selected != nullptr ? selectedPostDetail() : nullptr, presentationContext);
        else
            attack_lane_painter::paintSelectedTime (g, right, frame);
    }
    else if (lanes || shape.arrangement == attack_ui::Arrangement::line)
    {
        if (live && lanes && ! history.isEmpty())
            attack_band_summary_painter::paintReading (g, readingRow, summary, name, reason, waiting, presentationContext);
        else if (live && ! history.isEmpty())
            attack_band_summary_painter::paintRows (g, readingRow, summary,
                                                    static_cast<int> (summary.count) - 1, name, reason, waiting,
                                                    presentationContext);
        else if (! history.isEmpty())
        {
            paintSixSeconds();
            if (lanes)
                attack_lane_painter::paintSelectedTime (g, rectangleOf (attack_ui::readoutCell (shape, shape.history)), frame);
        }
    }
    if (lanes)
    {
        const auto ringed = live ? static_cast<int> (summary.count) - 1
                                 : summaryIndexOf (selected != nullptr ? selected->sample : -1);
        for (std::size_t lane = 0; lane < attack_ui::laneCount; ++lane)
        {
            attack_band_summary_painter::paintLaneValues (g, lane, rectangleOf (attack_ui::lanePlot (shape, lane)),
                                                          summary, ringed, presentationContext);
            const auto cell = rectangleOf (attack_ui::readoutCell (shape, shape.lanes[lane]));
            if (live)
                attack_band_summary_painter::paintReadout (g, lane, cell, summary, reason, presentationContext);
            else
                attack_band_summary_painter::paintHitReadout (g, attack_lanes::bandLanes[lane], cell, hit, delta,
                                                              presentationContext);
        }
        // The axis row is the number lines' scale; NOW (and, where the header has no room for it,
        // LIVE / HOLD / LOCK) stands beside it and returns to the summary.
        auto now = rectangleOf (attack_ui::readoutCell (shape, shape.axis)).reduced (8, 0);
        g.setFont (monoFont (presentationContext, typography::TextRole::axis, typography::Composition::visualization));
        g.setColour (followLatest ? juce::Colour (attack_ui::selectionColour) : COL_TEXT_TERTIARY);
        text_style::drawText (g, "NOW", now, juce::Justification::centredRight);
        if (getWidth() < 470)
        {
            g.setColour (COL_NORMAL);
            text_style::drawText (g, timeMode(), now, juce::Justification::centredLeft);
        }
        return;
    }
    if (shape.arrangement == attack_ui::Arrangement::glance)
    {
        paintSixSeconds();
        if (live)
            attack_band_summary_painter::paintGlance (g, shape, summary, name, reason, waiting, presentationContext);
        else
        {
            attack_lane_painter::paintGlance (g, shape, frame);
            attack_band_painter::paintGlanceCaption (g, history, presentationContext, chosenBand, bandNeedsPlay());
        }
        // Only a HISTORY that has stopped following the latest hit says so; LIVE is silent.
        if (timeMode() != "LIVE")
        {
            g.setColour (COL_TEXT_SECONDARY);
            g.setFont (monoFont (presentationContext, typography::TextRole::legend,
                                 typography::Composition::visualization));
            text_style::drawText (g, timeMode(), history.reduced (6, 3), juce::Justification::topRight);
        }
        return;
    }
    if (live)
    {
        // 125%: the six seconds give way to the small number lines; the axis row keeps the mode.
        attack_band_summary_painter::paintLine (g, shape, summary, reason, presentationContext);
        g.setFont (monoFont (presentationContext, typography::TextRole::axis, typography::Composition::visualization));
        g.setColour (juce::Colour (attack_ui::selectionColour));
        text_style::drawText (g, timeMode(), rectangleOf (attack_ui::axisPlot (shape)), juce::Justification::centredRight);
        return;
    }
    attack_lane_painter::paintLine (g, shape, frame);
    if (! shape.axis.empty())
        paintAxis (g, shape);
}
}
