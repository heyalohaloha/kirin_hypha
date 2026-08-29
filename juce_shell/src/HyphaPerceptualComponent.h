#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPerceptualHistory.h"
#include "kirin_hypha_ffi.h"

namespace hypha
{
class PerceptualComponent final : public juce::Component,
                                  public juce::SettableTooltipClient
{
public:
    PerceptualComponent();

    void setSnapshot (const KirinPerceptualView& next);
    void setBatch (const KirinPerceptualBatch& next);
    void clearSnapshot();
    void presentationTick();
    void presentationTickAt (double nowMs);
    void setAnalysisOwnerNames (const juce::String& names);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    size_t historySizeForTest() const noexcept { return history.size(); }
    bool historyHasGapsForTest() const noexcept { return history.hasDiscontinuity(); }
    int64_t newestEndpointForTest() const noexcept { return history.newestEndpoint(); }
    uint64_t curvePresentationCountForTest() const noexcept
    {
        return curvePresentationCount;
    }
    uint64_t numericPresentationCountForTest() const noexcept
    {
        return numericPresentationCount;
    }
    uint8_t channelModeForTest() const noexcept { return channelMode; }

    std::function<bool(uint8_t)> onChannelModeChange;

private:
    KirinPerceptualView snapshot {};
    KirinPerceptualView pendingSnapshot {};
    perceptual_history::History history;
    bool haveSnapshot = false;
    bool havePendingSnapshot = false;
    bool curveDirty = false;
    bool numericDirty = false;
    uint8_t channelMode = KIRIN_SPECTRUM_CHANNEL_LR;
    uint8_t inputChannels = 0;
    juce::String modeActionNotice;
    juce::String analysisOwnerNames;
    double modeActionNoticeUntilMs = 0.0;
    double lastCurvePresentationMs = 0.0;
    double lastNumericPresentationMs = 0.0;
    uint64_t curvePresentationCount = 0u;
    uint64_t numericPresentationCount = 0u;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerceptualComponent)
};
}
