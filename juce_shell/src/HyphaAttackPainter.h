#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::attack_painter
{
    enum class WaveformStyle
    {
        continuous,
        trace
    };

    void drawEnvelope (juce::Graphics&,
                       const KirinAttackWaveformBatch&,
                       juce::Rectangle<int>,
                       std::int64_t firstSample,
                       std::int64_t latestSample,
                       std::uint32_t sampleRate,
                       WaveformStyle,
                       float alpha);
}
