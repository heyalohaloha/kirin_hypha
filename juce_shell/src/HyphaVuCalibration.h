#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace hypha::vu_calibration
{
constexpr int defaultDbfs = -18;
constexpr std::array<int, 5> choices { -12, -14, -16, -18, -20 };

constexpr bool valid (int value) noexcept
{
    for (const auto choice : choices)
        if (choice == value) return true;
    return false;
}

inline juce::String label (int value)
{
    return "0 VU = " + juce::String (valid (value) ? value : defaultDbfs) + " dBFS";
}
}
