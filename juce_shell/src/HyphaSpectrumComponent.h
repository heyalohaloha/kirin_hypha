#pragma once

#include <array>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"
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
    void clearSnapshot();
    void presentationTick();
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool hasFocusLock() const noexcept { return focusFrequencyHz > 0.0f; }
    float focusLockFrequencyHz() const noexcept { return focusFrequencyHz; }
    bool hasMark() const noexcept { return haveMark; }
    uint8_t channelModeForTest() const noexcept { return channelMode; }

    std::function<bool(uint8_t)> onChannelModeChange;

private:
    KirinSpectrumView snapshot {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedPre {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedPost {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedDelta {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> markedDelta {};
    bool haveSnapshot = false;
    bool haveMark = false;
    float hoverNormalisedX = -1.0f;
    float focusFrequencyHz = -1.0f;
    uint8_t channelMode = KIRIN_SPECTRUM_CHANNEL_LR;
    uint8_t inputChannels = 0;
    juce::String modeActionNotice;
    double modeActionNoticeUntilMs = 0.0;
    bool hoverNeedsRepaint = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumComponent)
};
}
