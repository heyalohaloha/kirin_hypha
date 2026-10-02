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
    text_style::drawText (g, label, area.removeFromTop (compact ? 10 : 16),
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
        text_style::drawText (g, unit, unitArea, juce::Justification::centred);
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
    text_style::drawText (g, shortLabels ? "M>0" : "MID > 0",
                juce::Rectangle<int> { plot.getX(), plot.getY(), plot.getWidth(), rowHeight },
                juce::Justification::centred);
    text_style::drawText (g, shortLabels ? "M<0" : "MID < 0",
                juce::Rectangle<int> { plot.getX(), plot.getBottom() - rowHeight,
                                       plot.getWidth(), rowHeight },
                juce::Justification::centred);
    text_style::drawText (g, shortLabels ? "S<0" : "SIDE < 0", plot.withWidth (sideWidth),
                juce::Justification::centredLeft);
    text_style::drawText (g, shortLabels ? "S>0" : "SIDE > 0",
                plot.withX (plot.getRight() - sideWidth).withWidth (sideWidth),
                juce::Justification::centredRight);
}

// A bare number beside the 100% scatter: its label above, the value in the largest of the
// metric faces that fits the column, no panel.
void drawBareMetric (juce::Graphics& g, juce::Rectangle<int> column, const char* label,
                     const juce::String& value, presentation::Context presentation)
{
    const auto captionFont = labelFont (presentation, typography::TextRole::metricLabel,
                                        typography::Composition::visualization);
    auto valueFont = monoFont (presentation, typography::TextRole::primaryValue,
                               typography::Composition::facts);
    for (const auto composition : { typography::Composition::instrument,
                                    typography::Composition::visualization })
        if (tabularTextWidth (valueFont, value) > (float) column.getWidth())
            valueFont = monoFont (presentation, typography::TextRole::primaryValue, composition);
    const auto labelHeight = juce::roundToInt (captionFont.getHeight());
    const auto valueHeight = juce::roundToInt (valueFont.getHeight()) + 2;
    auto cell = column.withSizeKeepingCentre (column.getWidth(), labelHeight + valueHeight);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::visualization));
    text_style::drawText (g, label, cell.removeFromTop (labelHeight), juce::Justification::centred);
    g.setColour (value == "---" ? COL_MUTED : COL_NORMAL);
    drawTabularText (g, valueFont, value, cell.toFloat(), juce::Justification::centred);
}

// 100% is read at a glance: the scatter takes the whole height, BAL stands on its left and CORR
// on its right as bare numbers. The title row goes; a field still warming states its count in the
// corner, and a field that cannot be drawn states why in its centre.
void paintGlance (juce::Graphics& g, juce::Rectangle<int> area, const KirinMeterSession& meter,
                  bool available, bool fieldAvailable, const juce::String& state,
                  presentation::Context presentation)
{
    area.reduce (6, 4);
    const int column = juce::jlimit (56, 96, (area.getWidth() - area.getHeight()) / 2);
    auto balance = area.removeFromLeft (column);
    auto correlation = area.removeFromRight (column);
    const int side = juce::jmin (area.getWidth(), area.getHeight());
    // The page's main window, in its frame.
    const auto field = juce::Rectangle<int> (0, 0, side, side).withCentre (area.getCentre())
                           .reduced (main_frame::inset());
    main_frame::paint (g, field.toFloat());
    drawPanel (g, field, true);
    const auto plot = field.reduced (8).toFloat();
    drawFieldAxes (g, plot);
    if (fieldAvailable)
        drawDensity (g, plot, meter);
    drawAxisLabels (g, plot.getSmallestIntegerContainer(), true, presentation);
    if (state != "30/30")
    {
        g.setColour (fieldAvailable ? COL_SPECTRUM_POST : COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentation, typography::TextRole::legend,
                             typography::Composition::visualization));
        text_style::drawText (g, state, field.reduced (5, 3),
                    fieldAvailable ? juce::Justification::topLeft : juce::Justification::centred);
    }
    drawBareMetric (g, balance, "BAL", balanceText (meter, available), presentation);
    drawBareMetric (g, correlation, "CORR",
                    available && std::isfinite (meter.correlation)
                        ? juce::String (meter.correlation, 2) : juce::String ("---"),
                    presentation);
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
    const bool glance = compact && presentation.density == observatory::Density::compact;
    if (! glance)
        area.reduce (compact ? 6 : 9, compact ? 5 : 7);
    auto title = glance ? juce::Rectangle<int> {} : area.removeFromTop (compact ? 14 : 18);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (presentation, typography::TextRole::legend,
                         typography::Composition::visualization));
    text_style::drawText (g, compact ? "3 S M/S" : "3 S M/S POLARITY DENSITY",
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
    if (glance)
    {
        paintGlance (g, area, meter, available, fieldAvailable, compactFieldState, presentation);
        return;
    }
    g.setColour (fieldAvailable ? COL_SPECTRUM_POST : COL_MUTED);
    text_style::drawText (g, compact ? compactFieldState : fieldState,
                title, juce::Justification::centredRight);

    const int gap = compact ? 5 : 8;
    // MONO is added only where it costs nothing that SPACE already shows. The scatter answers
    // "wide or narrow" at a glance and stays the panel's own picture; MONO is a chart to read,
    // and it is worth having only once there is room for both to be themselves.
    //
    // With MONO the square keeps the panel's full height on the left, and BAL and CORR side by
    // side, then MONO under them, share the column on its right. MONO used to run under the whole
    // row, which cut the square to half the height and left wide empty sides at 300%. The column
    // has to hold MONO's plot, its six-second field and both boxes beside a full-height square,
    // measured from the panel rather than a size preset: today only the largest editor has it.
    constexpr int monoStripMinimum = 116;
    constexpr int monoColumnMinimum = 360;
    const int fieldInset = compact ? 11 : 16;
    const int metricsHeight = juce::jlimit (96, 150, area.getHeight() / 4);
    const bool showMono = ! compact
                       && area.getWidth() - area.getHeight() - gap >= monoColumnMinimum
                       && area.getHeight() - metricsHeight - gap >= monoStripMinimum;
    juce::Rectangle<int> field, mono, balance, correlationBox;
    if (showMono)
    {
        field = area.removeFromLeft (area.getHeight());
        area.removeFromLeft (gap);
        auto metrics = area.removeFromTop (metricsHeight);
        area.removeFromTop (gap);
        mono = area;
        balance = metrics.removeFromLeft ((metrics.getWidth() - gap) / 2);
        metrics.removeFromLeft (gap);
        correlationBox = metrics;
    }
    else
    {
        const int metricWidth = juce::jlimit (82, compact ? 102 : 168,
                                              juce::roundToInt (area.getWidth() * 0.31f));
        auto metrics = area.removeFromRight (metricWidth);
        area.removeFromRight (gap);
        const int side = juce::jmin (area.getWidth(), area.getHeight());
        field = juce::Rectangle<int> (0, 0, side, side).withCentre (area.getCentre());
        balance = metrics.removeFromTop ((metrics.getHeight() - gap) / 2);
        metrics.removeFromTop (gap);
        correlationBox = metrics;
    }
    field.reduce (main_frame::inset(), main_frame::inset()); // the page's main window, in its frame
    main_frame::paint (g, field.toFloat());
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
        text_style::drawText (g, fieldState, plot.getSmallestIntegerContainer(),
                    juce::Justification::centred);
    }
    if (showMono)
    {
        drawPanel (g, mono, compact);
        mono_sum_curve::paint (g, mono.reduced (4, 3), meter, monoHistory, available, false,
                               true, presentation);
    }

    drawMetric (g, balance, compact ? "BAL" : "L/R BALANCE",
                balanceText (meter, available), "dB L/R", compact, presentation);
    const auto correlation = available && std::isfinite (meter.correlation)
        ? juce::String (meter.correlation, 2) : juce::String ("---");
    drawMetric (g, correlationBox, compact ? "CORR" : "CORRELATION", correlation, "3 S",
                compact, presentation);
}
}
