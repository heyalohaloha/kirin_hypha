#pragma once

#include "HyphaTextLookAndFeel.h"
#include "HyphaUiContract.h"

namespace hypha::kirin_popup
{
class LookAndFeel final : public TextLookAndFeel
{
public:
    juce::Font getPopupMenuFont() override
    {
        return nativeTextFont (presentation::forOutput (450, 300, presentation::OutputTarget::popup),
                               typography::TextRole::menu);
    }
};

inline LookAndFeel& appearance()
{
    static LookAndFeel lookAndFeel;
    return lookAndFeel;
}

inline juce::PopupMenu::Options options (juce::Component& anchor, juce::Component& lifetime, int width)
{
    return juce::PopupMenu::Options().withTargetComponent (&anchor).withDeletionCheck (lifetime)
        .withMinimumWidth (juce::jlimit (300, 420, width)).withMaximumNumColumns (1)
        .withStandardItemHeight (ui_contract::pairMenuItemHeight);
}
}
