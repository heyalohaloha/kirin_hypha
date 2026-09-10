#include "HyphaReferenceComponent.h"

#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

namespace hypha::reference_ui
{
Component::SideButton::SideButton (const juce::String& text) : juce::TextButton (text)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Component::SideButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const bool selected = getToggleState();
    const bool separateTrial = getComponentID() == "reference-blind";
    g.setColour ((selected ? COL_SPECTRUM_POST : kFieldFill)
                     .withAlpha (selected ? 0.24f : highlighted ? 0.86f : 0.62f));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour ((selected ? COL_SPECTRUM_DELTA_BR
                           : separateTrial ? COL_FLORA : COL_MUTED)
                     .withAlpha (isEnabled() ? (down ? 1.0f : 0.82f) : 0.28f));
    g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, selected ? 1.2f : 0.7f);
    g.setColour (! isEnabled() ? COL_MUTED.withAlpha (0.32f)
                               : selected ? COL_OBSERVATORY_VALUE
                                          : separateTrial ? COL_FLORA_BR : COL_NORMAL);
    g.setFont (labelFont (presentationContext, typography::TextRole::action,
                          typography::Composition::information));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}
}
