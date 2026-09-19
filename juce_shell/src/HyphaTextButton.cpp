#include "HyphaTextButton.h"

#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

namespace hypha
{
HyphaTextButton::HyphaTextButton (const juce::String& text, bool shouldDrawFrame)
    : juce::TextButton (text),
      framed (shouldDrawFrame)
{
    setWantsKeyboardFocus (true);
}

void HyphaTextButton::paintButton (juce::Graphics& g,
                                   bool shouldDrawButtonAsHighlighted,
                                   bool shouldDrawButtonAsDown)
{
    auto area = getLocalBounds().toFloat().reduced (0.5f);
    const auto textColour = findColour (getToggleState()
                                            ? juce::TextButton::textColourOnId
                                            : juce::TextButton::textColourOffId,
                                        true);

    if (framed)
        surface_material::paintControl (
            g, area, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown,
            getToggleState(), COL_FLORA_BR);

    g.setColour (isEnabled() ? textColour : COL_MUTED);
    g.setFont (monoFont (presentationContext, typography::TextRole::action));
    text_style::draw (g, getButtonText(), getLocalBounds().reduced (6, 2),
                      presentationContext, typography::TextRole::action,
                      juce::Justification::centred);
}
}
