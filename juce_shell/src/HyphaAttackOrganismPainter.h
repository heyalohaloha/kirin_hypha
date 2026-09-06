#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackFanModel.h"
#include "HyphaAttackOverviewGlyphPainter.h"

namespace hypha::attack_organism
{
    void drawAbsoluteOverview (juce::Graphics&,
                               const KirinAttackDetailBatch&,
                               juce::Rectangle<int>,
                               std::int64_t firstSample,
                               std::int64_t latestSample,
                               std::uint32_t sampleRate, attack_overview_glyph::Cache* = nullptr);
    void drawDifferenceOverview (juce::Graphics&,
                                 const KirinAttackDetailBatch& preDetails,
                                 const KirinAttackDetailBatch& postDetails,
                                 const KirinAttackPairEventBatch& pairs,
                                 juce::Rectangle<int>,
                                 std::int64_t firstSample,
                                 std::int64_t latestSample,
                                 std::uint32_t sampleRate, attack_overview_glyph::Cache* = nullptr);
    void drawFocus (juce::Graphics&,
                    const KirinAttackDetail* preDetail,
                    const KirinAttackDetail* postDetail,
                    juce::Rectangle<int>,
                    const attack_fan::Motion& = {}, attack_overview_glyph::Cache* = nullptr);
}
