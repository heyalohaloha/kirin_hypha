#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPerceptualHistory.h"
#include "kirin_hypha_ffi.h"

namespace hypha
{
class PerceptualComponent final : public juce::Component
{
public:
    PerceptualComponent();

    void setSnapshot (const KirinPerceptualView& next);
    void clearSnapshot();
    void presentationTick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    size_t historySizeForTest() const noexcept { return history.size(); }
    uint8_t channelModeForTest() const noexcept { return channelMode; }

    std::function<bool(uint8_t)> onChannelModeChange;

private:
    KirinPerceptualView snapshot {};
    perceptual_history::History history;
    bool haveSnapshot = false;
    uint8_t channelMode = KIRIN_SPECTRUM_CHANNEL_LR;
    uint8_t inputChannels = 0;
    juce::String modeActionNotice;
    double modeActionNoticeUntilMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerceptualComponent)
};
}
