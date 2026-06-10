#include "PostControls.h"

namespace hypha
{
    PostControls::PostControls()
    {
        auto styleButton = [this] (juce::TextButton& b)
        {
            b.setColour (juce::TextButton::buttonColourId, kFieldFill); // palette-derived (BG lifted)
            b.setColour (juce::TextButton::textColourOnId,  COL_NORMAL);
            b.setColour (juce::TextButton::textColourOffId, COL_NORMAL);
            addChildComponent (b);
        };
        for (auto* b : { &keepBtn, &stopBtn, &noteBtn, &goodBtn, &fixBtn, &holdBtn, &cancelBtn })
            styleButton (*b);

        // Sense hint: frameless amber text (no fill / blends into BG) — opens the upsell URL.
        senseBtn.setColour (juce::TextButton::buttonColourId, BG);
        senseBtn.setColour (juce::TextButton::textColourOnId,  COL_FLORA);
        senseBtn.setColour (juce::TextButton::textColourOffId, COL_FLORA);
        addChildComponent (senseBtn);

        keepBtn.onClick = [this] { if (onKeep) onKeep(); };
        stopBtn.onClick = [this] { if (onStop) onStop(); };
        noteBtn.onClick = [this] { notePickerOpen = true; layoutVisible(); };
        cancelBtn.onClick = [this] { notePickerOpen = false; layoutVisible(); };
        goodBtn.onClick = [this] { if (onNote) onNote ("Good"); notePickerOpen = false; layoutVisible(); };
        fixBtn.onClick  = [this] { if (onNote) onNote ("Fix");  notePickerOpen = false; layoutVisible(); };
        holdBtn.onClick = [this] { if (onNote) onNote ("Hold"); notePickerOpen = false; layoutVisible(); };
        senseBtn.onClick = [this] { if (onSenseHint) onSenseHint(); };
    }

    void PostControls::update (bool recording, int license, bool pairNonEmpty)
    {
        const bool os    = (license == 0);
        const bool sense = (license == 1);

        if (! recording)
            notePickerOpen = false; // picker only exists during Record

        // Compute visibility (parity: show_save_button / show_stop_record_button / show_note_button
        // are all (license == Os); Keep hidden when pair_empty per W-283; Sense hint when Sense).
        keepBtn  .setVisible (! recording && os && pairNonEmpty);
        senseBtn .setVisible (! recording && sense);

        stopBtn  .setVisible (recording && os && ! notePickerOpen);
        noteBtn  .setVisible (recording && os && ! notePickerOpen);

        const bool picker = recording && os && notePickerOpen;
        goodBtn  .setVisible (picker);
        fixBtn   .setVisible (picker);
        holdBtn  .setVisible (picker);
        cancelBtn.setVisible (picker);

        layoutVisible();
    }

    void PostControls::resized()
    {
        layoutVisible();
    }

    void PostControls::layoutVisible()
    {
        // Lay visible buttons left-to-right across the row, equal widths, 6px gaps.
        juce::Component* const ordered[] = { &keepBtn, &senseBtn, &stopBtn, &noteBtn,
                                             &goodBtn, &fixBtn, &holdBtn, &cancelBtn };
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
