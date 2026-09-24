#include "HyphaAttackComponent.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "HyphaAttackLanePainter.h"
#include "HyphaAttackLoupePainter.h"
#include "HyphaAttackPainter.h"
#include "HyphaAttackSnapshotEquality.h"
#include "HyphaAttackStage.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

namespace hypha
{
namespace
{
const auto waveformColour = juce::Colour (attack_ui::waveformColour);
const auto selectionColour = juce::Colour (attack_ui::selectionColour);
constexpr auto visualization = typography::Composition::visualization;
}

using attack_painter::drawEnvelope;
using attack_painter::WaveformStyle;

void AttackComponent::setOverlayMode (bool shouldOverlay)
{
    if (overlayMode == shouldOverlay) return;
    overlayMode = shouldOverlay;
    repaint();
}

void AttackComponent::advancePresentation (double nowMs) noexcept
{
    if (presentationStartLatest < 0 || presentationTargetLatest < 0)
        return;
    constexpr double durationMs = 1'000.0 / attack_ui::presentationHz;
    const auto linear = juce::jlimit (
        0.0, 1.0, (nowMs - presentationStartMs) / durationMs);
    const auto distance = presentationTargetLatest - presentationStartLatest;
    latest = presentationStartLatest + static_cast<std::int64_t> (
        static_cast<long double> (distance) * linear); // Time must not ease in/out every 100 ms.
}

void AttackComponent::presentationTick (bool signalActive)
{
    const bool stateChanged = liveSignalActive != signalActive;
    liveSignalActive = signalActive;
    if (! signalActive)
    {
        const auto previousLatest = latest;
        const auto previousSelection = selectedEventSample;
        latest = presentationTargetLatest;
        presentationStartLatest = presentationTargetLatest;
        presentationStartMs = juce::Time::getMillisecondCounterHiRes();
        if (followLatest)
            selectBoundaryEvent (true);
        if (stateChanged || latest != previousLatest || selectedEventSample != previousSelection)
            repaint();
        return;
    }
    presentationTickAt (juce::Time::getMillisecondCounterHiRes());
    if (stateChanged) repaint();
}

void AttackComponent::presentationTickAt (double nowMs)
{
    const auto previousLatest = latest;
    const auto previousSelection = selectedEventSample;
    advancePresentation (nowMs);
    if (followLatest)
        selectBoundaryEvent (true);
    if (latest != previousLatest || selectedEventSample != previousSelection)
        repaint();
}

bool AttackComponent::setSnapshot (const KirinAttackEventBatch& events,
                                   const KirinAttackWaveformBatch& waveform,
                                   const KirinAttackDetailBatch& details,
                                   const KirinAttackWaveformBatch& preWaveform,
                                   const KirinAttackDetailBatch& preDetails,
                                   const KirinAttackPairEventBatch& pairEvents,
                                   std::int64_t latestSample,
                                   std::uint32_t sampleRate,
                                   std::uint64_t generation,
                                   const KirinAttackStats& stats)
{
    const auto current = [generation, sampleRate] (const auto& item) {
        return item.generation == generation && item.sample_rate == sampleRate; };
    const auto any = [] (const auto&) { return true; };
    const auto paired = [generation, sampleRate] (const auto& p) {
        return p.sample_rate == sampleRate && (p.post_available == 0 || p.post_generation == generation); };
    const auto incomingPairCount = juce::jmin (pairEvents.count,
        static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
    const auto validPairs = std::count_if (pairEvents.events, pairEvents.events + incomingPairCount, paired);
    const auto pairStatus = incomingPairCount != 0 && validPairs == 0
        ? KIRIN_SPECTRUM_WARMING_UP : pairEvents.status;
    using attack_equality::retained;
    if (generation == currentGeneration && sampleRate == rate && latestSample == presentationTargetLatest
        && attack_equality::same (runtimeStats, stats) && pairEventBatch.status == pairStatus
        && retained (eventBatch.events, eventBatch.count, events.events, events.count, current)
        && retained (waveformBatch.points, waveformBatch.count, waveform.points, waveform.count, current)
        && retained (detailBatch.details, detailBatch.count, details.details, details.count, current)
        && retained (preWaveformBatch.points, preWaveformBatch.count, preWaveform.points, preWaveform.count, any)
        && retained (preDetailBatch.details, preDetailBatch.count, preDetails.details, preDetails.count, any)
        && retained (pairEventBatch.events, pairEventBatch.count, pairEvents.events, pairEvents.count, paired))
        return false;
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const bool resetPresentation = currentGeneration == 0 || generation != currentGeneration
                                || sampleRate != rate || latestSample < presentationTargetLatest;
    if (currentGeneration != 0 && generation != currentGeneration)
        followLatest = true;
    eventBatch = events;
    waveformBatch = waveform;
    detailBatch = details;
    preWaveformBatch = preWaveform;
    preDetailBatch = preDetails;
    pairEventBatch = pairEvents;
    // UI polls can straddle a worker restart. Never paint a prior generation at a reused sample.
    const auto retainCurrent = [generation, sampleRate] (auto& batch, auto& items)
    {
        const auto count = juce::jmin (batch.count, static_cast<std::uint32_t> (std::size (items)));
        batch.count = static_cast<std::uint32_t> (std::remove_if (items, items + count,
            [generation, sampleRate] (const auto& item)
            { return item.generation != generation || item.sample_rate != sampleRate; }) - items);
    };
    retainCurrent (eventBatch, eventBatch.events);
    retainCurrent (waveformBatch, waveformBatch.points);
    retainCurrent (detailBatch, detailBatch.details);
    const auto pairCount = juce::jmin (pairEventBatch.count,
        static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
    auto* pairs = pairEventBatch.events;
    pairEventBatch.count = static_cast<std::uint32_t> (std::remove_if (pairs, pairs + pairCount,
        [generation, sampleRate] (const auto& pair) { return pair.sample_rate != sampleRate
            || (pair.post_available != 0 && pair.post_generation != generation); }) - pairs);
    if (pairCount != 0 && pairEventBatch.count == 0)
        pairEventBatch.status = KIRIN_SPECTRUM_WARMING_UP;
    attack_lanes::build (laneModel, pairEventBatch, detailBatch, preDetailBatch,
                         generation, sampleRate);
    runtimeStats = stats;
    if (resetPresentation)
    {
        latest = latestSample;
        presentationStartLatest = latestSample;
        presentationTargetLatest = latestSample;
        presentationStartMs = nowMs;
    }
    else if (latestSample != presentationTargetLatest)
    {
        advancePresentation (nowMs);
        presentationStartLatest = latest;
        presentationTargetLatest = latestSample;
        presentationStartMs = nowMs;
    }
    rate = sampleRate;
    currentGeneration = generation;
    if (followLatest)
    {
        selectedEventSample = -1;
        selectBoundaryEvent (true);
    }
    repaint();
    return true;
}

void AttackComponent::clearSnapshot()
{
    eventBatch = {};
    waveformBatch = {};
    detailBatch = {};
    preWaveformBatch = {};
    preDetailBatch = {};
    pairEventBatch = {};
    runtimeStats = {};
    laneModel.count = 0;
    laneModel.delta = false;
    latest = -1;
    presentationStartLatest = -1;
    presentationTargetLatest = -1;
    presentationStartMs = 0.0;
    rate = 0;
    currentGeneration = 0;
    selectedEventSample = -1;
    followLatest = true;
    repaint();
}

attack_ui::Layout AttackComponent::layout() const noexcept
{
    return attack_ui::layoutFor (getWidth(), getHeight(), presentationContext);
}

// Every row shares one horizontal plot column, so a hit has the same x in HISTORY and each lane.
juce::Rectangle<int> AttackComponent::plotColumn (attack_ui::Box row) const noexcept
{
    const auto shape = layout();
    auto column = rectangleOf (row);
    if (shape.arrangement == attack_ui::Arrangement::lanes)
    {
        column.removeFromLeft (shape.labelWidth);
        column.removeFromRight (shape.readoutWidth);
    }
    return column.reduced (1, 0);
}

juce::Rectangle<int> AttackComponent::historyPlotBounds() const noexcept
{
    const auto shape = layout();
    return shape.history.empty() ? juce::Rectangle<int> {}
                                 : plotColumn (shape.history).reduced (0, 1);
}

juce::Rectangle<int> AttackComponent::axisPlotBounds() const noexcept
{
    const auto shape = layout();
    return shape.axis.empty() ? juce::Rectangle<int> {} : plotColumn (shape.axis);
}

int AttackComponent::viewControlWidth() const
{
    const auto style = typography::resolve (
        presentationContext, typography::TextRole::action, visualization);
    const auto font = monoFont (presentationContext, typography::TextRole::action, visualization);
    const auto overlayWidth = text_style::requiredWidth (font, "VIEW  OVERLAY", style);
    const auto rowsWidth = text_style::requiredWidth (font, "VIEW  2 ROWS", style);
    return attack_ui::modeControlWidth (getWidth(), juce::jmax (overlayWidth, rowsWidth));
}

int AttackComponent::statusControlWidth() const
{
    const auto style = typography::resolve (
        presentationContext, typography::TextRole::status, visualization);
    const auto font = monoFont (presentationContext, typography::TextRole::status, visualization);
    return attack_ui::statusControlWidth (
        getWidth(), text_style::requiredWidth (font, "PAIR / HOLD", style));
}

const KirinAttackPairEvent* AttackComponent::selectedPairEvent() const noexcept
{
    if (! pairedObservation()) return nullptr;
    const auto count = juce::jmin (
        pairEventBatch.count,
        static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        if (pairEventBatch.events[index].event_sample == selectedEventSample)
            return &pairEventBatch.events[index];
    return nullptr;
}

const KirinAttackDetail* AttackComponent::selectedPostDetail() const noexcept
{
    if (const auto* pair = selectedPairEvent(); pair != nullptr && pair->post_available != 0)
        return attack_lanes::findDetail (detailBatch, pair->post_event_sample,
                                         pair->post_generation, pair->sample_rate);
    if (selectedPairEvent() != nullptr) return nullptr;
    return attack_lanes::findDetail (detailBatch, selectedEventSample, currentGeneration, rate);
}

const KirinAttackDetail* AttackComponent::selectedPreDetail() const noexcept
{
    if (const auto* pair = selectedPairEvent(); pair != nullptr && pair->pre_available != 0)
        return attack_lanes::findDetail (preDetailBatch, pair->pre_event_sample,
                                         pair->pre_generation, pair->sample_rate);
    return nullptr;
}

// Observations only: the cached chrome already holds the stage, grid, rows and labels.
void AttackComponent::paintHistory (juce::Graphics& g, juce::Rectangle<int> plot)
{
    const auto first = latest - attack_ui::windowSamples (rate);
    const bool paired = pairedObservation();
    if (paired && ! overlayMode)
    {
        const auto laneHeight = plot.getHeight() / 2;
        auto preLane = plot.removeFromTop (laneHeight);
        auto postLane = plot.removeFromTop (laneHeight);
        drawEnvelope (g, preWaveformBatch, preLane.reduced (0, 4),
                      first, latest, rate, WaveformStyle::continuous, 0.90f);
        drawEnvelope (g, waveformBatch, postLane.reduced (0, 4),
                      first, latest, rate, WaveformStyle::continuous, 0.90f);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (attack_stage::trackedFont (presentationContext, typography::TextRole::legend,
                                              attack_stage::captionTracking (presentationContext)));
        g.drawText ("PRE", preLane.reduced (5, 1), juce::Justification::topLeft);
        g.drawText ("POST", postLane.reduced (5, 1), juce::Justification::topLeft);
    }
    else if (paired)
    {
        const auto waveArea = plot.reduced (0, 7);
        drawEnvelope (g, preWaveformBatch, waveArea,
                      first, latest, rate, WaveformStyle::trace, 0.64f);
        drawEnvelope (g, waveformBatch, waveArea,
                      first, latest, rate, WaveformStyle::continuous, 0.94f);
    }
    else
    {
        drawEnvelope (g, waveformBatch, plot.reduced (0, 7),
                      first, latest, rate, WaveformStyle::continuous, 0.94f);
    }
}

void AttackComponent::paintAxis (juce::Graphics& g, juce::Rectangle<int> axis)
{
    std::uint32_t visibleCount = 0;
    const auto countVisible = [&] (std::int64_t sample)
    {
        if (attack_ui::eventIsVisible (sample, latest, rate))
            ++visibleCount;
    };
    if (pairedObservation())
        for (std::uint32_t index = 0; index < juce::jmin (pairEventBatch.count,
                 static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY)); ++index)
            countVisible (pairEventBatch.events[index].event_sample);
    else
        for (std::uint32_t index = 0; index < juce::jmin (eventBatch.count,
                 static_cast<std::uint32_t> (KIRIN_ATTACK_EVENT_BATCH_CAPACITY)); ++index)
            countVisible (eventBatch.events[index].event_sample);

    const auto labelWidth = juce::jmin (35, axis.getWidth() / 5);
    axis.removeFromLeft (labelWidth); // "-6 s" and the rail are cached chrome.
    g.setFont (monoFont (presentationContext, typography::TextRole::axis, visualization));
    g.setColour (followLatest ? selectionColour : COL_TEXT_TERTIARY);
    g.drawText ("NOW", axis.removeFromRight (labelWidth), juce::Justification::centredRight);
    g.setColour (COL_NORMAL);
    const auto mode = followLatest ? juce::String ("LIVE") : juce::String ("LOCK");
    const auto noun = visibleCount == 1 ? juce::String (" EVENT") : juce::String (" EVENTS");
    attack_lane_painter::drawFitting (g, { juce::String (visibleCount) + noun + "  /  " + mode,
                                           juce::String (visibleCount) + " / " + mode },
                                      axis, presentationContext, typography::TextRole::axis,
                                      juce::Justification::centred);
}

void AttackComponent::paintSelection (juce::Graphics& g, const attack_ui::Layout& shape)
{
    const auto history = historyPlotBounds();
    const auto x = attack_ui::eventX (selectedEventSample, latest, rate, history.getWidth());
    if (history.isEmpty() || x < 0)
        return;
    const bool lanes = shape.arrangement == attack_ui::Arrangement::lanes;
    const auto bottom = lanes ? shape.lanes.back().bottom() - 3 : history.getBottom() - 2;
    attack_lane_painter::paintHypha (
        g, static_cast<float> (history.getX() + x) + 0.5f, static_cast<float> (history.getY() + 2),
        static_cast<float> (bottom), static_cast<float> (history.getCentreY()),
        selectedEventSample);
}

void AttackComponent::paint (juce::Graphics& g)
{
    const auto shape = layout();
    const bool running = runtimeStats.available != 0 && runtimeStats.enabled != 0
                      && runtimeStats.worker_running != 0;
    const bool dormant = ! running || ! attack_ui::validTimeline (latest, rate);
    paintChrome (g, shape, dormant);
    paintHeaderState (g, shape);
    if (shape.arrangement == attack_ui::Arrangement::header)
        return;
    if (dormant)
    {
        // Lanes stay empty until data is valid; the state is stated once, inside HISTORY.
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentationContext, typography::TextRole::status, visualization));
        g.drawText (runtimeStats.available == 0 ? "UNAVAILABLE" : "WARMING UP",
                    shape.history.empty() ? getLocalBounds().withTrimmedTop (shape.header.height)
                                          : rectangleOf (shape.history),
                    juce::Justification::centred);
        return;
    }
    const attack_lane_painter::Frame frame { laneModel, latest, rate, selectedEventSample,
                                             presentationContext };
    const auto history = historyPlotBounds();
    if (! history.isEmpty())
        paintHistory (g, history);
    if (shape.arrangement == attack_ui::Arrangement::lanes)
    {
        const auto readout = rectangleOf (shape.history).removeFromRight (shape.readoutWidth);
        if (shape.loupe)
            attack_loupe::paint (g, readout.reduced (2, 1), selectedPreDetail(),
                                 selectedPostDetail(), presentationContext);
        else
            attack_lane_painter::paintSelectedTime (g, readout, frame);
        for (std::size_t index = 0; index < attack_ui::laneCount; ++index)
            attack_lane_painter::paintLaneValues (
                g, attack_lanes::lanes[index], plotColumn (shape.lanes[index]).reduced (0, 1),
                rectangleOf (shape.lanes[index]).removeFromRight (shape.readoutWidth), frame);
    }
    else
    {
        attack_lane_painter::paintLine (g, rectangleOf (shape.line), frame);
    }
    paintSelection (g, shape);
    if (! shape.axis.empty())
        paintAxis (g, axisPlotBounds());
}
}
