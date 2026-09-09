#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackMotion.h"
#include "HyphaAttackOverviewGlyphPainter.h"

namespace hypha::attack_organism
{
    bool textureAvailable (const KirinAttackDetail&) noexcept;
    float textureAmount (const KirinAttackDetail&) noexcept;

    void drawFocus (juce::Graphics&,
                    const KirinAttackDetail* preDetail,
                    const KirinAttackDetail* postDetail,
                    juce::Rectangle<int>,
                    const attack_motion::Motion& = {}, attack_focus::Cache* = nullptr);
}
