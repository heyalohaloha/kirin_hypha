#pragma once

#include <array>
#include <functional>
#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"
#include "HyphaSpectrumFocusTrail.h"
#include "kirin_hypha_ffi.h"

namespace hypha
{
// POST-only presentation component. It receives a fixed Rust snapshot and owns no timer, file,
// FFT, pairing, or audio state. The renderer uses only state-free frequency-axis presentation and
// y clipping; raw analysis remains in Rust and an incompatible exact frame becomes a factual status.
class SpectrumComponent final : public juce::Component
{
public:
    SpectrumComponent();

    void setSnapshot (const KirinSpectrumView& next);
    void setBatch (const KirinSpectrumBatch& batch);
    void queueSnapshot (const KirinSpectrumView& next);
    void clearSnapshot();
    void presentationTick();
    void presentationTickAt (double nowMs);
    void setAnalysisOwnerNames (const juce::String& names);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool hasFocusLock() const noexcept { return focusFrequencyHz > 0.0f; }
    float focusLockFrequencyHz() const noexcept { return focusFrequencyHz; }
    bool hasMark() const noexcept { return haveMark; }
    uint8_t channelModeForTest() const noexcept { return channelMode; }
    size_t focusTrailSizeForTest() const noexcept
    {
        return focusTrail != nullptr ? focusTrail->size() : 0u;
    }
    int64_t presentedEndpointForTest() const noexcept
    {
        return snapshot.presentation_end_samples;
    }
    float readoutDeltaForTest (size_t index) const noexcept
    {
        return index < readoutDelta.size() ? readoutDelta[index] : 0.0f;
    }

    std::function<bool(uint8_t)> onChannelModeChange;

private:
    KirinSpectrumView snapshot {};
    KirinSpectrumView pendingSnapshot {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedPre {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedPost {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedDelta {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> readoutPre {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> readoutPost {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> readoutDelta {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> pendingPre {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> pendingPost {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> pendingDelta {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> markedDelta {};
    std::unique_ptr<spectrum_focus::FocusTrailHistory> focusTrail;
    bool haveSnapshot = false;
    bool havePendingSnapshot = false;
    bool curveDirty = false;
    bool numericDirty = false;
    bool haveMark = false;
    float hoverNormalisedX = -1.0f;
    float focusFrequencyHz = -1.0f;
    uint8_t channelMode = KIRIN_SPECTRUM_CHANNEL_LR;
    uint8_t inputChannels = 0;
    juce::String modeActionNotice;
    juce::String analysisOwnerNames;
    double modeActionNoticeUntilMs = 0.0;
    bool hoverNeedsRepaint = false;
    double lastCurvePresentationMs = 0.0;
    double lastNumericPresentationMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumComponent)
};
}
