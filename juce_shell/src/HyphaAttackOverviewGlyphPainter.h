#pragma once
#include "HyphaAttackSpecimenPainter.h"
#include "HyphaAttackMotion.h"
#include <array>

// Historical filename retained to avoid the shared build manifest. This cache is focus-only.
namespace hypha::attack_focus
{
class Cache
{
public:
    static constexpr std::size_t byteBudget = 8 * 1024 * 1024;
    juce::Image lookup (attack_specimen::FeatureAmounts pre, attack_specimen::FeatureAmounts post,
                        bool paired, int width, int height, float scale, const attack_motion::Motion&);
    std::size_t bytes() const noexcept { return usedBytes; }
    std::uint64_t builds() const noexcept { return buildCount; }
private:
    std::array<float, 3> amounts {};
    juce::Image image;
    int width = 0, height = 0;
    float scale = 0;
    std::size_t usedBytes = 0;
    std::uint64_t buildCount = 0;
};
void drawFocus (juce::Graphics&, juce::Rectangle<int>,
                 attack_specimen::FeatureAmounts pre, attack_specimen::FeatureAmounts post,
                 bool paired, const attack_motion::Motion&, Cache* = nullptr);
}
