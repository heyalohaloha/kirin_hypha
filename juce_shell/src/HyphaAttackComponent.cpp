#include "HyphaAttackComponent.h"

#include "HyphaAttackPainter.h"
#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha
{
namespace
{
const auto waveformColour = juce::Colour (attack_ui::waveformColour);
const auto strengthColour = juce::Colour (attack_ui::strengthColour);
const auto brightnessColour = juce::Colour (attack_ui::brightnessColour);
const auto transientColour = juce::Colour (attack_ui::transientColour);
const auto textureColour = juce::Colour (attack_ui::textureColour);
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

using attack_painter::drawWaveform;
using attack_painter::drawWaveformDifferences;
using attack_painter::WaveformStyle;

void AttackComponent::setOverlayMode (bool shouldOverlay)
{
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
    const auto eased = linear * linear * (3.0 - 2.0 * linear);
    const auto distance = presentationTargetLatest - presentationStartLatest;
    latest = presentationStartLatest + static_cast<std::int64_t> (
        static_cast<long double> (distance) * eased);
}

void AttackComponent::presentationTick (bool signalActive)
{
    liveSignalActive = signalActive;
    if (! signalActive)
    {
        latest = presentationTargetLatest;
        presentationStartLatest = presentationTargetLatest;
        presentationStartMs = juce::Time::getMillisecondCounterHiRes();
        if (followLatest)
            selectBoundaryEvent (true);
        repaint();
        return;
    }
    presentationTickAt (juce::Time::getMillisecondCounterHiRes());
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

void AttackComponent::setSnapshot (const KirinAttackEventBatch& events,
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
    bounds.removeFromBottom (attack_ui::metricsHeight (getHeight()));
    return bounds.removeFromTop (attack_ui::timelineHeight (getHeight()));
}

juce::Rectangle<int> AttackComponent::scrubBounds() const noexcept
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (attack_ui::headerHeight + attack_ui::timelineHeight (getHeight()));
    bounds.removeFromBottom (attack_ui::metricsHeight (getHeight()));
    return bounds.removeFromTop (attack_ui::axisLabelHeight);
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
    if (getHeight() < 145)
    {
        if (bounds.getHeight() >= 78)
        {
            g.setColour (COL_MUTED); g.setFont (monoFont (11.0f));
            g.drawText (followLatest ? "DRUM / latest event" : "DRUM / locked event",
                        bounds.removeFromTop (18), juce::Justification::centredLeft);
        }
        g.setColour (juce::Colours::black); g.fillRect (bounds);
        if (running && (liveSignalActive || ! followLatest)) paintSelectedEvent (g, bounds);
        return;
    }
    auto header = bounds.removeFromTop (attack_ui::headerHeight);
    auto metrics = bounds.removeFromBottom (attack_ui::metricsHeight (getHeight()));
    auto timeline = bounds.removeFromTop (attack_ui::timelineHeight (getHeight()));
    auto scrub = bounds.removeFromTop (attack_ui::axisLabelHeight);
    const bool paired = pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE;
    if (! metrics.isEmpty())
    {
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (metrics.reduced (1).toFloat(), 4.0f);
    }

    auto titleRow = header.removeFromTop (20);
    auto viewButton = titleRow.removeFromRight (attack_ui::modeControlWidth (getWidth()));
    g.setFont (monoFont (9.2f * textScale));
    g.setColour (COL_NORMAL);
    g.drawText ("DRUM / ATTACK", titleRow, juce::Justification::centredLeft);
    g.setColour (waveformColour.withAlpha (0.10f));
    g.fillRoundedRectangle (viewButton.reduced (1).toFloat(), 3.0f);
    g.setColour (COL_NORMAL);
    g.setFont (monoFont (7.2f * textScale));
    g.drawText (overlayMode ? "VIEW  2 ROWS" : "VIEW  OVERLAY",
                viewButton, juce::Justification::centred);

    auto state = getWidth() >= 470 ? header.removeFromRight (84) : juce::Rectangle<int> {};
    const auto legendWidth = header.getWidth() / 4;
    const auto compact = getWidth() < 430;
    const auto legend = [&] (juce::Colour colour, const juce::String& text, bool last)
    {
        g.setColour (colour);
        g.setFont (monoFont (6.5f * textScale));
        g.drawText (text, last ? header : header.removeFromLeft (legendWidth),
                    juce::Justification::centredLeft);
    };
    legend (strengthColour, compact ? "CORE" : "CORE  STRENGTH", false);
    legend (textureColour, compact ? "FIBRE" : "FIBRE  TEXTURE", false);
    legend (brightnessColour, compact ? "MEMBRANE" : "MEMBRANE  BRIGHT", false);
    legend (transientColour, compact ? "TAIL" : "TAIL  TRANSIENT", true);
    if (! state.isEmpty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (6.5f * textScale));
        g.drawText (juce::String (paired ? "PAIR / " : "POST / ")
                        + (followLatest ? "LIVE" : "LOCK"),
                    state, juce::Justification::centredRight);
    }

    if (! liveSignalActive && followLatest)
    {
        g.setColour (juce::Colours::black);
        g.fillRect (getLocalBounds().withTrimmedTop (attack_ui::headerHeight));
        return;
    }
    if (! running || ! attack_ui::validTimeline (latest, rate))
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (8.0f * textScale));
        g.drawText (runtimeStats.available == 0 ? "UNAVAILABLE" : "WARMING UP",
                    bounds, juce::Justification::centred);
        return;
    }

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
        drawWaveform (g, preWaveformBatch, preDetailBatch, preLane.reduced (0, 4),
                      first, latest, rate, WaveformStyle::continuous, true, 0.90f);
        drawWaveform (g, waveformBatch, detailBatch, postLane.reduced (0, 4),
                      first, latest, rate, WaveformStyle::continuous, true, 0.90f);
        g.setColour (waveformColour.withAlpha (0.075f));
        g.drawHorizontalLine (preLane.getBottom(), static_cast<float> (preLane.getX() + 3),
                              static_cast<float> (preLane.getRight() - 3));
        g.setColour (COL_MUTED);
        g.setFont (monoFont (6.4f * textScale));
        g.drawText ("PRE", preLane.reduced (5, 1), juce::Justification::topLeft);
        g.drawText ("POST", postLane.reduced (5, 1), juce::Justification::topLeft);
    }
    else if (paired)
    {
        const auto waveArea = timeline.reduced (0, 7);
        drawWaveform (g, preWaveformBatch, preDetailBatch, waveArea,
                      first, latest, rate, WaveformStyle::trace, false, 0.64f);
        drawWaveform (g, waveformBatch, detailBatch, waveArea,
                      first, latest, rate, WaveformStyle::continuous, false, 0.94f);
        drawWaveformDifferences (g, preDetailBatch, detailBatch,
                                 pairEventBatch, waveArea, first, latest, rate);
    }
    else
    {
        drawWaveform (g, waveformBatch, detailBatch, timeline.reduced (0, 7),
                      first, latest, rate, WaveformStyle::continuous, true, 0.94f);
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

    const auto railY = scrub.getCentreY() - 2;
    g.setColour (waveformColour.withAlpha (0.28f));
    g.drawHorizontalLine (railY, static_cast<float> (scrub.getX() + 35),
                          static_cast<float> (scrub.getRight() - 35));
    g.setFont (monoFont (6.8f * textScale));
    g.setColour (COL_MUTED);
    g.drawText ("-6 s", scrub.removeFromLeft (35), juce::Justification::centredLeft);
    g.setColour (followLatest ? selectionColour : COL_MUTED);
    g.drawText ("NOW", scrub.removeFromRight (35), juce::Justification::centredRight);
    g.setColour (COL_NORMAL);
    g.drawText (juce::String (visibleCount)
                    + (followLatest ? " EVENTS  /  LIVE" : " EVENTS  /  LOCK"),
                scrub, juce::Justification::centred);

    paintSelectedEvent (g, metrics);
}
}
