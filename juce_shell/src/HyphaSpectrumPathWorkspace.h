#pragma once
#include <array>
#include <cstdint>
#include <juce_graphics/juce_graphics.h>

namespace hypha::spectrum_painter
{
// Separate storage for simultaneous curves and the coloured ribbon paths.
class PathWorkspace
{
public:
    static constexpr size_t curveCount = 6;
    static constexpr size_t layers = 6;
    static constexpr size_t levels = 25;
    static constexpr size_t tipBase = curveCount;
    static constexpr size_t highlightBase = tipBase + layers * levels;
    static constexpr size_t count = highlightBase + levels;

    void beginFrame()
    {
        for (auto& slot : slots)
            if (slot.reserved != 0) slot.path.clear();
    }
    juce::Path& path (size_t index, int capacity)
    {
        auto& slot = slots[index];
        if (capacity > slot.reserved)
        {
            slot.path.preallocateSpace (capacity);
            slot.reserved = capacity;
            ++allocationCount;
        }
        return slot.path;
    }
    const juce::Path& existing (size_t index) const { return slots[index].path; }
    uint64_t allocations() const noexcept { return allocationCount; }
    int reserved (size_t index) const noexcept { return slots[index].reserved; }
private:
    struct Slot { juce::Path path; int reserved = 0; };
    std::array<Slot, count> slots;
    uint64_t allocationCount = 0;
};
}
