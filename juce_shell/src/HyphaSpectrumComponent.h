#pragma once

#include <array>

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

private:
    static juce::String statusText (uint8_t status);
    static juce::String hoverFrequencyText (float hz);
    static float yForDeltaDb (float db, juce::Rectangle<float> plot) noexcept;
    static float xForFrequency (float hz, float minHz, float maxHz,
                                juce::Rectangle<float> plot) noexcept;
    static float frequencyForNormalisedX (float position, float minHz,
                                          float maxHz) noexcept;

    KirinSpectrumView snapshot {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedPre {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedPost {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> displayedDelta {};
    bool haveSnapshot = false;
    float hoverNormalisedX = -1.0f;
    bool hoverNeedsRepaint = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumComponent)
};
}
