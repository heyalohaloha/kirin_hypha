#include "HyphaAttackInternalComponent.h"
#include <cmath>

#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha
{
namespace
{
const auto strengthColour = juce::Colour (0xffe8b76a);
const auto brightnessColour = juce::Colour (0xff71d8ff);
const auto textureColour = juce::Colour (0xffff8c74);

float levelHeight (float db, float halfHeight)
{
    const auto normalized = juce::jlimit (
        0.0f, 1.0f, (db - attack_ui::absoluteFloorDb) / -attack_ui::absoluteFloorDb);
    return normalized * juce::jmax (0.0f, halfHeight - 1.0f);
}

float amplitudeHeight (float amplitude, float halfHeight)
{
    const auto db = 20.0f * std::log10 (juce::jmax (amplitude, 0.0002511886f));
    return levelHeight (db, halfHeight);
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

void drawWaveform (juce::Graphics& g,
                   const KirinAttackWaveformBatch& batch,
                   juce::Rectangle<int> area,
                   std::int64_t first,
                   std::int64_t latest,
                   std::uint32_t rate,
                   bool connected,
                   float alpha)
{
    const auto centreY = area.getCentreY();
    const auto halfHeight = area.getHeight() * 0.5f - 2.0f;
    juce::Path upper;
    juce::Path lower;
    bool started = false;
    const auto count = juce::jmin (
        batch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& point = batch.points[index];
        if (point.sample_rate != rate)
            continue;
        const auto sample = point.start_sample + (point.end_sample - point.start_sample) / 2;
        const auto localX = attack_ui::sampleX (sample, first, latest, area.getWidth());
        if (localX < 0)
            continue;
        const auto x = static_cast<float> (area.getX() + localX);
        const auto height = levelHeight (point.rms_dbfs, halfHeight);
        if (! connected || index % 2u == 0u)
        {
            g.setColour (dim (COL_NORMAL, alpha * 0.42f));
            g.drawVerticalLine (area.getX() + localX, centreY - height, centreY + height);
        }
        if (! connected)
            continue;
        if (! started)
        {
            upper.startNewSubPath (x, centreY - height);
            lower.startNewSubPath (x, centreY + height);
            started = true;
        }
        else
        {
            upper.lineTo (x, centreY - height);
            lower.lineTo (x, centreY + height);
        }
    }
    if (started)
    {
        g.setColour (dim (COL_NORMAL, alpha));
        g.strokePath (upper, juce::PathStrokeType (1.15f));
        g.strokePath (lower, juce::PathStrokeType (1.15f));
    }
}

void drawShape (juce::Graphics& g,
                const KirinAttackDetail& detail,
                juce::Rectangle<int> area,
                bool connected,
                float alpha)
{
    const auto centreY = area.getCentreY();
    const auto halfHeight = area.getHeight() * 0.5f - 2.0f;
    juce::Path upper;
    juce::Path lower;
    const auto count = juce::jmin (
        detail.shape_count, static_cast<std::uint32_t> (KIRIN_ATTACK_SHAPE_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto x = area.getX() + static_cast<float> (index) * area.getWidth()
                     / juce::jmax (1.0f, static_cast<float> (count - 1));
        const auto height = amplitudeHeight (detail.shape[index], halfHeight);
        if (! connected || index % 2u == 0u)
        {
            g.setColour (dim (COL_NORMAL, alpha * 0.46f));
            g.drawVerticalLine (juce::roundToInt (x), centreY - height, centreY + height);
        }
        if (! connected)
            continue;
        if (index == 0)
        {
            upper.startNewSubPath (x, centreY - height);
            lower.startNewSubPath (x, centreY + height);
        }
        else
        {
            upper.lineTo (x, centreY - height);
            lower.lineTo (x, centreY + height);
        }
    }
    if (connected && count > 0)
    {
        g.setColour (dim (COL_NORMAL, alpha));
        g.strokePath (upper, juce::PathStrokeType (1.35f));
        g.strokePath (lower, juce::PathStrokeType (1.35f));
    }
}

void drawOverlayAccents (juce::Graphics& g,
                         const KirinAttackDetail& pre,
                         const KirinAttackDetail& post,
                         juce::Rectangle<int> area)
{
    const auto strength = post.contrast_db - pre.contrast_db;
    const auto brightness = post.sharpness_acum - pre.sharpness_acum;
    const auto texture = post.sample_edge_ratio_db > pre.sample_edge_ratio_db
                      && post.crest_db < pre.crest_db
                      && post.peak_plateau_ms > pre.peak_plateau_ms;
    const auto count = juce::jmin (post.shape_count,
                                   static_cast<std::uint32_t> (KIRIN_ATTACK_SHAPE_CAPACITY));
    const auto centreY = static_cast<float> (area.getCentreY());
    const auto halfHeight = area.getHeight() * 0.5f - 2.0f;
    for (std::uint32_t index = 0; index < count; index += 4)
    {
        const auto x = area.getX() + static_cast<float> (index) * area.getWidth()
                     / juce::jmax (1.0f, static_cast<float> (count - 1));
        const auto height = amplitudeHeight (post.shape[index], halfHeight);
        if (std::abs (strength) >= 0.05f)
        {
            g.setColour (strengthColour);
            g.drawVerticalLine (juce::roundToInt (x), centreY - height, centreY + height);
        }
        if (pre.sharpness_available != 0 && post.sharpness_available != 0
            && std::abs (brightness) >= 0.005f)
        {
            g.setColour (brightnessColour);
            g.drawVerticalLine (juce::roundToInt (x), centreY - height * 0.58f,
                                centreY + height * 0.58f);
        }
        if (texture && index % 8u == 0u)
        {
            g.setColour (textureColour);
            g.drawLine (x - 1.5f, centreY + height * 0.68f,
                        x + 1.5f, centreY - height * 0.68f, 0.9f);
        }
    }
}
}

void AttackInternalComponent::setOverlayMode (bool shouldOverlay)
{
    overlayMode = shouldOverlay;
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
    eventBatch = events;
    waveformBatch = waveform;
    detailBatch = details;
    preWaveformBatch = preWaveform;
    preDetailBatch = preDetails;
    pairEventBatch = pairEvents;
    runtimeStats = stats;
    latest = latestSample;
    rate = sampleRate;
    currentGeneration = generation;
    if (pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE && selectedPairEvent() == nullptr)
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
    rate = 0;
    currentGeneration = 0;
    selectedEventSample = -1;
    repaint();
}

juce::Rectangle<int> AttackInternalComponent::timelineBounds() const noexcept
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (attack_ui::headerHeight);
    bounds.removeFromBottom (attack_ui::axisLabelHeight);
    return bounds.removeFromTop (juce::jmax (
        attack_ui::timelineMinimumHeight, juce::roundToInt (bounds.getHeight() * 0.42f)));
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
    auto axis = bounds.removeFromBottom (attack_ui::axisLabelHeight);
    auto timeline = bounds.removeFromTop (juce::jmax (
        attack_ui::timelineMinimumHeight, juce::roundToInt (bounds.getHeight() * 0.42f)));
    timeline = timeline.reduced (1, 2);
    auto detailArea = bounds.reduced (1, 2);

    g.setFont (monoFont (10.0f));
    g.setColour (COL_NORMAL);
    const auto paired = pairEventBatch.status == KIRIN_SPECTRUM_ACTIVE;
    g.drawText (paired ? "ATTACK / DRUM / PRE + POST" : "ATTACK / DRUM / POST ABSOLUTE",
                header, juce::Justification::centredLeft);
    g.setColour (COL_MUTED);
    g.drawText (overlayMode ? "OVERLAY" : "SPLIT",
                header.removeFromRight (125), juce::Justification::centredRight);

    const bool running = runtimeStats.available != 0 && runtimeStats.enabled != 0
                      && runtimeStats.worker_running != 0;
    if (! running || ! attack_ui::validTimeline (latest, rate))
    {
        g.setColour (COL_MUTED);
        g.drawText (runtimeStats.available == 0 ? "UNAVAILABLE" : "WARMING UP",
                    bounds, juce::Justification::centred);
        return;
    }

    g.setColour (kFieldFill.withAlpha (0.72f));
    g.fillRoundedRectangle (timeline.toFloat(), 3.0f);
    g.fillRoundedRectangle (detailArea.toFloat(), 3.0f);
    const auto first = latest - attack_ui::windowSamples (rate);
    if (paired && ! overlayMode)
    {
        auto preLane = timeline.removeFromTop (timeline.getHeight() / 2);
        auto postLane = timeline;
        drawWaveform (g, preWaveformBatch, preLane, first, latest, rate, true, 0.72f);
        drawWaveform (g, waveformBatch, postLane, first, latest, rate, true, 0.72f);
        g.setFont (monoFont (8.0f));
        g.setColour (COL_MUTED);
        g.drawText ("PRE", preLane.reduced (4, 1), juce::Justification::topLeft);
        g.drawText ("POST", postLane.reduced (4, 1), juce::Justification::topLeft);
    }
    else
    {
        if (paired)
            drawWaveform (g, preWaveformBatch, timeline, first, latest, rate, false, 0.72f);
        drawWaveform (g, waveformBatch, timeline, first, latest, rate, true, 0.82f);
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
            g.setColour (sample == selectedEventSample ? COL_FLORA_BR : COL_MUTED);
            g.drawVerticalLine (markerArea.getX() + x,
                                static_cast<float> (markerArea.getY() + 2),
                                static_cast<float> (markerArea.getBottom() - 2));
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
            g.setColour (attack.event_sample == selectedEventSample ? COL_FLORA_BR : COL_MUTED);
            g.drawVerticalLine (markerArea.getX() + x,
                                static_cast<float> (markerArea.getY() + 2),
                                static_cast<float> (markerArea.getBottom() - 2));
        }
    }

    const auto* preDetail = selectedPreDetail();
    const auto* postDetail = selectedPostDetail();
    if (postDetail != nullptr)
    {
        auto metrics = detailArea.removeFromBottom (attack_ui::detailMetricsHeight);
        auto shapeArea = detailArea.reduced (6, 3);
        if (preDetail != nullptr && ! overlayMode)
        {
            auto preShape = shapeArea.removeFromTop (shapeArea.getHeight() / 2);
            drawShape (g, *preDetail, preShape, true, 0.72f);
            drawShape (g, *postDetail, shapeArea, true, 0.72f);
            g.setFont (monoFont (8.0f));
            g.setColour (COL_MUTED);
            g.drawText ("PRE", preShape, juce::Justification::topLeft);
            g.drawText ("POST", shapeArea, juce::Justification::topLeft);
        }
        else
        {
            if (preDetail != nullptr)
                drawShape (g, *preDetail, shapeArea, false, 0.72f);
            drawShape (g, *postDetail, shapeArea, true, 0.86f);
            if (preDetail != nullptr)
                drawOverlayAccents (g, *preDetail, *postDetail, shapeArea);
        }

        g.setFont (monoFont (8.2f));
        if (preDetail != nullptr)
        {
            const auto strength = postDetail->contrast_db - preDetail->contrast_db;
            const auto edge = postDetail->sample_edge_ratio_db - preDetail->sample_edge_ratio_db;
            const auto crest = postDetail->crest_db - preDetail->crest_db;
            const auto plateau = postDetail->peak_plateau_ms - preDetail->peak_plateau_ms;
            const bool hasBrightness = preDetail->sharpness_available != 0
                                    && postDetail->sharpness_available != 0;
            const auto brightness = postDetail->sharpness_acum - preDetail->sharpness_acum;
            const bool texturePattern = edge > 0.0f && crest < 0.0f && plateau > 0.0f;
            g.setColour (strengthColour);
            g.drawText ("STRENGTH  " + signedValue (strength) + " dB contrast",
                        metrics.removeFromTop (metrics.getHeight() / 3),
                        juce::Justification::centredLeft);
            g.setColour (brightnessColour);
            g.drawText ("BRIGHTNESS  " + (hasBrightness
                            ? signedValue (brightness, 2) + " acum" : juce::String ("---")),
                        metrics.removeFromTop (metrics.getHeight() / 2),
                        juce::Justification::centredLeft);
            g.setColour (texturePattern ? textureColour : COL_MUTED);
            g.drawText ("TEXTURE  EDGE " + signedValue (edge)
                        + "  CREST " + signedValue (crest)
                        + "  PLATEAU " + signedValue (plateau, 2) + " ms",
                        metrics, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (COL_NORMAL);
            g.drawText ("CONTRAST " + signedValue (postDetail->contrast_db) + " dB"
                        + "   PEAK " + juce::String (postDetail->sample_peak_dbfs, 1) + " dBFS"
                        + "   CREST " + juce::String (postDetail->crest_db, 1) + " dB",
                        metrics.removeFromTop (metrics.getHeight() / 2),
                        juce::Justification::centredLeft);
            g.setColour (COL_MUTED);
            g.drawText ("EDGE " + juce::String (postDetail->sample_edge_ratio_db, 1)
                        + " dB   PLATEAU " + juce::String (postDetail->peak_plateau_ms, 2)
                        + " ms   BRIGHTNESS ---",
                        metrics, juce::Justification::centredLeft);
        }
    }
    else
    {
        g.setColour (COL_MUTED);
        g.drawText ("PLAY AUDIO TO CAPTURE AN ATTACK", detailArea,
                    juce::Justification::centred);
    }

    g.setFont (monoFont (9.0f));
    g.setColour (COL_MUTED);
    g.drawText ("-6 s", axis.removeFromLeft (40), juce::Justification::centredLeft);
    g.drawText ("NOW", axis.removeFromRight (36), juce::Justification::centredRight);
    g.setColour (COL_NORMAL);
    g.drawText (juce::String (visibleCount) + " EVENTS / CLICK TO SCRUB", axis,
                juce::Justification::centred);
}
}
