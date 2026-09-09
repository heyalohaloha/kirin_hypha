#include "HyphaAttackComponent.h"

#include "HyphaAttackPainter.h"
#include "HyphaAttackUiContract.h"
#include "HyphaAttackSnapshotEquality.h"
#include "HyphaTheme.h"

namespace hypha
{
namespace
{
const auto waveformColour = juce::Colour (attack_ui::waveformColour);
const auto selectionColour = juce::Colour (attack_ui::selectionColour);
const auto panelColour = juce::Colour (0xff0d1620);

const KirinAttackDetail* findDetail (const KirinAttackDetailBatch& batch,
                                     std::int64_t eventSample, std::uint64_t generation,
                                     std::uint32_t sampleRate) noexcept
{
    const auto count = juce::jmin (
        batch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        if (batch.details[index].event_sample == eventSample
            && batch.details[index].generation == generation
            && batch.details[index].sample_rate == sampleRate)
            return &batch.details[index];
    return nullptr;
}

void drawSelectionArc (juce::Graphics& g, int x, juce::Rectangle<int> timeline)
{
    g.setColour (selectionColour);
    g.fillEllipse (static_cast<float> (x - 2), static_cast<float> (timeline.getBottom() - 5),
                   4.0f, 4.0f);
}
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

juce::Rectangle<int> AttackComponent::timelineBounds() const noexcept
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (attack_ui::headerHeight);
    return bounds.removeFromTop (attack_ui::timelineHeight (getHeight()));
}

juce::Rectangle<int> AttackComponent::scrubBounds() const noexcept
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (attack_ui::headerHeight + attack_ui::timelineHeight (getHeight()));
    return bounds.removeFromTop (attack_ui::axisHeight (getHeight()));
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

bool AttackComponent::pairHasPostDetail (const KirinAttackPairEvent& pair) const noexcept
{
    return pair.post_available != 0
        && findDetail (detailBatch, pair.post_event_sample,
                       pair.post_generation, pair.sample_rate) != nullptr;
}

const KirinAttackDetail* AttackComponent::selectedPostDetail() const noexcept
{
    if (const auto* pair = selectedPairEvent(); pair != nullptr && pair->post_available != 0)
        return findDetail (detailBatch, pair->post_event_sample, pair->post_generation, pair->sample_rate);
    if (selectedPairEvent() != nullptr) return nullptr;
    return findDetail (detailBatch, selectedEventSample, currentGeneration, rate);
}

const KirinAttackDetail* AttackComponent::selectedPreDetail() const noexcept
{
    if (const auto* pair = selectedPairEvent(); pair != nullptr && pair->pre_available != 0)
        return findDetail (preDetailBatch, pair->pre_event_sample, pair->pre_generation, pair->sample_rate);
    return nullptr;
}

void AttackComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto textScale = attack_ui::textScale (getWidth(), getHeight());
    // ATTACK owns the Observatory body while selected. Keep the body opaque so the HISTORY
    // labels beneath this child cannot leak into its transparent header or capture composite.
    g.setColour (BG);
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    const bool running = runtimeStats.available != 0 && runtimeStats.enabled != 0
                      && runtimeStats.worker_running != 0;
    auto header = bounds.removeFromTop (attack_ui::headerHeight);
    auto timeline = bounds.removeFromTop (attack_ui::timelineHeight (getHeight()));
    auto scrub = bounds.removeFromTop (attack_ui::axisHeight (getHeight()));
    auto transient = bounds.removeFromTop (attack_ui::transientHeight (getHeight()));
    auto metrics = bounds;
    const bool paired = pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE;
    for (auto area : { transient, metrics })
    {
        if (area.isEmpty()) continue;
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (area.reduced (1).toFloat(), 4.0f);
    }

    auto titleRow = header.removeFromTop (20);
    auto viewButton = titleRow.removeFromRight (attack_ui::modeControlWidth (getWidth()));
    g.setFont (monoFont (juce::jmax (12.0f, 9.2f * textScale)));
    g.setColour (COL_NORMAL);
    g.drawText (getWidth() < 430 ? "DRUM / ATTACK" : "DRUM / ATTACK SPECIMEN",
                titleRow, juce::Justification::centredLeft);
    g.setColour (waveformColour.withAlpha (0.10f));
    g.fillRoundedRectangle (viewButton.reduced (1).toFloat(), 3.0f);
    g.setColour (COL_NORMAL);
    g.setFont (monoFont (juce::jmax (11.0f, 7.2f * textScale)));
    g.drawText (overlayMode ? "VIEW  2 ROWS" : "VIEW  OVERLAY",
                viewButton, juce::Justification::centred);

    auto state = getWidth() >= 470 ? header.removeFromRight (84) : juce::Rectangle<int> {};
    g.setColour (COL_MUTED); g.setFont (monoFont (11.0f));
    g.drawText (timeline.isEmpty() ? "POST FACTS"
                : getWidth() < 470 ? "RMS / 6 S"
                : getWidth() >= 700 ? "10 ms RMS / 6 S   PRE trace / POST body"
                                    : "10 ms RMS / 6 S / -72..0 dBFS",
                header, juce::Justification::centredLeft);
    if (! state.isEmpty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (11.0f));
        g.drawText (juce::String (paired ? "PAIR / " : "POST / ")
                        + (followLatest ? (liveSignalActive ? "LIVE" : "HOLD") : "LOCK"),
                    state, juce::Justification::centredRight);
    }
    if (! running || ! attack_ui::validTimeline (latest, rate))
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (juce::jmax (11.0f, 8.0f * textScale)));
        g.drawText (runtimeStats.available == 0 ? "UNAVAILABLE" : "WARMING UP",
                    timeline.isEmpty() ? getLocalBounds().withTrimmedTop (attack_ui::headerHeight)
                                       : timeline,
                    juce::Justification::centred);
        return;
    }

    if (! timeline.isEmpty())
    {
        timeline = timeline.reduced (1);
        g.setColour (panelColour.withAlpha (0.94f));
        g.fillRoundedRectangle (timeline.toFloat(), 4.0f);
        g.setColour (waveformColour.withAlpha (0.075f));
        g.drawRoundedRectangle (timeline.toFloat(), 4.0f, 0.7f);
        for (int second = 1; second < attack_ui::presentationSeconds; ++second)
        {
            const auto x = timeline.getX() + second * timeline.getWidth()
                         / attack_ui::presentationSeconds;
            g.setColour (waveformColour.withAlpha (second == 3 ? 0.10f : 0.035f));
            g.drawVerticalLine (x, static_cast<float> (timeline.getY() + 4),
                                static_cast<float> (timeline.getBottom() - 4));
        }
        const auto first = latest - attack_ui::windowSamples (rate);
        if (paired && ! overlayMode)
        {
            const auto laneHeight = timeline.getHeight() / 2;
            auto preLane = timeline.removeFromTop (laneHeight);
            auto postLane = timeline.removeFromTop (laneHeight);
            drawEnvelope (g, preWaveformBatch, preLane.reduced (0, 4),
                          first, latest, rate, WaveformStyle::continuous, 0.90f);
            drawEnvelope (g, waveformBatch, postLane.reduced (0, 4),
                          first, latest, rate, WaveformStyle::continuous, 0.90f);
            g.setColour (waveformColour.withAlpha (0.075f));
            g.drawHorizontalLine (preLane.getBottom(), static_cast<float> (preLane.getX() + 3),
                                  static_cast<float> (preLane.getRight() - 3));
            g.setColour (COL_MUTED);
            g.setFont (monoFont (11.0f));
            g.drawText ("PRE", preLane.reduced (5, 1), juce::Justification::topLeft);
            g.drawText ("POST", postLane.reduced (5, 1), juce::Justification::topLeft);
        }
        else if (paired)
        {
            const auto waveArea = timeline.reduced (0, 7);
            drawEnvelope (g, preWaveformBatch, waveArea,
                          first, latest, rate, WaveformStyle::trace, 0.64f);
            drawEnvelope (g, waveformBatch, waveArea,
                          first, latest, rate, WaveformStyle::continuous, 0.94f);
        }
        else
        {
            drawEnvelope (g, waveformBatch, timeline.reduced (0, 7),
                          first, latest, rate, WaveformStyle::continuous, 0.94f);
        }
    }

    std::uint32_t visibleCount = 0;
    const auto countVisible = [&] (std::int64_t sample)
    {
        if (attack_ui::eventIsVisible (sample, latest, rate))
            ++visibleCount;
    };
    if (paired)
    {
        const auto count = juce::jmin (
            pairEventBatch.count,
            static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            countVisible (pairEventBatch.events[index].event_sample);
    }
    else
    {
        const auto count = juce::jmin (
            eventBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_EVENT_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            countVisible (eventBatch.events[index].event_sample);
    }

    const auto markerArea = timelineBounds().reduced (1);
    const auto selectedX = attack_ui::eventX (
        selectedEventSample, latest, rate, markerArea.getWidth());
    if (! metrics.isEmpty() && selectedX >= 0)
        drawSelectionArc (g, markerArea.getX() + selectedX, markerArea);

    if (! scrub.isEmpty())
    {
        const auto railY = scrub.getCentreY() - 2;
        g.setColour (waveformColour.withAlpha (0.28f));
        g.drawHorizontalLine (railY, static_cast<float> (scrub.getX() + 35),
                              static_cast<float> (scrub.getRight() - 35));
        g.setFont (monoFont (11.0f));
        g.setColour (COL_MUTED);
        g.drawText ("-6 s", scrub.removeFromLeft (35), juce::Justification::centredLeft);
        g.setColour (followLatest ? selectionColour : COL_MUTED);
        g.drawText ("NOW", scrub.removeFromRight (35), juce::Justification::centredRight);
        g.setColour (COL_NORMAL);
        g.drawText (juce::String (visibleCount)
                        + (followLatest ? " EVENTS  /  LIVE" : " EVENTS  /  LOCK"),
                    scrub, juce::Justification::centred);
    }

    paintTransientComparison (g, transient);
    paintSelectedEvent (g, metrics);
}
}
