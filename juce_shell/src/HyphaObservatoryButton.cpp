#include "HyphaObservatoryButton.h"
#include "HyphaKeyLight.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

#include <utility>

namespace hypha::observatory
{
namespace
{
// The Reference selector's chevron (6 x 3.5 px, 1.1 px line) at the 200% text height, grown with
// the text so it stays in proportion beside the range label at 300%.
void paintMenuArrow (juce::Graphics& g, juce::Rectangle<float> bounds,
                     const presentation::Context& context)
{
    constexpr auto referenceHeight = typography::resolve (
        presentation::forEditor (600, 400), typography::TextRole::action).fontHeight;
    const auto scale = typography::resolve (context, typography::TextRole::action).fontHeight
                     / referenceHeight;
    const auto rise = 3.5f * scale;
    surface_material::strokeMenuArrow (g, { bounds.getCentreX(), bounds.getCentreY() + rise * 0.5f },
                                       3.0f * scale, rise, 1.1f * scale);
}
}

Button::Button (juce::String text, bool tabIn, Mark markIn)
    : juce::TextButton (std::move (text)), tab (tabIn), mark (markIn)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false); // Keep keyboard traversal, not mouse-driven focus theft.
}

void Button::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const key_light::Scope light (*this);
    highlighted = highlighted && isEnabled();
    down = down && isEnabled();
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const bool selected = getToggleState();
    if (! tab && ! statusOnly)
        surface_material::paintControl (g, area, highlighted, down, selected);
    else if (highlighted)
    {
        g.setColour (kFieldFill.withAlpha (0.34f));
        g.fillRoundedRectangle (area, 2.0f);
    }

    const auto textColour = statusOnly ? COL_NORMAL : ! isEnabled() ? COL_MUTED
                          : selected ? COL_FLORA_BR
                          : isColourSpecified (juce::TextButton::textColourOffId)
                              ? findColour (juce::TextButton::textColourOffId)
                          : highlighted ? COL_NORMAL.withAlpha (0.82f) : COL_TEXT_TERTIARY;
    g.setColour (textColour);
    if (mark == Mark::menuArrow)
        paintMenuArrow (g, getLocalBounds().toFloat(), presentationContext);
    else
    {
        g.setFont (labelFont (presentationContext, typography::TextRole::action));
        text_style::draw (g, getButtonText(), getLocalBounds().reduced (3, 1),
                          presentationContext, typography::TextRole::action,
                          juce::Justification::centred);
    }
    if (tab && selected)
    {
        const float width = juce::jmin (area.getWidth() * 0.66f, 34.0f);
        g.setColour (COL_FLORA_BR.withAlpha (0.92f));
        g.fillRect (area.getCentreX() - width * 0.5f, area.getBottom() - 1.0f, width, 1.0f);
    }
    if (hasKeyboardFocus (true))
    {
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.92f));
        g.drawRoundedRectangle (area.reduced (1.0f), 3.0f, 1.0f);
    }
}
}
