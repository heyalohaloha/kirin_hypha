#include "HyphaTimeHistoryPainter.h"
#include "HyphaTimeAxisContract.h"

#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace hypha::time_history
{
namespace
{
enum class Metric { momentary, shortTerm, truePeak, psr, correlation };

struct MetricVisual
{
    Metric metric;
    const char* label;
    juce::Colour colour;
    float glowWidth;
    float lineWidth;
};

const KirinMeterHistoryRange& rangeFor (const KirinMeterHistoryEntry& entry,
                                        Metric metric) noexcept
{
    if (metric == Metric::shortTerm)
        return entry.lufs_s;
    if (metric == Metric::truePeak)
        return entry.true_peak;
    if (metric == Metric::psr)
        return entry.psr;
    if (metric == Metric::correlation)
        return entry.correlation;
    return entry.lufs_m;
}

float normalizedMagnitude (Metric metric, double value, bool delta,
                           meter_context::ScaleMode scaleMode) noexcept
{
    if (delta)
        return (float) juce::jlimit (0.0, 1.0, (12.0 - value) / 24.0);
    const double floor = metric == Metric::truePeak ? -24.0
                                                    : meter_context::loudnessFloor (scaleMode);
    const double ceiling = metric == Metric::truePeak ? 6.0 : 0.0;
    return 1.0f - (float) juce::jlimit (
        0.0, 1.0, (value - floor) / (ceiling - floor));
}

float yFor (juce::Rectangle<float> plot, Metric metric, double value, bool delta,
            meter_context::ScaleMode scaleMode) noexcept
{
    return plot.getY()
         + normalizedMagnitude (metric, value, delta, scaleMode) * plot.getHeight();
}

float xFor (juce::Rectangle<float> plot,
            const KirinMeterHistoryEntry& entry,
            const HistoryAxis& axis,
            size_t index,
            size_t count) noexcept
{
    return dataXForEntry ({ plot.getX(), plot.getRight() }, entry, axis, index, count);
}

juce::String latestText (const std::vector<KirinMeterHistoryEntry>& history,
                         Metric metric,
                         bool delta)
{
    for (auto iterator = history.rbegin(); iterator != history.rend(); ++iterator)
    {
        const auto value = rangeFor (*iterator, metric).mean;
        if (std::isfinite (value))
            return ((delta || metric == Metric::correlation) && value >= 0.0 ? "+" : "")
                 + juce::String (value, metric == Metric::correlation ? 2 : 1);
    }
    return "---";
}

// PSR moves with the music, so its lane keeps one fixed scale per mode. Absolute PSR of mastered
// music lies between a crushed 4 dB and an open 16 dB. POST - PRE is mostly a reduction, so its
// scale keeps 3 dB above zero and 9 below. A value beyond either end rests on that end.
constexpr double psrBottom (bool delta) noexcept { return delta ? -9.0 : 4.0; }
constexpr double psrTop (bool delta) noexcept { return delta ? 3.0 : 16.0; }

float psrY (juce::Rectangle<float> plot, double value, bool delta) noexcept
{
    const auto proportion = juce::jlimit (
        0.0, 1.0, (value - psrBottom (delta)) / (psrTop (delta) - psrBottom (delta)));
    return plot.getBottom() - static_cast<float> (proportion) * plot.getHeight();
}

juce::String scaleText (double value, bool delta)
{
    return (delta && value > 0.0 ? "+" : "") + juce::String (value, 0);
}

// One row of numbers over the PSR trace: the current PSR and what it is, then the session facts
// that barely move (PLR, CORR) as numbers only.
void paintPsrReadout (juce::Graphics& g,
                      const AuxiliaryLaneGeometry& lane,
                      const std::vector<KirinMeterHistoryEntry>& history,
                      bool delta,
                      double sessionPlr,
                      presentation::Context presentation)
{
    const auto row = psrReadout (presentation, lane.readout, delta);
    g.setFont (monoFont (presentation, typography::TextRole::readout,
                         typography::Composition::visualization));
    g.setColour (COL_COPPER);
    text_style::drawText (g, "PSR " + latestText (history, Metric::psr, delta) + " dB",
                          row.value, juce::Justification::centredLeft);
    auto facts = row.facts;
    if (! facts.isEmpty())
    {
        const auto correlationText = "CORR " + latestText (history, Metric::correlation, delta);
        g.setColour (COL_SPECTRUM_SIDE);
        text_style::drawText (g, correlationText, facts, juce::Justification::centredRight);
        if (! delta)
        {
            const auto plr = std::isfinite (sessionPlr) ? juce::String (sessionPlr, 1)
                                                         : juce::String ("---");
            g.setColour (COL_TEXT_SECONDARY);
            text_style::drawText (g, "PLR " + plr + " dB", facts, juce::Justification::centredLeft);
        }
    }
    if (! row.definition.isEmpty())
    {
        g.setFont (monoFont (presentation, typography::TextRole::body,
                             typography::Composition::visualization));
        g.setColour (COL_TEXT_TERTIARY);
        text_style::drawText (g, psrDefinition (delta), row.definition,
                              juce::Justification::centredLeft);
    }
}

void paintPsrLane (juce::Graphics& g,
                   const AuxiliaryLaneGeometry& lane,
                   const std::vector<KirinMeterHistoryEntry>& history,
                   const HistoryAxis& axis,
                   bool delta,
                   double sessionPlr,
                   presentation::Context presentation)
{
    g.setColour (COL_MUTED.withAlpha (0.16f));
    g.fillRoundedRectangle (lane.bounds.toFloat(), 2.0f);
    paintPsrReadout (g, lane, history, delta, sessionPlr, presentation);

    const auto plot = lane.data;
    // Delta keeps its zero; absolute PSR keeps its middle, 10 dB, as the one reference line.
    const auto reference = delta ? 0.0 : 10.0;
    g.setColour (COL_MUTED.withAlpha (0.28f));
    g.drawHorizontalLine (juce::roundToInt (psrY (plot, reference, delta)),
                          plot.getX(), plot.getRight());

    juce::Path path;
    bool open = false;
    uint64_t previousGeneration = 0u;
    uint64_t previousRun = 0u;
    for (size_t index = 0u; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        const auto value = entry.psr.mean;
        if (! std::isfinite (value))
        {
            open = false;
            continue;
        }
        const auto x = xFor (plot, entry, axis, index, history.size());
        const auto y = psrY (plot, value, delta);
        const bool newRun = ! open || entry.generation != previousGeneration
                         || entry.run_id != previousRun;
        if (newRun) path.startNewSubPath (x, y); else path.lineTo (x, y);
        open = true;
        previousGeneration = entry.generation;
        previousRun = entry.run_id;
    }
    // Copper is a thin-line colour: no glow band and no filled range under the trace.
    g.setColour (COL_COPPER.withAlpha (0.92f));
    g.strokePath (path, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    auto axisArea = lane.axis;
    const auto top = scaleText (psrTop (delta), delta);
    const auto bottom = scaleText (psrBottom (delta), delta);
    // A lane too short for both ends keeps the top one, at the top where its line is.
    const auto axisLine = juce::roundToInt (std::ceil (typography::resolve (
        presentation, typography::TextRole::axis, typography::Composition::visualization).lineHeight));
    if (axisArea.getHeight() < 2 * axisLine)
    {
        text_style::drawText (g, top, axisArea.withHeight (axisLine), juce::Justification::centredRight);
        return;
    }
    text_style::drawText (g, top, axisArea.removeFromTop (axisArea.getHeight() / 2),
                          juce::Justification::centredRight);
    text_style::drawText (g, bottom, axisArea, juce::Justification::centredRight);
}

// The 3 s correlation keeps only one mark in the history: a lilac tick on the floor of the plot
// for each stretch where it fell below zero (more SIDE than MID), at that stretch's lowest point.
// Nothing is drawn while it stays at or above zero.
void paintCorrelationBelowZero (juce::Graphics& g,
                                juce::Rectangle<float> plot,
                                const std::vector<KirinMeterHistoryEntry>& history,
                                const HistoryAxis& axis)
{
    std::optional<size_t> lowest;
    const auto mark = [&]
    {
        if (! lowest.has_value()) return;
        const auto x = xFor (plot, history[*lowest], axis, *lowest, history.size());
        g.setColour (COL_SPECTRUM_SIDE.withAlpha (0.94f));
        g.fillRoundedRectangle (x - 1.0f, plot.getBottom() - 6.0f, 2.0f, 6.0f, 1.0f);
        lowest.reset();
    };
    for (size_t index = 0u; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        if (index > 0u && (entry.generation != history[index - 1u].generation
                           || entry.run_id != history[index - 1u].run_id))
            mark();
        const auto value = entry.correlation.min;
        if (! std::isfinite (value) || value >= 0.0)
        {
            mark();
            continue;
        }
        if (! lowest.has_value() || value < history[*lowest].correlation.min)
            lowest = index;
    }
    mark();
}

void paintAxes (juce::Graphics& g, juce::Rectangle<float> plot, bool delta,
                bool detailedAxes, meter_context::ScaleMode scaleMode,
                presentation::Context presentation)
{
    constexpr std::array<const char*, 5> difference { "+12", "+6", "0", "-6", "-12" };
    const auto floor = meter_context::loudnessFloor (scaleMode);
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    for (size_t index = 0u; index < difference.size(); ++index)
    {
        if (plot.getHeight() < 100.0f && index % 2u != 0u) continue;
        const float proportion = (float) index / (float) (difference.size() - 1u);
        const int y = juce::roundToInt (plot.getY() + proportion * plot.getHeight());
        const bool zero = delta && index == 2u;
        g.setColour ((zero ? COL_FLORA_BR : COL_MUTED)
                         .withAlpha (zero ? 0.42f
                                         : index == 0u || index == 4u ? 0.34f : 0.20f));
        g.drawHorizontalLine (y, plot.getX(), plot.getRight());
        if (detailedAxes)
        {
            g.setColour (COL_TEXT_TERTIARY);
            const auto level = floor * (double) index / 4.0;
            const auto loudness = juce::String (level == 0.0 ? 0.0 : level, 0); // never "-0"
            text_style::drawText (g, delta ? juce::String (difference[index]) : loudness,
                        juce::roundToInt (plot.getX()) - 32, y - 7,
                        28, 14, juce::Justification::centredRight);
            if (delta)
                text_style::drawText (g, difference[index], juce::roundToInt (plot.getRight()) + 4,
                            y - 7, 28, 14, juce::Justification::centredLeft);
        }
    }
    if (! detailedAxes || delta)
        return;
    constexpr std::array<double, 6> peakTicks { 6.0, 0.0, -6.0, -12.0, -18.0, -24.0 };
    for (const auto value : peakTicks)
    {
        if (plot.getHeight() < 100.0f && std::abs (value) > 0.01 && value > -23.99) continue;
        const auto y = juce::roundToInt (
            yFor (plot, Metric::truePeak, value, false, scaleMode));
        g.setColour ((value == 0.0 ? COL_TRUE_PEAK : COL_MUTED).withAlpha (0.78f));
        const auto label = juce::String (value > 0.0 ? "+" : "") + juce::String (value, 0);
        text_style::drawText (g, label, juce::roundToInt (plot.getRight()) + 4, y - 7,
                    28, 14, juce::Justification::centredLeft);
    }
}

void paintMetric (juce::Graphics& g,
                  juce::Rectangle<float> plot,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const MetricVisual& visual,
                  const HistoryAxis& axis,
                  bool delta,
                 meter_context::ScaleMode scaleMode,
                 presentation::Context presentation)
{
    juce::Path mean;
    juce::Path ranges;
    bool open = false;
    uint64_t previousGeneration = 0u;
    uint64_t previousRun = 0u;
    float lastX = 0.0f;
    float lastY = 0.0f;
    bool haveLast = false;
    for (size_t index = 0u; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        const auto& range = rangeFor (entry, visual.metric);
        const float x = xFor (plot, entry, axis,
                              index, history.size());
        if (! std::isfinite (range.mean))
        {
            open = false;
            continue;
        }
        if (entry.observation_count > 1u
            && std::isfinite (range.min) && std::isfinite (range.max))
        {
            const auto top = yFor (plot, visual.metric, range.max, delta, scaleMode);
            const auto bottom = yFor (plot, visual.metric, range.min, delta, scaleMode);
            if (top < bottom)
                ranges.addRectangle ((float) juce::roundToInt (x), top, 1.0f, bottom - top);
        }
        // Missing data breaks only this metric. Generation/run boundaries still break every
        // metric at the same factual endpoint without inventing a cross-metric validity rule.
        const float y = yFor (plot, visual.metric, range.mean, delta, scaleMode);
        const bool newRun = ! open || entry.generation != previousGeneration
                         || entry.run_id != previousRun;
        if (newRun)
            mean.startNewSubPath (x, y);
        else
            mean.lineTo (x, y);
        open = true;
        previousGeneration = entry.generation;
        previousRun = entry.run_id;
        lastX = x;
        lastY = y;
        haveLast = true;
    }
    // One retained path preserves every exact min/max column while avoiding dozens of separate
    // Direct2D brush/transform submissions per metric on Windows.
    g.setColour (visual.colour.withAlpha (0.16f));
    g.fillPath (ranges);
    g.setColour (visual.colour.withAlpha (0.14f));
    g.strokePath (mean, juce::PathStrokeType (visual.glowWidth,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (visual.colour.withAlpha (0.92f));
    g.strokePath (mean, juce::PathStrokeType (visual.lineWidth,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    if (haveLast)
    {
        g.setColour (visual.colour.withAlpha (0.20f));
        g.fillEllipse (lastX - 4.5f, lastY - 4.5f, 9.0f, 9.0f);
        g.setColour (visual.colour);
        g.fillEllipse (lastX - 1.7f, lastY - 1.7f, 3.4f, 3.4f);
        if (plot.getHeight() >= 100.0f && visual.metric != Metric::truePeak)
        {
            const auto offset = visual.metric == Metric::momentary ? -12 : 3;
            g.setFont (monoFont (presentation, typography::TextRole::legend,
                                 typography::Composition::visualization));
            text_style::drawText (g, visual.label, juce::roundToInt (lastX) - 22,
                        juce::roundToInt (lastY) + offset, 18, 10,
                        juce::Justification::centredRight);
        }
    }
}

void paintLegend (juce::Graphics& g,
                  juce::Rectangle<int> area,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const juce::String& rangeLabel,
                  const MetricVisual* firstVisual,
                  const MetricVisual* endVisual,
                  const HistoryAxis& axis,
                  bool delta,
                  bool compact,
                  presentation::Context presentation)
{
    auto left = area;
    const auto range = left.removeFromRight (legendBasisWidth (area.getWidth(), compact));
    const int metricWidth = compact ? 42 : juce::jmin (72, left.getWidth() / 3);
    g.setFont (monoFont (presentation, typography::TextRole::legend,
                         typography::Composition::visualization));
    for (auto* visual = firstVisual; visual != endVisual; ++visual)
    {
        auto cell = left.removeFromLeft (metricWidth);
        g.setColour (visual->colour);
        const auto text = compact ? juce::String (visual->label)
                                  : juce::String (visual->label) + " "
                                      + latestText (history, visual->metric, delta);
        text_style::drawText (g, text, cell, juce::Justification::centredLeft);
    }
    g.setColour (COL_TEXT_TERTIARY);
    const auto basis = delta
        ? juce::String ("POST-PRE / ") + axisLabel (axis.mode)
        : juce::String ("  ") + axisLabel (axis.mode);
    text_style::drawText (g, rangeLabel + (compact ? "" : basis), range,
                juce::Justification::centredRight);
}
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const std::vector<KirinMeterHistoryEntry>& history,
            const juce::String& rangeLabel,
            bool delta,
            bool compactMeter,
            meter_context::ScaleMode scaleMode,
            presentation::Context presentation,
            const juce::String& comparisonStatus,
            bool momentary,
            bool mainWindow,
            double sessionPlr)
{
    surface_material::paintPanel (g, area.toFloat(), compactMeter ? 0.96f : 0.76f);
    if (mainWindow) main_frame::paint (g, area.toFloat());
    if (delta && comparisonStatus.isNotEmpty())
    {
        auto statusArea = area.removeFromTop (compactMeter ? 18 : 22);
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentation, typography::TextRole::status,
                             typography::Composition::visualization));
        text_style::drawEllipsized (g, comparisonStatus, statusArea.reduced (4, 1),
                                    juce::Justification::centred);
    }
    const auto geometry = makeGeometry (area, compactMeter, presentation);
    area = geometry.content;
    if (history.empty())
    {
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentation, typography::TextRole::status,
                             typography::Composition::visualization));
        const auto emptyText = delta
            ? juce::String ("EXACT ") + hypha::delta() + " HISTORY " + hypha::emDash()
            : juce::String ("HISTORY ") + hypha::emDash();
        text_style::drawText (g, emptyText, area, juce::Justification::centred);
        return;
    }

    const std::array<MetricVisual, 3> visuals {{
        { Metric::momentary, "M", COL_SPECTRUM_POST, 4.0f, 1.35f },
        { Metric::shortTerm, "S", COL_NORMAL, 3.0f, 1.05f },
        { Metric::truePeak, "TP", COL_TRUE_PEAK, 2.4f, 0.9f },
    }};
    const auto* firstVisual = momentary ? visuals.data() : visuals.data() + 1;
    const auto* endVisual = visuals.data() + visuals.size();
    const auto axis = selectAxis (history);
    paintLegend (g, geometry.legend, history, rangeLabel,
                 firstVisual, endVisual, axis, delta, compactMeter, presentation);
    paintAxes (g, geometry.mainPlot, delta,
               ! compactMeter && geometry.mainPlot.getHeight() >= 55.0f, scaleMode,
               presentation);

    for (auto* visual = firstVisual; visual != endVisual; ++visual)
        paintMetric (g, geometry.mainPlot, history, *visual, axis, delta, scaleMode, presentation);
    if (! compactMeter)
    {
        if (! delta)
            paintCorrelationBelowZero (g, geometry.mainPlot, history, axis);
        paintPsrLane (g, geometry.psr, history, axis, delta, sessionPlr, presentation);
    }

}
}
