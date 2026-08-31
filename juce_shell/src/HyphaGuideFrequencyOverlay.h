#pragma once

#include <juce_graphics/juce_graphics.h>

#include "pre_display/PreDisplayModel.h"

namespace hypha::guide_frequency
{
    enum class Emphasis
    {
        hidden,
        cue,
        active,
    };

    enum class FactKind
    {
        inspect,
        masking,
    };

    // Display-only input. It carries no transport, clock, pairing, or analysis ownership.
    struct Overlay
    {
        Emphasis emphasis = Emphasis::hidden;
        FactKind kind = FactKind::inspect;
        juce::String guideId;
        juce::String itemId;
        juce::String label;
        juce::String frequencyBasis;
        double lowHz = 0.0;
        double highHz = 0.0;

        bool visible() const noexcept { return emphasis != Emphasis::hidden; }
    };

    Overlay fromGuidePresentation (
        const pre_display::GuidePresentationSnapshot& presentation);
    bool equivalent (const Overlay& left, const Overlay& right) noexcept;
    juce::Rectangle<float> bandBoundsFor (
        const Overlay& overlay, float minimumHz, float maximumHz,
        juce::Rectangle<float> plot) noexcept;
    void paint (juce::Graphics& graphics, juce::Rectangle<float> plot,
                float visualScale, const Overlay& overlay,
                float minimumHz, float maximumHz);
}
