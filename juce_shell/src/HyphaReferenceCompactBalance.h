#pragma once
#include "HyphaReferenceOverlay.h"
#include "HyphaReferenceBalance.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

namespace hypha::reference_ui
{
inline constexpr int compactBalanceMinimumHeight = 32; // 14 px title, 14 px plot, 2 px margins
// The compact B view keeps the same data and line colours as the full Balance view.
inline void paintCompactBalance (juce::Graphics& g, juce::Rectangle<int> bounds, const State& state,
                                 presentation::Context context, bool detailsOpen)
{
    surface_material::paintPanel (g, bounds.toFloat(), 0.72f);
    auto chart = bounds.reduced (5, 2);
    auto title = chart.removeFromTop (14);
    g.setColour (COL_NORMAL);
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::information));
    if (bounds.getHeight() < compactBalanceMinimumHeight)
    {
        const auto message = ! detailsOpen ? "ENLARGE WINDOW TO SEE BALANCE"
            : context.referenceCompact ? "CLOSE DETAILS FOR CHART" : "CLOSE DETAILS TO SEE BALANCE";
        text_style::drawEllipsized (g, message, title, juce::Justification::centredLeft);
        return;
    }
    using Drawing = reference_audition::BalanceOverlay::Drawing;
    const bool shapeMode = state.overlay.all && state.overlay.drawing != Drawing::lines;
    const auto items = overlayItems (state);
    const auto shape = shapeMode ? overlayShape (items, state.overlay.drawing == Drawing::allShape) : OverlayShape {};
    const auto caption = shapeMode ? "BALANCE / " + juce::String (state.overlay.drawing == Drawing::allShape ? "ALL SHAPE " : "SHAPE ") + juce::String (shape.count)
                                   : juce::String (state.overlay.all ? "BALANCE / ALL" : "BALANCE / ONE");
    const auto captionWidth = juce::jmin (title.getWidth(), juce::roundToInt (std::ceil (text_style::shownWidth (g.getCurrentFont(), caption))) + 8);
    text_style::drawText (g, caption, title.removeFromLeft (captionWidth), juce::Justification::centredLeft);
    g.setColour (COL_FLORA_BR);
    text_style::drawText (g, "A", title.removeFromLeft (14), juce::Justification::centred);
    if (shapeMode)
    {
        g.setColour (COL_SPECTRUM_DELTA);
        text_style::drawEllipsized (g, "MEAN", title, juce::Justification::centredLeft);
    }
    else if (state.overlay.all)
    {
        int remaining = 0;
        for (const auto& item : items)
        {
            if (item.line == nullptr || ! item.drawable()) continue;
            if (title.getWidth() < 34) { ++remaining; continue; }
            g.setColour (overlayLineColour (item.line->colour));
            text_style::drawText (g, juce::String (item.number), title.removeFromLeft (18), juce::Justification::centred);
        }
        if (remaining > 0) { g.setColour (COL_NORMAL); text_style::drawText (g, "+" + juce::String (remaining), title, juce::Justification::centredLeft); }
    }
    else
    {
        g.setColour (COL_SPECTRUM_DELTA);
        const auto* target = state.targets.collection.find (state.targets.collection.selected);
        text_style::drawEllipsized (g, target != nullptr ? target->name : juce::String ("B"), title, juce::Justification::centredLeft);
    }
    paintCompactBalanceChart (g, chart.toFloat(), state, context);
}
}
