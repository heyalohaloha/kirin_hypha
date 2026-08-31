#include "HyphaAttackInternalComponent.h"

#include <cmath>

#include "HyphaAttackUiContract.h"
#include "HyphaTheme.h"

namespace hypha
{
namespace
{
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
}

void AttackInternalComponent::setSnapshot (const KirinAttackEventBatch& events,
                                           const KirinAttackWaveformBatch& waveform,
                                           const KirinAttackDetailBatch& details,
                                           std::int64_t latestSample,
                                           std::uint32_t sampleRate,
                                           std::uint64_t generation,
                                           const KirinAttackStats& stats)
{
    eventBatch = events;
    waveformBatch = waveform;
    detailBatch = details;
    runtimeStats = stats;
    latest = latestSample;
    rate = sampleRate;
    currentGeneration = generation;
    if (selectedDetail() == nullptr)
    {
        selectedEventSample = -1;
        const auto count = juce::jmin (
            detailBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
        for (std::uint32_t index = 0; index < count; ++index)
            if (detailBatch.details[index].generation == currentGeneration)
                selectedEventSample = detailBatch.details[index].event_sample;
    }
    repaint();
}

void AttackInternalComponent::clearSnapshot()
{
    eventBatch = {};
    waveformBatch = {};
    detailBatch = {};
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

const KirinAttackDetail* AttackInternalComponent::selectedDetail() const noexcept
{
    const auto count = juce::jmin (
        detailBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
        if (detailBatch.details[index].generation == currentGeneration
            && detailBatch.details[index].event_sample == selectedEventSample)
            return &detailBatch.details[index];
    return nullptr;
}

void AttackInternalComponent::mouseDown (const juce::MouseEvent& event)
{
    const auto timeline = timelineBounds();
    if (! timeline.contains (event.getPosition()) || ! attack_ui::validTimeline (latest, rate))
        return;
    const auto first = latest - attack_ui::windowSamples (rate);
    const auto requested = first + static_cast<std::int64_t> (
        (static_cast<long double> (event.x - timeline.getX())
         * attack_ui::windowSamples (rate)) / juce::jmax (1, timeline.getWidth() - 1));
    std::int64_t bestDistance = std::numeric_limits<std::int64_t>::max();
    const auto count = juce::jmin (
        detailBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_DETAIL_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < count; ++index)
    {
        const auto& detail = detailBatch.details[index];
        if (detail.generation != currentGeneration
            || ! attack_ui::eventIsVisible (detail.event_sample, latest, rate))
            continue;
        const auto distance = detail.event_sample > requested
                            ? detail.event_sample - requested : requested - detail.event_sample;
        if (distance < bestDistance)
        {
            bestDistance = distance;
            selectedEventSample = detail.event_sample;
        }
    }
    repaint();
}

void AttackInternalComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto header = bounds.removeFromTop (attack_ui::headerHeight);
    auto axis = bounds.removeFromBottom (attack_ui::axisLabelHeight);
    auto timeline = bounds.removeFromTop (juce::jmax (
        attack_ui::timelineMinimumHeight, juce::roundToInt (bounds.getHeight() * 0.42f)));
    timeline = timeline.reduced (1, 2);
    auto detailArea = bounds.reduced (1, 2);

    g.setFont (monoFont (10.0f));
    g.setColour (COL_NORMAL);
    g.drawText ("ATTACK / DRUM / POST ABSOLUTE", header, juce::Justification::centredLeft);

    const bool running = runtimeStats.available != 0
                      && runtimeStats.enabled != 0
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
    const auto centreY = timeline.getCentreY();
    const auto halfHeight = timeline.getHeight() * 0.5f - 3.0f;
    juce::Path upperWaveform;
    juce::Path lowerWaveform;
    bool started = false;
    const auto waveformCount = juce::jmin (
        waveformBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_WAVEFORM_BATCH_CAPACITY));
    for (std::uint32_t index = 0; index < waveformCount; ++index)
    {
        const auto& point = waveformBatch.points[index];
        if (point.generation != currentGeneration || point.sample_rate != rate)
            continue;
        const auto sample = point.start_sample + (point.end_sample - point.start_sample) / 2;
        const auto localX = attack_ui::sampleX (sample, first, latest, timeline.getWidth());
        if (localX < 0)
            continue;
        const auto x = static_cast<float> (timeline.getX() + localX);
        const auto height = levelHeight (point.rms_dbfs, halfHeight);
        g.setColour (dim (COL_NORMAL, 0.17f));
        g.drawVerticalLine (timeline.getX() + localX, centreY - height, centreY + height);
        if (! started)
        {
            upperWaveform.startNewSubPath (x, centreY - height);
            lowerWaveform.startNewSubPath (x, centreY + height);
            started = true;
        }
        else
        {
            upperWaveform.lineTo (x, centreY - height);
            lowerWaveform.lineTo (x, centreY + height);
        }
    }
    if (started)
    {
        g.setColour (dim (COL_NORMAL, 0.38f));
        g.strokePath (upperWaveform, juce::PathStrokeType (1.1f));
        g.strokePath (lowerWaveform, juce::PathStrokeType (1.1f));
    }

    const auto eventCount = juce::jmin (
        eventBatch.count, static_cast<std::uint32_t> (KIRIN_ATTACK_EVENT_BATCH_CAPACITY));
    std::uint32_t visibleCount = 0;
    for (std::uint32_t index = 0; index < eventCount; ++index)
    {
        const auto& attack = eventBatch.events[index];
        if (attack.generation != currentGeneration || attack.sample_rate != rate)
            continue;
        const auto x = attack_ui::eventX (
            attack.event_sample, latest, rate, timeline.getWidth());
        if (x < 0)
            continue;
        ++visibleCount;
        g.setColour (attack.event_sample == selectedEventSample ? COL_FLORA_BR : COL_MUTED);
        g.drawVerticalLine (timeline.getX() + x,
                            static_cast<float> (timeline.getY() + 2),
                            static_cast<float> (timeline.getBottom() - 2));
    }

    const auto* detail = selectedDetail();
    if (detail != nullptr)
    {
        auto metrics = detailArea.removeFromBottom (attack_ui::detailMetricsHeight);
        auto shapeArea = detailArea.reduced (6, 3);
        const auto shapeCentre = shapeArea.getCentreY();
        const auto shapeHalf = shapeArea.getHeight() * 0.5f - 2.0f;
        juce::Path upperShape;
        juce::Path lowerShape;
        const auto shapeCount = juce::jmin (
            detail->shape_count, static_cast<std::uint32_t> (KIRIN_ATTACK_SHAPE_CAPACITY));
        for (std::uint32_t index = 0; index < shapeCount; ++index)
        {
            const auto x = shapeArea.getX()
                         + static_cast<float> (index) * shapeArea.getWidth()
                           / juce::jmax (1.0f, static_cast<float> (shapeCount - 1));
            const auto height = amplitudeHeight (detail->shape[index], shapeHalf);
            g.setColour (dim (COL_NORMAL, 0.23f));
            g.drawVerticalLine (juce::roundToInt (x), shapeCentre - height, shapeCentre + height);
            if (index == 0)
            {
                upperShape.startNewSubPath (x, shapeCentre - height);
                lowerShape.startNewSubPath (x, shapeCentre + height);
            }
            else
            {
                upperShape.lineTo (x, shapeCentre - height);
                lowerShape.lineTo (x, shapeCentre + height);
            }
        }
        g.setColour (dim (COL_NORMAL, 0.76f));
        g.strokePath (upperShape, juce::PathStrokeType (1.35f));
        g.strokePath (lowerShape, juce::PathStrokeType (1.35f));
        const auto eventX = attack_ui::sampleX (
            detail->event_sample, detail->shape_start_sample, detail->shape_end_sample,
            shapeArea.getWidth());
        if (eventX >= 0)
        {
            g.setColour (COL_FLORA);
            g.drawVerticalLine (shapeArea.getX() + eventX,
                                static_cast<float> (shapeArea.getY()),
                                static_cast<float> (shapeArea.getBottom()));
        }

        g.setFont (monoFont (8.5f));
        const juce::String firstLine = "CONTRAST " + signedValue (detail->contrast_db) + " dB"
            + "   PEAK " + juce::String (detail->sample_peak_dbfs, 1) + " dBFS"
            + "   CREST " + juce::String (detail->crest_db, 1) + " dB";
        const juce::String secondLine = "EDGE " + juce::String (detail->sample_edge_ratio_db, 1)
            + " dB   PLATEAU " + juce::String (detail->peak_plateau_ms, 2) + " ms"
            + "   BRIGHTNESS "
            + (detail->sharpness_available != 0
                ? juce::String (detail->sharpness_acum, 2) + " acum" : "---");
        g.setColour (COL_NORMAL);
        g.drawText (firstLine, metrics.removeFromTop (metrics.getHeight() / 2),
                    juce::Justification::centredLeft);
        g.setColour (COL_MUTED);
        g.drawText (secondLine, metrics, juce::Justification::centredLeft);
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
