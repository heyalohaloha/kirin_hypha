#pragma once

#include "HyphaTextButton.h"
#include "HyphaTextLookAndFeel.h"
#include "live_compare/LiveCompareProcessorState.h"

namespace hypha::live_blind_ui
{
class Component final : public juce::Component
{
public:
    Component();
    ~Component() override;
    std::function<void(int)> onSelect;
    std::function<void()> onAnswer, onEnd, onApprove;
    void setState (const live_compare::LiveBlindStatus&, bool playing, float postActual);
    juce::Component& answerAnchor() noexcept { return answer; }
    void paint (juce::Graphics&) override;
    void resized() override;
    const live_compare::LiveBlindStatus& state() const noexcept { return current; }

private:
    void refresh();
    TextLookAndFeel look;
    presentation::Context context = presentation::defaultContext();
    live_compare::LiveBlindStatus current;
    bool playing = false;
    float actualPost = 1.0f;
    juce::Label title, status, detail;
    HyphaTextButton one { "SOURCE 1" }, two { "SOURCE 2" };
    HyphaTextButton answer { "ANSWER" }, end { "END" }, approve { "LOWER POST" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Component)
};
}
