#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace hypha::reference_ui
{
// Keep room below each chart's 28 px heading and 16 px padding. A width-only
// breakpoint used to stack the default pair into almost zero-height plots at 600x400.
inline std::array<juce::Rectangle<float>, 3> referenceVisualCells (
    juce::Rectangle<float> area, int count, const juce::String& preferredLayout)
{
    std::array<juce::Rectangle<float>, 3> cells {};
    count = juce::jlimit (1, 3, count);
    constexpr float gap = 6.0f, minimumHeight = 100.0f;
    if (count == 1) { cells[0] = area; return cells; }
    const bool stackFits = area.getHeight() >= minimumHeight * count + gap * (count - 1);
    if (area.getWidth() < 620.0f && stackFits)
    {
        const auto height = (area.getHeight() - gap * (count - 1)) / count;
        for (int index = 0; index < count; ++index)
        { cells[static_cast<size_t> (index)] = area.removeFromTop (height); area.removeFromTop (gap); }
    }
    else if (count == 2)
    {
        const auto width = preferredLayout == "main" ? area.getWidth() * 0.62f
                                                     : (area.getWidth() - gap) * 0.5f;
        cells[0] = area.removeFromLeft (width); area.removeFromLeft (gap); cells[1] = area;
    }
    else if (preferredLayout == "equal" || area.getHeight() < minimumHeight * 2 + gap)
    {
        const auto width = (area.getWidth() - gap * 2) / 3;
        for (int index = 0; index < count; ++index)
        { cells[static_cast<size_t> (index)] = area.removeFromLeft (width); area.removeFromLeft (gap); }
    }
    else
    {
        cells[0] = area.removeFromLeft (area.getWidth() * 0.62f); area.removeFromLeft (gap);
        const auto height = (area.getHeight() - gap) * 0.5f;
        cells[1] = area.removeFromTop (height); area.removeFromTop (gap); cells[2] = area;
    }
    return cells;
}
}
