#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::attack_organism
{
    void drawAbsoluteOverview (juce::Graphics&,
                               const KirinAttackDetailBatch&,
                               juce::Rectangle<int>,
                               std::int64_t firstSample,
                               std::int64_t latestSample,
                               std::uint32_t sampleRate);
    void drawDifferenceOverview (juce::Graphics&,
                                 const KirinAttackDetailBatch& preDetails,
                                 const KirinAttackDetailBatch& postDetails,
                                 const KirinAttackPairEventBatch& pairs,
                                 juce::Rectangle<int>,
                                 std::int64_t firstSample,
                                 std::int64_t latestSample,
                                 std::uint32_t sampleRate);
    void drawFocus (juce::Graphics&,
                    const KirinAttackDetail* preDetail,
                    const KirinAttackDetail* postDetail,
                    juce::Rectangle<int>);
}
