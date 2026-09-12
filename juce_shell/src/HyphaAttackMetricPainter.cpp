#include "HyphaAttackPainter.h"

#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

namespace hypha::attack_painter
{
void drawMetricFact (juce::Graphics& g, juce::Rectangle<int> area,
                     const juce::String& title, const juce::String& value,
                     const juce::String& context, juce::Colour colour,
                     presentation::Context presentation)
{
    const auto justification = juce::Justification::centred;
    const auto labelHeight = text_style::requiredLineHeight (typography::resolve (
        presentation, typography::TextRole::metricLabel,
        typography::Composition::visualization));
    const auto valueHeight = text_style::requiredLineHeight (typography::resolve (
        presentation, typography::TextRole::primaryValue,
        typography::Composition::visualization));
    g.setColour (colour.withAlpha (0.92f));
    g.setFont (monoFont (presentation, typography::TextRole::metricLabel,
                         typography::Composition::visualization));
    g.drawText (title, area.removeFromTop (juce::jmin (labelHeight, area.getHeight())),
                justification);
    g.setColour (colour);
    g.setFont (monoFont (presentation, typography::TextRole::primaryValue,
                         typography::Composition::visualization));
    g.drawText (value, area.removeFromTop (juce::jmin (valueHeight, area.getHeight())),
                justification);
    if (area.getHeight() >= 11 && context.isNotEmpty())
    {
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentation, typography::TextRole::body,
                             typography::Composition::visualization));
        g.drawText (context, area, justification);
    }
}

}
