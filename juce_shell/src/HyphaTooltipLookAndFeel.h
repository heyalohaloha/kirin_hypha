#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"

namespace hypha
{
// JUCE's stock tooltip layout is allowed to grow to 400 px before it is positioned. That is
// wider than Hypha's 300 px editor, so constraining only the origin still lets the text leave the
// plug-in. Build and draw the same wrapped layout against the actual editor width instead.
class TooltipLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    TooltipLookAndFeel()
    {
        setColour (juce::TooltipWindow::backgroundColourId, BG.brighter (0.10f));
        setColour (juce::TooltipWindow::textColourId, COL_NORMAL);
        setColour (juce::TooltipWindow::outlineColourId,
                   COL_SPECTRUM_DELTA.withAlpha (0.48f));
    }

    juce::Rectangle<int> getTooltipBounds (
        const juce::String& text,
        juce::Point<int> position,
        juce::Rectangle<int> parentArea) override
    {
        const auto available = parentArea.reduced (ui_contract::margin);
        const int widthLimit = juce::jmax (1, available.getWidth());
        const auto layout = makeLayout (text, widthLimit - horizontalPadding);
        const int width = juce::jmin (
            widthLimit, juce::roundToInt (std::ceil (layout.getWidth())) + horizontalPadding);
        const int height = juce::jmin (
            available.getHeight(),
            juce::roundToInt (std::ceil (layout.getHeight())) + verticalPadding);
        auto bounds = juce::Rectangle<int> (
            position.x > available.getCentreX() ? position.x - width - pointerGap
                                                : position.x + pointerGap,
            position.y > available.getCentreY() ? position.y - height - pointerGap
                                                : position.y + pointerGap,
            width, height);
        return bounds.constrainedWithin (available);
    }

    void drawTooltip (juce::Graphics& graphics,
                      const juce::String& text,
                      int width,
                      int height) override
    {
        graphics.fillAll (findColour (juce::TooltipWindow::backgroundColourId));
        graphics.setColour (findColour (juce::TooltipWindow::outlineColourId));
        graphics.drawRoundedRectangle (
            juce::Rectangle<float> (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f),
            4.0f, 1.0f);
        auto layout = makeLayout (text, juce::jmax (1, width - horizontalPadding));
        layout.draw (graphics, juce::Rectangle<float> (
            (float) horizontalPadding * 0.5f,
            (float) verticalPadding * 0.5f,
            (float) width - (float) horizontalPadding,
            (float) height - (float) verticalPadding));
    }

private:
    static constexpr int horizontalPadding = 14;
    static constexpr int verticalPadding = 8;
    static constexpr int pointerGap = 8;

    juce::TextLayout makeLayout (const juce::String& text, int width) const
    {
        juce::AttributedString attributed;
        attributed.setJustification (juce::Justification::centred);
        attributed.append (
            text,
            juce::Font (nativeLabelFontFamily(), 11.5f, juce::Font::bold),
            findColour (juce::TooltipWindow::textColourId));
        juce::TextLayout layout;
        layout.createLayoutWithBalancedLineLengths (
            attributed, (float) juce::jmax (1, width));
        return layout;
    }
};
}
