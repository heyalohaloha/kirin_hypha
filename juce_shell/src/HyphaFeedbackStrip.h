#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// At the compact sizes the footer folds into the header, whose row has no room for a sentence.
// Feedback (a toast, a persistent status) is shown there as one line over the bottom edge of the
// body, across its full width, only while it lasts. It sits above the analysis pages, hides what
// is under it rather than blending with it, and a click opens the same details as the footer's
// status line does at the larger sizes.
namespace hypha
{
class FeedbackStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void()> onClick;

    FeedbackStrip()
    {
        setComponentID ("feedback-strip");
        setWantsKeyboardFocus (false);
    }

    void setFeedback (const juce::String& text)
    {
        if (feedback == text)
            return;
        feedback = text;
        setTitle (text);
        setTooltip (text);
        repaint();
    }

    void setPresentationContext (presentation::Context next) noexcept
    {
        context = next;
        repaint();
    }

    const juce::String& text() const noexcept { return feedback; }

    void paint (juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour (BG.withAlpha (0.94f));
        g.fillRect (area);
        g.setColour (COL_MUTED.withAlpha (0.36f));
        g.fillRect (area.withHeight (1.0f));
        g.setColour (COL_NORMAL);
        g.setFont (monoFont (context, typography::TextRole::status));
        text_style::drawEllipsized (g, feedback, getLocalBounds().reduced (6, 0),
                                    juce::Justification::centredLeft);
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        if (onClick && event.mouseWasClicked() && isEnabled())
            onClick();
    }

private:
    juce::String feedback;
    presentation::Context context = presentation::defaultContext();
};
}
