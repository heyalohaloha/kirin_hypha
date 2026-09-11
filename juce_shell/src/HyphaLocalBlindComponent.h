#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaLocalBlindUiContract.h"
#include "HyphaMeterContext.h"
#include "HyphaPresentationContext.h"
#include "PostControls.h"

namespace hypha::local_blind_ui
{
class Component final : public juce::Component
{
public:
    Component();

    void setPresentationContext (presentation::Context next)
    {
        if (presentationContext == next) return;
        presentationContext = next;
        for (auto* button : { &sourceOne, &sourceTwo, &answerOne, &answerTwo,
                              &noPreference, &cannotDistinguish, &startButton, &revealButton,
                              &captureButton, &contextButton, &stopButton, &returnButton,
                              &closeButton })
            button->setPresentationContext (next);
        resized();
        repaint();
    }

    std::function<void (bool approveLowerPost)> onStart;
    std::function<void()> onCapture;
    std::function<void()> onContextMenu;
    std::function<void (int stimulus)> onSelectStimulus;
    std::function<void (local_blind::TrialAnswer)> onAnswer;
    std::function<void()> onReveal;
    std::function<void()> onStop;
    std::function<void()> onReturn;
    std::function<void()> onClose;

    void setState (local_blind::ProductSessionView);
    void setMeterContext (meter_context::MeterContext);
    const local_blind::ProductSessionView& state() const noexcept { return current; }
    juce::Component& contextAnchor() noexcept { return contextButton; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refreshPresentation();
    void styleButton (juce::Button&, const juce::String& id,
                      const juce::String& title);
    void layoutRow (juce::Rectangle<int>, std::initializer_list<juce::Button*>);

    local_blind::ProductSessionView current;
    presentation::Context presentationContext = presentation::defaultContext();
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
    HyphaTextButton captureButton { "CAPTURE 4 S" };
    HyphaTextButton contextButton { "CHANGE CONTEXT" };
    HyphaTextButton stopButton { "STOP" };
    HyphaTextButton returnButton { "RETURN TO LIVE" };
    HyphaTextButton closeButton { "CLOSE" };
    meter_context::MeterContext preflightContext = meter_context::defaultContext;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Component)
};
}
