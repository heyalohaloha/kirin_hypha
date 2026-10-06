#include "HyphaReferenceMetricPainter.h"

#include "HyphaTheme.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

#include <cmath>
#include <limits>

namespace hypha::reference_metric_painter
{
namespace
{
void paintValue (juce::Graphics& g, juce::Rectangle<float> area,
                 const juce::String& heading, double value, const juce::String& unit,
                 juce::Colour colour, float scale, presentation::Context presentation)
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
    text_style::drawText (g, unit, unitArea, juce::Justification::centred);
    g.setColour (std::isfinite (value) ? colour : COL_MUTED);
    drawTabularText (g, monoFont (presentation, typography::TextRole::primaryValue,
                                  typography::Composition::information),
                     fmtVal (value), area, juce::Justification::centred);
}

// 差の列の数字（符号なし。表示の桁で 0 なら 0.0）と、下の段（単位と言葉「LU QUIETER」、0 なら「SAME」）。
struct Difference
{
    reference_ui::AComparison comparison;
    double value = std::numeric_limits<double>::quiet_NaN();
    juce::String bottom;
};

Difference differenceOf (reference_ui::AVersus pair, const juce::String& unit, reference_ui::AWords words, const juce::String& side)
{
    Difference result;
    result.comparison = reference_ui::compareA (pair, 1, " " + unit, words, static_cast<char> (side[0]));
    result.value = ! result.comparison.shown() ? std::numeric_limits<double>::quiet_NaN()
                 : result.comparison.same() ? 0.0 : result.comparison.number.getDoubleValue();
    result.bottom = ! result.comparison.shown() ? unit : result.comparison.same() ? juce::String ("SAME")
                                                                                  : unit + " " + result.comparison.word;
    return result;
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
                  reference_ui::AVersus pair, reference_ui::AWords words, presentation::Context presentation,
                  const juce::String& side)
{
    paintPanel (g, area);
    paintComparisonRoots (g, area);
    const float scale = juce::jlimit (1.0f, 2.2f, area.getHeight() / 170.0f);
    auto header = area.removeFromTop (18.0f * scale);
    g.setColour (COL_NORMAL.withAlpha (0.78f));
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::information));
    text_style::drawText (g, name, header.reduced (9.0f, 0.0f), juce::Justification::centredLeft);
    area.reduce (5.0f, 3.0f);
    const float columnWidth = area.getWidth() / 3.0f;
    paintValue (g, area.removeFromLeft (columnWidth), "A", pair.a, unit,
                COL_OBSERVATORY_VALUE, scale, presentation);
    paintValue (g, area.removeFromLeft (columnWidth), side, pair.other, unit,
                COL_OBSERVATORY_VALUE, scale, presentation);
    const auto difference = differenceOf (pair, unit == "LUFS" ? "LU" : "dB", words, side);
    paintValue (g, area, "A VS " + side, difference.value, difference.bottom,
                COL_SPECTRUM_DELTA_BR, scale * 1.12f, presentation);
}

void paintCompactDelta (juce::Graphics& g, juce::Rectangle<float> area,
                        const juce::String& name, reference_ui::AVersus pair, const juce::String& unit,
                        reference_ui::AWords words, presentation::Context presentation, const juce::String& side)
{
    paintPanel (g, area, 0.72f);
    const auto difference = differenceOf (pair, unit, words, side);
    if (area.getHeight() < 52.0f)
    {
        area.reduce (4.0f, 0.0f);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                              typography::Composition::information));
        const auto labelWidth = std::ceil (text_style::shownWidth (g.getCurrentFont(), name)) + 8.0f;
        text_style::drawText (g, name, area.removeFromLeft (labelWidth), juce::Justification::centredLeft);
        if (difference.comparison.shown())
            reference_ui::paintAComparison (g, difference.comparison, area, juce::Justification::centredRight, presentation,
                                            COL_TEXT_SECONDARY, COL_SPECTRUM_DELTA_BR);
        else
        {
            g.setColour (COL_MUTED);
            g.setFont (monoFont (presentation, typography::TextRole::readout,
                                 typography::Composition::information));
            text_style::drawText (g, fmtVal (difference.value), area, juce::Justification::centredRight);
        }
        return;
    }
    area.reduce (4.0f, 3.0f);
    paintValue (g, area, "A VS " + side + "  " + name, difference.value, difference.bottom,
                COL_SPECTRUM_DELTA_BR, 1.0f, presentation);
}
}
