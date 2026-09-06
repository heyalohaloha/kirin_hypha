#pragma once

#include "HyphaTheme.h"

namespace hypha::reference_ui
{
class ReferenceSelectorLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    juce::Font getComboBoxFont (juce::ComboBox& box) override
    {
        return displayTextFont (box.getText(), 13.0f);
    }

    juce::Font getPopupMenuFont() override
    {
        return nativeTextFont (ui_contract::menuFontHeight);
    }
};

inline ReferenceSelectorLookAndFeel& referenceSelectorLookAndFeel()
{
    static ReferenceSelectorLookAndFeel lookAndFeel;
    return lookAndFeel;
}
}
