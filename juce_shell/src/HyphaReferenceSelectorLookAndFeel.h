#pragma once

#include "HyphaTheme.h"

namespace hypha::reference_ui
{
class ReferenceSelectorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void setPresentationContext (presentation::Context next) noexcept { context = next; }

    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return displayTextFont (box.getText(), context, typography::TextRole::selector,
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
