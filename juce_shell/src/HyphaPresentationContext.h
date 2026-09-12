#pragma once

#include "HyphaObservatoryResizeContract.h"

namespace hypha::presentation
{
enum class OutputTarget
{
    editor,
    capture,
    referencePreview,
    popup,
    tooltip,
};

struct Context
{
    int logicalWidth = observatory::sizePresets[0].width;
    int logicalHeight = observatory::sizePresets[0].height;
    observatory::Density density = observatory::Density::compact;
    OutputTarget output = OutputTarget::editor;

    constexpr bool operator== (const Context& other) const noexcept
    {
        return logicalWidth == other.logicalWidth && logicalHeight == other.logicalHeight
            && density == other.density && output == other.output;
    }

    constexpr bool operator!= (const Context& other) const noexcept
    {
        return ! (*this == other);
    }
};

constexpr Context forEditor (int width, int height) noexcept
{
    return { width, height, observatory::densityForWidth (width), OutputTarget::editor };
}

constexpr Context forOutput (int width, int height, OutputTarget output) noexcept
{
    return { width, height, observatory::densityForWidth (width), output };
}

constexpr Context defaultContext() noexcept
{
    return forEditor (observatory::sizePresets[0].width,
                      observatory::sizePresets[0].height);
}

constexpr int densityIndex (observatory::Density density) noexcept
{
    using observatory::Density;
    switch (density)
    {
        case Density::compact: return 0;
        case Density::focused: return 1;
        case Density::standard: return 2;
        case Density::observatory: return 3;
        case Density::inspection: return 4;
    }
    return 0;
}

// Returns a continuous 0...4 position through the five editor anchors. Structural information
// changes use Context::density; typography and spacing can interpolate through this position.
constexpr float densityPosition (const Context& context) noexcept
{
    const auto width = context.logicalWidth;
    if (width <= observatory::sizePresets.front().width) return 0.0f;
    for (int index = 0; index < static_cast<int> (observatory::sizePresets.size()) - 1; ++index)
    {
        const auto left = observatory::sizePresets[static_cast<size_t> (index)].width;
        const auto right = observatory::sizePresets[static_cast<size_t> (index + 1)].width;
        if (width <= right)
            return static_cast<float> (index)
                 + static_cast<float> (width - left) / static_cast<float> (right - left);
    }
    return 4.0f;
}

static_assert (forEditor (300, 200).density == observatory::Density::compact);
static_assert (forEditor (450, 300).density == observatory::Density::standard);
static_assert (forEditor (900, 600).density == observatory::Density::inspection);
static_assert (densityPosition (forEditor (375, 250)) == 1.0f);
static_assert (densityPosition (forEditor (600, 400)) == 3.0f);
}
