#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::attack_painter
{
    enum class WaveformStyle
    {
        continuous,
        pulse
    };

    void drawWaveform (juce::Graphics&,
                       const KirinAttackWaveformBatch&,
                       const KirinAttackDetailBatch&,
                       juce::Rectangle<int>,
                       std::int64_t firstSample,
                       std::int64_t latestSample,
                       std::uint32_t sampleRate,
                       WaveformStyle,
                       bool colourAbsoluteFeatures,
                       float alpha);
    void drawWaveformDifferences (juce::Graphics&,
                                  const KirinAttackWaveformBatch& postWaveform,
                                  const KirinAttackDetailBatch& preDetails,
                                  const KirinAttackDetailBatch& postDetails,
                                  const KirinAttackPairEventBatch& pairs,
                                  juce::Rectangle<int>,
                                  std::int64_t firstSample,
                                  std::int64_t latestSample,
                                  std::uint32_t sampleRate);
    void drawMetricCard (juce::Graphics&,
                         juce::Rectangle<int>,
                         const juce::String& title,
                         const juce::String& value,
                         const juce::String& detail,
                         juce::Colour,
                         bool active = true);
}
