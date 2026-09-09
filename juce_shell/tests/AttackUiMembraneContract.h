#pragma once

#include "../src/HyphaAttackMembraneGeometry.h"
#include "../src/HyphaAttackOrganismPainter.h"
#include <iostream>
#include <limits>

namespace hypha::attack_ui_test
{
inline std::uint64_t opaquePixels (const juce::Image& image)
{
    std::uint64_t pixels = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            pixels += image.getPixelAt (x, y).getAlpha() > 8;
    return pixels;
}

inline juce::Image renderSpecimen (attack_specimen::FeatureAmounts amounts,
                                   int width = 400, int height = 124)
{
    juce::Image image (juce::Image::ARGB, width, height, true);
    juce::Graphics graphics (image);
    attack_specimen::drawSpecimen (graphics, image.getBounds(), amounts);
    return image;
}

inline bool verifyMembraneContract()
{
    using attack_specimen_geometry::geometry;
    const juce::Rectangle<float> area { 0, 0, 400, 124 };
    const auto base = geometry (area, { .5f, .5f, .5f });
    if (base.outline.isEmpty() || base.fibreCount < 4 || base.fibreCount > 12)
        { std::cerr << "membrane: base\n"; return false; }
    const auto ratio = base.bounds.getWidth() / base.bounds.getHeight();
    if (ratio < 1.22f || ratio > 1.38f) { std::cerr << "membrane: ratio " << ratio << "\n"; return false; }

    for (int mask = 0; mask < 8; ++mask)
    {
        const auto shape = geometry (area,
            { float (mask & 1), float ((mask >> 1) & 1), float ((mask >> 2) & 1) });
        if (shape.outline.isEmpty() || ! area.contains (shape.outline.getBounds())) { std::cerr << "membrane: outline bounds\n"; return false; }
        for (const auto& tissue : shape.tissue)
            if (tissue.isEmpty() || ! area.contains (tissue.getBounds())) { std::cerr << "membrane: tissue bounds\n"; return false; }
        for (const auto& arc : shape.sharpnessArcs)
            if (arc.isEmpty() || ! area.contains (arc.getBounds())) { std::cerr << "membrane: arc bounds\n"; return false; }
    }

    const auto weak = geometry (area, { 0.0f, .5f, .5f });
    const auto strong = geometry (area, { 1.0f, .5f, .5f });
    const auto heightDelta = strong.bounds.getHeight() - weak.bounds.getHeight();
    const auto widthDelta = strong.bounds.getWidth() - weak.bounds.getWidth();
    if (heightDelta < 8.0f || widthDelta > heightDelta / 3.0f + .05f
        || weak.outline == strong.outline)
        { std::cerr << "membrane: strength " << heightDelta << " " << widthDelta << "\n"; return false; }

    const auto smooth = geometry (area, { .5f, 0.0f, .5f });
    const auto textured = geometry (area, { .5f, 1.0f, .5f });
    if (smooth.outline != textured.outline || smooth.bounds != textured.bounds
        || smooth.fibreCount >= textured.fibreCount)
        { std::cerr << "membrane: texture geometry\n"; return false; }

    const auto soft = geometry (area, { .5f, .5f, 0.0f });
    const auto sharp = geometry (area, { .5f, .5f, 1.0f });
    if (soft.outline != sharp.outline || soft.bounds != sharp.bounds
        || sharp.sharpnessReach > sharp.bounds.getHeight() * .08f)
        { std::cerr << "membrane: sharpness geometry\n"; return false; }

    const auto invalid = std::numeric_limits<float>::quiet_NaN();
    const auto safe = geometry (area, { invalid, invalid, invalid });
    const auto zero = geometry (area, {});
    if (safe.outline != zero.outline || safe.fibreCount != zero.fibreCount
        || ! std::equal_to<float> {} (safe.sharpnessReach, zero.sharpnessReach))
        { std::cerr << "membrane: invalid\n"; return false; }

    for (const auto size : { juce::Point<int> { 180, 65 }, { 400, 124 }, { 600, 180 } })
        if (specimenLight (renderSpecimen ({}, size.x, size.y)) == 0
            || specimenLight (renderSpecimen ({ 1, 1, 1 }, size.x, size.y)) == 0)
            { std::cerr << "membrane: visible sizes\n"; return false; }

    const auto lowTexture = renderSpecimen ({ .5f, 0.0f, .5f });
    const auto highTexture = renderSpecimen ({ .5f, 1.0f, .5f });
    const auto lowTextureLight = static_cast<double> (specimenLight (lowTexture));
    const auto highTextureLight = static_cast<double> (specimenLight (highTexture));
    if (specimenDifferences (lowTexture, highTexture) < 80
        || std::abs (highTextureLight - lowTextureLight)
            / juce::jmax (1.0, lowTextureLight) > .05)
        { std::cerr << "membrane: texture light " << lowTextureLight << " " << highTextureLight << " diff " << specimenDifferences (lowTexture, highTexture) << "\n"; return false; }

    const auto lowSharpness = renderSpecimen ({ .5f, .5f, 0.0f });
    const auto highSharpness = renderSpecimen ({ .5f, .5f, 1.0f });
    const juce::Rectangle<int> centre { 120, 37, 160, 50 };
    const auto lowCentre = static_cast<double> (specimenLight (lowSharpness, centre));
    const auto highCentre = static_cast<double> (specimenLight (highSharpness, centre));
    const auto lowArea = static_cast<double> (opaquePixels (lowSharpness));
    const auto highArea = static_cast<double> (opaquePixels (highSharpness));
    if (specimenDifferences (lowSharpness, highSharpness) < 40
        || std::abs (highCentre - lowCentre) / juce::jmax (1.0, lowCentre) > .03
        || (highArea - lowArea) / juce::jmax (1.0, lowArea) > .07)
        { std::cerr << "membrane: sharpness light " << lowCentre << " " << highCentre << " area " << lowArea << " " << highArea << " diff " << specimenDifferences (lowSharpness, highSharpness) << "\n"; return false; }

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
