#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaSpectrumBallistics.h"
#include "HyphaTheme.h"
#include "kirin_hypha_ffi.h"

namespace hypha
{
// POST-only presentation component. It receives fixed Rust snapshots and owns only display
// ballistics plus their on-demand presentation timer; it owns no file, FFT, pairing, or audio
// state. Raw analysis remains in Rust, and a missing/incompatible frame is rendered factually.
class SpectrumComponent final : public juce::Component,
                                private juce::Timer
{
public:
    SpectrumComponent();

    void setSnapshot (const KirinSpectrumView& next);
    void clearSnapshot();
    void setPresentationActive (bool active);
    void presentationTick();
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    static juce::String statusText (uint8_t status);
    static juce::String hoverFrequencyText (float hz);
    static float yForDeltaDb (float db, juce::Rectangle<float> plot) noexcept;
    static float yForMagnitudeDbfs (float dbfs, juce::Rectangle<float> plot) noexcept;
    static float xForFrequency (float hz, float minHz, float maxHz,
                                juce::Rectangle<float> plot) noexcept;
    static float frequencyForNormalisedX (float position, float minHz,
                                          float maxHz) noexcept;

    KirinSpectrumView snapshot {};
    SpectrumBallistics ballistics;
    bool haveSnapshot = false;
    float hoverNormalisedX = -1.0f;
    bool hoverNeedsRepaint = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumComponent)
};
}
