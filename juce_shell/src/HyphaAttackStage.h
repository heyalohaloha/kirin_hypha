#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "HyphaTypographyContract.h"

// DRUM observation stage. Plots sit one step deeper than the graphite shell with the Hypha
// mycelium bed faintly beneath them: the visual system's fixed time floor under TIME and ATTACK.
// Stage material is structure only; it never changes with a measured value.
namespace hypha::attack_stage
{
// A vignette darkens only the HISTORY well; lanes and the loupe stay evenly lit.
void paint (juce::Graphics&, juce::Rectangle<float> area, float corner, float bed,
            bool vignette = false);

// Uppercase labels are tracked for an instrument face where the full-density columns leave room;
// smaller editors keep plain spacing so every lane name stays whole. Numbers are never tracked.
constexpr float labelTracking (const presentation::Context& context) noexcept
{
    return observatory::isFullDensity (context.density) ? 0.10f : 0.0f;
}

constexpr float captionTracking (const presentation::Context& context) noexcept
{
    return observatory::isFullDensity (context.density) ? 0.05f : 0.0f;
}

// For measuring text. A setFont call builds the same font inline:
// monoFont (context, role, visualization).withExtraKerningFactor (tracking).
juce::Font trackedFont (const presentation::Context&, typography::TextRole, float tracking);
}
