#pragma once

// Diagnostic executable only. The caller owns the fixed-size counters on its stack.
struct ProcessorHeapCounts
{
    unsigned allocations = 0, frees = 0;
    const void* firstAllocation = nullptr;
    const void* firstFree = nullptr;
};

#if defined(__APPLE__)
bool initialiseProcessorHeapProbe() noexcept;
bool beginProcessorHeapProbe (ProcessorHeapCounts*) noexcept;
bool endProcessorHeapProbe() noexcept;
#else
inline bool initialiseProcessorHeapProbe() noexcept { return false; }
inline bool beginProcessorHeapProbe (ProcessorHeapCounts*) noexcept { return true; }
inline bool endProcessorHeapProbe() noexcept { return true; }
#endif
