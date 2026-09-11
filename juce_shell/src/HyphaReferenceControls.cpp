#include "HyphaReferenceComponent.h"

#include "HyphaTheme.h"
#include "HyphaSurfaceMaterial.h"
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
    const auto accent = separateTrial ? COL_FLORA_BR : COL_SPECTRUM_DELTA_BR;
    surface_material::paintControl (g, area, highlighted, down, selected, accent, 4.0f);
    g.setColour (! isEnabled() ? COL_MUTED.withAlpha (0.32f)
                               : selected ? COL_OBSERVATORY_VALUE
                                          : separateTrial ? COL_FLORA_BR : COL_NORMAL);
    g.setFont (labelFont (presentationContext, typography::TextRole::action,
                          typography::Composition::information));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}
}
