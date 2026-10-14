#pragma once
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// At most two native linear maps in the current PRE generation. A split is a measured
// callback boundary, never a sample position rounded from PPQ. Audio is not retained here.
struct LinearIntervals
{
    std::int64_t start = 0, project = 0, split = 0, splitProject = 0, end = 0;
};
struct AtomicIntervals
{
    std::atomic<std::int64_t> start { 0 }, project { 0 }, split { 0 }, splitProject { 0 }, end { 0 };
    void store (const LinearIntervals& v) noexcept
    {
        start.store (v.start, std::memory_order_relaxed); project.store (v.project, std::memory_order_relaxed);
        split.store (v.split, std::memory_order_relaxed); splitProject.store (v.splitProject, std::memory_order_relaxed);
        end.store (v.end, std::memory_order_relaxed);
    }
    LinearIntervals load() const noexcept
    {
        return { start.load (std::memory_order_relaxed), project.load (std::memory_order_relaxed),
            split.load (std::memory_order_relaxed), splitProject.load (std::memory_order_relaxed),
            end.load (std::memory_order_relaxed) };
    }
};
}
