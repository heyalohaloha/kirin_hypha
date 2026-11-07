#pragma once

#include <array>
#include <cstdint>

#include <juce_graphics/juce_graphics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAbsoluteSpectrumHistory.h"
#include "HyphaPresentationContext.h"

namespace hypha::spectrum_painter
{
    using SpectrumBins = std::array<float, KIRIN_SPECTRUM_BAND_COUNT>;
    using SpectrumValidity = std::array<uint8_t, KIRIN_SPECTRUM_BAND_COUNT>;

    // Pure presentation helper. It has no timer, interaction, pair, FFT, or audio state.
    float yForMagnitude (float dbfs, juce::Rectangle<float> plot) noexcept;

    void paintPostCurve (juce::Graphics& graphics,
                         juce::Rectangle<float> plot,
                         float visualScale,
                         const SpectrumBins& post);

    void paintCurves (juce::Graphics& graphics,
                      juce::Rectangle<float> plot,
                      float visualScale,
                      const SpectrumBins& pre,
                      const SpectrumBins& post,
                      const SpectrumBins& delta,
                      const SpectrumValidity& deltaValid,
                      const SpectrumBins* mark,
                      const SpectrumValidity* markValid);

    // Retained history presentation for MID and SIDE only; LR keeps its current and hold curves.
    void paintAbsoluteHistory (juce::Graphics& graphics,
                               juce::Rectangle<float> plot,
                               const absolute_spectrum::History& history);

    void paintAbsolute (juce::Graphics& graphics,
                        juce::Rectangle<float> plot,
                        float visualScale,
                        const SpectrumBins& post,
                        const SpectrumBins& peakHold,
                        const absolute_spectrum::History& history,
                        presentation::Context,
                        bool showHistory = true);

    void paintMidSide (juce::Graphics& graphics,
                       juce::Rectangle<float> plot,
                       float visualScale,
                       const SpectrumBins& mid,
                       const SpectrumBins& side);
}
