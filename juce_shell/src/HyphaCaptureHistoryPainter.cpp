#include "HyphaCaptureHistoryPainter.h"

#include "HyphaChainActionPainter.h"
#include "HyphaChannelClipText.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTimeAxisContract.h"
#include "HyphaHistoryInspection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace hypha::capture_history
{
namespace
{
struct Layout
{
    juce::Rectangle<int> legend;
    juce::Rectangle<int> loudnessLabels;
    juce::Rectangle<int> timeLabels;
    juce::Rectangle<float> sharedPlot;
};
Layout layoutFor (juce::Rectangle<int> area)
{
    area.reduce (7, 5);
    const auto inspection = area.getWidth() >= 700;
    Layout result;
    result.legend = area.removeFromTop (inspection ? 36 : 30);
    result.loudnessLabels = area.removeFromLeft (inspection ? 54 : 40);
    area.removeFromRight (6);
    result.timeLabels = area.removeFromBottom (inspection ? 16 : 14);
    result.sharedPlot = area.reduced (2, 2).toFloat();
    return result;
}
float yForLoudness (juce::Rectangle<float> plot, double value, bool delta) noexcept
{
    const auto normalized = normalizedLoudness (value, delta);
    return plot.getBottom() - static_cast<float> (normalized) * plot.getHeight();
}

void paintCurrentLoudness (juce::Graphics& g,
                           juce::Rectangle<float> plot,
                           const std::vector<KirinMeterHistoryEntry>& history,
                           bool delta,
                           presentation::Context presentation)
{
    if (history.empty())
        return;
    const auto value = history.back().lufs_m.mean;
    if (! std::isfinite (value))
        return;
    const auto y = yForLoudness (plot, value, delta);
    const auto inspection = plot.getWidth() >= 650.0f;
    const auto belowFloor = ! delta && value < absoluteLoudnessMinimum;
    const auto valueText = belowFloor
        ? juce::String ("< -36")
        : (delta && value >= 0.0 ? "+" : "") + juce::String (value, 1);
    const auto text = juce::String ("NOW  ") + valueText;
    const auto font = monoFont (presentation, typography::TextRole::readout,
                               typography::Composition::visualization);
    const auto labelHeight = inspection ? 18.0f : 15.0f;
    const auto labelWidth = juce::jmin (plot.getWidth() - 8.0f,
                                       font.getStringWidthFloat (text) + 10.0f);
    auto label = juce::Rectangle<float> (
        plot.getRight() - labelWidth - 4.0f,
        juce::jlimit (plot.getY() + 2.0f,
                      plot.getBottom() - labelHeight - 2.0f,
                      y - labelHeight * 0.5f),
        labelWidth, labelHeight);
    g.setColour (BG.withAlpha (0.88f));
    g.fillRoundedRectangle (label, 3.0f);
    g.setColour (COL_SPECTRUM_POST.withAlpha (0.48f));
    g.drawRoundedRectangle (label, 3.0f, 0.7f);
    g.setColour (COL_OBSERVATORY_VALUE);
    g.setFont (monoFont (presentation, typography::TextRole::readout,
                         typography::Composition::visualization));
    g.drawText (text, label.toNearestInt().reduced (3, 0),
                juce::Justification::centredRight);
}
double normalizedHistoryX (const std::vector<KirinMeterHistoryEntry>& history,
                           const time_history::HistoryAxis& fallbackAxis,
                           const KirinMeterHistoryEntry& entry,
                           std::size_t index,
                           double sampleRate) noexcept
{
    if (! history.empty() && std::isfinite (sampleRate) && sampleRate > 0.0)
    {
        constexpr double windowSeconds = 60.0;
        const auto latest = history.back().last_observed_frames;
        if (entry.last_observed_frames <= latest)
        {
            const auto ageFrames = latest - entry.last_observed_frames;
            return juce::jlimit (
                0.0, 1.0, 1.0 - static_cast<double> (ageFrames)
                                     / (sampleRate * windowSeconds));
        }
    }
    return time_history::normalizedX (fallbackAxis, entry, index, history.size());
}

juce::String relativeTimeText (double seconds)
{
    return seconds < 0.05 ? "NOW" : "-" + juce::String (seconds, 1) + " S";
}

juce::String measuredText (double value, bool delta = false)
{
    if (! std::isfinite (value))
        return "---";
    return (delta && value >= 0.0 ? "+" : "") + juce::String (value, 1);
}

const KirinChainPoint* nearestChainPoint (const chain_action::View& view,
                                         std::uint64_t observed) noexcept
{
    if (! view.visible()) return nullptr;
    const KirinChainPoint* nearest = nullptr;
    auto distance = std::numeric_limits<std::uint64_t>::max();
    for (const auto& point : *view.points)
    {
        if (point.post_observed > view.axisEndObserved) continue;
        const auto gap = point.post_observed > observed
            ? point.post_observed - observed : observed - point.post_observed;
        if (gap < distance) { distance = gap; nearest = &point; }
    }
    return distance <= view.snapshot->sample_rate / 20u ? nearest : nullptr;
}

void paintPath (juce::Graphics& g,
                juce::Rectangle<float> plot,
                const std::vector<KirinMeterHistoryEntry>& history,
                const time_history::HistoryAxis& axis,
                bool delta,
                juce::Colour colour,
                float alpha,
                float width,
                double sampleRate)
{
    juce::Path path;
    bool open = false;
    bool haveEndpoint = false;
    std::uint64_t previousGeneration = 0;
    std::uint64_t previousRun = 0;
    juce::Point<float> endpoint;
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        const auto value = entry.lufs_m.mean;
        if (! std::isfinite (value))
        {
            open = false;
            continue;
        }
        const auto x = plot.getX()
                     + static_cast<float> (normalizedHistoryX (
                           history, axis, entry, index, sampleRate)) * plot.getWidth();
        const auto point = juce::Point<float> { x, yForLoudness (plot, value, delta) };
        const bool newRun = ! open || entry.generation != previousGeneration
                         || entry.run_id != previousRun;
        if (newRun)
            path.startNewSubPath (point);
        else
            path.lineTo (point);
        open = true;
        haveEndpoint = true;
        endpoint = point;
        previousGeneration = entry.generation;
        previousRun = entry.run_id;
    }

    g.setColour (colour.withAlpha (alpha * 0.14f));
    g.strokePath (path, juce::PathStrokeType (width * 4.0f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (colour.withAlpha (alpha));
    g.strokePath (path, juce::PathStrokeType (width,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    if (haveEndpoint)
    {
        g.setColour (COL_LED_BLUE.withAlpha (0.18f));
        g.fillEllipse (endpoint.x - 5.0f, endpoint.y - 5.0f, 10.0f, 10.0f);
        g.setColour (COL_LED_BLUE);
        g.fillEllipse (endpoint.x - 1.6f, endpoint.y - 1.6f, 3.2f, 3.2f);
    }
}

void paintTruePeakBand (juce::Graphics& g,
                        juce::Rectangle<float> band,
                        const std::vector<KirinMeterHistoryEntry>& history,
                        const time_history::HistoryAxis& axis,
                        double sampleRate)
{
    juce::Path moderate, strong;
    moderate.preallocateSpace (static_cast<int> (history.size() * 5u));
    strong.preallocateSpace (static_cast<int> (history.size() * 5u));
    bool previousValid = false;
    std::uint64_t previousGeneration = 0u, previousRun = 0u, previousObserved = 0u;
    std::uint8_t previousSeverity = 0u;
    const auto y = band.getCentreY();
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto& point = history[index];
        const auto value = point.true_peak.max;
        const auto severity = static_cast<std::uint8_t> (
            ! std::isfinite (value) ? 0u : value > 0.0 ? 3u
            : value > -1.0 ? 2u : 1u);
        const auto x = band.getX() + static_cast<float> (normalizedHistoryX (
            history, axis, point, index, sampleRate)) * band.getWidth();
        const bool continuous = previousValid
            && previousGeneration == point.generation && previousRun == point.run_id
            && point.last_observed_frames > previousObserved
            && point.last_observed_frames - previousObserved
                == static_cast<std::uint64_t> (sampleRate / 10.0);
        if (severity >= 2u)
        {
            auto& path = severity == 3u ? strong : moderate;
            if (continuous && previousSeverity == severity)
                path.lineTo (x, y);
            else
            {
                path.startNewSubPath (x - 1.2f, y);
                path.lineTo (x + 1.2f, y);
            }
        }
        previousValid = true;
        previousGeneration = point.generation;
        previousRun = point.run_id;
        previousObserved = point.last_observed_frames;
        previousSeverity = severity;
    }
    g.setColour (COL_FLORA.withAlpha (0.72f));
    g.strokePath (moderate, juce::PathStrokeType (1.5f));
    g.setColour (COL_FLORA_BR.withAlpha (0.18f));
    g.strokePath (strong, juce::PathStrokeType (5.0f));
    g.setColour (COL_FLORA_BR.withAlpha (0.96f));
    g.strokePath (strong, juce::PathStrokeType (2.5f));
}

void paintClipPips (juce::Graphics& g,
                    juce::Rectangle<float> plot,
                    const std::vector<KirinMeterHistoryEntry>& history,
                    const time_history::HistoryAxis& axis,
                    double sampleRate,
                    const KirinMeterSession* meter)
{
    const std::array<juce::Colour, 6> clipColours {
        COL_LED_BLUE, COL_SPECTRUM_POST, COL_FLORA_BR, COL_GUIDE_BR, COL_NORMAL, COL_FLORA
    };
    for (std::size_t index = 0; index < history.size(); ++index)
        for (std::size_t channel = 0; channel < channel_clip::count (meter); ++channel)
            if (history[index].clip_event_count[channel] > 0u)
            {
                const auto x = plot.getX()
                             + static_cast<float> (normalizedHistoryX (
                                   history, axis, history[index], index, sampleRate))
                               * plot.getWidth();
                const auto y = plot.getBottom() - 2.0f
                             - static_cast<float> (channel) * 4.0f;
                const auto colour = clipColours[channel % clipColours.size()];
                g.setColour (colour.withAlpha (0.24f));
                g.fillEllipse (x - 3.0f, y - 1.0f, 6.0f, 4.0f);
                g.setColour (colour.withAlpha (0.94f));
                g.fillRoundedRectangle (x - 1.5f, y, 3.0f, 2.0f, 1.0f);
            }
}

void paintHover (juce::Graphics& g,
                 const Layout& layout,
                 juce::Rectangle<float> loudnessPlot,
                 const std::vector<KirinMeterHistoryEntry>& history,
                 const time_history::HistoryAxis& axis,
                 std::optional<std::size_t> hoveredIndex,
                 bool delta,
                 double sampleRate)
{
    if (! hoveredIndex.has_value() || *hoveredIndex >= history.size())
        return;
    const auto index = *hoveredIndex;
    const auto& entry = history[index];
    const auto x = layout.sharedPlot.getX()
                 + static_cast<float> (normalizedHistoryX (
                       history, axis, entry, index, sampleRate)) * layout.sharedPlot.getWidth();
    g.setColour (COL_LED_BLUE.withAlpha (0.34f));
    g.drawVerticalLine (juce::roundToInt (x), layout.sharedPlot.getY(),
                        layout.sharedPlot.getBottom());
    if (std::isfinite (entry.lufs_m.mean))
    {
        const auto y = yForLoudness (loudnessPlot, entry.lufs_m.mean, delta);
        g.setColour (COL_SPECTRUM_POST);
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
    if (! delta && std::isfinite (entry.true_peak.max) && entry.true_peak.max > -1.0)
    {
        const auto y = layout.sharedPlot.getBottom() - 4.5f;
        g.setColour (COL_FLORA_BR);
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
}
}

std::optional<std::size_t> hitTest (juce::Rectangle<int> area,
                                    const std::vector<KirinMeterHistoryEntry>& history,
                                    juce::Point<float> position,
                                    double sampleRate)
{
    if (history.empty())
        return std::nullopt;
    const auto layout = layoutFor (area);
    if (! layout.sharedPlot.contains (position))
        return std::nullopt;
    const auto axis = time_history::selectAxis (history);
    auto nearest = std::size_t { 0 };
    auto distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto x = layout.sharedPlot.getX()
                     + static_cast<float> (normalizedHistoryX (
                           history, axis, history[index], index, sampleRate))
                       * layout.sharedPlot.getWidth();
        const auto candidate = std::abs (x - position.x);
        if (candidate < distance)
        {
            distance = candidate;
            nearest = index;
        }
    }
    constexpr auto maximumHoverDistance = 8.0f;
    return distance <= maximumHoverDistance
        ? std::optional<std::size_t> { nearest }
        : std::nullopt;
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const std::vector<KirinMeterHistoryEntry>& history,
            bool delta,
            double sampleRate,
            presentation::Context presentation,
            std::optional<std::size_t> hoveredIndex,
            juce::String contextFact,
            const KirinMeterSession* meter,
            const KirinChainSnapshot* chain,
            const std::vector<KirinChainPoint>* chainPoints,
            chain_action::GeometryCache* chainCache)
{
    surface_material::paintPanel (g, area.toFloat(), 0.62f);
    const auto layout = layoutFor (area);
    const auto peakSummary = delta ? TruePeakSummary {} : analyseTruePeak (history, sampleRate);

    auto legend = layout.legend;
    auto meanings = legend.removeFromTop (legend.getHeight() / 2);
    g.setFont (monoFont (presentation, typography::TextRole::legend,
                         typography::Composition::visualization));
    g.setColour (COL_SPECTRUM_POST);
    auto loudnessLegend = meanings.removeFromLeft (delta ? meanings.getWidth() : meanings.getWidth() / 2);
    const auto axisEnd = history.empty() ? 0u : history.back().last_observed_frames;
    const auto chainView = chain_action::View { chain, chainPoints, axisEnd };
    g.drawText (delta ? "M / POST - PRE / 60 S"
                      : chainView.visible() ? "CHAIN ACTION / PRE TO POST M"
                                            : "M / momentary LUFS",
                loudnessLegend, juce::Justification::centredLeft);
    if (! delta)
    {
        g.setColour (COL_FLORA_BR);
        g.drawText (chainView.visible() ? "TP CROSSING / > -1 dBTP"
                                        : "TP / > -1 dBTP events", meanings,
                    juce::Justification::centredRight);
    }
    g.setColour (COL_MUTED.brighter (0.15f));
    juce::String detail;
    if (hoveredIndex.has_value() && *hoveredIndex < history.size())
    {
        const auto& entry = history[*hoveredIndex];
        detail = history_inspection::positionText (entry, sampleRate)
               + "   M " + measuredText (entry.lufs_m.mean, delta);
        if (! delta)
            detail += "   TP " + history_inspection::peakText (entry.true_peak.max) + " dBTP";
        if (! delta && channel_clip::total (entry.clip_event_count, meter) > 0u)
            detail += "   " + channel_clip::text (entry.clip_event_count, meter, true);
        if (! delta)
            if (const auto* point = nearestChainPoint (chainView, entry.last_observed_frames))
            {
                const auto compound = "   DM " + measuredText (point->delta_m, true)
                    + "  DTP " + measuredText (point->delta_tp, true)
                    + "  REL " + measuredText (point->relation, true);
                detail = layout.legend.getWidth() >= 700
                    ? history_inspection::positionText (entry, sampleRate)
                        + "   M " + measuredText (point->pre_m) + "/" + measuredText (point->post_m)
                        + "   TP " + measuredText (point->pre_tp) + "/" + measuredText (point->post_tp)
                        + compound
                    : history_inspection::positionText (entry, sampleRate) + compound;
            }
    }
    else if (peakSummary.available)
    {
        detail = "60 S MAX TP " + history_inspection::peakText (peakSummary.windowMaximumDbtp) + " dBTP"
               + " @ " + relativeTimeText (peakSummary.secondsBeforeEnd);
    }
    else
        detail = delta ? juce::String ("M / 60 S AUDIO")
                       : juce::String ("TP ") + emDash() + " / 60 S AUDIO";
    if (contextFact.isNotEmpty())
        detail = contextFact + "   |   " + detail;
    g.drawText (detail, legend, juce::Justification::centredLeft);

    if (history.empty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (presentation, typography::TextRole::status,
                             typography::Composition::visualization));
        g.drawText (juce::String ("HISTORY ") + emDash(),
                    layout.sharedPlot.toNearestInt(), juce::Justification::centred);
        return;
    }

    const auto bandHeight = chainView.visible() ? 30.0f : delta ? 0.0f : 11.0f;
    const auto loudnessPlot = layout.sharedPlot.withBottom (
        layout.sharedPlot.getBottom() - bandHeight);
    const auto band = layout.sharedPlot.withTop (loudnessPlot.getBottom());
    constexpr std::array<double, 7> absoluteTicks {
        0.0, -6.0, -12.0, -18.0, -24.0, -30.0, -36.0
    };
    constexpr std::array<double, 5> deltaTicks { 12.0, 6.0, 0.0, -6.0, -12.0 };
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    const auto paintLoudnessTicks = [&] (const auto& ticks)
    {
        for (size_t index = 0; index < ticks.size(); ++index)
        {
            if (loudnessPlot.getHeight() < 110.0f
                && index != 0 && index != ticks.size() / 2 && index != ticks.size() - 1)
                continue;
            const auto tick = ticks[index];
            const auto y = juce::roundToInt (yForLoudness (loudnessPlot, tick, delta));
            const bool zero = delta && tick == 0.0;
            g.setColour ((zero ? COL_FLORA_BR : COL_MUTED).withAlpha (
                zero ? 0.42f : 0.25f));
            g.drawHorizontalLine (y, loudnessPlot.getX(), loudnessPlot.getRight());
            g.setColour (COL_MUTED.brighter (0.20f).withAlpha (0.86f));
            const auto label = (delta && tick > 0.0 ? "+" : "")
                             + juce::String (tick, 0);
            g.drawText (label, layout.loudnessLabels.getX(), y - 7,
                        layout.loudnessLabels.getWidth() - 3, 14,
                        juce::Justification::centredRight);
        }
    };
    if (delta)
        paintLoudnessTicks (deltaTicks);
    else
        paintLoudnessTicks (absoluteTicks);
    const auto axis = time_history::selectAxis (history);
    paintPath (g, loudnessPlot, history, axis, delta,
               COL_SPECTRUM_POST, 0.96f, 1.20f, sampleRate);
    if (! delta)
    {
        if (chainView.visible())
        {
            chain_action::GeometryCache temporary;
            auto& geometry = chainCache == nullptr ? temporary : *chainCache;
            geometry.update (band, chainView);
            geometry.paint (g);
            g.setColour (COL_MUTED.brighter (0.25f));
            g.setFont (monoFont (presentation, typography::TextRole::axis,
                                 typography::Composition::visualization));
            g.drawText ("REL", layout.loudnessLabels.getX(), juce::roundToInt (band.getY()),
                        layout.loudnessLabels.getWidth() - 2, 12,
                        juce::Justification::centredRight);
            g.drawText ("PRE", layout.loudnessLabels.getX(), juce::roundToInt (band.getBottom() - 14.0f),
                        layout.loudnessLabels.getWidth() - 2, 7,
                        juce::Justification::centredRight);
            g.drawText ("POST", layout.loudnessLabels.getX(), juce::roundToInt (band.getBottom() - 8.0f),
                        layout.loudnessLabels.getWidth() - 2, 7,
                        juce::Justification::centredRight);
        }
        else
            paintTruePeakBand (g, band, history, axis, sampleRate);
        paintClipPips (g, loudnessPlot, history, axis, sampleRate, meter);
    }
    paintCurrentLoudness (g, loudnessPlot, history, delta, presentation);
    g.setColour (COL_MUTED.withAlpha (0.64f));
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    g.drawText ("-60", layout.timeLabels.withWidth (24), juce::Justification::centredLeft);
    g.drawText ("-30", layout.timeLabels.withSizeKeepingCentre (30, layout.timeLabels.getHeight()),
                juce::Justification::centred);
    g.drawText ("NOW", layout.timeLabels.withLeft (layout.timeLabels.getRight() - 24),
                juce::Justification::centredRight);
    paintHover (g, layout, loudnessPlot, history, axis, hoveredIndex, delta, sampleRate);
}
}
