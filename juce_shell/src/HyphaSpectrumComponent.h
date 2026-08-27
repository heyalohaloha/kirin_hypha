#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaTheme.h"
#include "kirin_hypha_ffi.h"

namespace hypha
{
// POST-only presentation component. It receives a fixed Rust snapshot and owns no timer, file,
// FFT, pairing, or audio state. The renderer clips only its y-coordinate; raw analysis remains in
// Rust and a missing/incompatible exact frame is rendered as a factual status.
class SpectrumComponent final : public juce::Component
{
public:
    SpectrumComponent();

    void setSnapshot (const KirinSpectrumView& next);
    void paint (juce::Graphics&) override;

private:
    static juce::String statusText (uint8_t status);
    static float yForDeltaDb (float db, juce::Rectangle<float> plot) noexcept;
    static float yForMagnitudeDbfs (float dbfs, juce::Rectangle<float> plot) noexcept;
    static float xForFrequency (float hz, float minHz, float maxHz,
                                juce::Rectangle<float> plot) noexcept;

    KirinSpectrumView snapshot {};
    bool haveSnapshot = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumComponent)
};
}
