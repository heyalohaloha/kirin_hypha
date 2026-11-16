#pragma once
#include "HyphaReferenceOverlay.h"
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
    const auto point = [chart] (double hz, double db)
    {
        const auto x = chart.getX() + chart.getWidth() * std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0);
        const auto y = chart.getBottom() - chart.getHeight() * (juce::jlimit (-90.0, -6.0, db) + 90.0) / 84.0;
        return juce::Point<float> { static_cast<float> (x), static_cast<float> (y) };
    };
    const auto plot = [&g, &point] (const std::vector<double>& hz, const std::vector<float>& db, double gain, juce::Colour colour)
    {
        juce::Path path;
        for (size_t i = 0; i < hz.size() && i < db.size(); ++i)
        {
            if (i == 0) path.startNewSubPath (point (hz[i], db[i] + gain));
            else path.lineTo (point (hz[i], db[i] + gain));
        }
        g.setColour (colour); g.strokePath (path, juce::PathStrokeType (1.2f));
    };
    if (shape.count > 0)
    {
        juce::Path band;
        for (size_t i = 0; i < shape.centers.size(); ++i)
            if (i == 0) band.startNewSubPath (point (shape.centers[i], shape.high[i]));
            else band.lineTo (point (shape.centers[i], shape.high[i]));
        for (auto i = shape.centers.size(); i-- > 0;) band.lineTo (point (shape.centers[i], shape.low[i]));
        band.closeSubPath(); g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.22f)); g.fillPath (band);
        plot (shape.centers, shape.mean, 0.0, COL_SPECTRUM_DELTA);
    }
    if (state.aKirin && state.aKirin->frames >= 30)
        plot (state.aKirin->centersHz, state.aKirin->medianDb, 0.0, COL_FLORA_BR);
    if (shapeMode) return;
    for (const auto& item : items)
    {
        const bool selected = item.target ? item.id == state.targets.collection.selected
                                         : state.targets.collection.selected.isEmpty() && item.id == state.songId;
        if (! item.drawable() || (state.overlay.all ? item.line == nullptr : ! selected)) continue;
        const auto colour = state.overlay.all ? overlayLineColour (item.line->colour) : COL_SPECTRUM_DELTA;
        plot (*item.centers, *item.medians, std::isfinite (item.gainDb) ? item.gainDb : 0.0, colour);
    }
}
}
