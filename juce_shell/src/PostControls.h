#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"

// B-054: POST button row, element-for-element with hypha_post draw_button_row.
// States (license-gated; Os = license code 0, Sense = 1):
//   Watch  (not recording): [Keep] (Os, only when pair non-empty) | Sense hint (Sense) | nothing (Unknown)
//   Record (recording):     [Stop] [Note] (Os)  →  Note opens [Good] [Fix] [Hold] [Cancel]
// The playback pair-lock does NOT affect these (約束 5 #5: Stop/Keep/Note stay available); only
// the pair-name field is locked during playback (handled in the editor). All actions are routed
// back to the processor via the callbacks; no measurement logic lives here (R-12 / R-22).
namespace hypha
{
    class HyphaTextButton : public juce::TextButton
    {
    public:
        explicit HyphaTextButton (const juce::String& text, bool framed = true);

        void paintButton (juce::Graphics& g,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    private:
        bool framed = true;
    };

    class PostControls : public juce::Component
    {
    public:
        PostControls();

        std::function<void()>                     onKeep;       // -> kirin_hypha_keep
        std::function<void()>                     onStop;       // -> kirin_hypha_stop
        std::function<void (const juce::String&)> onNote;       // tag in {Good,Fix,Hold} -> add_annotation
        std::function<void()>                     onSenseHint;  // open upsell URL

        // Rebuild visibility for the current state. license: 0=Os 1=Sense 2=Unknown.
        void update (bool recording, int license, bool pairNonEmpty);

        void resized() override;

    private:
        void layoutVisible();

        HyphaTextButton keepBtn   { "Keep" };
        HyphaTextButton stopBtn   { "Stop" };
        HyphaTextButton noteBtn   { "Note" };
        HyphaTextButton goodBtn   { "Good" };
        HyphaTextButton fixBtn    { "Fix" };
        HyphaTextButton holdBtn   { "Hold" };
        HyphaTextButton cancelBtn { "Cancel" };
        HyphaTextButton senseBtn  { juce::CharPointer_UTF8 ("Record mode available in Kirin OS"), false };

        bool notePickerOpen = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PostControls)
    };
}
