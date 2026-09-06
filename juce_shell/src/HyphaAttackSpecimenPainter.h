#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "kirin_hypha_ffi.h"
#include "HyphaAttackFanModel.h"

namespace hypha::attack_specimen
{
    struct FeatureAmounts
    {
        float strength = 0.0f;
        float brightness = 0.0f;
        float transient = 0.0f;
        float texture = 0.0f;
    };

    void drawAbsolute (juce::Graphics&,
                       const KirinAttackDetail&,
                       juce::Rectangle<int>,
                       FeatureAmounts,
                       const attack_fan::Motion& = {});

    void drawComparison (juce::Graphics&,
                         const KirinAttackDetail& pre,
                         const KirinAttackDetail& post,
                         juce::Rectangle<int>,
                         FeatureAmounts preAmounts,
                         FeatureAmounts postAmounts,
                         const attack_fan::Motion& = {});

    void drawFan (juce::Graphics&, juce::Rectangle<int>, FeatureAmounts,
                  const attack_fan::Motion& = {}, bool reference = false, bool miniature = false);

}
