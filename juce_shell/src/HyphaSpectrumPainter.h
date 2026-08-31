#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAbsoluteSpectrumHistory.h"

namespace hypha::spectrum_painter
{
    using SpectrumBins = std::array<float, KIRIN_SPECTRUM_BAND_COUNT>;

    // Pure presentation helper. It has no timer, interaction, pair, FFT, or audio state.
    void paintCurves (juce::Graphics& graphics,
                      juce::Rectangle<float> plot,
                      float visualScale,
                      const SpectrumBins& pre,
                      const SpectrumBins& post,
                      const SpectrumBins& delta,
                      const SpectrumBins* mark);

    void paintAbsolute (juce::Graphics& graphics,
                        juce::Rectangle<float> plot,
                        float visualScale,
                        const SpectrumBins& post,
                        const SpectrumBins& peakHold,
                        const absolute_spectrum::History& history);
}
