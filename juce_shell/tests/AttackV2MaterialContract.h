#pragma once
#include "../src/HyphaAttackStage.h"
#include "../src/HyphaMaterialCache.h"
#include "AttackUiLaneContract.h"
#include <iostream>

namespace hypha::attack_ui_test
{
inline bool verifyAttackV2MaterialContract()
{
    material_cache::Lifetime lifetime;
    const auto store = juce::SharedResourcePointer<material_cache::Store>::getSharedObjectWithoutCreating();
    if (! store) return false;
    const auto render = [] (int width, float dpi, float bed, bool vignette) {
        juce::Image image (juce::Image::ARGB, juce::roundToInt (width * dpi),
                          juce::roundToInt (96 * dpi), true);
        juce::Graphics g (image); g.addTransform (juce::AffineTransform::scale (dpi));
        attack_stage::paint (g, { 2, 4, static_cast<float> (width - 4), 90 }, 3, bed, vignette);
        return image;
    };
    // A new held size/DPI must produce the same image on its first and subsequent paints.
    // Material variants must remain distinguishable, even though they share a size and role.
    for (const auto dpi : { 1.0f, 1.25f, 2.0f }) for (const auto width : { 116, 212, 316 })
    {
        const auto cold = render (width, dpi, .18f, true);
        const auto bytes = (*store)->bytes();
        const auto warm = render (width, dpi, .18f, true);
        if (differences (cold, warm) != 0 || bytes != (*store)->bytes()
            || differences (warm, render (width, dpi, .10f, true)) == 0
            || differences (warm, render (width, dpi, .18f, false)) == 0
            || (*store)->bytes() > (*store)->budget())
        {
            std::cerr << "DRUM V2 static material cache contract failed width=" << width << " dpi=" << dpi << '\n';
            return false;
        }
    }
    return true;
}
}
