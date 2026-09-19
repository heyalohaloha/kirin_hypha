#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"

namespace hypha
{
class HyphaTextButton : public juce::TextButton
{
public:
    explicit HyphaTextButton (const juce::String& text, bool framed = true);

    void setPresentationContext (presentation::Context next) noexcept
    {
        presentationContext = next;
        repaint();
    }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool framed = true;
    presentation::Context presentationContext = presentation::defaultContext();
};
}
