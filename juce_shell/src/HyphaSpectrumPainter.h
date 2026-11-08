#pragma once

#include <array>
#include <cstdint>

#include <juce_graphics/juce_graphics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAbsoluteSpectrumHistory.h"
#include "HyphaPresentationContext.h"
#include "HyphaSpectrumDisplay.h"

namespace hypha::spectrum_painter
{
    using SpectrumBins = std::array<float, KIRIN_SPECTRUM_BAND_COUNT>;
    using SpectrumValidity = std::array<uint8_t, KIRIN_SPECTRUM_BAND_COUNT>;

    // Pure presentation helper. It has no timer, interaction, pair, FFT, or audio state.
    float yForMagnitude (float dbfs, juce::Rectangle<float> plot, float range = 96.0f) noexcept;

    void paintPostCurve (juce::Graphics& graphics,
                         juce::Rectangle<float> plot,
                         float visualScale,
                         const SpectrumBins& post, float range = 96.0f, float brightness = 1.0f);

    void paintCurves (juce::Graphics& graphics,
                      juce::Rectangle<float> plot,
                      float visualScale,
                      const SpectrumBins& pre,
                      const SpectrumBins& post,
                      const SpectrumBins& delta,
                      const SpectrumValidity& deltaValid,
                      const SpectrumBins* mark,
                      const SpectrumValidity* markValid, float range = 96.0f, float brightness = 1.0f);

    // Retained history presentation for MID and SIDE only; LR keeps its current and hold curves.
    void paintAbsoluteHistory (juce::Graphics& graphics,
                               juce::Rectangle<float> plot,
                               const absolute_spectrum::History& history,
                               spectrum_display::Backdrop* cache = nullptr,
                               display_settings::Values settings = {}, float minimumHz = 10, float maximumHz = 22000);

    void paintAbsolute (juce::Graphics& graphics,
                        juce::Rectangle<float> plot,
                        float visualScale,
                        const SpectrumBins& post,
                        const SpectrumBins& peakHold,
                        const absolute_spectrum::History& history,
                        presentation::Context,
                        bool showHistory = true, display_settings::Values settings = {},
                        spectrum_display::Backdrop* cache = nullptr, float minimumHz = 10, float maximumHz = 22000);

    void paintMidSide (juce::Graphics& graphics,
                       juce::Rectangle<float> plot,
                       float visualScale,
                       const SpectrumBins& mid,
                       const SpectrumBins& side, float range = 96.0f, float brightness = 1.0f);
}
