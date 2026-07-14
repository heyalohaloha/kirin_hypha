#include "PostControls.h"

namespace hypha
{
    HyphaTextButton::HyphaTextButton (const juce::String& text, bool shouldDrawFrame)
        : juce::TextButton (text),
          framed (shouldDrawFrame)
    {
        setWantsKeyboardFocus (false);
    }

    void HyphaTextButton::paintButton (juce::Graphics& g,
                                       bool shouldDrawButtonAsHighlighted,
                                       bool shouldDrawButtonAsDown)
    {
        auto area = getLocalBounds().toFloat().reduced (0.5f);
        const auto fill = findColour (juce::TextButton::buttonColourId, true);
        const auto text = findColour (getToggleState()
                                          ? juce::TextButton::textColourOnId
                                          : juce::TextButton::textColourOffId,
                                      true);

        if (framed)
        {
            auto buttonFill = fill;
            if (shouldDrawButtonAsHighlighted)
                buttonFill = buttonFill.brighter (0.06f);
            if (shouldDrawButtonAsDown)
                buttonFill = buttonFill.darker (0.08f);

            g.setColour (buttonFill);
            g.fillRoundedRectangle (area, 3.0f);
            g.setColour (COL_MUTED.withAlpha (shouldDrawButtonAsHighlighted ? 0.65f : 0.45f));
            g.drawRoundedRectangle (area, 3.0f, 1.0f);
        }

        g.setColour (isEnabled() ? text : COL_MUTED);
        g.setFont (monoFont (framed ? 15.0f : 13.0f));
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (6, 2),
                          juce::Justification::centred, 1, 0.85f);
    }

    PostControls::PostControls()
    {
        auto styleButton = [this] (juce::TextButton& b)
        {
            b.setColour (juce::TextButton::buttonColourId, kFieldFill); // palette-derived (BG lifted)
            b.setColour (juce::TextButton::textColourOnId,  COL_NORMAL);
            b.setColour (juce::TextButton::textColourOffId, COL_NORMAL);
            addChildComponent (b);
        };
        for (auto* b : { &keepBtn, &stopBtn })
            styleButton (*b);

        // Sense hint: frameless amber text (no fill / blends into BG) — opens the upsell URL.
        senseBtn.setColour (juce::TextButton::buttonColourId, BG);
        senseBtn.setColour (juce::TextButton::textColourOnId,  COL_FLORA);
        senseBtn.setColour (juce::TextButton::textColourOffId, COL_FLORA);
        addChildComponent (senseBtn);

        keepBtn.onClick = [this] { if (onKeep) onKeep(); };
        stopBtn.onClick = [this] { if (onStop) onStop(); };
        senseBtn.onClick = [this] { if (onSenseHint) onSenseHint(); };
    }

    void PostControls::update (bool recording, int license, bool pairSelected)
    {
        const bool os    = (license == 0);
        const bool sense = (license == 1);

        // Keep owns a fixed layout slot in both AU and VST3. Pair state changes availability, not
        // geometry, so opening or losing a pair never moves controls or metrics around the panel.
        keepBtn  .setVisible (! recording && os);
        keepBtn  .setEnabled (pairSelected);
        senseBtn .setVisible (! recording && sense);

        stopBtn  .setVisible (recording && os);

        layoutVisible();
    }

    void PostControls::resized()
    {
        layoutVisible();
    }

    void PostControls::layoutVisible()
    {
        // Lay visible buttons left-to-right across the row, equal widths, 6px gaps.
        juce::Component* const ordered[] = { &keepBtn, &senseBtn, &stopBtn };
        juce::Array<juce::Component*> visible;
        for (auto* b : ordered)
            if (b->isVisible())
                visible.add (b);

        if (visible.isEmpty())
            return;

        const int gap = 6;
        auto area = getLocalBounds();
        const int n = visible.size();
        const int w = (area.getWidth() - gap * (n - 1)) / n;

        for (int i = 0; i < n; ++i)
        {
            visible[i]->setBounds (area.removeFromLeft (i == n - 1 ? area.getWidth() : w));
            if (i != n - 1)
                area.removeFromLeft (gap);
        }
    }
}
