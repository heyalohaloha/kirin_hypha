#pragma once
#include "../src/HyphaSpectrumComponent.h"
namespace hypha::tests
{
bool isReferenceCurveInk (juce::Colour pixel, juce::Colour target);
    struct SpectrumRenderResult
    {
        juce::Image image;
        double paintMs = 0.0;
    };


SpectrumRenderResult renderSpectrumAtSize (const KirinSpectrumView&,
    const ui_contract::SpectrumSizePreset&, const char*);
}
