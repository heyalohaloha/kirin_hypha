#pragma once

#include <array>
#include <cstddef>

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

    enum class BandRole
    {
        inspect,
        maskingFocus,
        maskingMeasured,
    };

    struct Band
    {
        Emphasis emphasis = Emphasis::hidden;
        BandRole role = BandRole::inspect;
        juce::String itemId;
        juce::String label;
        juce::String frequencyBasis;
        double lowHz = 0.0;
        double highHz = 0.0;

        bool visible() const noexcept { return emphasis != Emphasis::hidden; }
    };

    // Display-only input. Focus and measured MASKING ranges stay separate so the painter never
    // has to infer their meaning from coincident coordinates or transport text.
    struct Overlay
    {
        juce::String guideId;
        std::array<Band, 2> bands {};
        std::size_t count = 0;

        bool visible() const noexcept { return count > 0; }
        const Band* band (std::size_t index) const noexcept
        {
            return index < count ? &bands[index] : nullptr;
        }
    };

    Overlay fromGuidePresentation (
        const pre_display::GuidePresentationSnapshot& presentation);
    bool equivalent (const Overlay& left, const Overlay& right) noexcept;
    juce::Rectangle<float> bandBoundsFor (
        const Band& band, float minimumHz, float maximumHz,
        juce::Rectangle<float> plot) noexcept;
    void paint (juce::Graphics& graphics, juce::Rectangle<float> plot,
                float visualScale, const Overlay& overlay,
                float minimumHz, float maximumHz);
}
