#pragma once

#include "HyphaPresentationContext.h"

// JUCE-free dimensions shared by frame painting and constexpr DRUM layout. The full margin
// includes the cast shadow; the smaller inset is only for a bevel fitted into a page body.
namespace hypha::main_frame_geometry
{
struct Measures
{
    float ring = 7.0f;
    float shadow = 6.0f;
    float radius = 2.0f;
};

constexpr float squareRoot (double value) noexcept
{
    if (value <= 0.0) return 0.0f;
    auto root = value > 1.0 ? value : 1.0;
    for (int step = 0; step < 32; ++step) root = 0.5 * (root + value / root);
    return static_cast<float> (root);
}

constexpr Measures forDiagonal (float diagonal) noexcept
{
    const auto unit = diagonal / squareRoot (900.0 * 900.0 + 600.0 * 600.0);
    return { 7.0f * unit > 3.0f ? 7.0f * unit : 3.0f,
             6.0f * unit > 3.0f ? 6.0f * unit : 3.0f, 2.0f };
}

constexpr Measures forContext (const presentation::Context& context) noexcept
{
    const auto width = static_cast<double> (context.logicalWidth);
    const auto height = static_cast<double> (context.logicalHeight);
    return forDiagonal (squareRoot (width * width + height * height));
}

constexpr int insetFor (const presentation::Context& context) noexcept
{
    return static_cast<int> (forContext (context).ring + 0.5f) + 1;
}

// The smallest whole-point gap strictly wider than the bronze ring. DRUM uses this vertically
// and clips the cast shadow to its row, preserving the short 150% observation and its facts.
constexpr int bevelInsetFor (const presentation::Context& context) noexcept
{
    return static_cast<int> (forContext (context).ring) + 1;
}

constexpr int marginFor (const Measures& measures) noexcept
{
    const auto extent = measures.ring + 1.5f * measures.shadow + 3.0f;
    const auto whole = static_cast<int> (extent);
    return whole + (extent > static_cast<float> (whole) ? 1 : 0);
}
}
