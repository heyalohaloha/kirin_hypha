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

constexpr bool isFullDensity (Density density) noexcept
{
    return density == Density::observatory || density == Density::inspection;
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

// Every physical editor pixel is part of the layout contract. In particular, 900 x 600 is the
// Inspection View rather than a magnified 600 x 400 Observatory: the extra area must be available
// to histories, axes, channel strips, and each analysis surface.
constexpr DisplayViewport displayViewport (int width, int height) noexcept
{
    return { width, height, 1.0f };
}

constexpr DisplayViewport displayViewport (SizePreset preset) noexcept
{
    return displayViewport (preset.width, preset.height);
}

constexpr bool validEditorSize (int width, int height) noexcept
{
    const int aspectError = width * 2 - height * 3;
    return width >= 300 && width <= 900
        && height >= 200 && height <= 600
        // A host may round one axis to the nearest whole pixel while enforcing 3:2. Accept all
        // three possible results so a freely dragged size such as 500 x 333 survives state restore.
        && aspectError >= -1 && aspectError <= 1;
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
static_assert (unpackEditorSize (packEditorSize ({ 720, 480 })).width == 720);
static_assert (unpackEditorSize (packEditorSize ({ 720, 480 })).height == 480);
static_assert (editorSizeFromState (2, 3, 720, 480).width == 600);
static_assert (editorSizeFromState (3, 3, 720, 480).width == 720);
static_assert (editorSizeFromState (3, 3, 720, 400).width == 600);
}
