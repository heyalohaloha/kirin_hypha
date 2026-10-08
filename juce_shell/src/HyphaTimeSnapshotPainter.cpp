#include "HyphaTimeSnapshotPainter.h"

#include "HyphaLevelMetricContract.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

#include <array>
#include <cmath>

namespace hypha::time_snapshot
{
namespace
{
constexpr auto visualization = typography::Composition::visualization;

int lineHeight (presentation::Context context, typography::TextRole role)
{
    return text_style::requiredLineHeight (typography::resolve (context, role, visualization), 0);
}

juce::String number (double value, int places, bool signedValue)
{
    if (! std::isfinite (value)) return "---";
    // Keep the number font fixed and normalize zero at the displayed precision.
    const auto scale = std::pow (10.0, places);
    const auto rounded = std::round (value * scale) / scale;
    const auto text = juce::String (std::abs (rounded), places);
    return (rounded < 0.0 ? juce::String::fromUTF8 ("−")
                               : signedValue ? juce::String ("+") : juce::String()) + text;
}

float xFor (const Presentation& presentation, juce::Rectangle<float> plot, std::uint64_t cutoff)
{
    return plot.getX() + static_cast<float> (presentation.normalizedX (cutoff)) * plot.getWidth();
}

float yFor (juce::Rectangle<float> plot, Metric metric, double value, bool delta,
            meter_context::ScaleMode scale)
{
    const double low = metric == Metric::psr ? (delta ? -9.0 : 4.0)
        : delta ? -12.0 : metric == Metric::truePeak ? -24.0 : meter_context::loudnessFloor (scale);
    const double high = metric == Metric::psr ? (delta ? 3.0 : 16.0)
        : delta ? 12.0 : metric == Metric::truePeak ? 6.0 : 0.0;
    return plot.getBottom() - static_cast<float> (juce::jlimit (0.0, 1.0,
        (value - low) / (high - low))) * plot.getHeight();
}

void trace (juce::Graphics& g, const Presentation& presentation, const Component& component,
            juce::Rectangle<float> plot, Metric metric, juce::Colour colour,
            meter_context::ScaleMode scale)
{
    if (plot.isEmpty()) return;
    const bool delta = component.facts.current.target == KIRIN_TIME_DELTA;
    const auto index = rangeIndex (metric);
    juce::Path path, ranges;
    const KirinTimeHistoryEntryV2* previous = nullptr;
    bool open = false;
    for (const auto& entry : component.history)
    {
        const auto& range = entry.ranges[index];
        if (entry.valid_count[index] == 0u || ! std::isfinite (range.mean)
            || entry.first_observed < presentation.packet().range_start
            || entry.last_observed > component.facts.current.cutoff)
        {
            open = false;
            previous = nullptr;
            continue;
        }
        const auto x = xFor (presentation, plot, entry.last_observed);
        const auto y = yFor (plot, metric, range.mean, delta, scale);
        if (! open || previous == nullptr || ! connects (*previous, entry, index))
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
        if (entry.valid_count[index] > 1u && std::isfinite (range.min)
            && std::isfinite (range.max) && metric != Metric::psr)
        {
            const auto top = yFor (plot, metric, range.max, delta, scale);
            const auto bottom = yFor (plot, metric, range.min, delta, scale);
            if (bottom > top) ranges.addRectangle (x - 0.5f, top, 1.0f, bottom - top);
        }
        previous = &entry;
        open = true;
    }
    if (metric != Metric::psr)
    {
        g.setColour (colour.withAlpha (0.15f));
        g.fillPath (ranges);
        g.strokePath (path, juce::PathStrokeType (3.0f));
    }
    g.setColour (colour.withAlpha (0.92f));
    g.strokePath (path, juce::PathStrokeType (metric == Metric::psr ? 1.1f : 1.25f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void axes (juce::Graphics& g, juce::Rectangle<float> plot, bool delta, bool psr,
           meter_context::ScaleMode scale, presentation::Context context)
{
    g.setFont (monoFont (context, typography::TextRole::axis, visualization));
    const auto line = lineHeight (context, typography::TextRole::axis);
    const auto labelWidth = juce::roundToInt (std::ceil (text_style::shownWidth (g.getCurrentFont(), juce::String::fromUTF8 ("−72"))));
    for (unsigned tick = 0; tick < 3u; ++tick)
    {
        const auto value = psr ? (delta ? 3.0 - tick * 6.0 : 16.0 - tick * 6.0)
                               : delta ? 12.0 - tick * 12.0
                                       : meter_context::loudnessFloor (scale) * tick / 2.0;
        const auto y = yFor (plot, psr ? Metric::psr : Metric::shortTerm, value, delta, scale);
        g.setColour (COL_MUTED.withAlpha (tick == 1u ? 0.20f : 0.28f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        if (tick == 1u || (tick == 2u && plot.getHeight() < 2 * line)) continue;
        g.setColour (COL_TEXT_TERTIARY);
        const auto label = number (value, 0, delta && value != 0.0);
        text_style::drawText (g, label,
            juce::Rectangle<int> (juce::roundToInt (plot.getX()) - labelWidth - 2,
                tick == 0u ? juce::roundToInt (plot.getY())
                           : juce::roundToInt (plot.getBottom()) - line, labelWidth, line),
            juce::Justification::centredRight, false);
        if (! psr && ! delta)
        {
            g.setColour (COL_TRUE_PEAK.withAlpha (0.75f));
            text_style::drawText (g, number (tick == 0u ? 6.0 : -24.0, 0, tick == 0u),
                juce::Rectangle<int> (juce::roundToInt (plot.getRight()) + 2,
                    tick == 0u ? juce::roundToInt (plot.getY())
                               : juce::roundToInt (plot.getBottom()) - line, labelWidth, line),
                juce::Justification::centredLeft, false);
        }
    }
}

void timeline (juce::Graphics& g, juce::Rectangle<int> area, const Presentation& presentation,
               const Component& component, presentation::Context context)
{
    g.setFont (monoFont (context, typography::TextRole::axis, visualization));
    g.setColour (COL_TEXT_TERTIARY);
    const auto rate = presentation.packet().post_span.sample_rate;
    if (rate == 0u) return;
    const auto seconds = [&] (std::uint64_t frames)
    { return juce::String (static_cast<double> (frames) / rate, 1) + " s"; };
    auto left = area.removeFromLeft (area.getWidth() / 2);
    text_style::drawText (g, seconds (presentation.packet().range_start), left,
                          juce::Justification::centredLeft);
    const auto text = seconds (presentation.packet().local_cutoff)
        + (component.facts.history_hold != 0u ? " HOLD" : "");
    text_style::drawText (g, text, area, juce::Justification::centredRight);
}
}

Geometry geometry (juce::Rectangle<int> area, presentation::Context context, bool showPsr)
{
    Geometry result;
    auto remaining = area.reduced (6, 2);
    const auto legendHeight = lineHeight (context, typography::TextRole::legend);
    const auto statusHeight = lineHeight (context, typography::TextRole::axis);
    const auto axisHeight = lineHeight (context, typography::TextRole::axis);
    result.mainReadout = remaining.removeFromTop (legendHeight);
    const bool full = context.density == observatory::Density::observatory
                   || context.density == observatory::Density::inspection;
    if (context.density != observatory::Density::compact)
        result.mainFacts = full ? remaining.removeFromTop (legendHeight)
                               : result.mainReadout.removeFromRight (
                                   juce::roundToInt (result.mainReadout.getWidth() * 0.44f));
    if (full || ! showPsr) result.mainStatus = remaining.removeFromTop (statusHeight);
    result.mainAxis = remaining.removeFromBottom (axisHeight);
    if (showPsr)
    {
        const auto rowHeight = lineHeight (context, psrReadoutRole);
        const auto plotSpace = juce::jmax (0, (remaining.getHeight() - rowHeight
                                               - statusHeight - 4) / 2);
        auto psr = remaining.removeFromBottom (rowHeight + statusHeight + plotSpace);
        result.psrReadout = psr.removeFromTop (rowHeight);
        result.psrHelp = result.psrReadout.removeFromRight (rowHeight);
        result.psrStatus = psr.removeFromTop (statusHeight);
        result.psrPlot = psr.toFloat();
        remaining.removeFromBottom (4);
    }
    result.mainPlot = remaining.toFloat();
    const auto axisFont = monoFont (context, typography::TextRole::axis, visualization);
    const auto inset = std::ceil (text_style::shownWidth (axisFont, juce::String::fromUTF8 ("−72"))) + 2.0f;
    result.mainPlot.reduce (inset, 0.0f);
    result.psrPlot.reduce (inset, 0.0f);
    return result;
}

juce::String targetText (std::uint8_t target)
{
    return target == KIRIN_TIME_DELTA ? juce::String::fromUTF8 ("Δ")
         : target == KIRIN_TIME_PRE ? "PRE" : "POST";
}

juce::String currentText (const Component& component, Metric metric, bool includeTarget)
{
    const bool delta = component.facts.current.target == KIRIN_TIME_DELTA;
    const auto text = number (component.value (metric), metric == Metric::correlation ? 2 : 1,
                              delta || metric == Metric::correlation);
    return (includeTarget ? targetText (component.facts.current.target) + " " : juce::String()) + text;
}

juce::String currentReason (const Component& component)
{
    if (component.facts.current.state == KIRIN_TIME_CURRENT_WAITING
        || component.facts.current.state == KIRIN_TIME_CURRENT_MISSING)
    {
        switch (component.facts.reason)
        {
            case KIRIN_TIME_REASON_WAITING: return "Waiting for corresponding PRE observation";
            case KIRIN_TIME_REASON_STOPPED: return "PRE observation stopped; history held";
            case KIRIN_TIME_REASON_INCOMPATIBLE: return "PRE and POST observations are incompatible";
            case KIRIN_TIME_REASON_MISSING: return "PRE observation unavailable";
            default: break;
        }
    }
    switch (component.facts.current.state)
    {
        case KIRIN_TIME_CURRENT_LIVE: return {};
        case KIRIN_TIME_CURRENT_WAITING: return "Waiting for corresponding observation";
        case KIRIN_TIME_CURRENT_STOPPED: return "Stopped; history held";
        case KIRIN_TIME_CURRENT_EXPIRED: return "Observation expired; history held";
        default: return "Observation unavailable";
    }
}

juce::String helpAt (juce::Rectangle<int> area, const Presentation& presentation,
                     presentation::Context context, juce::Point<int> point)
{
    const auto layout = geometry (area, context, presentation.psrVisible());
    if (layout.psrHelp.contains (point) || layout.psrReadout.contains (point))
    {
        auto help = text_style::shownText ("PSR = 400 ms sample peak minus 3 s loudness at one 100 ms point. "
            "PSR independently compares POST minus PRE when the same moment is verified; "
            "otherwise it shows the local PRE or POST observation. Waiting never substitutes POST.");
        const auto reason = currentReason (presentation.psr());
        if (reason.isNotEmpty()) help += "\n" + text_style::shownText (reason);
        return help;
    }
    if (layout.psrPlot.contains (point.toFloat()))
        return "PSR history stops at its verified cutoff. Gaps and different runs are not connected.";
    if (layout.mainFacts.contains (point))
        return "PLR/CORR follow main. TIME PLR: processed Session prefix at that completed 100 ms point; LEVEL: latest Session. PSR is independent.";
    if (layout.mainReadout.contains (point) || layout.mainPlot.contains (point.toFloat()))
        return "M, S and TP share one observation. The common time axis follows the local cutoff; "
               "a delayed PSR comparison does not stop POST history.";
    return {};
}

void paint (juce::Graphics& g, juce::Rectangle<int> area, const Presentation& presentation,
            meter_context::ScaleMode scale, presentation::Context context,
            const juce::String& rangeLabel)
{
    surface_material::paintPanel (g, area.toFloat(), 0.84f);
    main_frame::paint (g, area.toFloat());
    const auto layout = geometry (area, context, presentation.psrVisible());
    const auto& main = presentation.main();
    const auto& psr = presentation.psr();
    const bool mainDelta = main.facts.current.target == KIRIN_TIME_DELTA;
    const bool momentary = context.density != observatory::Density::compact;
    g.setFont (monoFont (context, typography::TextRole::legend, visualization));
    auto legend = layout.mainReadout;
    const auto labelArea = ! momentary ? legend.removeFromRight (juce::jmin (84, legend.getWidth() / 3))
                                      : juce::Rectangle<int>();
    g.setColour (COL_TEXT_TERTIARY);
    text_style::drawText (g, rangeLabel, labelArea, juce::Justification::centredRight);
    const std::array<Metric, 3> metrics { Metric::momentary, Metric::shortTerm, Metric::truePeak };
    const std::array<const char*, 3> names { "M", "S", "TP" };
    const std::array<juce::Colour, 3> colours { COL_SPECTRUM_POST, COL_NORMAL, COL_TRUE_PEAK };
    const auto cellWidth = legend.getWidth() / (momentary ? 3 : 2);
    for (unsigned index = momentary ? 0u : 1u; index < 3u; ++index)
    {
        g.setColour (colours[index]);
        text_style::drawText (g, juce::String (names[index]) + " " + currentText (main, metrics[index]),
            legend.removeFromLeft (cellWidth), juce::Justification::centredLeft);
    }
    if (! layout.mainFacts.isEmpty())
    {
        g.setColour (COL_TEXT_SECONDARY);
        auto facts = layout.mainFacts;
        auto plr = facts.removeFromLeft (juce::roundToInt (facts.getWidth() * 0.55f));
        if (! mainDelta)
            text_style::drawText (g, "PLR " + currentText (main, Metric::plr) + " dB", plr,
                                  juce::Justification::centredLeft);
        g.setColour (COL_SPECTRUM_SIDE);
        text_style::drawText (g, "CORR " + currentText (main, Metric::correlation), facts,
                              juce::Justification::centredRight);
    }
    g.setFont (monoFont (context, typography::TextRole::axis, visualization));
    g.setColour (COL_TEXT_SECONDARY);
    text_style::drawText (g, currentReason (main), layout.mainStatus, juce::Justification::centredLeft);
    axes (g, layout.mainPlot, mainDelta, false, scale, context);
    for (unsigned index = momentary ? 0u : 1u; index < 3u; ++index)
        trace (g, presentation, main, layout.mainPlot, metrics[index], colours[index], scale);
    timeline (g, layout.mainAxis, presentation, main, context);
    if (! presentation.psrVisible()) return;
    const bool psrDelta = psr.facts.current.target == KIRIN_TIME_DELTA;
    g.setFont (monoFont (context, psrReadoutRole, visualization));
    g.setColour (COL_TEXT_SECONDARY);
    text_style::drawText (g, "PSR " + currentText (psr, Metric::psr, true) + " dB",
                          layout.psrReadout, juce::Justification::centredLeft, false);
    g.setFont (monoFont (context, typography::TextRole::axis, visualization));
    g.setColour (COL_TEXT_TERTIARY);
    text_style::drawText (g, "?", layout.psrHelp, juce::Justification::centred);
    const auto reason = currentReason (psr);
    const auto status = reason.isNotEmpty() ? reason
        : ! psr.currentAvailable (Metric::psr) ? juce::String ("PSR unavailable for this observation")
        : currentReason (main).isNotEmpty() && layout.mainStatus.isEmpty()
            ? targetText (main.facts.current.target) + ": " + currentReason (main)
            : presentation.packet().post_span.sample_rate == 0u ? juce::String()
                : "Observed through " + juce::String (static_cast<double> (psr.facts.current.cutoff)
                    / presentation.packet().post_span.sample_rate, 1) + " s";
    text_style::drawText (g, status, layout.psrStatus, juce::Justification::centredLeft);
    axes (g, layout.psrPlot, psrDelta, true, scale, context);
    trace (g, presentation, psr, layout.psrPlot, Metric::psr, COL_COPPER, scale);
}
}
