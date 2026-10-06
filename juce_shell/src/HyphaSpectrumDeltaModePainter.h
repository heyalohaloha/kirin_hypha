#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaPresentationContext.h"
#include "HyphaSpectrumGeometry.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

namespace hypha::spectrum_delta_mode
{
inline void paint (juce::Graphics& g, juce::Rectangle<float> outer, float scale,
                   bool shape, presentation::Context context)
{
    const auto bounds = spectrum_geometry::deltaModeBoundsFor (outer, scale);
    if (bounds.isEmpty())
        return;
    const auto half = bounds.getWidth() * 0.5f;
    const auto raw = bounds.withWidth (half);
    const auto normalized = bounds.withTrimmedLeft (half);
    const auto selected = shape ? normalized : raw;
    surface_material::paintControl (g, selected, false, false, true,
                                     COL_SPECTRUM_DELTA_BR, 2.5f * scale);
    g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.60f));
    g.drawRoundedRectangle (selected.reduced (0.3f), 2.5f * scale, 0.65f * scale);
    g.setFont (monoFont (context, typography::TextRole::legend,
                         typography::Composition::visualization));
    g.setColour ((shape ? COL_TEXT_SECONDARY : COL_SPECTRUM_DELTA_BR).withAlpha (0.94f));
    text_style::drawText (g, "RAW", raw.toNearestInt(), juce::Justification::centred);
    g.setColour ((shape ? COL_SPECTRUM_DELTA_BR : COL_TEXT_SECONDARY).withAlpha (0.94f));
    text_style::drawText (g, "SHAPE", normalized.toNearestInt(), juce::Justification::centred);
}
}
