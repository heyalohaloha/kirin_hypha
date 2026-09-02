#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaAttackSpecimenPainter.h"

namespace hypha::attack_overview_glyph
{
    void drawAbsolute (juce::Graphics&,
                       juce::Rectangle<int>,
                       attack_specimen::FeatureAmounts);

    void drawComparison (juce::Graphics&,
                         juce::Rectangle<int>,
                         attack_specimen::FeatureAmounts preAmounts,
                         attack_specimen::FeatureAmounts postAmounts);
}
