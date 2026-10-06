#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTextStyle.h"

namespace hypha
{
class HyphaTextButton : public juce::TextButton
{
public:
    explicit HyphaTextButton (const juce::String& text, bool framed = true,
                             text_style::LabelPolicy = text_style::LabelPolicy::localized);

    juce::String displayedText() const { return text_style::shownText (getButtonText(), labelPolicy); }

    void setPresentationContext (presentation::Context next) noexcept
    {
        presentationContext = next;
        repaint();
    }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool framed = true;
    text_style::LabelPolicy labelPolicy = text_style::LabelPolicy::localized;
    presentation::Context presentationContext = presentation::defaultContext();
};
}
