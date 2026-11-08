#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// The help line at 300% and above (PluginEditorHelpLine.cpp). Since 2026-10-04 it is given
// the whole footer row: pointing at a graph, a value or a tab, the line covers the footer row and
// has room for what the item is and how it is used; pointing at a control in the footer, it covers
// only the status at the left, so the controls stay in view. It paints the footer's own panel under
// the text, so it reads as the footer, and lets the pointer through to what lies under it.
namespace hypha
{
class HelpLineBar final : public juce::Component
{
public:
    HelpLineBar()
    {
        setComponentID ("help-line");
        setInterceptsMouseClicks (false, false);
        setAlwaysOnTop (true);
    }

    // `area`: the part of the footer row it covers; `footer`: the footer row's panel. Both in the
    // parent's coordinates. `mark`: the key of what is pointed at, at the right; the text gives way to
    // it, so the key is never the part cut off. Nothing to show hides it.
    void show (const juce::String& english, juce::Rectangle<int> area, juce::Rectangle<int> footer,
               presentation::Context next, const juce::String& keyMark = {})
    {
        const bool changed = english != text || keyMark != mark || area != getBounds()
            || footer != footerPanel || ! (next == context);
        text = english;
        mark = keyMark;
        footerPanel = footer;
        context = next;
        setBounds (area);
        setVisible ((text.isNotEmpty() || mark.isNotEmpty()) && ! area.isEmpty());
        if (changed) repaint();
    }

    static juce::Colour textColour() noexcept { return COL_NORMAL; }

    const juce::String& shownText() const noexcept { return text; }
    const juce::String& shownMark() const noexcept { return mark; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (BG);
        surface_material::paintPanel (g, (footerPanel - getPosition()).toFloat(), 0.76f, 4.0f);
        g.setColour (textColour());
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        auto row = getLocalBounds().reduced (8, 0);
        if (mark.isNotEmpty())
        {
            const auto width = juce::roundToInt (std::ceil (text_style::shownWidth (g.getCurrentFont(), mark))) + 2;
            g.setColour (COL_FLORA_BR);
            text_style::drawText (g, mark, row.removeFromRight (juce::jmin (width, row.getWidth())),
                                  juce::Justification::centredRight, false);
            row.removeFromRight (12);
            g.setColour (textColour());
        }
        text_style::drawEllipsized (g, text, row, juce::Justification::centredLeft);
    }

private:
    juce::String text, mark;
    juce::Rectangle<int> footerPanel;
    presentation::Context context = presentation::defaultContext();
};
}
