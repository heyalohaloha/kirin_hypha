#pragma once

#include "HyphaSurfaceMaterial.h"
#include "HyphaTextLookAndFeel.h"
#include "HyphaTheme.h"

namespace hypha::reference_ui
{
class ReferenceSelectorLookAndFeel final : public TextLookAndFeel
{
public:
    void setPresentationContext (presentation::Context next) noexcept { context = next; }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        const key_light::Scope light (button);
        surface_material::paintControl (g, button.getLocalBounds().toFloat().reduced (0.5f),
            highlighted && button.isEnabled(), down && button.isEnabled(), button.getToggleState(),
            button.findColour (juce::TextButton::textColourOffId));
    }

    void drawComboBox (juce::Graphics& g,
                       int width,
                       int height,
                       bool isButtonDown,
                       int,
                       int,
                       int,
                       int,
                       juce::ComboBox& box) override
    {
        const auto area = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                              .reduced (0.5f);
        const key_light::Scope light (box);
        const bool pressed = isButtonDown && box.isEnabled();
        surface_material::paintControl (
            g, area, box.isMouseOver() && box.isEnabled(), pressed, false, COL_FLORA_BR);

        const auto centreY = area.getCentreY() + (pressed ? 1.0f : 0.0f);
        const auto right = area.getRight() - 9.0f;
        g.setColour (box.findColour (juce::ComboBox::arrowColourId)
                         .withAlpha (box.isEnabled() ? 0.88f : 0.28f));
        surface_material::strokeMenuArrow (g, { right - 3.0f, centreY + 1.5f }, 3.0f, 3.5f, 1.1f);
    }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return displayTextFont (box.getText(), context, typography::TextRole::readout,
                                typography::Composition::information);
    }

    juce::Font getPopupMenuFont() override
    {
        return nativeTextFont (presentation::forOutput (
            context.logicalWidth, context.logicalHeight,
            presentation::OutputTarget::popup), typography::TextRole::menu);
    }

private:
    presentation::Context context = presentation::defaultContext();
};
}
