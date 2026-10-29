#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HyphaTheme.h"
#include <cmath>

namespace hypha::hybrid_vu::needle_painter
{
struct DialGeometry
{
    juce::Point<float> pivot;
    float radiusX;
    float topRise;
    float edgeFall;
};

inline DialGeometry dialGeometry (juce::Rectangle<float> face, int channel)
{
    const auto half = face.getWidth() * 0.5f;
    return {
        { face.getX() + half * (channel == 0 ? 0.515f : 1.475f),
          face.getY() + face.getHeight() * 0.865f },
        juce::jmin (half * 0.438f, face.getHeight() * 0.59f),
        face.getHeight() * 0.483f,
        face.getHeight() * 0.136f
    };
}

inline juce::Point<float> dialPoint (const DialGeometry& dial, float normalized)
{
    const auto angle = juce::jmap (normalized, -2.48f, -0.74f);
    const auto edge = std::abs (normalized * 2.0f - 1.0f);
    return { dial.pivot.x + std::cos (angle) * dial.radiusX,
             dial.pivot.y - dial.topRise + dial.edgeFall * std::pow (edge, 1.72f) };
}

inline void paint (juce::Graphics& g, juce::Rectangle<float> face, int channel, float normalized)
{
    const auto dial = dialGeometry (face, channel);
    const auto end = dial.pivot + (dialPoint (dial, normalized) - dial.pivot) * 1.11f;
    const auto width = face.getWidth();
    const auto scale = [width] (float size, float minimum)
    { return juce::jmax (minimum, size * width / 900.0f); };
    const auto ivory = COL_FLORA_BR.interpolatedWith (COL_NORMAL, 0.34f);
    g.setColour (COL_FLORA_BR.withAlpha (0.20f));
    g.drawLine ({ dial.pivot, end }, scale (5.2f, 1.5f));
    g.setColour (ivory.brighter (0.09f));
    g.drawLine ({ dial.pivot, end }, scale (2.7f, 0.95f));
}

inline juce::Rectangle<float> faceBounds (juce::Rectangle<int> requested)
{
    const auto bounds = requested.toFloat();
    auto face = juce::Rectangle<float> (
        bounds.getX() + bounds.getWidth() * 0.018f,
        bounds.getY() + bounds.getHeight() * 0.135f,
        bounds.getWidth() * 0.964f, bounds.getHeight() * 0.565f);
    const auto inset = juce::jmax (3.0f, 12.0f * face.getWidth() / 900.0f);
    return face.reduced (inset);
}

// Cover the swept needle with short rectangles rather than repainting the entire face.
inline juce::RectangleList<int> dirtyRegions (juce::Rectangle<int> bounds, int channel,
                                              float before, float after)
{
    juce::RectangleList<int> result;
    const auto face = faceBounds (bounds);
    const auto dial = dialGeometry (face, channel);
    const auto oldEnd = dial.pivot + (dialPoint (dial, before) - dial.pivot) * 1.11f;
    const auto newEnd = dial.pivot + (dialPoint (dial, after) - dial.pivot) * 1.11f;
    const auto pad = juce::jmax (2.0f, 2.0f + 2.6f * face.getWidth() / 900.0f);
    for (int segment = 0; segment < 5; ++segment)
    {
        const auto a = static_cast<float> (segment) / 5.0f;
        const auto b = static_cast<float> (segment + 1) / 5.0f;
        const auto p = dial.pivot + (oldEnd - dial.pivot) * a;
        const auto q = dial.pivot + (oldEnd - dial.pivot) * b;
        const auto r = dial.pivot + (newEnd - dial.pivot) * a;
        const auto t = dial.pivot + (newEnd - dial.pivot) * b;
        const auto x0 = juce::jmin (p.x, q.x, r.x, t.x), x1 = juce::jmax (p.x, q.x, r.x, t.x);
        const auto y0 = juce::jmin (p.y, q.y, r.y, t.y), y1 = juce::jmax (p.y, q.y, r.y, t.y);
        result.add (juce::Rectangle<float> (x0, y0, x1 - x0, y1 - y0).expanded (pad)
                        .getSmallestIntegerContainer().getIntersection (bounds));
    }
    return result;
}

inline void paintPair (juce::Graphics& g, juce::Rectangle<int> bounds, float left, float right)
{
    const auto face = faceBounds (bounds);
    paint (g, face, 0, left);
    paint (g, face, 1, right);
}

}
