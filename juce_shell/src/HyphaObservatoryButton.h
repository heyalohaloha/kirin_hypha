#pragma once
#include "HyphaWidgets.h"
namespace hypha::observatory
{
class Button final : public juce::TextButton
{
public:
    // What the button shows: its text, or a drawn menu arrow. The arrow is a path because JUCE 7
    // draws a label in one typeface with no fallback, and Windows' label fonts have no U+25BE:
    // the glyph showed there as an empty box.
    enum class Mark { none, menuArrow };
    Button (juce::String text, bool tabIn, Mark markIn = Mark::none);
    void setPresentationContext (presentation::Context next) noexcept
    {
        if (presentationContext == next) return;
        presentationContext = next;
        repaint();
    }
    float fontHeightForTest() const
    {
        return labelFont (presentationContext, typography::TextRole::action).getHeight();
    }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    // A pending operation is readable status, not a dimmed action to press again.
    void setStatusOnly (bool next)
    {
        if (statusOnly == next) return;
        statusOnly = next;
        setEnabled (! next);
        setMouseCursor (next ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
    bool isStatusOnly() const noexcept { return statusOnly; }

private:
    bool tab = false, statusOnly = false;
    Mark mark = Mark::none;
    presentation::Context presentationContext = presentation::defaultContext();
};

}
