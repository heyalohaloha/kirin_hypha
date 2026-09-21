#include "HyphaSpacePainter.h"

#include "HyphaMonoSumPainter.h"

#include "HyphaTheme.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

#include <cmath>

namespace hypha::space_field
{
int axisLabelWidth (presentation::Context presentation, bool compact)
{
    const auto style = typography::resolve (
        presentation, typography::TextRole::axis,
        typography::Composition::visualization);
    const auto font = monoFont (presentation, typography::TextRole::axis,
                                typography::Composition::visualization);
    return text_style::requiredWidth (font, compact ? "S<0" : "SIDE < 0", style);
}

int axisLabelHeight (presentation::Context presentation)
{
    return text_style::requiredLineHeight (typography::resolve (
        presentation, typography::TextRole::axis,
        typography::Composition::visualization));
}

namespace
{
void drawPanel (juce::Graphics& g, juce::Rectangle<int> area,
                bool compact, float radius = 4.0f)
{
    surface_material::paintPanel (g, area.toFloat(), compact ? 0.96f : 0.76f, radius);
}

juce::String balanceText (const KirinMeterSession& meter, bool available)
{
    if (! available)
        return "---";
    if (meter.balance_state == KIRIN_BALANCE_LEFT_ONLY)
        return "L ONLY";
    if (meter.balance_state == KIRIN_BALANCE_RIGHT_ONLY)
        return "R ONLY";
    if (meter.balance_state != KIRIN_BALANCE_NUMERIC || ! std::isfinite (meter.balance_db))
        return "---";
    return (meter.balance_db >= 0.0 ? "+" : "") + juce::String (meter.balance_db, 1);
}

void drawMetric (juce::Graphics& g,
                 juce::Rectangle<int> area,
                 const char* label,
                 const juce::String& value,
                 const char* unit,
                 bool compact,
                 presentation::Context presentation)
{
    drawPanel (g, area, compact);
    area.reduce (compact ? 5 : 9, compact ? 3 : 7);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::visualization));
    g.drawText (label, area.removeFromTop (compact ? 10 : 16),
                juce::Justification::centredLeft);
    auto unitArea = juce::Rectangle<int> {};
    if (! compact && juce::String (unit).isNotEmpty())
        unitArea = area.removeFromBottom (14);
    g.setColour (value == "---" ? COL_MUTED : COL_NORMAL);
    drawTabularText (g, monoFont (presentation, typography::TextRole::primaryValue,
                                  typography::Composition::visualization), value,
                     area.toFloat(), juce::Justification::centred);
    if (! unitArea.isEmpty())
    {
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (presentation, typography::TextRole::unit,
                              typography::Composition::visualization));
        g.drawText (unit, unitArea, juce::Justification::centred);
    }
}

void drawFieldAxes (juce::Graphics& g, juce::Rectangle<float> plot)
{
    const auto centre = plot.getCentre();
    g.setColour (COL_MUTED.withAlpha (0.30f));
    g.drawLine (centre.x, plot.getY(), centre.x, plot.getBottom(), 0.7f);
    g.drawLine (plot.getX(), centre.y, plot.getRight(), centre.y, 0.7f);

    juce::Path physicalBounds;
    physicalBounds.startNewSubPath (centre.x, plot.getY());
    physicalBounds.lineTo (plot.getRight(), centre.y);
    physicalBounds.lineTo (centre.x, plot.getBottom());
    physicalBounds.lineTo (plot.getX(), centre.y);
    physicalBounds.closeSubPath();
    g.setColour (COL_FLORA.withAlpha (0.23f));
    g.strokePath (physicalBounds, juce::PathStrokeType (0.8f));
}

void drawDensity (juce::Graphics& g,
                  juce::Rectangle<float> plot,
                  const KirinMeterSession& meter)
{
    const float cellW = plot.getWidth() / (float) KIRIN_STEREO_FIELD_SIZE;
    const float cellH = plot.getHeight() / (float) KIRIN_STEREO_FIELD_SIZE;
    for (size_t index = 0; index < KIRIN_STEREO_FIELD_BINS; ++index)
    {
        const auto density = meter.field_density[index];
        if (density == 0u)
            continue;
        const float strength = (float) density / 255.0f;
        const auto row = (float) (index / KIRIN_STEREO_FIELD_SIZE);
        const auto column = (float) (index % KIRIN_STEREO_FIELD_SIZE);
        const auto cell = juce::Rectangle<float> (
            plot.getX() + column * cellW,
            plot.getY() + row * cellH,
            cellW + 0.35f,
            cellH + 0.35f).reduced (0.15f);
        const auto colour = COL_SPECTRUM_POST.interpolatedWith (COL_FLORA_BR,
                                                                strength * 0.40f);
        g.setColour (colour.withAlpha (0.08f + strength * 0.78f));
        g.fillRoundedRectangle (cell, juce::jmin (1.2f, cellW * 0.28f));
    }
}

void drawAxisLabels (juce::Graphics& g, juce::Rectangle<int> plot, bool compact,
                     presentation::Context presentation)
{
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    const auto rowHeight = juce::jmin (axisLabelHeight (presentation), plot.getHeight() / 3);
    // The short form is chosen by what fits, not by which size preset this is. Adding MONO takes
    // height from the scatter, so a large editor can end up with a square too narrow for the long
    // labels; "SIDE ..." names nothing, while "S>0" still does.
    const bool shortLabels = compact
        || plot.getWidth() / 3 < axisLabelWidth (presentation, false);
    const auto sideWidth = juce::jmin (axisLabelWidth (presentation, shortLabels),
                                      plot.getWidth() / 3);
    g.drawText (shortLabels ? "M>0" : "MID > 0",
                juce::Rectangle<int> { plot.getX(), plot.getY(), plot.getWidth(), rowHeight },
                juce::Justification::centred);
    g.drawText (shortLabels ? "M<0" : "MID < 0",
                juce::Rectangle<int> { plot.getX(), plot.getBottom() - rowHeight,
                                       plot.getWidth(), rowHeight },
                juce::Justification::centred);
    g.drawText (shortLabels ? "S<0" : "SIDE < 0", plot.withWidth (sideWidth),
                juce::Justification::centredLeft);
    g.drawText (shortLabels ? "S>0" : "SIDE > 0",
                plot.withX (plot.getRight() - sideWidth).withWidth (sideWidth),
                juce::Justification::centredRight);
}
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const KirinMeterSession& meter,
            const mono_sum_history::History& monoHistory,
            bool available,
            bool compactMeter,
            presentation::Context presentation)
{
    const bool compact = compactMeter;
    drawPanel (g, area, compact);
    area.reduce (compact ? 6 : 9, compact ? 5 : 7);
    auto title = area.removeFromTop (compact ? 14 : 18);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (presentation, typography::TextRole::legend,
                         typography::Composition::visualization));
    g.drawText (compact ? "3 S M/S" : "3 S M/S POLARITY DENSITY",
                title, juce::Justification::centredLeft);
    const bool fieldAvailable = available && meter.channels == 2
                             && meter.field_size == KIRIN_STEREO_FIELD_SIZE
                             && meter.field_observation_count > 0u;
    const auto fieldState = ! available ? juce::String ("FIELD ") + hypha::emDash()
                          : meter.channels != 2 ? juce::String ("MONO INPUT")
                          : meter.field_observation_count < 30u
                              ? "WARMING " + juce::String (meter.field_observation_count) + "/30"
                              : juce::String ("30/30");
    const auto compactFieldState = ! available ? hypha::emDash()
                                 : meter.channels != 2 ? juce::String ("MONO")
                                 : meter.field_observation_count < 30u
                                     ? juce::String (meter.field_observation_count) + "/30"
                                     : juce::String ("30/30");
    g.setColour (fieldAvailable ? COL_SPECTRUM_POST : COL_MUTED);
    g.drawText (compact ? compactFieldState : fieldState,
                title, juce::Justification::centredRight);

    const int gap = compact ? 5 : 8;
    // MONO is added only where it costs nothing that SPACE already shows. The scatter answers
    // "wide or narrow" at a glance and stays the panel's own picture; MONO is a chart to read,
    // and it is worth having only once there is room for both to be themselves.
    //
    // Three conditions, all measured from what the parts lay out rather than from a size preset:
    // MONO needs its title, its frequency row and a plot tall enough to separate 0, -6 and -24;
    // the metric column needs the height drawMetric gives two boxes; and the square left over has
    // to stay wide enough for the scatter's own axis labels. The third is what rules out the
    // 600x400 editor, where the strip fits but pushes "SIDE < 0" down to "S<0". Today only the
    // largest editor clears all three.
    // The strip carries the curve, the six-second field under it and one frequency row for both.
    constexpr int monoStripMinimum = 116;
    constexpr int metricsRowMinimum = 136;
    const int metricWidth = juce::jlimit (82, compact ? 102 : 168,
                                          juce::roundToInt (area.getWidth() * 0.31f));
    // The same inset the square's plot is drawn with below, so the gate cannot disagree with what
    // is actually painted.
    const int fieldInset = compact ? 11 : 16;
    const auto squarePlotWidthAfter = [&] (int stripHeight) {
        const int rowHeight = area.getHeight() - (stripHeight > 0 ? stripHeight + gap : 0);
        return juce::jmin (area.getWidth() - metricWidth - gap, rowHeight) - fieldInset * 2;
    };
    const int monoHeight = juce::jmax (monoStripMinimum,
                                       juce::roundToInt (area.getHeight() * 0.42f));
    const bool showMono = ! compact
                       && area.getHeight() >= monoHeight + gap + metricsRowMinimum
                       && squarePlotWidthAfter (monoHeight) / 3
                              >= axisLabelWidth (presentation, false);
    auto mono = showMono ? area.removeFromBottom (monoHeight) : juce::Rectangle<int> {};
    if (showMono)
        area.removeFromBottom (gap);

    auto metrics = area.removeFromRight (metricWidth);
    area.removeFromRight (gap);

    const int side = juce::jmin (area.getWidth(), area.getHeight());
    auto field = juce::Rectangle<int> (0, 0, side, side).withCentre (area.getCentre());
    drawPanel (g, field, compact);
    auto plot = field.reduced (fieldInset).toFloat();
    drawFieldAxes (g, plot);
    if (fieldAvailable)
        drawDensity (g, plot, meter);
    drawAxisLabels (g, plot.getSmallestIntegerContainer(), compact, presentation);
    if (! fieldAvailable && ! compact)
    {
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentation, typography::TextRole::status,
                             typography::Composition::visualization));
        g.drawText (fieldState, plot.getSmallestIntegerContainer(),
                    juce::Justification::centred);
    }
    if (showMono)
    {
        drawPanel (g, mono, compact);
        mono_sum_curve::paint (g, mono.reduced (4, 3), meter, monoHistory, available, false,
                               true, presentation);
    }

    auto balance = metrics.removeFromTop ((metrics.getHeight() - gap) / 2);
    metrics.removeFromTop (gap);
    drawMetric (g, balance, compact ? "BAL" : "L/R BALANCE",
                balanceText (meter, available), "dB L/R", compact, presentation);
    const auto correlation = available && std::isfinite (meter.correlation)
        ? juce::String (meter.correlation, 2) : juce::String ("---");
    drawMetric (g, metrics, compact ? "CORR" : "CORRELATION", correlation, "3 S",
                compact, presentation);
}
}
