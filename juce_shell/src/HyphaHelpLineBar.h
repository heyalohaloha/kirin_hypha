#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// The help line at 300% and above (PluginEditorHelpLine.cpp). 2026-10-04 Daisuke chose to give it
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
    // parent's coordinates. An empty text hides it.
    void show (const juce::String& english, juce::Rectangle<int> area, juce::Rectangle<int> footer,
               presentation::Context next)
    {
        const bool changed = english != text || area != getBounds() || footer != footerPanel || ! (next == context);
        text = english;
        footerPanel = footer;
        context = next;
        setBounds (area);
        setVisible (text.isNotEmpty() && ! area.isEmpty());
        if (changed) repaint();
    }

    const juce::String& shownText() const noexcept { return text; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (BG);
        surface_material::paintPanel (g, (footerPanel - getPosition()).toFloat(), 0.76f, 4.0f);
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
        text_style::drawEllipsized (g, text, getLocalBounds().reduced (8, 0), juce::Justification::centredLeft);
    }

private:
    juce::String text;
    juce::Rectangle<int> footerPanel;
    presentation::Context context = presentation::defaultContext();
};
}
