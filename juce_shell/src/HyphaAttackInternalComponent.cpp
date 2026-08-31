#include "HyphaAttackInternalComponent.h"

#include <array>

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
const auto panelColour = juce::Colour (0xff111722);

std::array<juce::Rectangle<int>, 4> metricAreas (juce::Rectangle<int> area, bool grid)
{
    if (grid)
    {
        auto top = area.removeFromTop (area.getHeight() / 2);
        auto bottom = area;
        auto strength = top.removeFromLeft (top.getWidth() / 2);
        auto texture = top;
        auto brightness = bottom.removeFromLeft (bottom.getWidth() / 2);
        return { strength, texture, brightness, bottom };
    }
    auto strength = area.removeFromLeft (area.getWidth() / 4);
    auto texture = area.removeFromLeft (area.getWidth() / 3);
    auto brightness = area.removeFromLeft (area.getWidth() / 2);
    return { strength, texture, brightness, area };
}

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

}

using attack_painter::drawMetricCard;
using attack_painter::drawEventFocus;
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
    auto header = bounds.removeFromTop (attack_ui::headerHeight);
    auto metrics = bounds.removeFromBottom (attack_ui::metricsHeight (getHeight()));
    auto timeline = bounds.removeFromTop (attack_ui::timelineHeight (getHeight()));
    auto scrub = bounds.removeFromTop (attack_ui::axisLabelHeight);

    auto titleRow = header.removeFromTop (17);
    auto viewButton = titleRow.removeFromRight (attack_ui::modeControlWidth (getWidth()));
    g.setFont (monoFont (9.4f));
    g.setColour (COL_NORMAL);
    const auto paired = pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE;
    g.drawText ("ATTACK  /  TRANSIENT FLOW", titleRow, juce::Justification::centredLeft);
    g.setColour (waveformColour.withAlpha (0.11f));
    g.fillRoundedRectangle (viewButton.reduced (1, 1).toFloat(), 3.0f);
    g.setColour (COL_NORMAL);
    g.setFont (monoFont (7.4f));
    g.drawText (overlayMode ? "VIEW  2 ROWS" : "VIEW  OVERLAY",
                viewButton, juce::Justification::centred);
    g.setFont (monoFont (6.8f));
    auto legend = header;
    auto state = getWidth() >= 500 ? legend.removeFromRight (92)
                                   : juce::Rectangle<int> {};
    const bool compactLegend = getWidth() < 430;
    const auto legendWidth = legend.getWidth() / 4;
    const auto drawLegend = [&] (juce::Colour colour, const juce::String& text, bool last)
    {
        g.setColour (colour);
        g.drawText (text, last ? legend : legend.removeFromLeft (legendWidth),
                    juce::Justification::centredLeft);
    };
    drawLegend (strengthColour, compactLegend ? "CORE" : "CORE STRENGTH", false);
    drawLegend (textureColour, compactLegend ? "FIELD" : "FIELD TEXTURE", false);
    drawLegend (brightnessColour, compactLegend ? "SHELL" : "SHELL BRIGHT", false);
    drawLegend (transientColour, compactLegend ? "AURA" : "AURA TRANSIENT", true);
    if (! state.isEmpty())
    {
        g.setColour (COL_MUTED);
        g.drawText (juce::String (paired ? "DRUM / " : "POST / ")
                        + (followLatest ? "LIVE" : "LOCK"),
                    state, juce::Justification::centredRight);
    }

    const bool running = runtimeStats.available != 0 && runtimeStats.enabled != 0
                      && runtimeStats.worker_running != 0;
    if (! running || ! attack_ui::validTimeline (latest, rate))
    {
        g.setColour (COL_MUTED);
        g.drawText (runtimeStats.available == 0 ? "UNAVAILABLE" : "WARMING UP",
                    bounds, juce::Justification::centred);
        return;
    }

    timeline = timeline.reduced (1, 1);
    g.setColour (panelColour.withAlpha (0.88f));
    g.fillRoundedRectangle (timeline.toFloat(), 3.5f);
    g.setColour (waveformColour.withAlpha (0.07f));
    for (int second = 1; second < attack_ui::presentationSeconds; ++second)
    {
        const auto x = timeline.getX() + second * timeline.getWidth()
                     / attack_ui::presentationSeconds;
        g.drawVerticalLine (x, static_cast<float> (timeline.getY() + 2),
                            static_cast<float> (timeline.getBottom() - 2));
    }
    const auto first = latest - attack_ui::windowSamples (rate);
    if (paired && ! overlayMode)
    {
        const auto laneHeight = timeline.getHeight() / 2;
        auto preLane = timeline.removeFromTop (laneHeight);
        auto postLane = timeline.removeFromTop (laneHeight);
        drawWaveform (g, preWaveformBatch, preDetailBatch, preLane.reduced (0, 5),
                      first, latest, rate, WaveformStyle::continuous, true, 0.90f);
        drawWaveform (g, waveformBatch, detailBatch, postLane.reduced (0, 5),
                      first, latest, rate, WaveformStyle::continuous, true, 0.90f);
        g.setColour (waveformColour.withAlpha (0.10f));
        g.drawHorizontalLine (preLane.getBottom(), static_cast<float> (preLane.getX()),
                              static_cast<float> (preLane.getRight()));
        g.setFont (monoFont (7.3f));
        g.setColour (COL_MUTED);
        g.drawText ("PRE  SAME SCALE", preLane.reduced (5, 2), juce::Justification::topLeft);
        g.drawText ("POST  SAME SCALE", postLane.reduced (5, 2), juce::Justification::topLeft);
    }
    else if (paired)
    {
        const auto waveArea = timeline.reduced (0, 8);
        drawWaveform (g, preWaveformBatch, preDetailBatch, waveArea,
                      first, latest, rate, WaveformStyle::trace, false, 0.72f);
        drawWaveform (g, waveformBatch, detailBatch, waveArea,
                      first, latest, rate, WaveformStyle::continuous, false, 0.92f);
        drawWaveformDifferences (g, preDetailBatch, detailBatch,
                                 pairEventBatch, waveArea, first, latest, rate);
        g.setFont (monoFont (7.3f));
        g.setColour (COL_MUTED);
        g.drawText ("PRE PULSE  /  POST OUTLINE  /  COLOUR = DIFFERENCE",
                    timeline.reduced (5, 2), juce::Justification::topLeft);
    }
    else
    {
        drawWaveform (g, waveformBatch, detailBatch, timeline.reduced (0, 8),
                      first, latest, rate, WaveformStyle::continuous, true, 0.92f);
        g.setFont (monoFont (7.3f));
        g.setColour (COL_MUTED);
        g.drawText ("POST ABSOLUTE", timeline.reduced (5, 2), juce::Justification::topLeft);
    }

    std::uint32_t visibleCount = 0;
    const auto markerArea = timelineBounds();
    if (paired)
    {
        const auto count = juce::jmin (
            pairEventBatch.count,
            static_cast<std::uint32_t> (KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const auto sample = pairEventBatch.events[index].event_sample;
            const auto x = attack_ui::eventX (sample, latest, rate, markerArea.getWidth());
            if (x < 0)
                continue;
            ++visibleCount;
            if (sample != selectedEventSample)
            {
                g.setColour (COL_MUTED.withAlpha (0.30f));
                g.drawVerticalLine (markerArea.getX() + x,
                                    static_cast<float> (markerArea.getY() + 2),
                                    static_cast<float> (markerArea.getBottom() - 2));
            }
        }
    }
    else
    {
        const auto count = juce::jmin (
            eventBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_EVENT_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const auto& attack = eventBatch.events[index];
            const auto x = attack_ui::eventX (
                attack.event_sample, latest, rate, markerArea.getWidth());
            if (x < 0)
                continue;
            ++visibleCount;
            if (attack.event_sample != selectedEventSample)
            {
                g.setColour (COL_MUTED.withAlpha (0.30f));
                g.drawVerticalLine (markerArea.getX() + x,
                                    static_cast<float> (markerArea.getY() + 2),
                                    static_cast<float> (markerArea.getBottom() - 2));
            }
        }
    }

    const auto* preDetail = selectedPreDetail();
    const auto* postDetail = selectedPostDetail();
    const auto selectedX = attack_ui::eventX (
        selectedEventSample, latest, rate, markerArea.getWidth());
    if (selectedX >= 0)
    {
        const auto x = markerArea.getX() + selectedX;
        g.setColour (selectionColour.withAlpha (0.10f));
        g.fillRect (x - 5, markerArea.getY(), 11, markerArea.getHeight());
        g.setColour (selectionColour.withAlpha (0.95f));
        g.fillRect (x - 1, markerArea.getY(), 2, markerArea.getHeight());
        if (preDetail != nullptr && postDetail != nullptr)
        {
            auto badge = juce::Rectangle<int> (x - 22, markerArea.getY() + 3, 44, 13);
            badge.setX (juce::jlimit (markerArea.getX(), markerArea.getRight() - badge.getWidth(),
                                     badge.getX()));
            g.setColour (panelColour.withAlpha (0.94f));
            g.fillRoundedRectangle (badge.toFloat(), 2.0f);
            g.setColour (transientColour);
            g.setFont (monoFont (7.5f));
            g.drawText (signedValue (postDetail->contrast_db - preDetail->contrast_db)
                            + " dB", badge, juce::Justification::centred);
        }
    }

    g.setColour (COL_MUTED.withAlpha (0.55f));
    const auto railY = scrub.getCentreY() - 2;
    g.fillRoundedRectangle (2.0f, static_cast<float> (railY),
                            static_cast<float> (juce::jmax (1, scrub.getWidth() - 4)), 2.0f, 1.0f);
    if (selectedX >= 0)
    {
        g.setColour (selectionColour.withAlpha (0.22f));
        g.fillEllipse (static_cast<float> (selectedX - 5), static_cast<float> (railY - 5),
                       11.0f, 11.0f);
        g.setColour (selectionColour);
        g.fillEllipse (static_cast<float> (selectedX - 2), static_cast<float> (railY - 2),
                       5.0f, 5.0f);
    }
    g.setFont (monoFont (7.2f));
    g.setColour (COL_MUTED);
    g.drawText ("-6 s", scrub.removeFromLeft (32), juce::Justification::bottomLeft);
    g.setColour (followLatest ? selectionColour : COL_MUTED);
    g.drawText ("NOW", scrub.removeFromRight (32), juce::Justification::bottomRight);
    g.setColour (COL_NORMAL);
    g.drawText (juce::String (visibleCount)
                    + (followLatest ? " EVENTS  /  LIVE FOLLOW"
                                    : " EVENTS  /  LOCK  /  DRAG TO SCRUB  /  END = LIVE"),
                scrub, juce::Justification::centredBottom);

    const bool showFocus = postDetail != nullptr
                        && metrics.getHeight() >= 50 && metrics.getWidth() >= 400;
    if (showFocus)
    {
        auto focus = metrics.removeFromLeft (metrics.getWidth() * 2 / 5).reduced (1, 1);
        g.setColour (panelColour.withAlpha (0.92f));
        g.fillRoundedRectangle (focus.toFloat(), 2.5f);
        auto focusHeader = focus.removeFromTop (12).reduced (4, 0);
        g.setColour (COL_MUTED);
        g.setFont (monoFont (6.8f));
        const auto beforeMs = postDetail->sample_rate > 0
            ? (postDetail->event_sample - postDetail->shape_start_sample) * 1'000
                / static_cast<std::int64_t> (postDetail->sample_rate) : 0;
        const auto afterMs = postDetail->sample_rate > 0
            ? (postDetail->shape_end_sample - postDetail->event_sample) * 1'000
                / static_cast<std::int64_t> (postDetail->sample_rate) : 0;
        g.drawText ("EVENT SHAPE  -" + juce::String (beforeMs)
                        + " / +" + juce::String (afterMs) + " ms",
                    focusHeader, juce::Justification::centredLeft);
        drawEventFocus (g, preDetail, postDetail, focus.reduced (4, 1));
    }
    const auto cardAreas = metricAreas (metrics, showFocus || metrics.getHeight() >= 38);

    if (postDetail != nullptr && preDetail != nullptr)
    {
        const auto strength = postDetail->attack_rms_dbfs - preDetail->attack_rms_dbfs;
        const auto edge = postDetail->sample_edge_ratio_db - preDetail->sample_edge_ratio_db;
        const auto crest = postDetail->crest_db - preDetail->crest_db;
        const auto plateau = postDetail->peak_plateau_ms - preDetail->peak_plateau_ms;
        const bool hasBrightness = preDetail->sharpness_available != 0
                                && postDetail->sharpness_available != 0;
        const auto brightness = postDetail->sharpness_acum - preDetail->sharpness_acum;
        const auto transient = postDetail->contrast_db - preDetail->contrast_db;
        const bool texturePattern = edge > 0.0f && crest < 0.0f && plateau > 0.0f;
        drawMetricCard (g, cardAreas[0], "STRENGTH",
                        "PRE " + juce::String (preDetail->attack_rms_dbfs, 1)
                            + "  POST " + juce::String (postDetail->attack_rms_dbfs, 1),
                        "ATTACK RMS  D " + signedValue (strength) + " dB", strengthColour);
        drawMetricCard (g, cardAreas[1], "TEXTURE",
                        "EDGE " + signedValue (edge) + "  CREST " + signedValue (crest),
                        "PLATEAU  D " + signedValue (plateau, 2) + " ms",
                        textureColour, texturePattern);
        drawMetricCard (g, cardAreas[2], "BRIGHTNESS",
                        hasBrightness
                            ? "PRE " + juce::String (preDetail->sharpness_acum, 2)
                                + "  POST " + juce::String (postDetail->sharpness_acum, 2)
                            : "---",
                        "SHARPNESS  D " + signedValue (brightness, 2),
                        brightnessColour, hasBrightness);
        drawMetricCard (g, cardAreas[3], "TRANSIENT",
                        "PRE " + juce::String (preDetail->contrast_db, 1)
                            + "  POST " + juce::String (postDetail->contrast_db, 1),
                        "CONTRAST  D " + signedValue (transient) + " dB", transientColour);
    }
    else if (postDetail != nullptr)
    {
        drawMetricCard (g, cardAreas[0], "STRENGTH",
                        juce::String (postDetail->attack_rms_dbfs, 1) + " dBFS",
                        "ATTACK RMS", strengthColour);
        drawMetricCard (g, cardAreas[1], "TEXTURE",
                        "EDGE " + juce::String (postDetail->sample_edge_ratio_db, 1)
                            + "  CREST " + juce::String (postDetail->crest_db, 1),
                        "PLATEAU " + juce::String (postDetail->peak_plateau_ms, 2) + " ms",
                        textureColour);
        drawMetricCard (g, cardAreas[2], "BRIGHTNESS",
                        postDetail->sharpness_available != 0
                            ? juce::String (postDetail->sharpness_acum, 2) + " acum" : "---",
                        "SHARPNESS", brightnessColour,
                        postDetail->sharpness_available != 0);
        drawMetricCard (g, cardAreas[3], "TRANSIENT",
                        juce::String (postDetail->contrast_db, 1) + " dB",
                        "LOCAL CONTRAST", transientColour);
    }
}
}
