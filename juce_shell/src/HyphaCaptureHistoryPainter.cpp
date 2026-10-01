#include "HyphaCaptureHistoryPainter.h"
#include "HyphaCaptureHistoryGeometry.h"
#include "HyphaCaptureHistoryTruePeak.h"

#include "HyphaChainActionPainter.h"
#include "HyphaChannelClipText.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
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
float yForLoudness (juce::Rectangle<float> plot, double value, bool delta) noexcept
{
    const auto normalized = normalizedLoudness (value, delta);
    return plot.getBottom() - static_cast<float> (normalized) * plot.getHeight();
}

}

float currentLabelWidth (const juce::String& text, bool inspection, presentation::Context presentation)
{
    constexpr auto visualization = typography::Composition::visualization;
    const auto font = monoFont (presentation, typography::TextRole::readout, visualization);
    const auto style = typography::resolve (presentation, typography::TextRole::readout, visualization);
    return std::max (inspection ? 88.0f : 72.0f,
                     (float) text_style::requiredWidth (font, text, style) + 8.0f);
}

namespace
{
void paintCurrentLoudness (juce::Graphics& g,
                           juce::Rectangle<float> plot,
                           const std::vector<KirinMeterHistoryEntry>& history,
                           bool delta,
                           bool held,
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
    const auto text = juce::String (held ? "HOLD  " : "NOW  ") + valueText;
    const auto font = monoFont (presentation, typography::TextRole::readout,
                               typography::Composition::visualization);
    const auto labelHeight = inspection ? 18.0f : 15.0f;
    const auto labelWidth = currentLabelWidth (text, inspection, presentation);
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
    text_style::drawText (g, text, label.toNearestInt().reduced (3, 0),
                juce::Justification::centredRight);
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

const KirinChainPoint* chainAtExactEndpoint (const chain_action::View& view,
                                             const KirinMeterHistoryEntry& entry) noexcept
{
    if (! view.visible()) return nullptr;
    for (const auto& point : *view.points)
        if (point.post_observed == entry.last_observed_frames
            && point.post_run == entry.run_id
            && point.post_epoch == entry.measurement_epoch
            && point.post_generation == entry.generation)
            return &point;
    return nullptr;
}

juce::String chainDetail (const KirinChainPoint& point, bool wide)
{
    const auto compound = juce::String ("   DM ") + measuredText (point.delta_m, true)
        + "  DTP " + measuredText (point.delta_tp, true)
        + "  REL " + measuredText (point.relation, true);
    const auto label = juce::String ("400 MS / CONTENT END ") + juce::String (point.endpoint);
    return wide ? label + "   M400 " + measuredText (point.pre_m) + "/" + measuredText (point.post_m)
            + "   TP400 " + measuredText (point.pre_tp) + "/" + measuredText (point.post_tp)
            + compound
        : label + compound;
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
                 bool chainBand,
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
    if (! delta && std::isfinite (entry.true_peak.max) && (! chainBand || entry.true_peak.max > -1.0))
    {
        const auto y = chainBand ? layout.sharedPlot.getBottom() - 4.5f
                                 : true_peak::yFor (true_peak::overlayFor (layout.sharedPlot),
                                                    entry.true_peak.max);
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

std::optional<std::size_t> hitTestChain (juce::Rectangle<int> area,
                                        const KirinChainSnapshot& snapshot,
                                        const std::vector<KirinChainPoint>& points,
                                        std::uint64_t axisEndObserved,
                                        juce::Point<float> position)
{
    const auto layout = layoutFor (area);
    const auto view = chain_action::View { &snapshot, &points, axisEndObserved };
    if (! view.visible() || ! inChainBand (area, position))
        return std::nullopt;
    auto nearest = std::optional<std::size_t> {};
    auto distance = 8.0f;
    for (std::size_t at = 0; at < points.size(); ++at)
        if (const auto x = chain_action::xFor (layout.sharedPlot, view, points[at]))
        {
            const auto gap = std::abs (*x - position.x);
            if (gap <= distance) { nearest = at; distance = gap; }
        }
    return nearest;
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
            chain_action::GeometryCache* chainCache,
            const KirinChainPoint* selectedChain)
{
    main_frame::paint (g, area.toFloat()); // LEVEL's main window
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
    text_style::drawText (g, delta ? "M / POST - PRE / 60 S"
                      : chainView.visible() ? "CHAIN ACTION / PRE TO POST M"
                                            : "M / momentary LUFS",
                loudnessLegend, juce::Justification::centredLeft);
    if (! delta)
    {
        g.setColour (COL_FLORA_BR);
        text_style::drawText (g, chainView.visible() ? "TP CROSSING / > -1 dBTP"
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
            if (const auto* point = chainAtExactEndpoint (chainView, entry))
            {
                detail = chainDetail (*point, layout.legend.getWidth() >= 700);
            }
    }
    else if (selectedChain != nullptr && chainView.visible())
        detail = chainDetail (*selectedChain, layout.legend.getWidth() >= 700);
    else if (peakSummary.available && ! peakSummary.eventIndices.empty())
    {
        detail = "60 S MAX TP " + history_inspection::peakText (peakSummary.windowMaximumDbtp) + " dBTP"
               + " @ " + relativeTimeText (peakSummary.secondsBeforeEnd);
    }
    else
        detail = delta ? juce::String ("M / 60 S AUDIO")
                       : juce::String ("TP ") + emDash() + " / 60 S AUDIO";
    if (contextFact.isNotEmpty())
        detail = contextFact + "   |   " + detail;
    text_style::drawText (g, detail, legend, juce::Justification::centredLeft);

    if (history.empty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (presentation, typography::TextRole::status,
                             typography::Composition::visualization));
        text_style::drawText (g, juce::String ("HISTORY ") + emDash(),
                    layout.sharedPlot.toNearestInt(), juce::Justification::centred);
        return;
    }

    const auto bandHeight = chainView.visible() ? 30.0f : 0.0f;
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
            text_style::drawText (g, label, layout.loudnessLabels.getX(), y - 7,
                        layout.loudnessLabels.getWidth() - 3, 14,
                        juce::Justification::centredRight);
        }
    };
    if (delta)
        paintLoudnessTicks (deltaTicks);
    else
        paintLoudnessTicks (absoluteTicks);
    if (! delta && ! chainView.visible())
        true_peak::paintAxis (g, layout, presentation);
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
            text_style::drawText (g, "REL", layout.loudnessLabels.getX(), juce::roundToInt (band.getY()),
                        layout.loudnessLabels.getWidth() - 2, 12,
                        juce::Justification::centredRight);
            text_style::drawText (g, "PRE", layout.loudnessLabels.getX(), juce::roundToInt (band.getBottom() - 14.0f),
                        layout.loudnessLabels.getWidth() - 2, 7,
                        juce::Justification::centredRight);
            text_style::drawText (g, "POST", layout.loudnessLabels.getX(), juce::roundToInt (band.getBottom() - 8.0f),
                        layout.loudnessLabels.getWidth() - 2, 7,
                        juce::Justification::centredRight);
        }
        else
            true_peak::paintEvents (g, layout.sharedPlot, history, axis, peakSummary, sampleRate);
        paintClipPips (g, loudnessPlot, history, axis, sampleRate, meter);
    }
    paintCurrentLoudness (g, loudnessPlot, history, delta,
                          contextFact == "HOLD", presentation);
    g.setColour (COL_MUTED.withAlpha (0.64f));
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    text_style::drawText (g, "-60", layout.timeLabels.withWidth (24), juce::Justification::centredLeft);
    text_style::drawText (g, "-30", layout.timeLabels.withSizeKeepingCentre (30, layout.timeLabels.getHeight()),
                juce::Justification::centred);
    text_style::drawText (g, "NOW", layout.timeLabels.withLeft (layout.timeLabels.getRight() - 24),
                juce::Justification::centredRight);
    paintHover (g, layout, loudnessPlot, history, axis, hoveredIndex, delta, chainView.visible(),
                sampleRate);
    if (selectedChain != nullptr && chainView.visible())
        if (const auto x = chain_action::xFor (layout.sharedPlot, chainView, *selectedChain))
        {
            g.setColour (COL_LED_BLUE.withAlpha (0.48f));
            g.drawVerticalLine (juce::roundToInt (*x), band.getY(), band.getBottom());
        }
}
}
