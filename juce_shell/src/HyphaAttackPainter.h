#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackMotion.h"
#include "HyphaAttackOverviewGlyphPainter.h"

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
    void drawEventFocus (juce::Graphics&,
                         const KirinAttackDetail* preDetail,
                         const KirinAttackDetail* postDetail,
                         juce::Rectangle<int>,
                         const attack_motion::Motion& = {}, attack_focus::Cache* = nullptr);
    void drawMetricFact (juce::Graphics&,
                         juce::Rectangle<int>,
                         const juce::String& title,
                         const juce::String& value,
                         const juce::String& context,
                         juce::Colour,
                         bool alignRight);
}
