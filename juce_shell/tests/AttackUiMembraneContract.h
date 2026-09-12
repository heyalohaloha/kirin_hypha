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

inline double averageLight (const juce::Image& image, juce::Rectangle<int> area)
{
    area = area.getIntersection (image.getBounds());
    if (area.isEmpty()) return 0.0;
    double sum = 0.0;
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            sum += image.getPixelAt (x, y).getPerceivedBrightness();
    return sum / static_cast<double> (area.getWidth() * area.getHeight());
}

inline int visibleArea (const juce::Image& image)
{
    int result = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            result += image.getPixelAt (x, y).getAlpha() > 12;
    return result;
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
        || strongBounds.getWidth() - baseBounds.getWidth() < 2
        || strongBounds.getHeight() - baseBounds.getHeight() < 10)
        { std::cerr << "specimen asset: strength footprint\n"; return false; }
    const auto middleBounds = visibleBounds (renderSpecimen ({ 0.5f, 0.5f, 0.5f }));
    const auto middleAspect = static_cast<float> (middleBounds.getWidth())
                            / static_cast<float> (middleBounds.getHeight());
    if (middleAspect < 1.22f || middleAspect > 1.38f
        || std::abs (strongBounds.getX() - baseBounds.getX()) > 2
        || 3 * (strongBounds.getWidth() - baseBounds.getWidth())
             > strongBounds.getHeight() - baseBounds.getHeight() + 3)
        { std::cerr << "specimen asset: compact anchored strength geometry\n"; return false; }
    if (specimenDifferences (smooth, textured) < 500
        || visibleBounds (smooth).getCentre().getDistanceFrom (
               visibleBounds (textured).getCentre()) > 2.0f)
        { std::cerr << "specimen asset: texture layer\n"; return false; }
    const auto textureArea = visibleBounds (smooth).getUnion (visibleBounds (textured));
    const auto smoothLight = averageLight (smooth, textureArea);
    const auto texturedLight = averageLight (textured, textureArea);
    if (smoothLight <= 0.0 || std::abs (texturedLight - smoothLight) / smoothLight > 0.05)
        { std::cerr << "specimen asset: texture brightness conservation\n"; return false; }
    if (specimenDifferences (soft, sharp) < 500
        || visibleBounds (soft).getCentre().getDistanceFrom (
               visibleBounds (sharp).getCentre()) > 2.0f)
        { std::cerr << "specimen asset: sharpness layer\n"; return false; }
    auto centre = visibleBounds (soft).withSizeKeepingCentre (
        juce::jmax (1, visibleBounds (soft).getWidth() * 2 / 5),
        juce::jmax (1, visibleBounds (soft).getHeight() * 2 / 5));
    const auto softCentre = averageLight (soft, centre);
    const auto sharpCentre = averageLight (sharp, centre);
    const auto softArea = visibleArea (soft);
    const auto sharpArea = visibleArea (sharp);
    if (softCentre <= 0.0 || std::abs (sharpCentre - softCentre) / softCentre > 0.03
        || softArea <= 0 || std::abs (sharpArea - softArea) * 100 > softArea * 7)
        { std::cerr << "specimen asset: sharpness isolation\n"; return false; }

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
