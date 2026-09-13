#include "HyphaReferenceMetricPainter.h"

#include "HyphaTheme.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

namespace hypha::reference_metric_painter
{
namespace
{
juce::String valueText (double value, bool delta)
{
    return delta ? fmtDelta (value) : fmtVal (value);
}

void paintValue (juce::Graphics& g, juce::Rectangle<float> area,
                 const juce::String& heading, double value, const juce::String& unit,
                 juce::Colour colour, bool delta, float scale,
                 presentation::Context presentation)
{
    auto label = area.removeFromTop (14.0f * scale);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::information));
    text_style::draw (g, heading, label.toNearestInt(), presentation,
                      typography::TextRole::metricLabel, juce::Justification::centred,
                      1, typography::Composition::information);
    auto unitArea = area.removeFromBottom (12.0f * scale);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::unit,
                          typography::Composition::information));
    g.drawText (unit, unitArea, juce::Justification::centred);
    g.setColour (std::isfinite (value) ? colour : COL_MUTED);
    drawTabularText (g, monoFont (presentation, typography::TextRole::primaryValue,
                                  typography::Composition::information),
                     valueText (value, delta), area, juce::Justification::centred);
}
}

void paintPanel (juce::Graphics& g, juce::Rectangle<float> area, float alpha)
{
    surface_material::paintPanel (g, area, alpha);
}

void paintComparisonRoots (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto field = area.reduced (18.0f, 28.0f);
    for (int strand = 0; strand < 3; ++strand)
    {
        const float offset = (static_cast<float> (strand) - 1.0f) * field.getHeight() * 0.08f;
        juce::Path root;
        root.startNewSubPath (field.getX(), field.getCentreY() + offset);
        root.cubicTo (field.getX() + field.getWidth() * 0.28f,
                      field.getCentreY() - offset * 1.4f,
                      field.getX() + field.getWidth() * 0.70f,
                      field.getCentreY() + offset * 1.6f,
                      field.getRight(), field.getCentreY() - offset);
        g.setColour ((strand == 1 ? COL_SPECTRUM_POST : COL_FLORA)
                         .withAlpha (strand == 1 ? 0.075f : 0.045f));
        g.strokePath (root, juce::PathStrokeType (0.55f + strand * 0.18f));
    }
}

void paintMetric (juce::Graphics& g, juce::Rectangle<float> area,
                  const juce::String& name, const juce::String& unit,
                  double a, double b, double delta, presentation::Context presentation, const juce::String& side)
{
    paintPanel (g, area);
    paintComparisonRoots (g, area);
    const float scale = juce::jlimit (1.0f, 2.2f, area.getHeight() / 170.0f);
    auto header = area.removeFromTop (18.0f * scale);
    g.setColour (COL_NORMAL.withAlpha (0.78f));
    g.setFont (labelFont (presentation, typography::TextRole::sectionTitle,
                          typography::Composition::information));
    g.drawText (name, header.reduced (9.0f, 0.0f), juce::Justification::centredLeft);
    area.reduce (5.0f, 3.0f);
    const float columnWidth = area.getWidth() / 3.0f;
    paintValue (g, area.removeFromLeft (columnWidth), "A", a, unit,
                COL_OBSERVATORY_VALUE, false, scale, presentation);
    paintValue (g, area.removeFromLeft (columnWidth), side, b, unit,
                COL_OBSERVATORY_VALUE, false, scale, presentation);
    paintValue (g, area, side + "-A", delta, unit == "LUFS" ? "LU" : "dB",
                COL_SPECTRUM_DELTA_BR, true, scale * 1.12f, presentation);
}

void paintCompactDelta (juce::Graphics& g, juce::Rectangle<float> area,
                        const juce::String& name, double value, const juce::String& unit,
                        presentation::Context presentation, const juce::String& side)
{
    paintPanel (g, area, 0.72f);
    if (area.getHeight() < 52.0f)
    {
        area.reduce (4.0f, 0.0f);
        auto label = area.removeFromLeft (area.getWidth() * 0.54f);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                              typography::Composition::information));
        g.drawText (side + "-A " + name, label, juce::Justification::centredLeft);
        g.setColour (std::isfinite (value) ? COL_SPECTRUM_DELTA_BR : COL_MUTED);
        g.setFont (monoFont (presentation, typography::TextRole::readout,
                             typography::Composition::information));
        g.drawText (valueText (value, true), area, juce::Justification::centredRight);
        return;
    }
    area.reduce (4.0f, 3.0f);
    paintValue (g, area, side + "-A  " + name, value, unit,
                COL_SPECTRUM_DELTA_BR, true, 1.0f, presentation);
}
}
