#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "PostControls.h"
#include "local_blind/LocalBlindProductSession.h"

namespace hypha::local_blind_ui
{
// C1 product entry remains closed until the same exact-range PDC proof passes in macOS AU.
// The complete UI is compiled and tested now; changing this one fact opens the large-frame entry.
inline constexpr bool productEntryEnabled = false;

bool blocksDisclosure (const local_blind::ProductSessionView&) noexcept;
bool needsRecoveryScreen (const local_blind::ProductSessionView&) noexcept;

class Component final : public juce::Component
{
public:
    Component();

    std::function<void (bool approveLowerPost)> onStart;
    std::function<void (int stimulus)> onSelectStimulus;
    std::function<void (local_blind::TrialAnswer)> onAnswer;
    std::function<void()> onReveal;
    std::function<void()> onStop;
    std::function<void()> onReturn;
    std::function<void()> onClose;

    void setState (local_blind::ProductSessionView);
    const local_blind::ProductSessionView& state() const noexcept { return current; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshPresentation();
    void styleButton (juce::Button&, const juce::String& id,
                      const juce::String& title);
    void layoutRow (juce::Rectangle<int>, std::initializer_list<juce::Button*>);

    local_blind::ProductSessionView current;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::Label detailLabel;
    juce::Label resultLabel;
    HyphaTextButton sourceOne { "SOURCE 1" };
    HyphaTextButton sourceTwo { "SOURCE 2" };
    HyphaTextButton answerOne { "PREFER 1" };
    HyphaTextButton answerTwo { "PREFER 2" };
    HyphaTextButton noPreference { "NO PREFERENCE" };
    HyphaTextButton cannotDistinguish { "CANNOT TELL" };
    HyphaTextButton startButton { "START BLIND" };
    HyphaTextButton revealButton { "REVEAL" };
    HyphaTextButton stopButton { "STOP" };
    HyphaTextButton returnButton { "RETURN TO LIVE" };
    HyphaTextButton closeButton { "CLOSE" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Component)
};
}
