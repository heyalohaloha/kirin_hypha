#include "HyphaReferenceComponent.h"
#include "HyphaKeyLight.h"

#include "HyphaTheme.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

namespace hypha::reference_ui
{
void Component::configureVisualNavigation()
{
    viewButton.setComponentID ("reference-visual-slot");
    viewButton.setTitle ("Visual comparison");
    viewButton.onClick = [this]
    {
        const bool captured = current.captureAccess && current.captureAccess->capturedView;
        if (onSelectVisualSlot) onSelectVisualSlot (!captured && current.comparisonSlot == 2 ? 1 : 2);
    };
    addChildComponent (viewButton);
}

void Component::updateVisualNavigation (bool enabled)
{
    viewButton.setVisible (enabled && current.separateComparisons && current.libraryReceived);
    const bool captured = current.captureAccess && current.captureAccess->capturedView;
    viewButton.setButtonText (captured ? "VIEW HELD" : current.comparisonSlot == 2 ? "VIEW A/C" : "VIEW A/B");
    viewButton.setTooltip (captured ? "Showing captured A. Switch to live A/C without changing audio."
        : current.comparisonSlot == 2
        ? "Showing the Preset's A/C visuals. Switch to A/B without changing audio."
        : "Showing A/B. Switch to the Preset's A/C visuals without changing audio.");
}

Component::SideButton::SideButton (const juce::String& text) : juce::TextButton (text)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Component::SideButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const key_light::Scope light (*this);
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const bool selected = getToggleState();
    const bool separateTrial = getComponentID() == "reference-blind";
    const auto accent = separateTrial || attention ? COL_FLORA_BR : COL_SPECTRUM_DELTA_BR;
    surface_material::paintControl (g, area, highlighted && ready, down && ready, selected, accent, 4.0f);
    g.setColour (! isEnabled() || ! ready ? COL_MUTED.withAlpha (0.32f)
                               : selected ? COL_OBSERVATORY_VALUE
                                          : separateTrial || attention ? COL_FLORA_BR : COL_NORMAL);
    g.setFont (labelFont (presentationContext, typography::TextRole::action,
                          typography::Composition::information));
    text_style::drawText (g, getButtonText(), getLocalBounds(), juce::Justification::centred);
}
}
