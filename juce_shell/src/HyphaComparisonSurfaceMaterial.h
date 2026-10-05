#pragma once

#include "HyphaKeyLight.h"
#include "HyphaSurfaceMaterial.h"

namespace hypha::comparison_surface
{
// Comparison instructions and anonymous listening are quiet forms, not observation plots.
// Their recessed body shares the page material; only the raised controls catch the key light.
inline void paintQuietBody (juce::Graphics& g, juce::Rectangle<float> area,
                            float fillAlpha = 0.94f, float corner = 7.0f)
{
    surface_material::paintPanel (g, area, fillAlpha, corner);
}

inline juce::Rectangle<float> paintScreen (juce::Graphics& g, const juce::Component& component)
{
    const key_light::Scope light (component);
    g.fillAll (BG);
    const auto area = component.getLocalBounds().toFloat().reduced (10.0f);
    paintQuietBody (g, area);
    return area;
}
}
