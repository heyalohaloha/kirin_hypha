#pragma once
#include <array>
#include <cstddef>

namespace hypha::spectrum_painter
{
// Preserve the selected original depths/alphas, not three newly rescaled layers.
inline constexpr std::array<std::size_t, 3> glowLayers { 0, 2, 5 };
}
