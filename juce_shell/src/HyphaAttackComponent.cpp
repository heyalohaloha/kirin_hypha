#include "HyphaAttackInternalComponent.h"

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

juce::String signedValue (float value, int decimals = 1)
{
    return (value >= 0.0f ? "+" : "") + juce::String (value, decimals);
}

const KirinAttackDetail* findDetail (const KirinAttackDetailBatch& batch,
                                     std::int64_t eventSample) noexcept
{
    const auto count = juce::jmin (
        batch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        if (batch.details[index].event_sample == eventSample)
            return &batch.details[index];
    return nullptr;
}

void drawSelectionArc (juce::Graphics& g, int x, juce::Rectangle<int> timeline)
{
    const auto y = static_cast<float> (timeline.getBottom() - 2);
    juce::Path arc;
    arc.startNewSubPath (static_cast<float> (x - 13), y);
    arc.cubicTo (static_cast<float> (x - 7), y,
                 static_cast<float> (x - 7), y - 7.0f,
                 static_cast<float> (x), y - 7.0f);
    arc.cubicTo (static_cast<float> (x + 7), y - 7.0f,
                 static_cast<float> (x + 7), y,
                 static_cast<float> (x + 13), y);
    g.setColour (selectionColour);
    g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}
}

using attack_painter::drawEventFocus;
using attack_painter::drawMetricFact;
using attack_painter::drawWaveform;
using attack_painter::drawWaveformDifferences;
using attack_painter::WaveformStyle;

void AttackInternalComponent::setOverlayMode (bool shouldOverlay)
{
    overlayMode = shouldOverlay;
    repaint();
}

void AttackInternalComponent::advancePresentation (double nowMs) noexcept
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

void AttackInternalComponent::presentationTick (bool signalActive)
{
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

void AttackInternalComponent::presentationTickAt (double nowMs)
{
    advancePresentation (nowMs);
    if (followLatest)
        selectBoundaryEvent (true);
    repaint();
}

void AttackInternalComponent::setSnapshot (const KirinAttackEventBatch& events,
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
    else if (pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE && selectedPairEvent() == nullptr)
    {
        const auto count = juce::jmin (
            pairEventBatch.count,
            static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
        selectedEventSample = count > 0 ? pairEventBatch.events[count - 1].event_sample : -1;
    }
    else if (pairEventBatch.status != KIRIN_SPECTRUM_ACTIVE
             && selectedPostDetail() == nullptr)
    {
        const auto count = juce::jmin (
            detailBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
        selectedEventSample = count > 0 ? detailBatch.details[count - 1].event_sample : -1;
    }
    repaint();
}

void AttackInternalComponent::clearSnapshot()
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

juce::Rectangle<int> AttackInternalComponent::timelineBounds() const noexcept
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (attack_ui::headerHeight);
    bounds.removeFromBottom (attack_ui::metricsHeight (getHeight()));
    return bounds.removeFromTop (attack_ui::timelineHeight (getHeight()));
}

juce::Rectangle<int> AttackInternalComponent::scrubBounds() const noexcept
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (attack_ui::headerHeight + attack_ui::timelineHeight (getHeight()));
    bounds.removeFromBottom (attack_ui::metricsHeight (getHeight()));
    return bounds.removeFromTop (attack_ui::axisLabelHeight);
}

const KirinAttackPairEvent* AttackInternalComponent::selectedPairEvent() const noexcept
{
    const auto count = juce::jmin (
        pairEventBatch.count,
        static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        if (pairEventBatch.events[index].event_sample == selectedEventSample)
            return &pairEventBatch.events[index];
    return nullptr;
}

const KirinAttackDetail* AttackInternalComponent::selectedPostDetail() const noexcept
{
    if (const auto* pair = selectedPairEvent(); pair != nullptr && pair->post_available != 0)
        return findDetail (detailBatch, pair->post_event_sample);
    return findDetail (detailBatch, selectedEventSample);
}

const KirinAttackDetail* AttackInternalComponent::selectedPreDetail() const noexcept
{
    if (const auto* pair = selectedPairEvent(); pair != nullptr && pair->pre_available != 0)
        return findDetail (preDetailBatch, pair->pre_event_sample);
    return nullptr;
}

void AttackInternalComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    // ATTACK owns the Observatory body while selected. Keep the body opaque so the HISTORY
    // labels beneath this child cannot leak into its transparent header or capture composite.
    g.setColour (BG);
    g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
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

    auto titleRow = header.removeFromTop (16);
    auto viewButton = titleRow.removeFromRight (attack_ui::modeControlWidth (getWidth()));
    g.setFont (monoFont (9.2f));
    g.setColour (COL_NORMAL);
    g.drawText ("ATTACK  /  EVENT MATTER", titleRow, juce::Justification::centredLeft);
    g.setColour (waveformColour.withAlpha (0.10f));
    g.fillRoundedRectangle (viewButton.reduced (1).toFloat(), 3.0f);
    g.setColour (COL_NORMAL);
    g.setFont (monoFont (7.2f));
    g.drawText (overlayMode ? "VIEW  2 ROWS" : "VIEW  OVERLAY",
                viewButton, juce::Justification::centred);

    auto state = getWidth() >= 470 ? header.removeFromRight (84) : juce::Rectangle<int> {};
    const auto legendWidth = header.getWidth() / 4;
    const auto compact = getWidth() < 430;
    const auto legend = [&] (juce::Colour colour, const juce::String& text, bool last)
    {
        g.setColour (colour);
        g.setFont (monoFont (6.5f));
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
        g.setFont (monoFont (6.5f));
        g.drawText (juce::String (paired ? "DRUM / " : "POST / ")
                        + (followLatest ? "LIVE" : "LOCK"),
                    state, juce::Justification::centredRight);
    }

    const bool running = runtimeStats.available != 0 && runtimeStats.enabled != 0
                      && runtimeStats.worker_running != 0;
    if (! running || ! attack_ui::validTimeline (latest, rate))
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (8.0f));
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
        g.setFont (monoFont (6.4f));
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
    if (selectedX >= 0)
        drawSelectionArc (g, markerArea.getX() + selectedX, markerArea);

    const auto railY = scrub.getCentreY() - 2;
    g.setColour (waveformColour.withAlpha (0.28f));
    g.drawHorizontalLine (railY, static_cast<float> (scrub.getX() + 35),
                          static_cast<float> (scrub.getRight() - 35));
    g.setFont (monoFont (6.8f));
    g.setColour (COL_MUTED);
    g.drawText ("-6 s", scrub.removeFromLeft (35), juce::Justification::centredLeft);
    g.setColour (followLatest ? selectionColour : COL_MUTED);
    g.drawText ("NOW", scrub.removeFromRight (35), juce::Justification::centredRight);
    g.setColour (COL_NORMAL);
    g.drawText (juce::String (visibleCount)
                    + (followLatest ? " EVENTS  /  LIVE" : " EVENTS  /  LOCK"),
                scrub, juce::Justification::centred);

    const auto* preDetail = selectedPreDetail();
    const auto* postDetail = selectedPostDetail();
    if (metrics.isEmpty() || postDetail == nullptr)
        return;

    metrics = metrics.reduced (1);
    g.setColour (selectionColour.withAlpha (0.16f));
    g.drawRoundedRectangle (metrics.toFloat(), 4.0f, 0.75f);

    auto content = metrics.reduced (7, 3);
    auto focusHeader = content.removeFromTop (12);
    g.setColour (COL_MUTED);
    g.setFont (monoFont (6.5f));
    const auto beforeMs = postDetail->sample_rate > 0
        ? (postDetail->event_sample - postDetail->shape_start_sample) * 1'000
            / static_cast<std::int64_t> (postDetail->sample_rate) : 0;
    const auto afterMs = postDetail->sample_rate > 0
        ? (postDetail->shape_end_sample - postDetail->event_sample) * 1'000
            / static_cast<std::int64_t> (postDetail->sample_rate) : 0;
    g.drawText ("SELECTED EVENT  /  -" + juce::String (beforeMs)
                    + "  +" + juce::String (afterMs) + " ms  /  "
                    + (preDetail != nullptr ? "POST - PRE" : "POST ABSOLUTE"),
                focusHeader, juce::Justification::centred);

    const auto pairedDetail = preDetail != nullptr;
    const auto strengthValue = pairedDetail
        ? signedValue (postDetail->attack_rms_dbfs - preDetail->attack_rms_dbfs) + " dB"
        : juce::String (postDetail->attack_rms_dbfs, 1) + " dBFS";
    const auto strengthContext = pairedDetail
        ? "PRE " + juce::String (preDetail->attack_rms_dbfs, 1)
            + "  POST " + juce::String (postDetail->attack_rms_dbfs, 1)
        : "30 ms ATTACK RMS";
    const auto edge = pairedDetail
        ? postDetail->sample_edge_ratio_db - preDetail->sample_edge_ratio_db
        : postDetail->sample_edge_ratio_db;
    const auto crest = pairedDetail ? postDetail->crest_db - preDetail->crest_db
                                    : postDetail->crest_db;
    const auto plateau = pairedDetail
        ? postDetail->peak_plateau_ms - preDetail->peak_plateau_ms
        : postDetail->peak_plateau_ms;
    const auto textureValue = (pairedDetail ? signedValue (edge) : juce::String (edge, 1))
                            + " dB";
    const auto textureContext = "CREST " + (pairedDetail ? signedValue (crest)
                                                        : juce::String (crest, 1))
                              + "  PLAT " + (pairedDetail ? signedValue (plateau, 2)
                                                          : juce::String (plateau, 2));
    const bool brightnessAvailable = postDetail->sharpness_available != 0
        && (! pairedDetail || preDetail->sharpness_available != 0);
    const auto brightnessValue = brightnessAvailable
        ? (pairedDetail ? signedValue (postDetail->sharpness_acum - preDetail->sharpness_acum, 2)
                        : juce::String (postDetail->sharpness_acum, 2)) + " acum"
        : "---";
    const auto brightnessContext = pairedDetail ? "SHARPNESS DIFFERENCE" : "100 ms SHARPNESS";
    const auto transientValue = pairedDetail
        ? signedValue (postDetail->contrast_db - preDetail->contrast_db) + " dB"
        : juce::String (postDetail->contrast_db, 1) + " dB";
    const auto transientContext = pairedDetail ? "CONTRAST DIFFERENCE" : "LOCAL CONTRAST";

    if (content.getWidth() >= 390 && content.getHeight() >= 65)
    {
        const auto sideWidth = juce::jmin (112, content.getWidth() / 4);
        auto left = content.removeFromLeft (sideWidth);
        auto right = content.removeFromRight (sideWidth);
        auto specimen = content.reduced (4, 1);
        drawEventFocus (g, preDetail, postDetail, specimen);
        auto leftTop = left.removeFromTop (left.getHeight() / 2).reduced (1);
        auto leftBottom = left.reduced (1);
        auto rightTop = right.removeFromTop (right.getHeight() / 2).reduced (1);
        auto rightBottom = right.reduced (1);
        drawMetricFact (g, leftTop, "STRENGTH", strengthValue, strengthContext,
                  strengthColour, false);
        drawMetricFact (g, leftBottom, "BRIGHTNESS", brightnessValue, brightnessContext,
                  brightnessColour, false);
        drawMetricFact (g, rightTop, "TEXTURE", textureValue, textureContext,
                  textureColour, true);
        drawMetricFact (g, rightBottom, "TRANSIENT", transientValue, transientContext,
                  transientColour, true);
    }
    else
    {
        drawEventFocus (g, preDetail, postDetail, content.reduced (2, 1));
        const auto width = content.getWidth() / 4;
        auto strength = content.removeFromLeft (width);
        auto brightness = content.removeFromLeft (width);
        auto texture = content.removeFromLeft (width);
        drawMetricFact (g, strength, "STRENGTH", strengthValue, {}, strengthColour, false);
        drawMetricFact (g, brightness, "BRIGHT", brightnessValue, {}, brightnessColour, false);
        drawMetricFact (g, texture, "TEXTURE", textureValue, {}, textureColour, true);
        drawMetricFact (g, content, "TRANSIENT", transientValue, {}, transientColour, true);
    }
}
}
