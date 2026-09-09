#pragma once

#include "../src/HyphaAttackOrganismPainter.h"
#include <cmath>
#include <iostream>
#include <limits>

namespace hypha::attack_ui_test
{
inline juce::Image renderSpecimen (attack_specimen::FeatureAmounts amounts,
                                   int width = 500, int height = 180)
{
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    attack_specimen::drawSpecimen (graphics, image.getBounds(), amounts);
    return image;
}

inline juce::Rectangle<int> visibleBounds (const juce::Image& image)
{
    auto left = image.getWidth(), top = image.getHeight(), right = -1, bottom = -1;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            if (image.getPixelAt (x, y).getAlpha() > 8)
            {
                left = juce::jmin (left, x); top = juce::jmin (top, y);
                right = juce::jmax (right, x); bottom = juce::jmax (bottom, y);
            }
    return right >= left && bottom >= top
        ? juce::Rectangle<int> (left, top, right - left + 1, bottom - top + 1)
        : juce::Rectangle<int> {};
}

inline bool verifyMembraneContract()
{
    const auto base = renderSpecimen ({ 0.0f, 0.0f, 0.0f });
    const auto strong = renderSpecimen ({ 1.0f, 0.0f, 0.0f });
    const auto smooth = renderSpecimen ({ 0.5f, 0.0f, 0.5f });
    const auto textured = renderSpecimen ({ 0.5f, 1.0f, 0.5f });
    const auto soft = renderSpecimen ({ 0.5f, 0.5f, 0.0f });
    const auto sharp = renderSpecimen ({ 0.5f, 0.5f, 1.0f });

    const auto baseBounds = visibleBounds (base);
    const auto strongBounds = visibleBounds (strong);
    if (baseBounds.isEmpty() || strongBounds.isEmpty()
        || strongBounds.getWidth() - baseBounds.getWidth() < 30
        || strongBounds.getHeight() - baseBounds.getHeight() < 10)
        { std::cerr << "specimen asset: strength footprint\n"; return false; }
    if (specimenDifferences (smooth, textured) < 500
        || visibleBounds (smooth).getCentre().getDistanceFrom (
               visibleBounds (textured).getCentre()) > 2.0f)
        { std::cerr << "specimen asset: texture layer\n"; return false; }
    if (specimenDifferences (soft, sharp) < 500
        || visibleBounds (soft).getCentre().getDistanceFrom (
               visibleBounds (sharp).getCentre()) > 2.0f)
        { std::cerr << "specimen asset: sharpness layer\n"; return false; }

    const auto invalid = std::numeric_limits<float>::quiet_NaN();
    if (specimenDifferences (base, renderSpecimen ({ invalid, invalid, invalid })) != 0)
        { std::cerr << "specimen asset: invalid values\n"; return false; }

    for (const auto size : { juce::Point<int> { 180, 65 }, { 500, 180 }, { 800, 260 } })
    {
        const auto image = renderSpecimen ({ 1.0f, 1.0f, 1.0f }, size.x, size.y);
        const auto bounds = visibleBounds (image);
        if (bounds.isEmpty() || specimenLight (image) == 0
            || bounds.getWidth() < size.x / 3 || bounds.getHeight() < size.y / 2)
            { std::cerr << "specimen asset: responsive visibility\n"; return false; }
    }

    // Missing one source field makes TEXTURE unavailable, never a fabricated partial value.
    for (int field = 0; field < 3; ++field)
    {
        auto detail = comparisonDetail();
        setComparisonFeature (detail, ComparisonFeature::texture, .8f);
        if (field == 0) detail.sample_edge_ratio_db = invalid;
        if (field == 1) detail.crest_db = invalid;
        if (field == 2) detail.peak_plateau_ms = invalid;
        if (attack_organism::textureAvailable (detail)
            || attack_organism::textureAmount (detail) != 0.0f) return false;
    }
    return true;
}
}
