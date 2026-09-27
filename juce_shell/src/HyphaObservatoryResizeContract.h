#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace hypha::observatory
{
enum class Density
{
    compact,
    focused,
    standard,
    observatory,
    inspection,
};

struct SizePreset
{
    int width = 0;
    int height = 0;
    Density density = Density::compact;
    const char* label = "";
};

constexpr std::array<SizePreset, 5> sizePresets {{
    { 300, 200, Density::compact, "100%" },
    { 375, 250, Density::focused, "125%" },
    { 450, 300, Density::standard, "150%" },
    { 600, 400, Density::observatory, "200%" },
    { 900, 600, Density::inspection, "300%" },
}};

// Above 300% the editor shows the Inspection View magnified: the 900 x 600 layout drawn larger,
// never a sixth layout. Every layout rule and check stays at the five sizes above; a magnified
// size only scales them. The cap bounds what a paint can ask of the renderer (1200%).
constexpr int magnifiedMaximumWidth = 3600;
constexpr int magnifiedMaximumHeight = 2400;

constexpr bool isFullDensity (Density density) noexcept
{
    return density == Density::observatory || density == Density::inspection;
}

// One canonical mapping is used by the editor, every child surface, Capture, and tests. The
// boundaries are the midpoints between the five saved presets, matching the historical nearest-
// preset behaviour without asking child bounds to guess the editor density.
constexpr Density densityForWidth (int width) noexcept
{
    return width < 338 ? Density::compact
         : width < 413 ? Density::focused
         : width < 525 ? Density::standard
         : width < 750 ? Density::observatory
                       : Density::inspection;
}

constexpr SizePreset presetForWidth (int width) noexcept
{
    for (const auto& preset : sizePresets)
        if (preset.density == densityForWidth (width))
            return preset;
    return sizePresets.front();
}

struct DisplayViewport
{
    int width = 0;
    int height = 0;
    float scale = 1.0f;
};

struct EditorSize
{
    int width = 0;
    int height = 0;
};

// Up to 900 x 600 every physical editor pixel is part of the layout contract. In particular,
// 900 x 600 is the Inspection View rather than a magnified 600 x 400 Observatory: the extra area
// must be available to histories, axes, channel strips, and each analysis surface. Beyond it the
// Inspection View is laid out at 900 x 600 and magnified uniformly to fill the editor.
constexpr DisplayViewport displayViewport (int width, int height) noexcept
{
    const auto& inspection = sizePresets.back();
    if (width <= inspection.width || height <= inspection.height)
        return { width, height, 1.0f };
    const auto horizontal = static_cast<float> (width) / static_cast<float> (inspection.width);
    const auto vertical = static_cast<float> (height) / static_cast<float> (inspection.height);
    return { inspection.width, inspection.height, horizontal < vertical ? horizontal : vertical };
}

constexpr DisplayViewport displayViewport (SizePreset preset) noexcept
{
    return displayViewport (preset.width, preset.height);
}

constexpr bool validEditorSize (int width, int height) noexcept
{
    const int aspectError = width * 2 - height * 3;
    return width >= 300 && width <= magnifiedMaximumWidth
        && height >= 200 && height <= magnifiedMaximumHeight
        // A host may round one axis to the nearest whole pixel while enforcing 3:2. Accept all
        // three possible results so a freely dragged size such as 500 x 333 survives state restore.
        && aspectError >= -1 && aspectError <= 1;
}

// The magnified sizes keep the Inspection View on whole device pixels: 900 x 600 times s, where s
// times the display scale is a whole number above it. Every cached image is then copied pixel for
// pixel; a fractional device scale sends each one through the renderer's resampler instead, which
// cost two to three times the pixels' share at 400% on DPI 2. Steps, from the first:
// DPI 2: 450%, 600%, 750%. DPI 1: 600%, 900%. DPI 1.5: 400%, 600%, 800%. DPI 1.25: 480%, 720%.
constexpr EditorSize magnifiedStep (float displayScale, int index) noexcept
{
    if (! (displayScale > 0.0f) || index < 0)
        return {};
    const auto devicePixelsPerPoint = static_cast<float> (static_cast<int> (displayScale) + 1 + index);
    auto width = static_cast<int> (static_cast<float> (sizePresets.back().width) * devicePixelsPerPoint
                                   / displayScale + 0.5f);
    width -= width % 3;
    if (width > magnifiedMaximumWidth)
        return {};
    return { width, width * 2 / 3 };
}

// The step nearest `width` that fits `maximumWidth`, or the Inspection View itself when that is
// nearer (a tie keeps the smaller size).
constexpr EditorSize nearestMagnifiedSize (int width, float displayScale, int maximumWidth) noexcept
{
    EditorSize best { sizePresets.back().width, sizePresets.back().height };
    for (int index = 0; index < 16; ++index)
    {
        const auto step = magnifiedStep (displayScale, index);
        if (step.width == 0 || step.width > maximumWidth)
            break;
        const auto distance = step.width > width ? step.width - width : width - step.width;
        const auto bestDistance = best.width > width ? best.width - width : width - best.width;
        if (distance < bestDistance)
            best = step;
    }
    return best;
}

// The largest size the editor may take inside `available` (a display's usable area): a magnified
// step when one fits, otherwise the largest 3:2 size up to 300%, and never below 100%.
constexpr EditorSize largestEditorSizeWithin (int availableWidth, int availableHeight,
                                              float displayScale) noexcept
{
    auto width = availableWidth < availableHeight * 3 / 2 ? availableWidth : availableHeight * 3 / 2;
    if (width > sizePresets.back().width)
        return nearestMagnifiedSize (width, displayScale, width);
    width = width < sizePresets.front().width ? sizePresets.front().width : width;
    width -= width % 3;
    return { width, width * 2 / 3 };
}

constexpr uint32_t packEditorSize (EditorSize size) noexcept
{
    return (static_cast<uint32_t> (size.width) << 16u)
         | static_cast<uint32_t> (size.height);
}

constexpr EditorSize unpackEditorSize (uint32_t packed) noexcept
{
    return { static_cast<int> (packed >> 16u),
             static_cast<int> (packed & 0xffffu) };
}

constexpr EditorSize editorSizeFromState (int stateVersion,
                                           uint8_t presetIndex,
                                           int storedWidth,
                                           int storedHeight) noexcept
{
    const auto boundedIndex = presetIndex < sizePresets.size()
        ? static_cast<size_t> (presetIndex) : size_t { 0 };
    const auto preset = sizePresets[boundedIndex];
    return stateVersion >= 3 && validEditorSize (storedWidth, storedHeight)
        ? EditorSize { storedWidth, storedHeight }
        : EditorSize { preset.width, preset.height };
}

static_assert (sizePresets[0].width == 300 && sizePresets[0].height == 200);
static_assert (sizePresets[1].width == 375 && sizePresets[1].height == 250);
static_assert (sizePresets[2].width == 450 && sizePresets[2].height == 300);
static_assert (sizePresets[3].width == 600 && sizePresets[3].height == 400);
static_assert (sizePresets[4].width == 900 && sizePresets[4].height == 600);
static_assert (sizePresets[4].density == Density::inspection);
static_assert (densityForWidth (337) == Density::compact);
static_assert (densityForWidth (338) == Density::focused);
static_assert (densityForWidth (412) == Density::focused);
static_assert (densityForWidth (413) == Density::standard);
static_assert (densityForWidth (524) == Density::standard);
static_assert (densityForWidth (525) == Density::observatory);
static_assert (densityForWidth (749) == Density::observatory);
static_assert (densityForWidth (750) == Density::inspection);
static_assert (displayViewport (sizePresets[3]).width == 600
               && displayViewport (sizePresets[3]).height == 400);
static_assert (displayViewport (sizePresets[4]).width == 900
               && displayViewport (sizePresets[4]).height == 600);
static_assert (displayViewport (720, 480).width == 720
               && displayViewport (720, 480).height == 480);
static_assert (validEditorSize (300, 200));
static_assert (validEditorSize (500, 333));
static_assert (validEditorSize (720, 480));
static_assert (validEditorSize (900, 600));
static_assert (! validEditorSize (900, 500));
static_assert (validEditorSize (1200, 800) && validEditorSize (3600, 2400));
static_assert (! validEditorSize (3603, 2402) && ! validEditorSize (1200, 700));
static_assert (displayViewport (sizePresets[4]).scale == 1.0f);
static_assert (displayViewport (1200, 800).width == 900 && displayViewport (1200, 800).height == 600
               && displayViewport (1200, 800).scale > 1.333f && displayViewport (1200, 800).scale < 1.334f);
static_assert (magnifiedStep (2.0f, 0).width == 1350 && magnifiedStep (2.0f, 0).height == 900);
static_assert (magnifiedStep (2.0f, 1).width == 1800 && magnifiedStep (2.0f, 2).width == 2250);
static_assert (magnifiedStep (1.0f, 0).width == 1800 && magnifiedStep (1.0f, 1).width == 2700);
static_assert (magnifiedStep (1.5f, 0).width == 1200 && magnifiedStep (1.5f, 2).width == 2400);
static_assert (magnifiedStep (1.25f, 0).width == 1440 && magnifiedStep (1.25f, 0).height == 960);
static_assert (magnifiedStep (1.0f, 2).width == 3600 && magnifiedStep (1.0f, 3).width == 0);
static_assert (magnifiedStep (0.0f, 0).width == 0);
static_assert (nearestMagnifiedSize (1300, 2.0f, 3600).width == 1350);
static_assert (nearestMagnifiedSize (1000, 2.0f, 3600).width == 900);
static_assert (nearestMagnifiedSize (1125, 2.0f, 3600).width == 900);
static_assert (nearestMagnifiedSize (1600, 2.0f, 1500).width == 1350);
static_assert (largestEditorSizeWithin (1440, 836, 2.0f).width == 900);
static_assert (largestEditorSizeWithin (1696, 1015, 2.0f).width == 1350);
static_assert (largestEditorSizeWithin (2528, 1376, 2.0f).width == 1800);
static_assert (largestEditorSizeWithin (1280, 736, 2.0f).width == 900);
static_assert (largestEditorSizeWithin (800, 500, 1.0f).width == 750
               && largestEditorSizeWithin (800, 500, 1.0f).height == 500);
static_assert (unpackEditorSize (packEditorSize ({ 720, 480 })).width == 720);
static_assert (unpackEditorSize (packEditorSize ({ 720, 480 })).height == 480);
static_assert (editorSizeFromState (2, 3, 720, 480).width == 600);
static_assert (editorSizeFromState (3, 3, 720, 480).width == 720);
static_assert (editorSizeFromState (3, 3, 720, 400).width == 600);
}
