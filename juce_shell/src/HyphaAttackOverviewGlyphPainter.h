#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaAttackSpecimenPainter.h"
#include <array>

namespace hypha::attack_overview_glyph
{
    class Cache
    {
    public:
        static constexpr std::size_t byteBudget = 8 * 1024 * 1024;
        juce::Image lookup (attack_specimen::FeatureAmounts pre,
                            attack_specimen::FeatureAmounts post, bool paired,
                            int width, int height, float scale, bool miniature = true,
                            const attack_fan::Motion* motion = nullptr);
        std::size_t bytes() const noexcept { return usedBytes; }
        std::uint64_t builds() const noexcept { return buildCount; }
    private:
        struct Entry
        {
            std::array<float, 8> amounts {};
            juce::Image image;
            int width = 0, height = 0;
            float scale = 0;
            bool paired = false, miniature = true, animatedFocus = false;
            attack_fan::Motion motion;
            std::uint64_t use = 0;
        };
        // Both absolute lanes can retain 240 distinct events; 256 entries would thrash.
        std::array<Entry, 512> entries;
        std::size_t usedBytes = 0;
        std::uint64_t clock = 0, buildCount = 0;
    };
    void drawAbsolute (juce::Graphics&,
                       juce::Rectangle<int>,
                       attack_specimen::FeatureAmounts, Cache* = nullptr);

    void drawComparison (juce::Graphics&,
                         juce::Rectangle<int>,
                         attack_specimen::FeatureAmounts preAmounts,
                         attack_specimen::FeatureAmounts postAmounts, Cache* = nullptr);
    void drawFocus (juce::Graphics&, juce::Rectangle<int>,
                     attack_specimen::FeatureAmounts preAmounts,
                     attack_specimen::FeatureAmounts postAmounts, bool paired,
                     const attack_fan::Motion&, Cache* = nullptr);
}
