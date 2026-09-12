#pragma once

#include <array>
#include <cmath>

#include "HyphaAttackSpecimenPainter.h"

namespace hypha::attack_specimen_geometry
{
constexpr std::size_t maximumFibres = 12;

struct Geometry
{
    juce::Path outline;
    std::array<juce::Path, 3> tissue;
    std::array<juce::Path, maximumFibres> fibres;
    std::array<juce::Path, 3> sharpnessArcs;
    juce::Rectangle<float> bounds;
    std::size_t fibreCount = 0;
    float sharpnessReach = 0.0f;
};

inline float unit (float value) noexcept
{
    return std::isfinite (value) ? juce::jlimit (0.0f, 1.0f, value) : 0.0f;
}

inline juce::Path node (juce::Rectangle<float> bounds)
{
    const auto x = [&] (float fraction) { return bounds.getX() + bounds.getWidth() * fraction; };
    const auto y = [&] (float fraction) { return bounds.getY() + bounds.getHeight() * fraction; };
    juce::Path path;
    path.startNewSubPath (x (.065f), y (.51f));
    path.cubicTo (x (.10f), y (.31f), x (.22f), y (.31f), x (.31f), y (.17f));
    path.cubicTo (x (.42f), y (.015f), x (.55f), y (.16f), x (.63f), y (.19f));
    path.cubicTo (x (.72f), y (.22f), x (.77f), y (.11f), x (.86f), y (.25f));
    path.cubicTo (x (.93f), y (.35f), x (.97f), y (.39f), x (.955f), y (.52f));
    path.cubicTo (x (.94f), y (.67f), x (.84f), y (.66f), x (.78f), y (.76f));
    path.cubicTo (x (.70f), y (.89f), x (.58f), y (.78f), x (.49f), y (.83f));
    path.cubicTo (x (.37f), y (.90f), x (.28f), y (.75f), x (.20f), y (.76f));
    path.cubicTo (x (.11f), y (.77f), x (.055f), y (.63f), x (.065f), y (.51f));
    path.closeSubPath();
    return path;
}

inline juce::Path transformedNode (juce::Rectangle<float> bounds, float scaleX,
                                   float scaleY, float offsetX, float offsetY)
{
    auto path = node (bounds);
    const auto centre = bounds.getCentre();
    path.applyTransform (juce::AffineTransform::translation (-centre.x, -centre.y)
        .scaled (scaleX, scaleY).translated (centre.x + offsetX, centre.y + offsetY));
    return path;
}

inline Geometry geometry (juce::Rectangle<float> area, attack_specimen::FeatureAmounts raw)
{
    Geometry result;
    if (! std::isfinite (area.getX()) || ! std::isfinite (area.getY())
        || ! std::isfinite (area.getWidth()) || ! std::isfinite (area.getHeight())
        || area.getWidth() < 8 || area.getHeight() < 8)
        return result;
    const auto strength = unit (raw.strength);
    const auto texture = unit (raw.texture);
    const auto sharpness = unit (raw.sharpness);
    const auto maximumHeight = juce::jmin (area.getHeight() * .99f / .96f,
                                           area.getWidth() / 1.22f);
    const auto height = maximumHeight * (.84f + .12f * strength);
    const auto width = maximumHeight * 1.22f * (.96f + .03f * strength);
    result.bounds = { area.getCentreX() - width * .5f, area.getCentreY() - height * .5f,
                      width, height };
    result.outline = node (result.bounds);
    result.tissue[0] = transformedNode (result.bounds, .88f, .78f, -.025f * width, -.035f * height);
    result.tissue[1] = transformedNode (result.bounds, .69f, .56f, .045f * width, .075f * height);
    result.tissue[2] = transformedNode (result.bounds, .46f, .35f, -.11f * width, .025f * height);

    result.fibreCount = static_cast<std::size_t> (4 + std::lround (texture * 8.0f));
    for (std::size_t index = 0; index < result.fibreCount; ++index)
    {
        const auto fraction = result.fibreCount == 1 ? .5f
            : static_cast<float> (index) / static_cast<float> (result.fibreCount - 1);
        const auto spread = (fraction - .5f) * .72f;
        auto& fibre = result.fibres[index];
        fibre.startNewSubPath (result.bounds.getX() + width * .10f,
                               result.bounds.getCentreY() + spread * height * .08f);
        fibre.cubicTo (result.bounds.getX() + width * .28f,
                       result.bounds.getCentreY() + spread * height * .90f,
                       result.bounds.getX() + width * (.47f + .04f * std::sin (index * 1.7f)),
                       result.bounds.getCentreY() + spread * height,
                       result.bounds.getX() + width * (.73f + .12f * (1.0f - std::abs (spread))),
                       result.bounds.getCentreY() + spread * height * .72f);
    }

    result.sharpnessReach = height * (.015f + .055f * sharpness);
    const auto arc = [&] (juce::Path& path, float x0, float y0, float cx, float cy,
                          float x1, float y1)
    {
        path.startNewSubPath (result.bounds.getX() + width * x0,
                              result.bounds.getY() + height * y0);
        path.quadraticTo (result.bounds.getX() + width * cx,
                          result.bounds.getY() + height * cy - result.sharpnessReach,
                          result.bounds.getX() + width * x1,
                          result.bounds.getY() + height * y1);
    };
    arc (result.sharpnessArcs[0], .15f, .30f, .24f, .10f, .35f, .13f);
    arc (result.sharpnessArcs[1], .58f, .18f, .70f, .08f, .81f, .22f);
    arc (result.sharpnessArcs[2], .12f, .66f, .19f, .83f, .31f, .80f);
    return result;
}
}
