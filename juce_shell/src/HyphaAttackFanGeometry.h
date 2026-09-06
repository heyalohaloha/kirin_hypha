#pragma once
#include "HyphaAttackSpecimenPainter.h"

namespace hypha::attack_fan
{
// Four bounded paths: 7 ribs, at most 28 connected offshoots. RIGHT is the growth point,
// not a time coordinate. Only curve interiors move; feature extents remain factual.
struct Geometry
{
    juce::Path root, ribs, branches, front;
    float rootHalfHeight = 0.0f, fanHalfHeight = 0.0f, frontX = 0.0f;
    int offshoots = 0;
};
inline Geometry geometry (juce::Rectangle<float> area, attack_specimen::FeatureAmounts raw,
                          const Motion& motion = {}, bool miniature = false)
{
    Geometry result;
    if (area.getWidth() < 4.0f || area.getHeight() < 4.0f) return result;
    const auto s = unit (raw.strength), b = unit (raw.brightness);
    const auto x = unit (raw.texture), t = unit (raw.transient);
    const auto point = [area] (float px, float py) {
        return juce::Point<float> { area.getX() + area.getWidth() * (0.04f + px * 0.92f),
                                   area.getCentreY() + area.getHeight() * 0.43f * py }; };
    const auto bend = [&] (std::size_t tap) {
        const auto v = motion.bend[tap];
        return std::isfinite (v) ? juce::jlimit (-0.24f, 0.24f, v) : 0.0f; };
    const auto spread = 0.22f + b * 0.58f, rootHalf = 0.025f + s * 0.18f;
    const auto tip = 0.79f + t * 0.17f;
    result.rootHalfHeight = area.getHeight() * 0.43f * rootHalf;
    result.fanHalfHeight = area.getHeight() * 0.43f * spread;
    result.frontX = point (tip, 0.0f).x;
    result.root.preallocateSpace (20);
    result.root.startNewSubPath (point (0.55f, 0.0f));
    result.root.cubicTo (point (0.61f, -rootHalf), point (0.72f, -rootHalf), point (0.79f, 0.0f));
    result.root.cubicTo (point (0.72f, rootHalf), point (0.61f, rootHalf), point (0.55f, 0.0f));
    result.root.closeSubPath();
    result.front.preallocateSpace (20);
    result.front.startNewSubPath (point (0.71f, -0.018f - t * 0.012f));
    result.front.cubicTo (point (0.79f, -0.015f), point (tip - 0.02f, -0.010f), point (tip, 0.0f));
    result.front.cubicTo (point (tip - 0.02f, 0.010f), point (0.79f, 0.015f),
                          point (0.71f, 0.018f + t * 0.012f));
    // In a 20 px overview glyph, more than one offshoot per rib becomes subpixel overdraw.
    const int ribs = miniature ? 3 : 7, slots = miniature ? 1 : 4;
    result.ribs.preallocateSpace (ribs * 10);
    result.branches.preallocateSpace (ribs * slots * 10);
    for (int rib = 0; rib < ribs; ++rib)
    {
        const auto z = static_cast<float> (rib) / static_cast<float> (ribs - 1) * 2.0f - 1.0f;
        const auto endX = 0.07f + 0.045f * static_cast<float> ((rib * 3) % 5) / 4.0f;
        const auto sy = z * 0.022f, ey = z * spread;
        const auto p0 = point (0.73f, sy);
        const auto p1 = point (0.60f, sy + bend (static_cast<std::size_t> (rib)) * 0.36f);
        const auto p2 = point (0.39f, ey * 0.45f + bend (static_cast<std::size_t> ((rib + 3) % 7))
                                                * (0.75f + z * 0.15f));
        const auto p3 = point (endX, ey);
        result.ribs.startNewSubPath (p0); result.ribs.cubicTo (p1, p2, p3);
        for (int branch = 0; branch < slots; ++branch)
        {
            const auto growth = unit (x * static_cast<float> (slots) - static_cast<float> (branch));
            if (growth <= 0.0f) continue;
            const auto f = 0.28f + static_cast<float> (branch) * 0.16f, u = 1.0f - f;
            const auto start = p0 * (u*u*u) + p1 * (3*u*u*f) + p2 * (3*u*f*f) + p3 * (f*f*f);
            const auto side = branch % 2 == 0 ? -1.0f : 1.0f;
            const auto dx = area.getWidth() * 0.13f * growth;
            const auto dy = area.getHeight() * 0.43f * growth * (side * 0.13f + z * spread * 0.14f);
            const auto end = start.translated (-dx, dy);
            result.branches.startNewSubPath (start);
            result.branches.cubicTo (start.translated (-dx*0.35f, bend (2)*area.getHeight()*0.1f),
                                    end.translated (dx*0.23f, bend (6)*area.getHeight()*0.1f), end);
            ++result.offshoots;
        }
    }
    return result;
}
}
