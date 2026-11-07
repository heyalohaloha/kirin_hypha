#pragma once
#include <array>
#include <utility>

// The sizes the entry checks walk: the five presets, the sizes just inside the limits, and 750 x 500.
namespace hypha::tests::editor_sizes
{
inline constexpr std::array<std::pair<int, int>, 8> entrySizes {{
    { 300, 200 }, { 303, 202 }, { 375, 250 }, { 450, 300 }, { 600, 400 }, { 750, 500 }, { 897, 598 }, { 900, 600 } }};
}
