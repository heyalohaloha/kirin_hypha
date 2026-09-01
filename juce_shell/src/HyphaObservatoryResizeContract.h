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
    { 900, 600, Density::observatory, "300%" },
}};

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

// Above 200%, render the proven 200% anatomy at one continuous scale. This keeps every domain,
// analysis page, label, hit target, and decorative asset legible together. Sizes at and below
// 200% remain one logical pixel per editor pixel and retain their responsive density modes.
constexpr DisplayViewport displayViewport (int width, int height) noexcept
{
    return width > 600
        ? DisplayViewport { 600, 400, static_cast<float> (width) / 600.0f }
        : DisplayViewport { width, height, 1.0f };
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
static_assert (displayViewport (sizePresets[3]).width == 600
               && displayViewport (sizePresets[3]).height == 400);
static_assert (displayViewport (sizePresets[4]).width == 600
               && displayViewport (sizePresets[4]).height == 400);
static_assert (displayViewport (720, 480).width == 600
               && displayViewport (720, 480).height == 400);
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
